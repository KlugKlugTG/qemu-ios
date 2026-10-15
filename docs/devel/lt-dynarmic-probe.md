# Asset-free Dynarmic/QEMU bridge experiment

This default-off, unqualified diagnostic is not a product CPU backend or a
performance optimization. The first Linux/x86-64 build and synthetic
differential proof passed in the narrow scope recorded below.

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

## Actual first proof (historical)

Engine source `653c948ab2` built with an independently pinned Ubuntu Dynarmic
6.5 shared library. The same executable with the switch OFF and ON produced
identical complete sixteen-register/CPSR observations in eight cases: actual
CP15 MMU bootstrap, two integer instructions, readable load, permission and
translation data aborts, current pre-v7 ignored-XN behavior and an unmapped
instruction-fetch prefetch abort. ON emitted the two required execution
witnesses; OFF emitted none. A stock-engine negative passed the same state
assertions but correctly failed the required execution witnesses.

The proof used only legal synthetic RAM with no iOS assets or device model.
Data loads and all architectural faults executed through TCG. The observation
does not establish a Dynarmic data-MMU adapter, board CP15/GLES behavior,
interrupt/lifecycle correctness, Android/AArch64 support or a speedup.

Retain the exact build/source/library hashes and failed configuration/compile
attempts with the private integration receipt. Build attempt01 lacked the
target-required FDT dependency; attempt02 exposed a target-poisoning error in
the generic execution loop. The tested correction routes the experiment
through a default-off internal TCGCPUOps callback, assigned only by ARM.

## Historical research02 preparation (subsequently tested below)

The next source revision expands the single-step diagnostic with aligned
`LDR Rd,[Rn,#imm12]` using positive, preindexed, non-writeback word addressing.
Both registers must be r0-r12. QEMU checks actual EL0 data-load translation
and permissions in C and resolves an ordinary RAM region before creating the
JIT. A misaligned address, permission/translation failure, unassigned physical
address, RAM-device region or other non-RAM region rejects the alternate path.
TCG sees the original instruction and all original architectural state and
performs the real access/fault. No MMIO callback is invoked speculatively.

The C++ callback can read only the one prepared virtual address and value,
exactly once, at the approved width. It cannot call QEMU or read live host RAM.
Any other read, write, exception, SVC or fallback rejects the complete capsule.
A successful actual JIT step must return `HaltReason::Step` and match all
sixteen expected registers and the entire reconstructed CPSR before commit.
Live QEMU breakpoints/watchpoints conservatively disable this diagnostic.

The same source prepares ordinary r0-r12 MOV/ADD/SUB/AND/EOR/ORR with unrotated
imm8, CMP with unrotated imm8, and B/BEQ/BNE with immediate displacement. Only
CMP changes flags. A snapshot capsule/planner can represent up to eight
ALU/branch/read steps and verifies actual intermediate states. It has no
writes, indirect branches, PC operands, stack/link-register operands, VFP,
Thumb, CP15 or privileged instructions. A failed prospective instruction ends
the safe prefix; a future caller must return its final state to normal QEMU
for precise unsupported/fault handling. Fresh JIT state is used per capsule.

**The execution-loop caller still supplies budget=1 and still requires GDB
single-step. No multi-instruction or free-running caller is enabled.** This
preserves debug/interrupt/exit/timer boundaries while the new memory/flag/
branch contracts await actual differential tests. Source preparation of the
larger capsule is not a block-execution result or performance candidate.
A useful block caller requires separate review of its instruction budget,
async exits, interruption between steps, protected breakpoints, context and
invalidation, followed by actual synthetic and guest lifecycle evidence.

Before any build/run: freeze exact source/library hashes and use a separately
admitted finite j1 build. First repeat the historical eight-state OFF/ON and
stock witness-negative, then new exact load/ALU/flags/branch cases with
witness-required negatives. Compare every integer register and CPSR, require
real TCG permission/translation/alignment/prefetch faults with no alternate
witness, and preserve all failed attempts. No iOS/Android/real-board operation
or speed measurement is authorized by this source change.

## Actual RAM/ALU/flags/branch follow-up

Source `c6a943c63a` compiled and linked successfully against the pinned
Dynarmic6.5 Linux/x86-64 library. Its real data-load adapter executed aligned
LDR successfully. Twelve serial synthetic processes covered the historical
reference plus new integer, flags/branch and memory suites, each with same
binary OFF/ON and a stock witness-required negative.

All 35 complete sixteen-register/CPSR observations matched across OFF, ON and
stock. ON emitted exactly 23 real execution witnesses across four fresh
processes: four historical MOV/ADD/load witnesses, eight integer witnesses,
eight flag/branch witnesses and three new LDR witnesses. OFF and stock emitted
none. All four stock harnesses passed every state assertion but returned the
required failure solely for missing alternate-execution witnesses. All twelve
QEMU processes exited normally with no owned process remaining.

