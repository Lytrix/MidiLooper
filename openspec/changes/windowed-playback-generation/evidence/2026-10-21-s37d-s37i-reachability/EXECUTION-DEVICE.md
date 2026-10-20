# 3.7d / 3.7i — the two owed device rows, and why the production path cannot produce them

**Verdict.** Two gate-row-grade device runs on the qualification image recorded **neither** owed row
(`win_blocked_by_request` for 3.7d, `launch_bound_owned` for 3.7i). That absence is **not** timing luck: in the
current ordering each row's precondition is excluded by a different, already-landed rule. The firmware for both
increments stays as landed (native RED→GREEN, both builds inside the RAM1 floor); this packet records the runs, the
lane verdicts, and the ordering argument that closes the device-read obligation by analysis (owner decision,
2026-10-21). No firmware, host-tooling, stimulus or verifier line was changed for it.

## Runs

| run | image | clock | bars (asked) | take stop (`RECS,stop`) | transport (playback interval) | switch commit |
|---|---|---|---|---|---|---|
| `captures/i5-s37i-device-32bar-01` | `eace974f…` | device, 120 BPM | 32 | `3,18837,0,18837,19200` (25 bars) | `Transport started` 41.152 s → `Transport stopped` 334.744 s | `LoopEnd playback commit track=7 3->4` 240.944 s |
| `captures/i5-s37i-device-32bar-02` | `eace974f…` | device, 120 BPM | 32 | `3,343,0,18775,18432` (24 bars) | `Transport started` 41.119 s → `Transport stopped` 264.096 s | `LoopEnd playback commit track=7 3->4` 232.943 s |

Both: `--preset windowed_content_parity --build-env teensy41-capture-serial-window-qualification --track 8
--loop-slot 4 --midi-channel 8 --record-bars 32 --boot-reset --switch-target-slot 5`, settle 170 s (run 01) and
100 s (run 02), switch queued at 163.362 s / 162.697 s. The take is a **device-clock** take, so the preset's own
play press supplies the transport edges; the two runs differ only in the post-switch observation window, and they
agree row-for-row on the switch.

**Lane verdict (both runs): five of six checks ok**, the only failure the pre-existing
`capture_evidence_ring_intact` (`tier_a_evicted=1`, the ring budget measured since 6.4):

| check | run 01 | run 02 |
|---|---|---|
| `readiness_receipts_under_transport` | 32/32 under transport, 48 source-qualified of 340 | 31/31, 45 of 334 |
| `windowed_service_coverage_under_transport` | 559 opportunities | 560 opportunities |
| `window_boundaries_crossed_under_transport` | 516 completions, loop 768 | 513 completions, loop 768 |
| `switch_target_span_alignment` | `t=7 s=4 len=18432` | `t=7 s=4 len=18432` |
| `no_windowed_overrun_under_transport` | 0 rows | 0 rows |
| `window_demand_observed` | ok | ok |

So the lane was live, the transport-scoped service was live, and the switch target was span-aligned in both runs;
the two rows are absent from a capture where the lane is demonstrably being asked for work.

## 3.7i — `launch_bound_owned` cannot be produced by one switch

The guard reports when `preparePlaybackLaunchForSlot(slotIndex)` finds
`launchPreparation == Bound || FallbackBound` for a slot that still has a **pending switch** and is **not** the active
slot (`TrackPlaybackWindowBuild.cpp`, the `launch_bound_owned` branch). Three facts exclude that state in the
single-switch path:

1. **`Bound` has one writer.** `bindPlaybackLaunchForSlot` is called only from the commit branch in
   `TrackManagerTransportTick.cpp`, and only after `playbackLaunchReadyForSlot` has passed.
2. **The commit consumes the switch in the same tick.** Immediately after a successful bind the same branch runs
   `clearPendingSlotSwitch(i)`, `sendAllNotesOff()`, `applyActiveLoopIndex(i, targetSlot)` and the queued start. There
   is no interleaving point between the bind and the switch being consumed.
3. **A successful bind implies the commit's own next guard.** `bindPlaybackLaunchForSlot` requires
   `playbackLaunchReadyForSlot`, which requires `loop.hasCommittedPasses()`; `hasDataInSlot` is
   `hasCommittedPasses() || loopLengthTicks > 0 || !capture.store.empty()`, so the `else if (hasDataInSlot(targetSlot))`
   that performs the commit can never be false after a bind.

`preparePlaybackLaunchForSlot` is only reached from `TrackManager::servicePendingSlotProvision` for
`hasPendingSlotSwitch(trackIndex) && targetSlot != activeLoopIndex && immutableLaunch`. With the switch consumed and
the target made active in the bind's own tick, the provision owner has no turn in which `Bound` and a pending switch
coexist.

**Measured, both runs.** The target's launch walks `launch_requested` → `launch_requested_match` ×3 →
`launch_building_match` (`prep=2`) → `launch_ready_owns_source` → `launch_ready` (`prep=3`), and stays `Ready` until
the commit:

| run | `building_match` | `ready_owns_source` | `ready` | commit |
|---|---|---|---|---|
| 01 | 195.706 s | 195.783 s | 195.798 s | 240.944 s |
| 02 | 195.005 s | 195.082 s | 195.097 s | 232.943 s |

