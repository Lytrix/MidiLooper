# DEC-194 — bounded rootless first window: work in progress

Owner direction (2026-10-29): rolling/windowed playback over the **authoritative committed passes**, with a whole-loop
root as an **optional acceleration source**, not a precondition for liveness. The measured defect this targets is the
device's ~38 s wait: `session_20261029_032402.log` shows `pending_position_invalidated` → `snapshot_unit,kind=start` →
root only at 58.49 s.

## What landed (production)

`Track::requestCommittedPlaybackWindowForSlot` now establishes the source for a rootless slot that is being asked for
playback:

```cpp
if (!loop.publishedSnapshotIsCurrent() &&
    servingRetainedRevisionForSlot(slot) != loop.playbackRevision) {
  const PlaybackSourceRetainResult retained =
      retainPlaybackSourceForSlot(slot, loop.playbackRevision, playbackGeneration);
  if (retained != Admitted && retained != AlreadyRetained)
    return PlaybackViewRequestResult::SourceUnavailable;
}
```

Why this is the right seam: `retainPlaybackSourceForSlot` pins **passes and committed chunk ids only** — no event
gather, no root, no content copy — and it is idempotent (`AlreadyRetained` refreshes the role without re-owning a
body). It had **no production caller at all** before this slice; the mechanism existed but was never wired.

## What is proven

- Behaviour-neutral for the existing suite: 252 cases, 248 pass (3 failures: this new acceptance RED, the DEC-193
  RED, and the pre-existing frozen-packet characterization).
- `teensy41-capture-serial` SUCCESS, RAM1 `variables 96,192 / code 391,656 / padding 1,560` unchanged.
- The retention call itself cannot grow with loop length: it enumerates passes and chunk ids, not events.

## What is NOT proven — the acceptance fixture is RED

`test_dec194_a_rootless_playing_slot_builds_its_first_window_without_a_root` (device shape: committed passes, no root,
`TRACK_PLAYING`, no fixture-side retention) still fails to reach a serving window.

Observed with the probe from 3.5b:

- the lane reports **`gate_no_role`** (`coverageRoleAvailable` false) during the first turns;
- after the loop the role *is* available again, so the refusal is **transient**, and the demand never recovers from it;
- no serving window is published in 256 turns.

So wiring the source is necessary but not sufficient: the first window is refused at role arbitration while the
prewarmed view set is still settling. That is the next term to name, not a reason to widen the slice.

## Reverted, deliberately

- A candidate change to `requestCommittedPlaybackWindowForSlot`'s `holds()` (treat unqualified/invalidated demands as
  not holding the interval) was tried and **reverted**: it is a plausible lifecycle fix, but it did not turn the
  acceptance fixture green, and an unproven semantic change in the demand lifecycle does not belong in a WIP commit.
  It stays a hypothesis with its own RED to write.
- The noisy per-turn diagnostic asserts used while narrowing the blocker were removed; the probe rows
  (`gate_*`, `dispatch_*`) remain in the production image.

## Next

1. Name **which** `coverageRoleAvailable` term is false on the first turns of a rootless `TRACK_PLAYING` slot, with the
   same one-enum/one-row pattern (`viewBuild.active`, `completedPromisePendingJudgement`, `ownsCoverageSource`,
   `canonicalWindowPhase`, build-role published/complete).
2. Fix only that term (identity-scoped), then re-run the acceptance fixture: `root_rev 0` allowed, serving window
   required, whole-root publication still absent.
3. Then the rolling handoff and the 4-bar vs 40-bar cost-class assertion.

## 9.3 result — the blocking term is `role_promise_pending`

Mechanism: `LoopPlaybackRuntime::coverageRoleRefusal(keepCaptureReady)` now names the first false term of
`coverageRoleAvailable` in its own order (one implementation; availability delegates). Report-only.

- Pure mapping test `test_dec194_coverage_role_refusal_names_the_first_false_term` **passes**: a fresh prewarmed
  runtime is `role_available`; forcing `completedPromisePendingJudgement` yields `role_promise_pending` with
  availability false; forcing `canonicalWindowPhase = Merge` yields `role_canonical_busy`; clearing restores
  availability. Availability is exactly `term == none`.
- The acceptance fixture now names its blocker at the gate: **`role_promise_pending`** for slot 0
  (`completedPromisePendingJudgement` true), which is why the gate answers `gate_no_role`, the selector skips the
  holder, and the retry never runs.

So this is the second category the review named: **not a stale owner** (retirement would be the wrong repair), but a
role occupied by the lane's *own* completed promise awaiting judgement. The shape is a self-deadlock candidate: the
gate refuses the turn that would judge the promise.

### The naive bypass was unsafe and is reverted

Treating `CompletedPromisePending` as progress in the gate was tried and **crashed the suite** (179 of 253 cases),
so it is reverted. It is not a fix candidate until the judgement path is traced: where
`completedPromisePendingJudgement` is set, who clears it (`revalidateCompletedPlaybackWindow` /
`finishPlaybackViewGrain`), and whether that site is reachable on a turn the gate admits.

