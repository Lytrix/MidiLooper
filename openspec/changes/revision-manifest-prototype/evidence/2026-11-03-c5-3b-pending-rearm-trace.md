# C5.3b: who re-arms the superseded pending request (2026-11-03)

Owner ruling: name the writer before changing the C5 price. Measurement only, capture images only.

## Method

- `PendingRequestProvenance` (`include/TrackPlaybackRuntime.h`). Every writer of `pendingViewRequest_` records itself
  only on the inactive -> active edge:
  1. live Record retention;
  2. `retainViewPendingRequest`, plus its caller's return address;
  3. the coverage-retire coalesce;
  4. the retained gather;
  5. the paged lane;
  6. the view transition;
  7. launch preparation.
- The owner-set trace (`refreshRetainedDemandIdentityForSlot`) emits
  `win_frozen_readiness,<t>,<s>,pending_armed,named,<writer>,<site>,<deadline>,<start>,<length>,<manifest identity>,
  <frame phase>,<frame deadline & 0xFF>,<reason>` when the pending bit appears.
- **Image.** succprobe HEX sha256 `5ec96be77bfea909cc09281bc576fc15f0877572f07cb3bee2af305cab8da5fc`.
- **Run.** `captures/session_20261103_092547.log`: 32 dense bars, a 32-bar overdub, stop, restart, `0:1536:10`. Boot
  `crash=0`.

## Result

- The first window served 61.5 s after the restart; 10/10 seams handed off.
- The discharge fired at 180.1 s and 205.8 s (`DeadlineExpired`), with no Blocked frame.
- The demand that expires was armed at **154.49 s**, in the same lane turn the first window's build completed into
  Ready (`completed_unpublished`, then `win_ready dm=1`):

```
pending_armed,named,2,1611203015,28928,3584,1536,0,2,56,7
  writer 2 = retainViewPendingRequest, return address 0x600901c7 -> Track::retainRefusedPlaybackViewRequest
  deadline 28,928 = 25,344 + 3,584; interval [3,584, +1,536), not on the 1,496/1,488 equal partition
  manifest identity 0; frame phase Ready; reason AwaitingSourceQualification
```

- `AwaitingSourceQualification` is passed only by `Track::requestCommittedPlaybackWindow`. Its non-rolling caller with
  a playhead-centred interval is `Track::requestActiveCommittedPlaybackWindow`, the main loop's per-turn production
  demand (`main.cpp`, the `PlaybackViewMaintenance` window lane).
- **That demand defers to the rolling owner only while the frame is Requested.** It checks
  `frame.phase == RollingPhase::Requested` and a matching deadline and start. With the frame **Ready** for a window
  still ahead (here two traversals out, because the old DEC-147 price places it there), it arms a fresh demand for the
  playhead's current interval. No frame owns that demand and it is never served; one traversal later it expires.
- The other tracks show the same writer 2 every turn. That is their normal per-turn demand (no rolling frame).

## Classification (for the owner)

- The writer is legitimate code acting on an incomplete deference rule; it is not state that must exist. On a
  routed slot with a positioned Ready or Serving frame, the per-turn playhead demand duplicates an obligation the frame
  already owns.
- The cheapest correct repair is to extend the existing deference from `Requested` to `Ready` / `Serving` on that
  slot. The discharge (`fa346fea`) stays as the guard either way. Not changed yet.

## Same-deadline control (owner: a deadline is not demand identity)

- Fixture `test_5_1_a_stale_request_at_the_frame_s_deadline_is_still_superseded`: a stale demand at the frame's own
  deadline, but naming another source revision.
- It does not reach the discharge. With the discharge mutated off it still passes, because the handoff's adoption
  releases every record with the frame's demand key: generation, epoch, deadline and start (`matches` in the adoption
  path). The adopted window covers that interval.
- The discharge predicate now uses that same key (`supersededPositionalRequest`) instead of the deadline alone, so the
  two definitions of "the frame's own demand" cannot drift apart.
- AFFECTED: 17 suites, 554 cases, 0 failed, 0 errored.

## Fix: the per-turn demand defers to a positioned rolling owner (owner ruling 2026-11-03)

- **Contract.** Once the rolling owner holds a positioned obligation for the routed slot, the per-turn requester
  creates no competing obligation until that ownership is released.
  - "Positioned obligation" means a Ready or Serving frame of this generation, clock epoch and loop geometry,
    positioned at a deadline (`rollingFrameOwnsPositionedObligation`).
  - A Requested frame was already matched by its own record.
  - The check is on the routed slot only, inside `requestCommittedPlaybackWindowForSlot`.
- **The discharge stays** as the independent liveness guard (`fa346fea`).
- **Host RED → GREEN.** `test_5_1_the_per_turn_demand_defers_to_a_ready_or_serving_routed_frame` runs `main.cpp`'s
  lane order: the per-turn demand, then the owner lane.
  - Before: `armedBesideOwner=1`, at frame phase Ready.
  - After: 0, with the window served.
  - AFFECTED: 17 suites, 555 cases, 0 failed, 0 errored.