Between `launch_ready` and the commit the provision owner keeps returning `launch_ready` (state-change deduped, so no
further row) — i.e. the preparation is `Ready`, never `Bound`, at every provision turn. The commit then binds and
adopts (`win_adopted_launch` 49 ms / 40 ms later, at 240.993 s / 232.983 s; `win_serving` at 241.011 s /
232.999 s) with no guard row.

**What *would* produce it.** A `Bound` preparation that outlives its switch: a **switch away and back** to the same
slot while the earlier bind is still live. That is not drivable with the current preset, and not only for tooling
reasons: the bound view's handoff window is at most one traversal of the target's two-bar launch span, while the
commit ladder only lands on a boundary of the **outgoing** loop (here a 24–25-bar take, 77 s and 70 s between the
switch being queued and the commit in these two runs). A second switch queued inside the bound window would need the
first switch-away to commit inside that same window, which the outgoing loop's own boundary wait rules out.

*Recorded, not chased:* the 3.7f capture's destructive turn (`captures/i5-s37f-device-32bar-01`, `launch_cancelled` →
`win_seed_refused rq=3`) is real, but its row reads `prep=0` **after** the cancel, so that log alone cannot separate
`Bound` from a stale `Ready`; on the current ordering only the stale-`Ready` variant is producible at a provision
turn, and the guard's `Bound` arm remains a defensive branch.

## 3.7d — `win_blocked_by_request` is closed by DEC-147 and 3.7h

The exit fires on a service turn whose frame is not `Idle`/`Serving`/`Blocked` and whose retained request is
`positionInvalidated`, or carries `CannotCompleteBeforeHandoff`, `WindowExceedsRetainedCapacity` or
`PinnedEditRowsUnresolved` (`serviceRollingPlaybackWindow`). Every production writer of those states now either
re-positions in the same gather turn or blocks the frame by name:

* **`CannotCompleteBeforeHandoff`** is written by the admission refusal and by the gather's
  `leadTooShort || siblingBroken` term. The gather's own re-position branch (`win_repositioned`) runs for
  `leadTooShort && !siblingBroken && frame.phase == Requested` and **clears the retained record** before returning, so
  the next service turn sees no request at all. When the span budget is spent, DEC-147's traversal terminal fires as
  `win_blocked_by_budget` and sets the frame `Blocked`, which returns before the request is read.
* **`DeadlineExpired`** (the late-build route that produced the 2026-10-07/2026-10-19 captures) is what 3.7h removed:
  the selector now positions the demand with the same price the admission charges, so on the take the build no longer
  finishes past its deadline, and where the price cannot fit inside one loop (the 24-bar target) the declared
  lookahead and DEC-147's bounded refusal stand.
* **`WindowExceedsRetainedCapacity` / `PinnedEditRowsUnresolved`** are the paged lane's answers; the lane is
  `Excluded` (`win_track_not_playing`, or the note-edit guard) before the exit can read them on the active slot.

**Measured, both runs — the storm is exactly this shape.** After the adopted launch the target's successor demand
walks the bounded refusal to its terminal:

| run | repositions | terminal | frame |
|---|---|---|---|
| 01 | 12 (`rq=1…12`, 246.314 → 246.957 s) | `win_blocked_by_budget rq=12` 246.982 s | `win_blocked` 246.993 s |
| 02 | 12 (`rq=1…12`, 238.314 → 238.960 s) | `win_blocked_by_budget rq=12` 238.985 s | `win_blocked` 238.995 s |

Each attempt is `win_requested` → `win_waiting` → `win_repositioned`; there is no turn between the retained record and
its retirement, and the budget's own terminal blocks the frame before any request can be read. Run 01's 12
`handoff_unreachable` drops and run 02's 12 are all on the 24-bar target (`len=18432`), as recorded for 3.7h.

**The one remaining production route** is `siblingBroken`: the gather sets reason `CannotCompleteBeforeHandoff` and
returns **without** the re-position branch, leaving the record for the next service turn to read (which would print
`rq=2`). It requires `playbackViewAdmissionKeepsAdmittedDeadlines` to find a **sibling slot on the same track with an
admitted build** (`activeViewBuildWork()` non-null) whose deadline cannot hold the candidate's grains. In this preset
the only such sibling is the switch target's launch build: 117 ms (`building_match` → `ready`, both runs) against the
take's ~11 s one-bar span period, so the overlap is not something the gesture can place — and in both runs the launch
was `Ready` long before the take's last span boundary.

**What *would* produce it.** A deliberate two-build overlap on one track (a long sibling build entered while the
active slot's gather is in flight), or a qualification-only trigger that forces one. Both are code changes, not
gestures.

## What this closes, and what it does not

* **3.7d and 3.7i close on the native proofs plus this reachability record.** The rows' instruments are present in the
  qualification image and would fire if the state occurred; the two runs show the state does not occur through the
  production path, and the ordering above says why.
* **Not claimed:** that the rows are unobservable forever. Each has a named synthetic trigger (a `Bound` preparation
  that outlives its switch; a sibling build with a short lead). A future slice that wants a device read of either must
  add that trigger behind the qualification image, with its own native fixture and DEC.
* **No change** to DEC-147, DEC-154/155, `boundLaunchView`, the admission constants or the verifier. The two runs add
  no new lane failure: the only red check is the pre-existing ring budget.