Suite after the naming slice: **253 cases, 3 failed, 249 pass**; `teensy41-capture-serial` SUCCESS, RAM1 unchanged.

### Next (9.4)

Trace the judgement path and prove, with a RED, which turn is supposed to clear the flag; then either
(a) let that turn run before the gate (reoffer), or (b) clear the flag at its own completion site. Do not retire the
role — the owner is current.

## 9.4b — the missing caller is real, but wiring it is not yet sufficient

`completedPromisePendingJudgement` lifecycle, traced:

- **set** on build completion with a qualified epoch (`TrackPlaybackWindowBuild.cpp` completion path), then cleared
  inline **only** when the completion is inside its own window: not invalidated, same epoch, `admittedTick <= finished
  < deadlineTick`, no missed traversal, same loop length.
- A completion **at or after** its deadline therefore leaves the flag set. The only other clearer is
  `Track::revalidateCompletedPlaybackWindow`, and it had **no production caller at all** (verified by grep: definition
  and tests only).

That is a real missing handoff, and it matches the "missing reoffer" category rather than a role leak.

Wired it at the lane boundary (before holder selection / the gate), per slot, with the loop-relative phase:

```
for each slot with completedPromisePendingJudgement:
  revalidateCompletedPlaybackWindow(slot, currentPhase % loopLength, clock)
```

Result: **still RED** — the acceptance fixture does not reach a serving window, and the first gate-time role term is
still `role_promise_pending`. So one of the clearer's own preconditions is not met in this state. Candidates, in its
order: `coverageBuildNeedsService()` still true; boundary phase never matches `candidate.coverage.startPhase`;
`clock.tick < deadlineTick + admittedLoopLength` (judgement is only legal one loop after the deadline); or the source
identity moved.

The call is guarded and precondition-checked, so leaving it wired is a no-op for every state that is not eligible; it
is kept as WIP, not as acceptance.

### Next

Name `revalidateCompletedPlaybackWindow`'s first false precondition the same way (one enum, one row), then fix the
actual term. Do not loosen the boundary rule itself: judging a late completion early is exactly the "silently
re-grant the deadline" behaviour the spec forbids.

## 9.4c — the revalidation's preconditions, and a publish-path crash

`revalidateCompletedPlaybackWindowRefusal()` now names the first false precondition of the revalidation in its own
order (24 terms; the predicate itself delegates to it, so verdict and name cannot disagree).

Observed while a temporary lane caller drove it on the acceptance fixture:

- first standing refusal: **`rv_phase_boundary`** — judgement requires `currentPhase == candidate.coverage.startPhase`
  exactly, and the lane samples at arbitrary phases;
- later turns move through the "one loop after the deadline" terms (`rv_too_early`, `rv_not_aligned`), so a late
  completed promise is only judgeable at an exact later boundary;
- **driving it far enough to actually publish crashed the native harness (179 of 253 cases)**, twice, with 4096-turn
  windows. The execution path is therefore **not wired**: the lane caller and the reporting table were removed, and
  only the naming helper remains.

That crash is important in itself: the publish-after-rejudge path has no production caller today, so a route that
reaches it is unproven code. It must not be enabled by wiring alone.

### What this implies for the repair

The first window in the measured state is not recoverable by re-judging: the promise completed late, and the only legal
recovery boundary is one full loop later, after which publication takes the unproven path. The honest options are:

1. **Make the first build land inside its window** (solve the timing at admission/lead), which avoids the late-completion
   path entirely; or
2. **Prove and harden the re-judge/publish path** with its own RED and crash-free evidence before any caller exists.

Option 1 is the smaller, safer answer and matches the owner's framing: a bounded first window should be admitted with a
lead it can actually meet, not recovered a loop late.

### State

Suite: 253 cases, 3 failed, 249 pass (DEC-194 RED naming `role_promise_pending`, DEC-193 RED, pre-existing
characterization). `teensy41-capture-serial` SUCCESS, RAM1 unchanged. No upload, no device or SD action.

## 9.4d — three hypotheses excluded, none replaced

Tried, in order, and **excluded** by measurement (all host, all reverted afterwards):

1. **"The builder is starved of turns."** Ran the fixture at one tick per owner turn (production-like density) instead
   of 64 ticks. Still no serving window. Excluded.
2. **"Prewarm leaves a promise pending."** Asserted `completedPromisePendingJudgement == false` before the first owner
   turn. Passed. Excluded.
3. **"The completion transition is observable and late."** Sampled the flag after every turn and captured
   admitted/deadline/completed plus fill/order grains at the false→true transition. The transition was **never
   observed**, while the gate-time role term still read `role_promise_pending`.

The third result contradicts a simple late-completion model: the flag is true when the gate reads it and false when the
turn ends, so the refusal is **transient inside a turn**, and the fixture's "first blocked term" was reading that
transient rather than the standing cause. The standing cause is therefore *after* the role: something else keeps the
window from being served once the role is free.

