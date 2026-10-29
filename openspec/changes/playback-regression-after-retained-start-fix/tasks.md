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
- [x] 1.2 Read-only card inventory (**done 2026-10-29**). Attached to the running image without reset and sent
  only `!LIB_OBSERVE` (`scripts/hitl_boot_census.py --library-observe --no-reset`); no upload, no SD write, no
  transport gesture. Live state: 16 committed slots, 8 required for initial playback, 16 pass-backed and 7
  root-backed after DEC-192 recovery. **Restore-time shape is rootless on both sides:** the pre-regression
  session (`session_20261029_002728.log`) shows `root_rev,0` after restore yet plays (`playback_read` at 168.2 s,
  `lcr … from=passes, root_rev=0`), while the first-bad session never builds a root. Verify next time that a
  conclusion is not drawn from the window (it reported `complete=0`) but from the per-slot rows plus the boot
  logs. **The 00:18 backup is not a body reference** for this slot (78-byte stub, no 23808-tick body), so do not
  plan a byte-level body diff against it. Evidence:
  [BOUNDARY-2026-10-29.md](evidence/BOUNDARY-2026-10-29.md#card-inventory--2026-10-29-read-only-no-upload-no-sd-write).

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

- [x] 3.1 Enumerate the production behavioral deltas D1–D5 from [design](design.md#dec-191-behavioral-deltas--the-complete-candidate-set)
  against the actual tree and record reachability. **Done:** no DEC-191 production call site remains; only
  helpers plus tests (`overdubStartPreflight`, `overdubStartRetryDecision`,
  `settleOrphanedSessionExitReservation`). See [delta review](evidence/2026-10-29-DELTA-REVIEW.md).
- [x] 3.2 Review each delta for owner eligibility/order, claim lifetime, and early returns before the playback
  producer. **Done:** D1/D2 low; D3 medium (claim lifetime, Start path); D4 high for the `011304` signature but
  its removal did not restore playback; the playback lane (`main.cpp:571`) is not gated by the retained latch,
  so turn consumption is *not* the playback cause. Verify the two signatures do not match before choosing the
  next experiment.
- [ ] 3.3 **D4 single mutation** on a scratch worktree (only the unconditional idle-lane settlement restored):
  verify whether the `pub=1` / `hold_gap` / `playback_read 0` signature returns with a decoy Record stop. If it
  does, D4's claim release is the named boundary; if not, record NON-REPRODUCTION and do not infer.
- [x] 3.4 **Sticky-invalidation probe** (native, no device): rootless passes slot + Start + transport restart.
  **Done — RED, and the simple hypothesis is excluded.** The demand survives, adopts the new epoch, is not
  born-rejected, keeps the turn eligible and has no gather refusal; readiness stays `OwesBuild` and no serving
  window is published in 256 production turns. The stall is inside the build's own admission/service.
  Fixture `test_dec193_a_restarted_rootless_slot_keeps_owing_a_build_that_is_never_served`; 2 failures out of
  251 (the other is the pre-existing frozen-packet characterization). Evidence:
  [sticky-invalidation RED](evidence/2026-10-29-STICKY-INVALIDATION-RED.md).
- [x] 3.5 **Name the build refusal (retry gate).** **Done — report-only, negative for this site.** Added
  `PlaybackBuildRefusal` + `playbackBuildRefusalName` (`include/Utils/PlaybackBuildRefusal.h`),
  `Track::playbackBuildGateRefusal` (the gate's order, written once; `committedPlaybackRetryCanProgress`
  delegates), and a once-per-change `playback_build,<token>` row in `stepCommittedPlaybackRetry`. The fixture now
  also asserts the holder offer and the gate term: the slot **is** an eligible holder and the gate reports `none`.
  Build SUCCESS, RAM1 unchanged, RAM2 +64 B; focused suite still 248 passing. Evidence:
  [sticky-invalidation RED](evidence/2026-10-29-STICKY-INVALIDATION-RED.md#naming-probe-report-only--2026-10-29).
- [x] 3.5b **Name the step's branch.** **Done — the chain is named.** Eligible holder, but `canProgress` false because
  the gate answers **`gate_no_role`** (no build role in the prewarmed view set); the selector skips it
  (`dispatch_no_holder`) and the committed retry is never reached, so readiness stays `OwesBuild`. The defect is a
  **view-role leak** after position invalidation. Report-only; target build SUCCESS, RAM1 unchanged, RAM2 +128 B.
  Evidence: [sticky-invalidation RED](evidence/2026-10-29-STICKY-INVALIDATION-RED.md#dispatch-probe--2026-10-29-35b-and-the-chain-it-names).
- [x] 3.5c **Device correlation (minimal) — DIVERGENT.** Flashed `3f74ecd5…`, captured
  `session_20261029_032402.log`: at 20.16 s `playback_build,pending_position_invalidated` for t7s3, 1 ms later
  `snapshot_unit,kind=start`, root published at 58.49 s (**~38.3 s**), zero `gate_*` rows. The device answers an
  invalidated demand with whole-root publication, not a window build; without a retained source it never reaches the
  role question. **3.5d is therefore not authorized.** Evidence:
  [device run 2](evidence/2026-10-29-STICKY-INVALIDATION-RED.md#device-run-2-session_20261029_032402log--the-device-answers-with-whole-root-publication).
  (Superseded recipe text follows for the record.) Image built and pinned:
  HEX SHA-256 `3f74ecd5f07fc283438850ec71dd403cc4fb18fc0fff06b0951d89179f9f0718`, bundle
  `captures/dec193-naming-image-20261029/` (firmware.hex, sha256, git-head, git-status, build-flags).
  Recipe: capture from boot, play the restored rootless slot, then transport Stop → Start → Stop inside the first
  window; look for `playback_build,gate_no_role` / `dispatch_no_holder` after `pending_position_invalidated`.
  Acceptance: same name → 3.5d; different name → the fixture lacks one production condition; **no row at all** → the
  maintenance owner is not being serviced, which moves the boundary up one level and forbids touching role retirement.
  No SD write, no record, no slot change. Do not flash without the owner's word in the session that flashes.
- [ ] 3.5d **Repair candidate — retire the leaked role (NOT AUTHORIZED; device diverged).** When a demand's position is invalidated, retire the
  stale view role so `coverageRoleAvailable` is true for the next epoch. One existing owner, no new state; verify both
  oracles and that no accepted content or sounding slot is disturbed. Not authorized until 3.5c agrees on the name.
- [ ] 3.6 Only if 3.5 names nothing actionable, build **one two-state pair** on a scratch worktree from
  checkpoint `a3d56f07`: `reconstruction-control` (no DEC-191 behavior) and `reconstruction-dec191` (same tree
  plus DEC-191 behavior), every unrelated DEC-192/post-02:00 edit identical and the DEC-192 recovery request
  guarded off in the fixture. Verify the pair differs only in the DEC-191 deltas and that playback toggles.
  Name them as reconstructions, never as `0dbf08c1`/`3e79bd1b`.
- [ ] 3.7 If 3.6 toggles, split behaviorally: D1+D2 only, D3 only, D4 only, then all. Verify Oracle A reds on
  exactly the smallest mutation and greens otherwise; record interactions instead of forcing a single winner.
  Remove every scratch mutation before proceeding (verify `git diff` on the primary tree contains none).

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
  and separate USB output evidence. Retain each programmed HEX per
  [TEENSY_FIRMWARE_UPLOAD](../../../docs/Guides/TEENSY_FIRMWARE_UPLOAD.md#procedure) step 8 — the four
  2026-10-29 images are already unrecoverable. A repaired card that shows playback on both images is not
  acceptance.

## Not authorized by this list

Window-first/root-independent playback, standby pools, any-slot coverage, lifetime or working-set changes,
restore/re-record, SD deletion or rename, bulk boot hydration, MIDI-tick gather, automatic upload, archive.
Reopening the parked architecture requires a recorded reason (last-good playback depended on an unsafe
accident) and explicit owner approval.

## 9. Bounded rootless first window (DEC-194 — owner-directed)

Direction (2026-10-29, owner): rolling/windowed playback over the **authoritative committed passes**; a whole-loop
root is an **optional acceleration source**, not a precondition. One reader, several source providers — rootless vs
rooted changes how a window is *filled*, never how MIDI is played afterwards.

- [x] 9.1 Wire the retained committed source into the production demand. `requestCommittedPlaybackWindowForSlot`
  establishes it for a rootless slot being asked for playback (`retainPlaybackSourceForSlot`, passes/chunk pins only,
  idempotent). Verify the existing suite is behaviour-neutral and the target build is unchanged in RAM1.
- [ ] 9.2 Acceptance fixture (RED): `test_dec194_a_rootless_playing_slot_builds_its_first_window_without_a_root` —
  committed passes, no root, `TRACK_PLAYING`, no fixture-side retention; require a serving window with
  `root_rev = 0`. Verify it fails before 9.3 and passes after.
- [x] 9.3 **Name the transient role refusal — `role_promise_pending`.** `coverageRoleRefusal()` names the first false
  term (pure mapping test passes); the acceptance fixture reports `role_promise_pending` at the gate
  (`completedPromisePendingJudgement`), so the gate answers `gate_no_role` and the retry never runs. This is the
  lane's **own** completed promise awaiting judgement — not a stale owner, so retirement is the wrong repair. A naive
  gate bypass crashed the suite (179/253) and was reverted. Evidence:
  [9.3 result](evidence/2026-10-29-DEC194-FIRST-WINDOW-WIP.md#93-result--the-blocking-term-is-role_promise_pending).
- [x] 9.4b **Trace the pending judgement — the clearer had no caller.** Set on completion; cleared inline only when the
  completion lands inside its window. A late completion leaves the flag set, and `revalidateCompletedPlaybackWindow`
  (the only other clearer) had **no production caller**, so the role stayed blocked forever. Wired it at the lane
  boundary before the gate (guarded; no-op when ineligible). The acceptance fixture is **still RED** — one of the
  clearer's own preconditions is unmet — so this is WIP, not acceptance.
- [ ] 9.4c **Name `revalidateCompletedPlaybackWindow`'s first false precondition** (same one-enum/one-row pattern) and
  fix that term only. Do not relax the boundary rule: judging a late completion early re-grants a deadline the spec
  forbids. Verify the acceptance fixture greens and DEC-193 stays named.
- [ ] 9.4 Rolling handoff: prove the second and third windows are built and adopted without a whole-root publication,
  with the same reader.
- [ ] 9.5 Scaling property: 4-bar vs 40-bar first window in the same cost class (cost proportional to the content
  intersecting the requested window, not bars × passes × events).
- [ ] 9.6 Keep DEC-192 whole-root publication as background only; verify enabling/disabling it cannot gate playback.
