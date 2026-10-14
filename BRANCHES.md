# Integration and branch policy

`main` is the consolidated desktop and Android/Linux backend development branch.
The normal merge retains the existing desktop/device fixes as well as the
reviewed portable engine; it does not establish target qualification for a new
combined engine binary.

| Branch | Status after the 2026-10-14 reconciliation |
| --- | --- |
| `main` | Consolidated integration; base new reviewed changes here. |
| `port/android-backend` | Retained port checkpoint/compatibility entry; the integrator keeps it synchronized with `main`, not an independent engine product. |
| `work/android-backend` | Active backend lane. Exact tested producer `d9a00001ad` and all its ancestors are now reachable from `main`. Later CPU experiments remain separate and unqualified until reviewed. |
| `work/android-graphics` | Active graphics lane; `7f469781ba` reconciled, including reviewed renderer changes and useful selected-ANGLE provenance tooling. Older diagnostic documentation remains available in history; current canonical target plans govern execution. |

All existing fork branches were accounted for, with adapted cherry-picks kept as
the canonical final implementation. Worker ancestry is retained without
reapplying obsolete variants. No active worker branch/worktree was deleted or
reset. Producer identities named by existing engine/source/notice receipts,
including `259e4ad3` and `d9a00001ad`, remain reachable independently of patch
equivalence. Exact kit references are not rewritten to the new merge revision.

## Device fixes retained from the previous main

The previous desktop `main` (`e169e18b17`) contained four real fixes absent from
the tested port checkpoint `3e17ae14d4`: RGB scaler source stride, A4 output
gamma/CLCD frame IRQ, M2 LCD output gamma, and 32-bit AAC output slots. This normal
merge retains all four. The sole conflict in A4 `vbl_tick` retains both the CLCD
frame interrupt update and portable visible-console painting at vsync.

Focused production slices pass under ASan/UBSan for scaler, A4/M2 gamma, LCD
readback, AAC, A4 vsync (normal/stall/Apple variants), deferred publication and
frame deduplication. Two test dependencies were updated for the combined
production code. These checks are not a full engine build, a Mac application
regression, or Linux/Android guest acceptance. Existing APK26/engine results
continue to identify their earlier exact producers. New combined engine builds
and target regression must be recorded separately. No optional upstream120Hz
refresh feature or CPU backend replacement is introduced by reconciliation.

Reuse assigned lanes. Review unmatched changes and owner status before retiring
a branch, preserve exact producer references, and keep legal source checkpoints
pushed. No force push, unrelated dirty-worktree cleanup, private asset upload or
automatic inclusion of later experimental lane commits is authorized by this
policy.
