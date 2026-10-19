# S4.5 — owner-level repositioning after a handoff-lead refusal (G2 packet)

Status: **design frozen, implementation not started.** Scope owner: 4.5 / 6.4 (ordered receipts on device).

## 1. Contract

A rolling demand refused because its **handoff lead cannot fit its own derived build cost** is a *position* refusal, not
a promise refusal. The owner may move that still-current demand to the **next eligible span** and try there. The refused
attempt keeps its own receipt (`build_drop,handoff_unreachable`, reason `CannotCompleteBeforeHandoff`); nothing about the
guard is relaxed.

Hard constraints (owner-level, from the approved brief):

1. **Not a deadline-guard relaxation.** `CoverageServiceContract::canCompleteWithinLead` and
   `playbackViewAdmissionKeepsAdmittedDeadlines` keep their current arithmetic and their current call sites.
2. **Admitted-deadline protection unchanged.** A refusal caused by the *sibling* term
   (`playbackViewAdmissionKeepsAdmittedDeadlines` — "admitting would break a promise already made to a window on this
   track") is **not** repositionable and stays terminal, exactly as today.
3. **No same-handoff retry loop.** Every reposition advances to a span strictly later than the last refused handoff; a
   whole-traversal wrap is terminal.
4. **Staleness suppresses retry.** A demand that is `positionInvalidated`, generation/epoch/geometry-mismatched, or whose
   source is no longer current is never repositioned; it takes the existing terminal path.
5. **No new sticky state.** The frame's refused-handoff mark is cleared wherever the frame is re-minted (Idle → Requested
   at a fresh creation) and by `cancelRollingPlaybackWindow`.

## 2. Seam

`src/Track/TrackPlaybackWindowBuild.cpp`, qualification/admission step (`stepPlaybackView…`, the block beginning at
`:5393`):

```cpp
if ((!launch && !cold && !contract.canCompleteWithinLead(request.deadlineTick - clock.tick, outstanding)) ||
    (!launch && !cold && !playbackViewAdmissionKeepsAdmittedDeadlines(contract, request.requestedPhase,
        request.loopLengthTicks, contract.requiredGrains(), clock))) { … HandoffUnreachable … }
```

Today both terms collapse into `request.reason = CannotCompleteBeforeHandoff`, `retireCoverageSource()`, drop, and the
lane later turns that into `RollingPhase::Blocked` (`win_blocked_by_request`) — terminal for the episode.

**Change:** evaluate the two predicates separately.

| term | meaning | action |
|---|---|---|
| `leadTooShort` | the demand's own build cannot finish before **its** handoff | mark `repositionEligible`, keep drop + reason, reposition |
| `siblingBroken` | admitting would break an **already-admitted** window's deadline | unchanged: drop + terminal reason |

When **both** are true the sibling term wins (unchanged protection): not repositionable.

## 3. State (minimal)

`include/TrackPlaybackRuntime.h`

- `PlaybackViewPendingRequest::repositionEligible` (`bool`, default false) — set only by the lead-only refusal; cleared by
  any admission, by a new request, and by a reposition.
- `LoopPlaybackRuntime::RollingFrame::refusedHandoffPhase` (`uint32_t`) + `refusedHandoffValid` (`bool`) — the strictly
  advancing mark. RAM: ~8 B per allocated slot runtime; no heap.

## 4. Algorithm (all inside the owner, no lane change required)

