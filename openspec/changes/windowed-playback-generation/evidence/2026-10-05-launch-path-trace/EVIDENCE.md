# Evidence — launch-path trace (switch leg, run 06)

Sources: `captures/s64-switch-06/host_midi_hitl_windowed_post_stop_drain_20261005_115248.log`,
`…_serial_20261005_115213.log`, `…_window_20261005_115213.json`;
`captures/s64-window-census-01/census.json`; `captures/s64-switch-0{1,4,5}/*.log`.
Device image: qualification (`teensy41-capture-serial-window-qualification`), track 8
(lane index 7), leg `long_loop_display_window` with `record_bars = 24`.

## 1. The switch target is lane slot 4, the demoted 24-bar slot

- `Loop 5: Queued playback switch` and
  `Slot switch hold slot_switch_hold_exceeded track=7 target=4 age=3075 ticks`
  (drain log, host `[136.851]` / `[144.807]`).
- The press is `Button pressed: Ch16 Note54` — note 54 = `LOOP_SELECT_NOTE_BASE 50 + 4`
  ⇒ **index 4**; the human row prints the 1-based display label ("Loop 5").
- Census identity (`census.json` → `library_observation.slots`, `track=7`):
  slot 3 = `loop_length_ticks 1536`, 3 passes, 146 events, `root_is_current=True`;
  **slot 4 = `loop_length_ticks 18432` (24 bars), 1 pass, 1144 events,
  `has_root_content=False`, `root_is_current=False`**; slots 5-7 empty.
  (Census is a separate session; used for identity, not for run-time state.)

## 2. At switch time the target selects the immutable-launch branch

The device states the selected loop's length itself when the selection opens the edit
session (drain log, host `[136.810]`):

```
Sent loop length CC feedback: 24 bars (18432 ticks) -> CC=23
Sent tempo CC feedback: 119.9 BPM -> CC=60
Loop edit session entered — sent loop start/length/tempo fader feedback
```

`shouldAvoidFullVisualRebuild(18432)` is true for a 24-bar loop
(`src/Loop/LoopMaterialization.cpp:596`; `> kMaxDetailedWindowBars * TICKS_PER_BAR`), and
the target is not the active slot, so `immutableLaunch` holds
(`src/TrackManager/TrackManagerSlotMatrix.cpp:395-396`).

## 3. Provisioning did not refuse

`provisionSlotForSwitch` reports its only refusal through
`MemoryMonitor::reportPrewarmAdmissionDenied(OperationKind::SlotSwitch)`
(`TrackManagerSlotMatrix.cpp:353-355`), which prints `#MEM_DENIED,op=<name>,…`
(`src/Utils/MemoryMonitor.cpp:256+`). **Zero `#MEM_DENIED` lines** in both logs of all
four switch runs (01/04/05/06). So the immutable branch was entered and
`Track::preparePlaybackLaunchForSlot(4)` was called (`:398-399`).

## 4. The target's root publication committed, and the target then produced no row

Serial log, track 7 slot 4 — the boot publication, complete:

```
#CAP,7119760,RRQ,7,4,0,0,queued,none,none
#CAP,8957338,RRQ,7,4,0,0,running,none,none
#CAP,9140559,RRQ,7,4,0,0,terminal,none,committed
```

Grepping both run-06 logs for `,7,4,` / `slot=4` returns **only those three rows**: for
the rest of the session the switch target produced no `playback_build`, no
`win_readiness`, no `capture_prewarm`, no publication row — nothing. Its boot prewarm
set was `capture_prewarm,track=7,slot={0,3,5}` (slot 4 not among them in this run).

⇒ The blocker is **not** "no publication ever happened for this slot". The boot
publication reached `terminal,none,committed`.

## 5. The lane serves a demoted 24-bar slot in this same run

`playback_build,snapshot,<durationUs>,<windowStartTick>,1536,1,7,5,1,<atUs>,<prev>,<cur>,96`

| windowStartTick | durationUs | atUs |
|---|---|---|
| 6154 | 1591 | 103 955 002 |
| 6922 | 552 | 105 987 625 |
| 7690 | 446 | 107 989 065 |
| 8458 | 443 | 109 990 482 |
| 9226 | 443 | 111 991 868 |
| 9994 | 455 | 113 993 303 |