- **Device.** succprobe HEX sha256 `ead10f19ef484a39096f7cfae14f5b4819adb45266f5ece7625440ac68ea8a29`,
  `captures/session_20261103_093844.log`, boot `crash=0`. Same run: 32 dense bars, a 32-bar overdub, stop, restart,
  `0:1536:20`.

| | before (`session_20261103_092547`) | after |
|---|---|---|
| superseded discharges | 2 | **0** |
| routed `win_blocked` exits | 0 | **0** |
| `pending_armed` on the routed slot while Ready/Serving | 1 (154.49 s) | **0** |
| successors handed / missed | 10 / 0 | 20 / 0, min margin 832 |
| first window after the restart | 61.5 s | 61.5 s (the old price; unchanged) |

  After the restart the only `pending_armed` rows on the routed slot are legitimate:
  - 150.1 s: the per-turn first demand, with the frame Idle;
  - 152.2 s: the rolling owner's own demand, with the frame Requested at 7,480 / 58,168.

  Grants per window are still exactly G(E): E = 138 → 16, 150 → 19, 164–188 → 20.
- **Next.** The C5 work estimator on G(E) grants: the work count only, not the timing rule. The writer trace stays
  in capture images until that run; then it is removed or demoted.

## The routed slot has one view-lifecycle owner; a dead build retires and its frame is served (owner ruling 2026-11-03)

- **Exposed by the G(E) estimator** (`8b80cb6f`, `session_20261103_095257`). One pre-restart block after a live overdub
  stop: a build published outside its Requested frame, then `CandidateNotReady`. An association trace (capture images
  only) then records the frame, build and ready keys at the request, at both admission sites
  (`admit` / `admit_waived`) and at a publication made while the frame is Requested (`publish_outside_frame`). The
  traced run did not reproduce the block.
- **Host RED for the post-overdub shape:** a routed take, one traversal of interleaved overdub, a live stop, then play
  in `main.cpp`'s lane order. Backtrace of the view reset:
  `serviceBridgeWindow` → `publishCompletedBuildAsReady` → `RuntimePlaybackView::resetMetadata`. After the overdub stop
  the old path's **bridge-window owner** (deferred idle maintenance) rotated the slot's three playback views and reset
  the view holding the manifest candidate during the dense count. The dead build then answered `Denied` about 12,000
  times, and the lane step returned `Idle` for a Denied grain, so nothing retired it and the frame timed out.
- **Fix 1, ownership.** On a manifest-routed slot the bridge-window owner never rotates, publishes or resets the
  playback views (`TrackDeferredMaintenance`); the stop job's bridge storage is unaffected. Fixture
  `test_5_1_no_deferred_owner_mutates_the_routed_slot_s_views_after_an_overdub_stop`: 95 foreign mutations → 0.
- **Fix 2, liveness guard.** A coverage build whose next grain is Denied is dead and is retired that turn by the
  denied-step path (`retireCoverageSource`).
  - If it was the Requested frame's own build (same deadline and epoch), the frame's interval is requested again
    (`win_dead_build_rerequested`). If the new demand does not land on the frame's position, the frame is cancelled so
    the requester repositions it (`win_dead_build_repositioned`).
  - Fixture `test_5_1_a_dead_coverage_build_retires_and_its_frame_is_served` moves the destination under an admitted
    build. Before the fix: 6,143 dead turns, then `CandidateNotReady` Blocked, the device's own term. After: retired
    within 5 lane turns, 0 blocked, 3,072 window-served ticks.
- **Open, host only.** In the overdub fixture the routed slot is then played from the stop job's full-loop **bridge
  route** for about 21,000 ticks. Its Ready frame is not adopted and later becomes `HandoffMissed`. The device served
  0 bridge ticks on the routed slot. Removing the bridge from a routed slot's audible route is a clock-path resolver
  change that needs the owner's ruling, so it is reported and not asserted.
- **Host.** Routed suite 23/23. FULL (escalated by `include/Track.h`): 147/147 suites, 3,959 cases, 0 failed,
  0 errored.
- **Device.** succprobe HEX sha256 `419bc3df1caeb5bdfc3d8a01b8854c70e9d8c97b380a6d3939fd51e2b92cc118`,
  `captures/session_20261103_104233.log`, boot `crash=0`. Same overdub run.
  - Routed `win_blocked` 0, superseded discharges 0, dead builds 0, `publish_outside_frame` 0, bridge ticks 0.
  - The first window after the restart served at 14.3 s; successors 20/20, 0 missed in the measured phase.
  - Estimate == measured on all 21 handed seams.
  - One `succ_seam,missed` row at 124.4 s is unexplained: before the restart and the probe configuration, E = 0,
    tagged slot 0. It is not counted.