At the drop site, after `retireCoverageSource()` (the attempt's own receipt is already emitted):

```
if (leadTooShort && !siblingBroken && demandStillCurrent(request, frame, loop, clock)
    && repositionToNextSpan(request, frame, loop, clock)) {
      request.reason = AwaitingSourceQualification;   // not terminal; the obligation survives
      request.repositionEligible = false;
      return GrainDenied;                            // next turn re-qualifies with the new position
}
request.reason = CannotCompleteBeforeHandoff;        // terminal, as today
```

`repositionToNextSpan`:

1. `currentPhase = tickPhaseInProjectionCycle(clock.tick, getProjectionCycleStartTick(), loop.loopLengthTicks)`.
2. `refused = frame.refusedHandoffValid ? frame.refusedHandoffPhase : (request.coverage.startPhase + span) % loopLength`.
3. `next = ((refused / span) + 1) * span` (strictly later; `span = 2 * TICKS_PER_BAR`).
4. **Wrap is terminal:** if `next >= loop.loopLengthTicks` (or `next <= refused` after wrap) return false → caller blocks.
5. New position: `coverage.startPhase = next`, `requestedPhase = currentPhase`,
   `deadlineTick = clock.tick + delta(currentPhase → next) + span`, `work.deadlineTick` mirrored.
6. `frame.startPhase = next`, `frame.deadlineTick = request.deadlineTick`,
   `frame.refusedHandoffPhase = next`, `frame.refusedHandoffValid = true`, `frame.phase` unchanged (`Requested`).
7. Mirror the position into the retained `pendingViewRequest_` **iff** it is active for the same
   `operationGeneration`/`loopLengthTicks`/coverage identity (the frozen copy and the retained demand are two views of one
   debt; repositioning one alone would let the next turn re-freeze the old position).

`demandStillCurrent` is the **same predicate the qualification head already applies** (`request.active`,
`operationGeneration == playbackGeneration`, `!positionInvalidated`, `clockEpoch == clock.epoch`,
`loopLengthTicks == loop.loopLengthTicks`, `playbackWindowSourceIsCurrentForSlot`,
`source.playbackRevision == loop.playbackRevision`, cold snapshot identity where applicable). No new staleness terms.

**Why the wrap is the loop bound.** Each reposition advances the refused handoff by ≥ `span`; `loopLength / span` is the
maximum number of attempts in one traversal (12 at 24 bars, 16 at 32 bars). A geometry where every span is refused ends
at the wrap with today's terminal reason, not in an endless retry.

## 5. RED (transition oracle, native)

New fixture `test_a_handoff_shortfall_repositions_the_current_demand_to_the_next_span`:

1. Demoted, span-aligned target (24 bars, `av=1, al=1`) — reuse the lane fixture's own setup
   (`commitBarSpacedPassIntoTrackSlot` + `publishBarSpacedRootForSlot` + `nativePlaybackDeadlineClock = {tick, 1}`).
2. Manually retain a **current** pending request positioned with a one-tick lead
   (`deadlineTick = clock.tick + 1`, `coverage.startPhase = currentPhase + span`, source = published snapshot,
   `operationGeneration = playbackGeneration`, `clockEpoch = clock.epoch`).
3. Drive `stepPlaybackViewMaintenance(0, nullptr, true, clock)`:
   - **first attempt**: `build_drop,handoff_unreachable` emitted, `reason == CannotCompleteBeforeHandoff`,
     `repositionEligible == true` (RED today: no such field / no reposition, frame goes `Blocked`);
   - **reposition**: `request.deadlineTick` advanced by ≥ `span`, `coverage.startPhase` on the next span,
     `frame.refusedHandoffValid == true`, reason no longer terminal;
   - **progress**: further turns reach `RollingPhase::Ready` then `Serving`, with `RollingGateRows` containing
     `win_requested → win_ready → win_serving` in that order (the ordered oracle from `8bd4dcf9`).
4. **Loop prevention**: with a fixture whose every span is refused, assert the number of reposition attempts ≤
   `loopLength / span` and the final state is `Blocked` with `win_blocked_by_request`.
5. **Staleness**: repeat (3) with `positionInvalidated = true` and with `operationGeneration - 1`; assert **no** reposition
   (deadline unchanged, frame `Blocked`).

Controls that must keep passing: `test_rolling_gate_rows_name_the_demand_paths_silent_exits_once_per_state_change`
(ordered receipts), the containment/deadline fixtures, and the full native suite (`pio test -e native`).

## 6. Budget

Expected cost: one predicate split (two locals), one owner method, two fields. Estimate **+80…140 B ITCM** on
`teensy41-capture-serial` (HEAD `392,216 + padding 1,000`) and the same on the qualification image
(`392,376 + padding 840` → ~`700…760`). Measure both; **no rebase is implied** while padding stays positive, but record
the number in the slice packet. If the cost pushes the lane image to zero padding, stop and report as a budget boundary
rather than shaving unrelated code.

## 7. After native green

1. Flash the qualification image (dedicated-preset exception) and run the registered windowed preset: the same 24-bar
   target that produced the 38.7 ms refusal (`captures/i5-s64-f8-parity-32bar-01`) must now show, in one episode,
   `win_requested → win_ready → win_serving`, and the drain must show playback continuity (the unmuted row's notes present
   across the switch).
2. Keep the existing 24/32-bar high-contention captures as the **refusal control**; do not overwrite them.
3. With that image, run the 4.4 demoted-length parity sweep (23/24/25/26/32 bars) and record per-length
   `Requested/Ready/Serving` plus content parity.

## 8. Implementation attempt — findings that change step 1 (2026-10-19)

The slice was implemented to a compiling state (fields, predicate split, helper, lane call) and **reverted**; the branch
is green at `215/215` on the focused suite. Three findings are load-bearing and must be designed in, not discovered again:

1. **The lane reads the retained record, not the priced attempt.** `LoopPlaybackRuntime::pendingViewRequest()` returns
   `pendingViewRequest_.active ? &pendingViewRequest_ : nullptr` (`include/TrackPlaybackRuntime.h:1321`), while the drop
   site and the admission price `frozenViewRequest`. `retireCoverageSource()` coalesces **only the deadline** into an
   already-active retained record (`TrackPlaybackRuntime.cpp:1907`–`1914`): if the retained record was active, the
   refusal's `reason` (and any eligibility bit) **never reaches the lane**. On device the retained record happened to be
   inactive, so the whole frozen record was copied — that is why `win_blocked_by_request` appeared there and not in a
   fixture that opened its own retained demand. Any reposition must **carry the reading explicitly** at the drop site.
2. **DEC-108 §13(a) re-imposes the refused position.** `retainViewPendingRequest` keeps the **oldest** outstanding
   deadline, so a re-request made through `requestCommittedPlaybackWindow` coalesces back onto the very deadline that was
   just measured as unreachable — every retry then fails identically. A reposition must therefore **replace** the
   positional fields (`coverage`, `requestedPhase`, `deadlineTick`, `work`, `clockEpoch`), not coalesce. Clearing the
   whole retained record is **wrong**: it broke `test_rolling_selector_preserves_existing_adjacent_refusal_and_deadline`
   (expected 2, was 0), because the identity/obligation must survive the position change.
3. **The RED must be built at the freeze boundary, not by forcing every turn.** Holding `deadlineTick = tick + 1` each
   turn did not produce `handoff_unreachable` inside a 12-bar drive: the qualification completed before the forced
   deadline was priced, so `refusedHandoffValid` never became true. The deterministic construction is: drive until
   `frozenViewRequest.active`, overwrite **that** request's `deadlineTick` once (the freeze is the priced attempt), tick
   once to force the drop, then assert the reposition. This is also the sequence the 38.7 ms device episode shows.

### Revised step 1

1. At the drop site, after the existing reason assignment, mirror `reason` **and** `repositionEligible` into the retained
   record when it is active — do not rely on `retireCoverageSource()`'s deadline-only coalescing.
2. In `repositionRollingDemandToNextSpan`, replace only the positional fields on the retained record
   (`coverage.startPhase`, `requestedPhase`, `deadlineTick`, `work.deadlineTick`, `clockEpoch`) and call
   `requestCommittedPlaybackWindow`; keep `active`, source identity, `operationGeneration` and `loopLengthTicks`.
3. Build the RED at the freeze boundary (finding 3), then the ordered-receipt assertion.

## 9. Second attempt — the measured blocker is *re-entry*, not the guard

Implemented the revised step 1 (predicate split, retained-record mirror, positional replacement, lane re-position) and got
as far as a working refusal-then-reposition transition. Two measurements decide the remaining design:

1. **The lane does re-position once** (`win_repositioned` emitted, `startPhase` moved, deadline advanced past the refused
   position, `refusedHandoffValid` set) — the predicate split, the reading mirror and the strict-advance rule all work.
2. **The re-request leaves no owed demand.** After `requestCommittedPlaybackWindow(...)` returns
   `DeferredSourceQualification` from the re-position site, the recorded state is
   `pendingViewRequest_.active == 0`, `coveragePreparation == Idle`, `rolling.phase == Requested` — and it stays there:
   no build, no `win_ready`, no `win_serving`. `requestCommittedPlaybackWindow` returns the deferred result
   unconditionally for a valid call (`TrackPlaybackWindowBuild.cpp:4198`–`4205`), so the return value cannot be used to
   tell "retained" from "retention declined", and the lane's post-retire state does not re-arm preparation.

So the open question is **how a re-positioned interval re-enters preparation**, and there are two candidates:

- **(A) Reset to the creation path.** On a lead-only refusal, set `frame.phase = Idle` but keep
  `refusedHandoffValid/Phase`, and let the existing creation branch (which already emits `win_requested` and drives
  preparation) re-request with `boundary` forced past the refused handoff. Uses the proven path; needs the boundary
  comparison expressed in phase space (`boundary % loopLength` vs `refusedHandoffPhase`).
- **(B) Re-arm preparation explicitly from the lane.** Call the preparation entry (`beginCoverageQualification` /
  `retainRefusedPlaybackViewRequest`) for the new interval before returning `Requested`. Fewer moving parts in the frame,
  but it re-implements the creation branch's ordering.

**Rejected: in-place reposition at `AwaitAdmission`.** Moving `request.coverage` on a demand whose coverage source was
already counted for the old span mismatches the counted units with the new interval — the build would be priced and read
from the wrong events. The re-position must start a fresh qualification for the new interval.

Branch reverted to green (`215/215`) after this measurement; the two candidate implementations are the next step, with
(A) preferred because it reuses the path the device already exercises (`win_requested` at every new demand).
