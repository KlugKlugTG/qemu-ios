# Experimental CPU execution work

Sealed producer `d9a00001ad050025b790e44be53e86f78b26ca5c` remains the
production baseline. No faster or replacement backend is qualified here. The
portable machine executes an ARM1176 system, including privileged exception
modes, address translation, CP15 and the board's `QEMU_CALL` CP15 device bridge.
An arithmetic-only CPU demo cannot qualify a replacement for this machine.

The reviewed alternatives have concrete boundaries:

* [Dynarmic a41c380](https://github.com/lioncash/dynarmic/tree/a41c380246d3d9f9874f0f792d234dc0cc17c180)
  supports ARMv6K instructions and AArch64/x86-64 hosts, but its README explicitly
  excludes emulation of privileged modes. Its coprocessor/memory callbacks do
  not provide QEMU's privileged register banks, exceptions and MMU semantics.
  A new hybrid translator would require implementing and validating those
  transitions and coherence, not simply linking this library.
* [KVM's ARM API](https://docs.kernel.org/virt/kvm/api.html#kvm-arm-vcpu-init)
  requires an ARM64 host and `KVM_CAP_ARM_EL1_32BIT` for a 32-bit guest kernel.
  ARM32 application support is insufficient. This build's `arm-softmmu` target
  is not in QEMU's KVM target list; x86 Linux KVM cannot run its ARM guest.
  Even an accessible ARM64 KVM descriptor would not establish ARM1176/custom
  CP15 compatibility. That requires an actual supported vCPU and device bridge.
* [Protected KVM](https://docs.kernel.org/virt/kvm/arm/pkvm.html) excludes
  AArch32 guests. The [Android Virtualization Framework](https://android.googlesource.com/platform/packages/modules/Virtualization/+/refs/heads/main/libs/framework-virtualization/README.md)
  uses restricted System APIs, not an ordinary third-party app acceleration
  contract. No permission, root, development grant or security workaround is
  part of this experiment.
* [Unicorn](https://www.unicorn-engine.org/docs/beyond_qemu.html) reuses QEMU's
  CPU translation core; it is not an independent full-machine drop-in or a
  demonstrated speed improvement. Replacing the device/MMU integration with
  Unicorn would require another validated system-emulator integration.

Device capability receipts must distinguish normal app SELinux identity from
`run-as` access checks, node mode bits from successful open/ioctl, and KVM
availability from this guest's compatibility. A refusal is a blocker result,
not a successful replacement-engine test. A native x86 Android emulator using
outer KVM does not accelerate the inner ARM guest.

## Narrow TCG experiment

This is explicitly a **TCG optimization**, not a second CPU backend. The measured
Poco profiles include substantial TB lookup/MMU work; they do not yet establish
jump-cache collision rate. The existing cache is direct mapped with 4096 entries.
The candidate changes its geometry to 16384 entries, adding approximately192KiB
per vCPU on 64-bit hosts. It preserves the existing physical QHT lookup, complete
PC/context/cflags validation, page-subset invalidation, per-TB invalidation and
full-cache flush algorithms. A larger page subset also costs more to invalidate;
a speedup must not be presumed.

Use one committed experimental source revision for both arms, the same compiler,
API28, dependencies and flags. Fresh independent outputs are required: changed
recipe/geometry must fail the existing cache guard, including `--source-update`.
Do not overwrite sealed d9 files or migrate their stamps.

```sh
# Compiler execution requires an allocated slot. Prefixes/NDK are exact pins.
python3 contrib/portable/build-engine.py "$FRESH_CONTROL" \
  --abi "$ABI" --prefix "$PREFIX" --crypto-prefix "$CRYPTO_PREFIX" \
  --configure-only --experimental-tb-cache control
python3 contrib/portable/build-engine.py "$FRESH_LARGE" \
  --abi "$ABI" --prefix "$PREFIX" --crypto-prefix "$CRYPTO_PREFIX" \
  --configure-only --experimental-tb-cache large
# For Android both commands additionally use --ndk "$NDK" --android-api 28.
# Then the allocated j1 operator builds/shared-links and seals actual flags,
# source/ELF/manifests/notices/source kits and APK input correspondence.
```

Both selections compile identical optional accounting code. With the existing
fixed `IT_VCPU_ACCT=5` fresh-worker diagnostic, `[tb-cache]` prints cumulative
fast hits, empty misses, conflicting-PC misses, same-PC context misses, slow QHT
hits and absent results. Subtract integer totals between matching records;
lookups that fault before QHT returns can leave an unmatched slow result.
These are cache requests, not all chained TB entries or complete guest MIPS.
Counters are diagnostic, never consulted for correctness. They are per-vCPU
and not migrated. Turning accounting off suppresses increments, but the compiled
branch/fields can still affect performance; both comparison arms must match.
Omitting `--experimental-tb-cache` compiles the new fields, branches and logging
out and retains the original 4096-entry cache.

First compare actual collision/miss distributions with the same instrumentation
in both arms. Measure unmodified production timing separately; do not claim
counter overhead reduction as speedup. If conflicts are rare or page invalidation
dominates, reject this candidate rather than adding another cache layer blindly.

## Sparse cold/warm translation diagnostics

Existing QMP `x-query-jit` (or HMP `info jit`) returns the generated code size and
capacity, live TB count, TB flush/invalidation and TLB flush counters. These
describe the code buffer and physical lookup structures, separate from the
4096/16384-entry jump cache. System-mode code capacity defaults to the smaller
of host physical RAM/8 and1GiB; capacity is not resident RAM. Obtain actual
capacity from the query, rather than assuming it from Android available RAM.

An owner may collect three snapshots on a fresh matched worker: inspected Home
before the first Settings→General→Back cycle, Home after that cycle, and Home
after the repeated cycle. Take each query outside the tap/capture timing burst,
retain its request/response time bounds and raw response, and use identical
instrumentation in both arms. "First" means first observed in this session,
not that host/guest file or shader caches were cleared. The query traverses TB
and QHT metadata; its overhead is not zero. Do not poll it per frame or enable
per-TB translation/execution traces for the latency comparison.

`jit-snapshot-delta.py` compares retained snapshots **offline**. Transport and
live worker verification belong to the runtime owner. Its input envelope is:

```json
{
  "schema": 1,
  "label": "home-before-cold",
  "identity": {
    "host_boot_id": "owner-verified-host-boot",
    "pid": 123,
    "start_ticks": "456",
    "engine_sha256": "64-lowercase-hex-digits"
  },
  "begin_monotonic_ns": 100,
  "end_monotonic_ns": 110,
  "response": {"return": {"human-readable-text": "complete x-query-jit text"}}
}
```

```sh
python3 contrib/portable/jit-snapshot-delta.py before.json after.json > delta.json
python3 tests/portable-jit-snapshot.py
```

The tool requires one worker/engine epoch, ordered observation bounds and stable
capacity; it refuses missing/duplicate fields, QMP errors and decreasing
cumulative counters. Live TB/code-size deltas may decrease after flushing.
These are net retained state, not a cumulative count of successful translations
or time spent compiling. Statistics are not one atomic snapshot; invalidation
and flushing can hide generated work. A first-only increase supports new
translation activity but cannot establish the cause of a slower first action.
Correlate a separate sampled CPU profile with `tb_gen_code`/`tcg_gen_code`,
guest execution, MMU and MMIO handlers. File/library/texture initialization
needs its own evidence. Existing accounting's run/halt/io fields are wall-time
categories, and `io` is not disk I/O. No causal attribution or speedup follows
from these counter deltas alone.

## Required validation

`tests/portable-tb-cache.py --output NEW_PRIVATE_DIRECTORY` extracts actual
production lookup, physical comparator, hash geometry and three invalidation
paths. It covers conflicting virtual PCs, context/cflags rejection, changed
physical mapping, second-page remap, full/page/TB invalidation, PC-relative full
flush, counter-off behavior and subset bounds under sanitizers. It is a slice
regression, not an emulator/guest pass. `--prepare-only` invokes no compiler.

After reviewed source and allocated builds, require the actual Linux and normal
Android app-UID n72 guest on both control and candidate: visually inspected Home,
Settings→General→Back transitions, correct CP15 guest services, input, concurrent
render/audio, pause/resume and clean Stop/persistent state. Existing legal ARM
TCG/CP15/MMIO/remap tests provide focused correctness gates, never substitute for
that real guest. Preserve every failed run and original dirty state.

Compare same fixture/provider/awake scene with separate writable state and fresh
workers, ABBA paired runs, monitor closed, declared QMP/profiler/capture overhead,
thermal/power and thread CPU/CPU-ID observations. Separate input→guest frame from
input→Android Surface and warm vs first-observed launch. Retain exact APK/native
source/flags and matching unstripped symbols; do not infer cold caches or guest
CPU usage. Reject behavior, latency, memory or invalidation regressions. All
alternative-backend and TCG performance gates remain open until executed.