Consequence: do not implement a lead change yet. The next instrument must be **per-turn and slot-qualified at the gate**
(turn index, role term, holder eligibility, retry entry) so a transient single-turn refusal cannot be mistaken for the
standing blocker. Suite unchanged: 253 cases, 3 failed, 249 pass.

## DEC-192 guard — publication requests are idempotent while in flight

Device evidence: `session_20261029_032402.log` shows the rootless recovery asking on every Snapshot owner turn; each
`requestCommittedPassesOnPublicationLane` began with `clearPendingSnapshotPublish()` and re-queued every pass, so the
rebuild restarted **nine times** in 38 s (`q` reset 0..2 → 7, `a` back to 0, restart interval shrinking to ~0.5 s).

Repair (at the operation, so every caller is covered — Snapshot owner, undo, future callers):

```
if (pending box && !fromLoadedPasses && coveredPassRevision == committedPassRevision()
    && rebuildFromPassInventory == rebuildFromPassInventory) return true;  // already in flight
```

Identity is the inventory the request covers (`coveredPassRevision`, DEC-073) plus the construction shape. A newer
inventory still supersedes.

Verification:

- `test_dec192_a_repeat_request_for_the_same_inventory_does_not_restart_publication`: partial progress on a two-pass
  inventory → repeat request → applied/queued unchanged; newer inventory (`++playbackRevision`) → candidate replaced
  (applied 0, queue refilled).
- Mutation: disabling the guard reds exactly that fixture (`Expected 1 Was 2 — a repeat request must not repopulate the
  queue`); restored.
- Native 254 cases / 3 failed / 250 pass (the two known REDs plus the pre-existing characterization).
- `teensy41-capture-serial` SUCCESS: RAM1 `variables 96,192`, code `391,992` (+336 B), padding `1,224` (floor 840).
  No upload.

## DEC-194 — passes-as-playback-source: proven, then gated out again

Implemented in `ensurePlaybackMergedMidiEventsBuilt` (the 2-bar merged window, long-loop branch): when the gather has
no root and no Record overlay, and the slot's committed source is qualified (`playbackWindowSourceIsCurrentForSlot`,
i.e. the retained-source pin from `8e5b47e3`), fill the same bounded window with
`loop.copyEffectiveCommittedEventsInRange(events, winStart, winLen)`.

Measured result:

- **The parked acceptance RED turned green**: `test_pass_only_playback_serves_due_notes_before_whole_root_publication`
  ("accepted Start must emit the due On; waiting for a whole-loop root is silence") passed with `root_rev = 0`. That is
  direct proof that the committed passes can serve playback without a whole-root publication.
- **It broke an existing contract**: `test_3_12_a_restored_over_l_interval_beyond_the_declared_closure_capacity_is_refused_by_name`
  failed (`Expected 0 Was 2513`) because the fallback served an interval that must be refused by name and served by the
  paged lane. Bounding the copy by `RuntimePlaybackViewConfig::kWindowAdmissionLimit` did not separate the cases.

Not shipped: reverted. The missing piece is a **paged-domain gate** — take the passes fallback only when the paged lane
does not own the interval. That predicate is decided inside the paged requester; exposing/reusing it is the next slice,
after which this fallback can be re-landed with both oracles:

```
test_pass_only_playback_serves_due_notes_before_whole_root_publication   → green
test_3_12_a_restored_over_l_interval_beyond_the_declared_closure_capacity_is_refused_by_name → green
```

Suite at revert: 254 cases, 3 failed, 250 pass (DEC-193 RED, DEC-194 window-lane RED, the parked acceptance RED).

## DEC-194 — passes-as-playback-source, landed with the paged-domain gate

`ensurePlaybackMergedMidiEventsBuilt` (2-bar merged window, long-loop branch) now has a third source besides the root and
the Record overlay: the **authoritative committed passes**, when

- the slot is rootless (`!publishedSnapshotIsCurrent()`),
- its committed source is qualified (`playbackWindowSourceIsCurrentForSlot` — the retained-source pin), and
- the paged lane does **not** own the interval (`!pagedActive && pagedOutcome == None`).

The window is filled with `copyEffectiveCommittedEventsInRange(winStart, winLen)`: bounded by the playhead's window,
never by the pass inventory or loop length.

Both oracles green, in the same run:

- `test_pass_only_playback_serves_due_notes_before_whole_root_publication` — the parked acceptance criterion — **passes**;
- `test_3_12_a_restored_over_l_interval_beyond_the_declared_closure_capacity_is_refused_by_name` — the over-capacity
  refusal — **passes**.

Suite: 254 cases, 2 failed (DEC-193, DEC-194 window-lane REDs), 251 pass.

Known ordering dependency: the gate reads the paged lane's *outcome*; on a turn where the pager has not yet answered, a
rootless over-capacity interval would be served from passes for that turn. It is worth a dedicated adversarial case
before this is called closed on device.
