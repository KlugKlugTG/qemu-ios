# Asset-free Dynarmic/QEMU bridge experiment

This default-off, unqualified diagnostic is not a product CPU backend or a
performance optimization. Its first revision is source only until an actual
build and differential execution receipt exists.

The empty `lt_dynarmic_probe_prefix` Meson option preserves existing builds.
An explicit dependency prefix compiles two experimental ARM source files and
an internal execution-loop hook. An additionally required environment variable
`LT_DYNARMIC_PROBE=1` enables eligibility. The experiment requires `-M none`,
ARM1176, one CPU, little-endian ARM user mode, GDB single-step, single-threaded
TCG, no instruction counting, no replay and no plugins. At most eight eligible
instructions execute. All real device boards always use ordinary TCG.

Only `MOV r0,#imm8` and `ADD r0,r0,#imm8`, with unconditional AL and no flag
updates, are whitelisted. QEMU first checks actual instruction-fetch MMU
permissions and resolves a RAM region. The C wrapper copies the live integer
registers and reconstructed CPSR to a C++ bridge. That bridge creates a fresh
8 MiB ARMv6K Dynarmic JIT, steps the one actual instruction and verifies all
sixteen resulting registers and unchanged CPSR before committing any QEMU
state. It emits an exact stderr execution witness. Any unsupported outcome
leaves the live QEMU state unchanged and continues through TCG.

No QEMU state, MMU helper or nonlocal-exit path enters an active C++ frame.
Loads, stores, CP15, SVC, privileged execution, Thumb and other instructions
remain ordinary TCG. Faulting instruction fetch falls back to TCG for genuine
exception delivery. A fresh alternate JIT context on every instruction avoids
pretending that cross-engine code-cache invalidation has been implemented.
VFP is unchanged because neither eligible instruction accesses it.

A dependency prefix must contain `include/dynarmic` and a shared library in
`lib` or `lib/x86_64-linux-gnu`; runtime dependencies must be supplied separately.
No automatic download, system installation or library vendoring is performed.
The `lt-dynarmic-probe` device configuration selects no iOS boards and needs no
firmware. A future bounded build can pass
`-Dlt_dynarmic_probe_prefix=/absolute/dependency/usr` to Meson and
`--with-devices-arm=lt-dynarmic-probe` to configure, with plugins disabled.

The LightTouch `tools/arm1176-mmu-reference.py` differential harness must run
the same legal input first on the existing reference engine, then with
`--require-dynarmic-probe` on the experimental engine. Besides identical
integer/MMU/fault states it requires two actual execution witnesses, so a
stock engine passing only the TCG state assertions cannot qualify the bridge.
The control proof already established real ARM1176 readable-load, data-abort
and prefetch-abort state; preserve its original failed XN assumption separately.

This experiment does not qualify the board-only PL0 CP15 QEMU_CALL/GLES bridge,
an alternate data-memory adapter, persistent cache identity/invalidation,
Android/AArch64 deployment, iOS execution, free-running interrupts/lifecycle or
any speedup. The fresh-JIT, single-step path is unsuitable for benchmarking.
