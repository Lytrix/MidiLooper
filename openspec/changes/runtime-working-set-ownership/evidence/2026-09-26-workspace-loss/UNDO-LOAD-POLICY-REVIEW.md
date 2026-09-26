# Undo after load — correction and proposed contract

2026-09-26. Original review below is preserved. **Approval received subsequently:**
the user approved reconstruction and clarified that redo starts empty, then arises
from in-session Undo. DEC-088 — load-time undo reconstruction resolves this policy
blocker. See UNDO-LOAD-FIX.md for implementation and verification.
User authorized the next fix after the host reconstruction result. Discovery found
that the proposed behavior conflicts with an explicit existing contract, which was
missed in the preceding diagnosis.

## Established evidence (G0 discovery)

- VERIFIED: the preserved library supports 122 derived active-content actions;
  the host roundtrips pass for all 60 nonempty loops. Original cross-slot chronology,
  old redo branch and clear snapshots were not reconstructed.
- VERIFIED: `applyLoadedLoopSlotBodyCommit` installs the LSV1 published root then
  clears slot undo deliberately. `test_lsv1_load_commit_skips_rebuild` pins this.
- VERIFIED: `StorageManager::processDeferredUndoSnapshots` reads legacy bundle
  history then calls `TrackUndo::rebuildTrackFromLoopContent`; legacy raw slot
  loading uses `rebuildSlotFromLoopContent`. The observed nine surviving actions
  are the result of the mixed load/hydration sequence, not a guarantee of the spec.
- VERIFIED: [DEC-057 — snapshot body format](../../../../../docs/DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)
  says “runtime undo depth = 0” and “Do not invent `U:` depth from passes.”
- VERIFIED: the active [working-set spec](../../specs/runtime-working-set-ownership/spec.md)
  scenario “Undo history is not persisted” requires zero undo depth after reboot.
  Its [design](../../design.md) explicitly reserves changing this policy for later.
- VERIFIED: the newer [snapshot content-history delta](../../../immutable-loop-snapshot-passes/specs/loop-content-history/spec.md)
  replaces the older archived content-history rule with no reconstruction of
  pre-reboot operations. Reading only the older spec leads to the wrong diagnosis.
- CONFLICT: treating history reconstruction after every load as a bug fix would
  silently reverse these requirements. Formal trigger: undo semantics / authority
  conflict under `docs/ARCHITECTURE_REASSESSMENT.md`.
- UNKNOWN: general legacy compatibility coverage, new memory cost and device timing
  of the changed policy. Host results from this preserved library do not settle them.

G0 verdict: evidence sufficient; implementation BLOCKED on policy decision.
The older diagnostic bundle is evidence only, not a new persistence format or an
approved mechanism to store undo lineage. The production solution should derive
history from existing content, not write nonempty runtime undo stacks.

## Proposed behavior for approval

**On loading a file that retains sufficient compatible pass records, reconstruct
its per-slot undo actions from those records. Start at the saved content tip with
redo empty. Preserve the saved snapshot as the current content authority.**

This deliberately changes normal load/reboot behavior for those files, not only
this one host recovery. It does not promise the original global gesture order,
clear snapshots, a previous redo branch, or undo for formats lacking the records.
Do not fabricate missing actions or silently reinterpret a snapshot-only file.

Recommendation: approve this explicit policy if undo of preserved overdub layers
after reboot is desired. Extend existing `StorageManager`/`TrackUndo` owners; do
not add another owner, new saved undo payload, or a new SD format for this fix.
The alternative is retain the current zero-after-reboot policy and keep the
reconstructed actions solely as an offline diagnostic artifact.

## Concrete implementation and acceptance boundary after approval

- Amend the zero-after-load clause of the snapshot decision and both active deltas
  before changing production behavior; keep snapshot content authority intact.
- Slot commit and deferred undo hydration must produce the same derived per-slot
  units regardless of ordering. Repeated hydration must not duplicate actions;
  background completion must not move selection or replace another slot's history.
- Reuse `rebuildSlotFromLoopContent`/`rebuildTrackFromLoopContent`, with the load
  publication boundary explicitly tested. Do not solve this by a blanket late
  rebuild on a user gesture or adding a full-library synchronous scan.
- Baseline recovery bundle must retain empty undo headers. Test all 122 expected
  action identities/groupings after real deferred load, without a test-side rebuild
  supplying missing history. Reload must reconstruct the same units from content.
- Exercise hydration before, during and after slot loads; legacy raw and LSV1;
  repeated load; selected-slot focus; corrupt/unsupported body rejection and stale
  load cancellation. No history becomes visible from a refused load.
- Perform full Undo/Redo after loading, checking materialized MIDI bytes, published
  note identities and length; repeat after reload. Earlier root-baseline correction
  remains required for raw-format loops, without replacing the undo state under test.
- Keep no extra old snapshot roots pinned by derived history. Measure firmware RAM1
  against the existing approved floor; do not silently grow it. Load commit already
  executes through the stopped deferred owner; no new synchronous realtime work.
- Run focused native and full native regressions plus capture-serial build. Device
  timing and persistent hardware restore remain unverified until separately run;
  incident hold and standalone reader remain in place.

## Review and limits

G2 red-team review: BLOCKED by the explicit policy conflict, not by speculation.
TIGHTENING for the eventual contract: compatibility pass data must not displace a
saved root; passing tests on this library is not blanket permission to replay any
historical file. Memory/timing and mixed load ordering require the tests above.
No fixture, production patch, upload, reset, serial operation or card write was
performed in this review. No stage/task implementation checkbox was closed.
