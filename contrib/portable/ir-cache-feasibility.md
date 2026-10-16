# Persistent frontend IR cache: feasibility measurements

`ir-cache-feasibility.py` reads bounded retained native logs, their cache receipt
and its runtime-result hash binding. It never starts a guest, reopens a writable
store, repairs state or qualifies a run. Failed/dirty evidence remains labelled.
Use it alongside the independent owner's scene/shutdown/retirement reviews:

```
python3 contrib/portable/ir-cache-feasibility.py \
  --native-log /private/trial/exploration01/stderr.log \
  --cache-receipt /private/trial/exploration01/cache-trial.json \
  --runtime-result /private/trial/exploration01/RESULT.json
```

Existing hits+misses+rejected count key-eligible candidate lookups after the
store was enabled. Rejected means a record was found but could not be replayed;
it does not count all unsupported instructions/capture failures. Neither this
denominator nor stored/misses gives the fraction of all translated ARM TBs that
are A32 eligible. Thumb, unsupported CPU/context and capture-rejection counters
are not currently recorded. Translation attempts also differ from executed
instructions: repeated execution of an existing native TB does not enter the
persistent-cache lookup path. The report leaves unavailable quantities null.

The format limits are4096records/32MiB. The first recorded sample at the record
limit gives lookup/store yield at that sample, not its elapsed time. Final
inventory bytes distinguish count saturation from byte saturation. Every
successful new publication calls file fsync and directory fsync on the caller's
vCPU path;2*stored is a lower bound on fsync invocations including possible extra
failed attempts. Directory-fsync success is not recorded. Durations cannot be
inferred from call counts or from untimestamped native log milestones.

The supported subset is deliberately narrow: ARM1176 little-endian A32 integer
instructions, one page and a closed symbolic IR set. Thumb, helper calls,
privileged/coproc/VFP/SIMD instructions and other guest CPUs are excluded. A hit
still hashes context/source bytes, validates/decodes/replays the retained IR,
optimizes it and emits fresh native code. No native machine code is persisted.

Keep the current engine frozen for actual matched OFF/COLD/REUSE correctness,
strict shutdown and fresh-worker hit coverage first. Do not change store limits,
durability semantics or enablement while comparing those arms. A feasible
speedup needs frontend decoding to consume a material fraction of workload CPU
and saved decoding to exceed lookup/replay/record-store overhead. GPU/execution,
IR optimization/emission, I/O and repeated already-translated execution remain.
No observed candidate hit ratio alone establishes those conditions.

If actual replay is useful, a later separately pinned diagnostic engine should
collect fixed aggregate counters only with explicit opt-in: total translated ARM
TB attempts, Thumb/other-context rejection, key-eligible attempts, unsupported
capture/IR rejection, hit/miss, capacity skips and first-capacity elapsed time.
Aggregate monotonic wall/thread-CPU spans should separately cover frontend
translation, key hashing, replay, optimization/emission and cache store, including
file/directory fsync counts and wall time. Preserve original final cache telemetry
and avoid per-TB logging. Include measurement overhead and emitted-sample bounds
in tests. Such an engine invalidates existing cache keys and therefore needs its
own COLD producer and matching REUSE. These diagnostic counters are proposed,
not implemented or measured by the offline report.

Tests are synthetic offline contracts:

```
python3 tests/portable-ir-cache-feasibility.py -v
python3 tests/portable-ir-cache-session-report.py -v
```

Default cache remains OFF and unqualified. The offline report makes no Android
qualification, actual iOS reuse or performance claim.