Actual TCG fallbacks retained precise permission, translation and alignment
data-abort states, plus genuine unmapped-fetch prefetch abort. Byte loads,
writeback loads, SP operands and Thumb stayed on TCG and matched the controls.
Current pre-v7 ignored-XN behavior remains separately named; this does not
qualify architectural XN protection. The memory tests used real CP15/MMU
bootstrap and SCTLR.A for the alignment negative.

The measured guard ceilings were respected: one build at 253329408 bytes
owned RSS, and the serial runtime matrix at 83152896 bytes; minimum available
host RAM stayed above 18 GB. These tiny synthetic wall times are not CPU
throughput measurements. Exact hashes, source snapshots, commands, raw states,
logs and retirement evidence remain in the private research02 receipt.

The larger capsule still has no enabled multi-instruction caller. Actual
break/watchpoint rejection, MMIO/device accesses, injected C++ rejection
atomicity, asynchronous interrupts/pause/Stop, persistent context/invalidation,
real-board CP15/GLES and Android/AArch64/iOS execution remain unqualified.
No performance or production-backend change follows from this proof.


## Current research03: reusable bounded Jit::Run caller (SOURCE ONLY)

The new `LT_DYNARMIC_BLOCK_PROBE=1` switch selects an independent, default-off
MachineNone/ARM1176 diagnostic. **This revision has not been compiled or run.**
All historical research02 receipts describe the preceding single-step source;
none qualifies this caller. Real boards, production builds, Android and iOS
remain on TCG. `LT_DYNARMIC_PROBE=1` retains the historical GDB-Step experiment.

The new caller accepts one branch-terminated block of two through eight
instructions. It uses one genuine `Jit::Run()`, never a loop of `Step()` calls.
The whitelist is the previously tested immediate r0-r12 ALU/CMP, B/BEQ/BNE and
aligned positive-offset non-writeback LDR word. The first branch terminates
preparation, whether taken or not. No branch within eight instructions,
unsupported instruction or fault before that branch rejects the entire block
with original architectural state. A fault after the completed branch returns
to ordinary TCG at the resulting exact PC. No stores or device accesses are
prepared, and speculative MMIO never enters a C++ callback.

C checks actual fetch/data permissions and ordinary RAM, then passes only a
snapshot and scalar owner/code identities to C++. These identity integers are
never dereferenced there. The complete final sixteen registers and full CPSR,
exact tick count and exact ordered data-read count must match expectations
before live QEMU state changes. Rejection has no memory-write side effect.
C++ exceptions retire the private engine and return failure across the C ABI.
Process-level signals or a library crash are not converted into success.

One private thread-local JIT retains an eight-MiB code allocation. Compiled
code is reusable only while CPU identity, MMU context, block length, every PC,
opcode and resolved RAM identity match. Values for data reads are fresh on
every preparation; callbacks explicitly report memory as not read-only.
Every ordinary TCG handoff or historical Step entry invalidates compiled code;
a signature change also clears it. Only the last signature is retained. This
is deliberately narrow reuse, not a full-system code-cache invalidation scheme.
A total 16384 successful block executions is a diagnostic cap, then TCG resumes.

Block linking, return-stack-buffer and fast dispatch optimizations are disabled;
safe IR optimizations remain. Cycle counting is enabled. `AddTicks` requests
`UserDefined1` at the exact approved block budget; any different halt result,
unapproved callback or state/count mismatch rejects the snapshot. The proposed
tests must establish that the library really honors this boundary and that a
warm cache uses zero code-read callbacks. Source inspection cannot prove it.

Only one CPU and single-thread TCG are eligible. Debug single-step, active
break/watchpoints, replay, icount, parallel execution and specialized TB count,
no-IRQ, memory-instrumentation or breakpoint-page cflags remain TCG. Pending
interrupts, exit requests and forced loop exits reject entry, checked again
after preparation. An async request arriving during Run is observed after the
completed block of at most eight instructions; its committed state then returns
to ordinary QEMU interrupt/exit/timer handling. No QEMU helper or nonlocal exit
runs beneath an active C++ frame. Actual device IRQ/timer injection, race stress
and precise real-board timing remain unqualified even if QMP pause/resume passes.

The matching LightTouch `tools/arm1176-dynarmic-blocks.py` is also UNRUN. Its
finite contracts suite proposes same-binary OFF/ON and stock witness-required
negative comparisons for dynamic LDR loop/cache reuse, changed code, genuine
data/fetch faults after a block, unsupported tail, exhausted branch budget,
single-step, breakpoints and read watchpoints. All sixteen registers plus CPSR
must match across engines. The separate ON-only async suite proposes QMP
stop/resume and debugger code modification while stopped, checking committed
state against genuine block witnesses and rejecting diagnostic-cap exhaustion.
Each harness has an original25s observation limit, original5s running-phase
limits, ordinary QMP quit, and requires a separate aggregate resource guard.
No engine is started by source preparation or documentation.

