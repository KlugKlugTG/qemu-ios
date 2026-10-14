# Android branch policy

`port/android-backend` is the canonical Android backend integration branch.
Reviewed worker changes land there, usually by cherry-pick. `main` preserves the
original desktop lineage. Worker branches are ownership and source-provenance
lanes, not separate supported engines.

| Branch | Purpose and status |
| --- | --- |
| `port/android-backend` | Canonical Android backend integration. |
| `work/android-backend` | Retained exact engine producer history, available for explicitly assigned backend work. |
| `work/android-graphics` | Graphics source and target-qualification lane. |

The 2026-10-14 remote audit found four branches including `main`, with no backup
or scratch branches. The backend lane alone retains thirteen exact producer
revisions named by existing source/notice receipts, including `259e4ad3` and its
ancestors. Equivalent cherry-picked integration code does not preserve those
commit identities. Keep that history reachable until an intentional producer tag
or canonical ancestry preserves it; do not delete the branch merely because its
tracked tree matched an integration snapshot.

Reuse assigned lanes rather than creating a branch per build. Close a lane only
after owner confirmation, review of unmatched changes, and preservation of exact
producer references. Retire redundant remote branches and clean worktrees then;
leave unrelated dirty worktrees untouched. Periodically push reviewed source
checkpoints, without private assets or device evidence. Default branch changes,
force pushes and release claims require separate review.