Six 2-bar windows (`windowLengthTicks 1536`), one per bar (`+768` ticks ≈ 2 s), each
with `eventCount 96`, for track 7 **slot 5** — the leg's own 24-bar loop
(`record_bars = 24`, `expected_loop_len_ticks = 18432` in the run JSON) and the active
playing slot, freshly recorded in this session. Six matching `playback_order` rows.
Rows are labelled `note_edit` for the same slot from 114.13 s (the edit session opened
by the press).

⇒ The lane, its `snapshot` path and its demand admission **work on this board**, and a
24-bar demoted slot *can* be served. The served slot differs from the switch target in
being the active slot and in having just been recorded.

## 6. The demand counters are not a work indicator (correction to evidence practice)

All 32 `win_gap` rows in this drain report `eligibleTurns=0` and `progressTurns=0` —
**including the phases that carried the six builds above** (`win_gap` phases 8905, 10487;
build `previousPhase/currentPhase` 7689/7690, 8457/8458, 10761/10762). `running=1` on 29
of them, `permitted=64`, `overruns=0`, `epoch=64`.

⇒ "all `win_gap` rows show `eligible=progress=0`" cannot be read as "the lane had no
work"; the material evidence for lane work is the `playback_build` rows. (This affects
how F-7/F-8/AC-8 evidence must be read.)

## 7. The failing transition: inside `preparePlaybackLaunchForSlot`, and silent

Reached (findings 2-3); the target is unchanged afterwards (finding 4); the lane is
capable (finding 5); so the first failed transition is in
`src/Track/TrackPlaybackWindowBuild.cpp:2142-2204`, at one of its exits — and **every one
of them returns without a row** (the function's only row is `launch_legacy_dense` at
`:2163`):

| line | exit | input that decides it |
|---|---|---|
| `:2143` | not active / not allocated | ruled out by finding 2-3 |
| `:2147` | `!hasCommittedPasses() \|\| !shouldAvoidFullVisualRebuild(loopLengthTicks)` | ruled out: 1 committed pass, 24-bar length (finding 1-2) |
| `:2151-2152` | source request guard `!current && !hasPendingSnapshotPublish() && requestCommittedPassesRepublish()` | **open** — a skipped request is silent, and so is a failed `queueAllCommittedPassesInto` (it returns false; `LoopSnapshotPublish.cpp:1869`) |
| `:2167` | `prewarmRollingViews()` false (`TrackPlaybackRuntime.cpp:414-422`: allocation or non-empty capture staging) | **open**, silent |
| `:2168`, `:2174-2185` | build-owned / Ready / Requested-match guards | **open**, silent |
| `:2188` | `pendingViewRequest_.active \|\| (!ownsCoverageSource() && !coverageRoleAvailable())` | **open**, silent |

Leading hypothesis (state, **not** a finding): the role guard at `:2188`. Through the
whole switch episode an edit session rebuilt the **active** slot continuously — 3031
`playback_build,note_edit` rows for track 7 slot 5 (loopLengthTicks 18432, revision
0/1/2) between device 114.13 s and 129.28 s — i.e. the track was in an edit session while
the switch was pending, the state run 05 first raised and that `coverageRoleAvailable()`
(`TrackPlaybackRuntime.cpp:1648`) is sensitive to. Not proven here.

## Next discriminating step (preferred: no firmware change)

1. Re-run the same switch leg with the edit session provably closed at switch time (and,
   for contrast, one switch from a track with no edit session) and look for
   `playback_build,snapshot` rows naming the **target** slot. Builds ⇒ the role /
   edit-session state is the blocker (`:2188`); still none ⇒ step 2.
2. Only then: one narrow row at `preparePlaybackLaunchForSlot`'s exits — `(track, slot,
   exit id, currentRoot, pendingPublish, sourceRequested, coverageRoleAvailable)` — as the
   single measurement the silent exits cannot otherwise give. That is a firmware
   instrumentation change (its own execution packet, RAM1 check and stimulus), **not**
   started by this slice.