Even a passing synthetic run would establish correctness of a small eligible
block only. This implementation recomputes every expected instruction through
C, performs MMU checks and logs every block; it is not a speed candidate. A
useful performance decision requires a representative measured EL0 share,
separate cold translation/warm execution/MMU/callback/handoff costs and genuine
ARM64 Linux/Android execution after broader architectural contracts. No speedup
or replacement production CPU engine is claimed.

## Research04 throughput candidate (SOURCE ONLY / UNBUILT / UNRUN)

After the separately recorded successful research03 block/async contracts,
`LT_DYNARMIC_THROUGHPUT=1` additionally opts the block experiment into a narrow
throughput candidate. It still requires the dependency build and
`LT_DYNARMIC_BLOCK_PROBE=1`; product/real-board behavior remains TCG. No current
binary contains this candidate. Historical proof binaries/receipts are retained.

This path decodes and MMU-checks each sequential code word before entry, but
never evaluates the ALU result in C. Only the existing r0-r12 immediate ALU,
CMP and first-terminal B/BEQ/BNE forms are accepted; loads remain confined to
the fully checked correctness mode. No stores, Thumb, VFP, privileged or device
operation is newly admitted. The same two-through-eight-instruction bound,
pre-entry exit/interrupt checks, generic debugger/replay/icount/cflags rejection,
post-block host handling, fresh context/code/RAM identity and TCG invalidation
are retained. Correctness mode still compares the entire expected result.

The candidate instead requires exact returned instruction/tick count, no data
callback, exact non-NZCV CPSR preservation (all CPSR if no CMP), unchanged
unwritten registers and PC at one of the prepared terminal branch outcomes.
The external harness must compare the exact written registers/NZCV/branch
outcome against TCG. This is a deliberate diagnostic contract boundary, not a
claim that internally checking write masks proves instruction semantics. The
cache signature distinguishes correctness and throughput plans. Rejection
preserves original QEMU state and goes to normal TCG. A candidate run is capped
at1048576 successful blocks, then ordinary TCG; cap exhaustion must fail the
prospective witness gate.

Per-block stderr witnesses are replaced by one aggregate at each TCG handoff:
committed block and instruction totals, cold/warm counts, initial preparation
start/first-commit/end times and terminal PC/r0/r1/CPSR. Three monotonic-clock
reads per segment avoid a timer call at every warm block. The first interval
includes allocation/translation/preparation/Run; the remaining span includes
all MMU checks, signature matching, snapshots, actual JIT, commits and host-loop
overhead. These wall intervals are diagnostic, not pure-JIT or isolated warm
instruction timings. An external observer also measures complete process and
vCPU-thread CPU ticks, records their resolution and retains raw observations.
There are no per-block native CPU timers or hidden logging in the candidate.

The finite prospective workload is `ADD r0,#1; SUB r1,#1; CMP r1,#0; BNE loop`,
initialized with1 or65536 iterations and followed by SVC plus privileged WFI.
It has a closed instruction-count oracle of4*N user instructions, r0=N/r1=0,
precise final flags and terminal state. The ON aggregate must account for every
one of those instructions across all segments, with one cold block and
segment-blocks-minus-one warm blocks per segment. Ordinary asynchronous host
exits may split the workload and conservatively recompile; intermediate PC,
cumulative countdown/flags and final terminal state must still be exact. Total
process CPU and measured cold/warm counts include those boundary costs. Same-built OFF and a stock witness-required negative
must match all16registers/fullCPSR. No observer pause/input occurs during the
workload. Completion observation and its uncertainty must be disclosed; low
CPU-tick resolution must never be converted into a fabricated short-run ratio.

This is a bounded decision experiment, not an alternate iOS engine roadmap
without stopping points. If complete warm workload CPU cost loses to TCG,
first attribute overhead to the repeated MMU/snapshot/dispatcher boundaries;
do not add instructions merely to expand synthetic test counts. A materially
better path would require validated cached block metadata plus a live-MMU data
adapter and meaningful larger execution windows without weakening exits. If
cost remains dominated by safe boundary work, stop this hybrid direction.

Before any production usefulness claim: measure actual iOS privileged/EL0 and
ARM/Thumb hot-code fractions on an unchanged production engine. High EL0 ARM
coverage is necessary for this limited path; otherwise Thumb/other engines
are a separate explicit feasibility decision. Next qualify actual memory and
SMC/context changes, precise fault/IRQ/timer/debug exits and real board PL0
CP15/GLES handoffs. Build the proven library for ARM64 Linux, run the same
finite differential tests there, then Android app-UID JIT/cache/lifecycle tests
on both4/16KiB hosts. Only then measure an isolated real iOS guest workload
against the production TCG baseline and decide whether a product option helps.
KVM/AVF cannot replace this ISA/MMU work. No candidate speedup, ARM64 support,
Android installation or iOS CPU change is claimed from source or x86 results.
