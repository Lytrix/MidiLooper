## Status

**This task list is deliberately small.** It replaces the window-first replacement section that was removed
from [`display-freeze-under-overdub-chain`](../display-freeze-under-overdub-chain/tasks.md) (that material is
parked in [`docs/Plans/future_root_independent_playback.md`](../../../docs/Plans/future_root_independent_playback.md)
and is not authorized here).

Artifact creation is **documentation only**: no firmware/test edit, build, upload, transport gesture or SD
operation was performed. Tasks 5–7 stay unchecked until task 3 or 4 names the boundary; task 2 stays
unchecked until a RED is observed. No new architecture is authorized anywhere in this list.

## 1. Pin the last-good and first-bad states

- [ ] 1.1 Confirm the image boundary from records, not memory: last-good `852b15f7…` (session
  `session_20261029_004321.log`), first-bad `0dbf08c1…` (`session_20261029_011304.log`), revert image
  `c06f2e43…` (owner report only). Re-measure the A/B counts in
  [BOUNDARY-2026-10-29.md](evidence/BOUNDARY-2026-10-29.md) and correct any row whose provenance cannot be
  established (e.g. `session_20261029_012142.log` sits between flashes). Verify no image identity is taken
  from a source comment.
- [ ] 1.2 Read-only card inventory: dump the current slot's pass/root/pass-edit inventory and diff it against
  the pre-regression backup `captures/sd-backup-20261029_001835/`. Record whether the live body is still
  passes-only (unrepaired), already republished (DEC-192 repaired it), or never damaged. **No SD write, no
  delete, no rename.** Verify the conclusion does not depend on the withdrawn 01:55 causal story.

## 2. Reproduce the regression (RED first)

- [ ] 2.1 Build the production-path fixture in `test_runtime_working_set_ownership`: seeded passes-only slot,
  production admission/Start, ordinary deferred/rolling/MIDI turns, advancing clock, **DEC-192 recovery request
  disabled**, no frozen-clock rescue and no injected ready source. Verify it REDs at the first due MIDI event
  while the historical repair is disabled, and record the first failing assertion plus the exact patch that
  disables the recovery. A fixture that only passes because DEC-192 recovery runs later is not a reproducer.
- [ ] 2.2 Confirm the same fixture is GREEN on the last-good behavior (recovery enabled is acceptable here as
  the control, or the pre-DEC-191 tree in a scratch worktree). Verify the two runs differ in the same assertion,
  not merely in timing.

## 3. Diff the DEC-191 deltas to the smallest switching mutation

- [ ] 3.1 Enumerate the production behavioral deltas D1–D5 from [design](design.md#dec-191-behavioral-deltas--the-complete-candidate-set)
  against the actual tree, and verify each is either absent, reverted, or reachable from a production call site.
  Record reachability per delta; do not test a delta that no production path can enter.
- [ ] 3.2 Run the minimal mutation matrix on a scratch tree: D4 alone, D3 alone, D1+D2 alone, then combinations
  that remain reachable. Verify Oracle A reds on exactly the smallest mutation and greens otherwise; record the
  matrix. Remove every scratch mutation before proceeding (verify `git diff` contains none of them).

## 4. Name the first missing producer or opportunity

- [ ] 4.1 For the winning mutation, name the producer that stops running (publication request, source admission,
  or reader qualification), its owner, and its refusing predicate. Verify the claim against the identical
  last-good path, not against the rejected whole-root recovery. Record it in `evidence/` with the exact
  call sites and one bounded before/after trace.
- [ ] 4.2 Decide the fork honestly: if the matrix reproduces nothing, or the device A/B shows identical playback
  on both images, record **data path** (save/publication parity) as the finding and stop; do not repair inside
  this change. Verify this decision is written down before any patch is proposed.

## 5. Repair only that boundary (gated)

- [ ] 5.1 Fill the G2 execution packet for the named boundary ([design](design.md#execution-packet--first-implementation-slice-task-2-only)
  is the template) and verify one focused independent review approves the contract before code. Reuse the
  existing owner; no new state, queue, lifetime or persistence representation.
- [ ] 5.2 Apply the minimal repair only. Verify Oracle A passes, Oracle C holds (diff names one boundary, no new
  reader/window producer/pool), and the DEC-192 recovery code is neither deleted nor depended upon. A patch that
  makes the fixture pass by enabling recovery-on-demand is rejected.

## 6. Keep the 7.5 Hz fix (second oracle)

- [ ] 6.1 Production-carrier fixture: unchanged refusal tuple across N owner turns. Verify
  `overdubStartBodyEntryCount()` does not advance, the exact Start is retained, and playback on other sounding
  slots continues. Verify the pure `OverdubStartGate` decision test is not counted as this proof.

## 7. Gates

- [ ] 7.1 Focused native suites (`test_runtime_working_set_ownership`, `test_continuous_capture`,
  `test_storage_manager_load`, `test_live_capture_display_boundary`), then full `pio test -e native`; record
  actual counts and any characterization failure explicitly.
- [ ] 7.2 `pio run -e teensy41-capture-serial`; record RAM1 variables/ITCM and the HEX SHA-256. No upload until
  the owner authorizes it in that session.

## 8. Device A/B

- [ ] 8.1 List registered stimuli, fill the HITL recipe, and reuse a non-destructive saved-set playback gesture
  (no record/clear on the owner's set). Verify no auto-heartbeat, source mutation or SD action is implied.
- [ ] 8.2 Run last-good vs repaired on the **same pinned body**: first due event after Start, `playback_read`
  non-zero, `out=` note-ons, no second Play press, no Stop/restart. Record the exact image hashes, boot proof
  and separate USB output evidence. A repaired card that shows playback on both images is not acceptance.

## Not authorized by this list

Window-first/root-independent playback, standby pools, any-slot coverage, lifetime or working-set changes,
restore/re-record, SD deletion or rename, bulk boot hydration, MIDI-tick gather, automatic upload, archive.
Reopening the parked architecture requires a recorded reason (last-good playback depended on an unsafe
accident) and explicit owner approval.
