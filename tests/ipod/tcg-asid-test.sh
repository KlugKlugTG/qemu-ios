#!/bin/bash
# ASID switches, TLB maintenance and rewritten code on the ARM1176 and Cortex-A8 boards.
#
# TCG keeps a TB jump cache per ASID and the kernel's global TLB entries across an ASID switch, as the hardware's
# tagged TLB does. Both are only right if every way a guest changes or invalidates a mapping still reaches them:
# non-global kernel-mode pages must go at a switch (one by one, or all past 32), a page invalidated while another ASID
# runs must leave that ASID's jump cache, as must a TLBIASID or TLBIALL, a reused cache must start empty, and code
# rewritten through another mapping (a JIT) must be seen in every address space.
#
# tests/ipod/tcg-asid.S does each of these on the iPod touch 2G (ARM1176, RAM at 0x08000000) and the iPad
# (Cortex-A8, RAM at 0x40000000), run as the bootrom; both boards use 4K target pages, as the kept-entry path needs.
# It prints each check's letter on the semihosting console, in lowercase if the check passed. Needs clang with the ARM target (Xcode's has it) and
# python3. QEMU= selects the binary (default build/qemu-system-arm).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
QEMU="${QEMU:-$ROOT/build/qemu-system-arm}"
[ -x "$QEMU" ] || { echo "FAIL no emulator at $QEMU: build it or set QEMU="; exit 1; }
TMP="$(mktemp -d "${TMPDIR:-/tmp}/asid.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
CHECKS="abc kernel load from a non-global page follows the ASID switch
de more non-global kernel pages than are tracked one by one
fgh code at one VA in two ASIDs, reached by an indirect branch
i ASID 1's code remapped while ASID 2 runs, TLBIMVA
j the same, TLBIASID
kl the current ASID's code remapped, TLBIASID, then TLBIMVA
m more ASIDs than jump caches
nopq a global mapping kept across switches, then remapped (TLBIMVA, TLBIALL)
rst code rewritten through another mapping, in another ASID and in its own
u ASID 1's code remapped while ASID 2 runs, TLBIALL"

# image RAM SIZE OUT: the program for a board with RAM at RAM, as a SIZE-byte bootrom
image() {
    clang --target=armv6k-none-eabi -c -DRAM="$1" -x assembler-with-cpp "$ROOT/tests/ipod/tcg-asid.S" -o "$TMP/asid.o" ||
        return 1
    # .text, with the branches to its own labels resolved (clang leaves R_ARM_CALL/JUMP24 for a linker)
    python3 -I - "$TMP/asid.o" "$3" <<'PY' || return 1
import struct, sys
d = open(sys.argv[1], 'rb').read()
shoff, = struct.unpack_from('<I', d, 0x20)
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x2e)
secs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * shentsize) for i in range(shnum)]
name = lambda s: d[secs[shstrndx][4] + s[0]:].split(b'\0', 1)[0]
ti = next(i for i, s in enumerate(secs) if name(s) == b'.text')
text = bytearray(d[secs[ti][4]:secs[ti][4] + secs[ti][5]])
symtab = next(s for s in secs if s[1] == 2)
for s in secs:
    if s[1] == 9 and s[7] == ti:
        for k in range(s[5] // 8):
            off, info = struct.unpack_from('<II', d, s[4] + k * 8)
            _, value, _, _, _, shndx = struct.unpack_from('<IIIBBH', d, symtab[4] + (info >> 8) * 16)
            assert info & 0xff in (0x1c, 0x1d) and shndx == ti, 'unexpected relocation'
            insn, = struct.unpack_from('<I', text, off)
            addend = ((insn & 0xffffff) ^ 0x800000) - 0x800000
            struct.pack_into('<I', text, off, (insn & 0xff000000) | (((value - off >> 2) + addend) & 0xffffff))
open(sys.argv[2], 'wb').write(text)
PY
    truncate -s "$2" "$3"
}

fail=0
# board NAME MACHINE RAM ROMSIZE
board() {
    local out="$TMP/$1.out"
    if ! image "$3" "$4" "$TMP/$1.rom"; then
        echo "FAIL $1: couldn't build the program"; fail=1; return
    fi
    ${TIMEOUT:+$TIMEOUT 60} "$QEMU" -M "$2,bootrom=$TMP/$1.rom$5" -display none -monitor none -serial none -nic none \
        -audio driver=none -semihosting-config enable=on,target=native,chardev=sh -chardev "file,id=sh,path=$out" \
        </dev/null >/dev/null 2>&1
    local got; got="$(head -c 64 "$out" 2>/dev/null | tr -d '\n')"
    case "$got" in *E) ;; *) echo "FAIL $1: the program didn't finish (output '$got')"; fail=1; return ;; esac
    while read -r letters what; do
        local bad=0 i
        for ((i = 0; i < ${#letters}; i++)); do
            case "$got" in *"${letters:i:1}"*) ;; *) bad=1 ;; esac
        done
        if [ $bad = 0 ]; then echo "PASS $1: $what"; else echo "FAIL $1: $what ($got)"; fail=1; fi
    done <<< "$CHECKS"
}
mkdir "$TMP/nand"
truncate -s 1048576 "$TMP/nor.bin"
board ARM1176 iPod-Touch 0x08000000 131072 ",nor=$TMP/nor.bin,nand=$TMP/nand"
board Cortex-A8 ipad1 0x40000000 65536 ""
exit $fail
