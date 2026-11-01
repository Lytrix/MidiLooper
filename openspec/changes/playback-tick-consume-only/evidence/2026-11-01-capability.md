# Capability proof — one lane, every length (task 3.3, 2026-11-01)

## Verdict

**No capability gap.** The windowed lane builds, publishes and serves 1-, 8- and 20-bar loops from a cold PLAYING
entry, with exact note closure across every seam. Each window builds in about 5 producer turns (≈ 320 ticks, under half
a bar).

**Every observed bypass is routing/admission.** Four defects, all in demand, admission or recovery:

| # | Defect | Lengths | Phase-2 device reason | Repair phase |
|---|---|---|---|---|
| A | The length gates refuse loops of 16 bars or fewer | 1, 8 | `frame_idle` (t0, t1) | 4 |
| B | The successor is requested only **at expiry**, a full priced lookahead (8 bars) ahead | every length above the window (8, 20) | `waiting_for_handoff` (not seen on device, where B was masked by C) | 4 |
| C | A transport restart blocks the frame for the session (the producer's `win_frame_invalidated`) | 8, 20 | `frame_blocked(clock_epoch_changed)` (t7 s3) | 5 |
| D | A revision change while playing blocks the frame (`CandidateRejected`) | 8, 20 | — | 5 |

A merged `ready` buffer is therefore **not** justified (design D4).

## Fixture

The fixture is in `test/test_runtime_working_set_ownership`:
- `CapabilityFixture::run`;
- `test_capability_measure_every_length_and_case`, which prints the table below;
- `test_capability_classification_baseline`, which pins it.

Run shape:
- **Drivers.** The production producer (`serviceRollingPlaybackWindow` + `stepPlaybackViewMaintenance`, one turn every
  `kPlaybackViewPermittedAdvancePerTurn` = 64 ticks) and the production tick (`TrackManager::updateAllTracks`).
- **Content.** A committed, root-published, bar-spaced slot (one note per bar).
- **Entry.** The slot enters PLAYING before its prewarm, so there is **no stopped-time publication**: `fullview0=0` in
  every case.
- **Length.** 30 bars per run.
- **Events.** The restart and revision events land at bar 15.
- **Gate switch.** The two length gates are bypassed by the native-only `Track::setWindowLengthGatesBypassedForTest`,
  which goes through `windowLaneAdmitsLength`. The target image is unchanged: RAM1 96,192 and ITCM 391,944 / 1,272.

## Measurements

Ticks are counted after the lane first serves ("acquired"). `ons`/`offs` are the bar-spaced pitch after acquisition,
and `bars` is the bar starts crossed.

| Gates | Bars | Case | Acquired at | Merged before | View | Merged | Re-acquired after event | ons / offs / bars | Last reason (cause) |
|---|---|---|---|---|---|---|---|---|---|
| on | 1 | all | **never** | 23,040 | 0 | — | — | — | `frame_idle` |
| on | 8 | all | **never** | 23,040 | 0 | — | — | — | `frame_idle` |
| on | 20 | steady | 5,944 | 5,943 | 4,607 | 12,489 | — | 22 / 23 / 23 | `waiting_for_handoff` |
| on | 20 | restart | 5,944 | 5,943 | 1,535 | 15,561 | **never** | 22 / 23 / 23 | `frame_blocked` (producer) |
| on | 20 | revision | 5,944 | 5,943 | 1,535 | 15,561 | **never** | 22 / 23 / 23 | `frame_blocked(candidate_rejected)` |
| bypassed | 1 | steady | 5,176 | 5,175 | 17,864 | **0** | — | 23 / 24 / 24 | — |
| bypassed | 1 | restart | 5,176 | 5,175 | 17,864 | **0** | (never lost) | 23 / 24 / 24 | — |
| bypassed | 1 | revision | 5,176 | 5,175 | 12,688 | 5,176 | 5,176 | 23 / 24 / 24 | — |
| bypassed | 8 | steady | 5,944 | 5,943 | 4,607 | 12,489 | — | 22 / 23 / 23 | `waiting_for_handoff` |
| bypassed | 8 | restart | 5,944 | 5,943 | 1,535 | 15,561 | **never** | 22 / 23 / 23 | `frame_blocked` (producer) |
| bypassed | 8 | revision | 5,944 | 5,943 | 1,535 | 15,561 | **never** | 22 / 23 / 23 | `frame_blocked(candidate_rejected)` |
| bypassed | 20 | (same as gates on) | | | | | | | |

**Length is geometry once the gates are bypassed.** The 8-bar loop is indistinguishable from the 20-bar one. The 1-bar
loop's window is clamped to the whole loop and has no seam, so it never leaves the lane.

**Note closure is exact everywhere.** There is one NoteOff per bar crossed and one NoteOn per bar after the
acquisition bar. Nothing is lost or duplicated at a seam, including at handoffs between the merged fallback and the
lane.

**The lane never builds on a tick.** Every window build ran in a producer turn. The only builds that ran on the tick
path were the merged fallback's own:
- 16–21 on the 20-bar loop, its moving 2-bar window rebuilt by the main-loop tick (the external-clock path phase 7
  makes consume-only);
- 0 on the 8-bar loop, whose whole-loop merged window was built before acquisition.

They belong to the fallback that phase 6 retires.

## Defect B in detail (steady 20-bar trace, `CapabilityFixture::run(..., trace=true)`)

| Bar | Frame | Producer exit |
|---|---|---|
| 0.08 | Requested, start 6,144 (bar 8), deadline 6,144 | `win_requested` |
| 0.58 | Ready (built in ≈ 0.5 bar) | `win_ready` |
| 7.74 | Serving | `win_serving` |
| 9.74 | Expired → Idle | — |
| 9.75 | Requested, start bar 16 | `win_requested` |
| 10.25 | Ready | `win_ready` |
| 15.74 | Serving | … |

The cycle repeats every 8 bars: 2 bars served, 6 on the merged fallback. That is a **25 % duty cycle**, which matches
the device's 14–33 % view share. The causes:
- one rolling frame per slot;
- a successor demand opened only after `Expired`;
- the request positioned at the priced lookahead (8 bars) rather than at the current window's end.

The repair is to request the successor **while the current window serves**, positioned at its end. This is the D4
"trigger is the lead to the next seam, not arrival at it", and it belongs to phase 4 with the gate flip.

## Caveats

- **The acquisition lead from a cold PLAYING entry is 5,176–5,944 ticks (6.7–7.7 bars), covered by the merged
  fallback.** Phase 5's liveness wording ("within the lookahead") must state this bound. Defect B's repair shortens the
  lead only if the first request is positioned nearer than the priced lookahead.
- **The restart is modelled as an epoch re-stamp with continuous ticks.** The device's transport start may also move
  the tick or projection; the device chain in 5.3 checks that.
- **The revision event is a root replacement** (`publishBarSpacedRootForSlot` with a new revision), not a full overdub
  pass publication.
