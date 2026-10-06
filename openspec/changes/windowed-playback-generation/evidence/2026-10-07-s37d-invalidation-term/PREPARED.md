# Task 3.7d — prepared, not landed: the blocked-demand row must name the invalidation term

**Status: PREPARED AND REVERTED (2026-10-07).** The change was written, the host-side decoding was written and
**passing (40/40)**, and the native row-contract fixture was attempted three times; it did not go green, and the
repository's rule is that a firmware change does not land without a green production-path fixture and a red-proof. So
the code was reverted and the tree left byte-identical to `117b9a61`. This file is the hand-off.

## Why the slice exists (measured)

`captures/s64-37c-01` (see [`../2026-10-07-s37c-device-run/DEVICE-RUN.md`](../2026-10-07-s37c-device-run/DEVICE-RUN.md) §4):
the active slot's first-window demand is **accepted** and then reported as `win_blocked_by_request` twice, 60–71 ms
later, and **nothing in the capture says which term invalidated it**. The retry lane's validation can change the epoch,
the geometry, the admitted position or the deadline, or find the source stale, and records that only in
`request.reason` — which the `win_gate` row does not carry. Five candidates, no reader.

## The change that was written (and is reverted)

1. `src/Track/TrackPlaybackWindowBuild.cpp`, at the `win_blocked_by_request` site — the exit carries the term:

```cpp
      // Task 3.7d: **the term travels with the exit.** `rq` names why the retained demand may not proceed ...
      const PlaybackViewPendingReason blockedBy =
          (request->reason == PlaybackViewPendingReason::None ||
           request->reason == PlaybackViewPendingReason::AwaitingSourceQualification)
              ? pendingViewRequestInvalidation(activeLoopIndex, clock)   // the owner's existing recomputation
              : request->reason;                                        // the term the retry lane just recorded
      reportPlaybackRollingGateRow("win_blocked_by_request", clock.epoch != 0, /*reprojectedTick=*/false,
                                   static_cast<uint8_t>(blockedBy));
```

2. `include/Utils/DebugSessionCapture.h` — the row's declaration states that for this exit `rq` carries the
   **`PlaybackViewPendingReason` ordinal** rather than a request result (the field is already `0 = no request attempted`
   on this path, so nothing is lost and no field was added: no schema, tier or parser-index change).
3. `scripts/hitl/verify/windowed_lane.py` — a `PENDING_REASONS` table (the enum's own order) plus a clause in
   `_refusal_detail` that renders the term for this exit: *"the retained demand records
   deadline_invalidated_geometry"*. `scripts/test_windowed_content_parity_verify.py` gained one test covering three
   ordinals (geometry, epoch, an unknown ordinal named as `term_99`) — **40/40 green**.
4. Native fixture `test_3_7d_the_blocked_demand_row_names_the_term_that_invalidated_it`, registered next to 3.7c's.

Useful pin for whoever lands it — `PlaybackViewPendingReason` ordinals (`include/TrackPlaybackRuntime.h`):
`None=0, InterferenceBudget=1, CannotCompleteBeforeHandoff=2, BuildInFlight=3, SuccessorAlreadyPrepared=4,
DeadlineInvalidatedEpoch=5, DeadlineInvalidatedGeometry=6, AwaitingSourceQualification=7, SourceNotCurrent=8,
WindowExceedsRetainedCapacity=9, DeadlineExpired=10, ClosuresExceedDeclaredCapacity=11, PinnedEditRowsUnresolved=12`.
`RollingOutcome` ordinals: `Disabled=0, Excluded=1, Busy=2, Requested=3, Waiting=4, Ready=5, Serving=6, Expired=7,
Rejected=8`.

## The blocker (exactly as measured, so the next attempt starts here)

The fixture builds the 3.7c shape, opens the demand (`Requested`, `x=win_requested` — asserted, passing), then makes
the demand invalid, then asserts the **row**. Three attempts, all failing at the outcome assertion with
**`Expected 8 Was 4`** — `Rejected` expected, `Waiting` observed:

| attempt | how the demand was invalidated | result |
|---|---|---|
| 1 | `reason` + `positionInvalidated` set through `Track::pendingViewRequest(slot)` (a `const_cast` of the const accessor) | `Waiting` |
| 2 | the same, written through `runtime->pendingViewRequest_.reason` / `.positionInvalidated` (the object both the resolver and the accessor read) | `Waiting` |
| 3 | **the real path**: the clock driven past the frame's own `deadlineTick` and 64 retry turns run, so the retry lane records the term itself | `Waiting` |

`Waiting` is reached only *after* the resolver's request check
(`runtime->pendingViewRequest() && (positionInvalidated || CannotCompleteBeforeHandoff || WindowExceedsRetainedCapacity ||
PinnedEditRowsUnresolved)`), so the check did not match: **the record the resolver reads was not active-and-latched at
that moment** — the preceding retry turn (or the request path itself) left the demand frozen/discharged rather than
pending. On the device the pending record *was* active (`dm=1` in the `win_blocked_by_request` rows), which is exactly
the state the fixture has to reproduce.

**Where to look next (one diagnostic run):** assert `runtime->pendingViewRequest_.active`, `.positionInvalidated` and
`.reason` **immediately before** the row-producing call, and assert them again after it; that localizes whether the
record was already inactive (then drive no retry turn at all, or re-open the demand after the freeze) or the resolver
read a different record. Note that `serviceRollingPlaybackWindow` cancels every non-active slot's rolling frame at
entry, so the fixture must also assert that the slot it mutates is `activeLoopIndex` at that point.

## What landing it requires

Focused `test_runtime_working_set_ownership` green with the new fixture registered; the red-proof (passing a constant
`0` instead of `blockedBy` must fail **only** that fixture, file restored byte-identically); full native; both capture
builds through the RAM1 guard (the change is inside an existing `TRACK_COLD_MEM` function and adds no state, so the
lines are expected to be unchanged, as 3.7c's were); the host suites; then the device run that gives the slice its
payoff — the same windowed leg, which should then print *which* term blocked the demand instead of leaving five
candidates.
