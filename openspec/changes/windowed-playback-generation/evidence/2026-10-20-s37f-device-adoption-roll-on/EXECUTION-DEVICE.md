# 3.7f — the adopted launch window rolls on to its next span (device evidence)

Slice: `windowed-playback-generation` task **3.7f — adoption continuation**, with its blocker **3.7i — bound-launch
ownership** (`DEC-153`, `DEC-154`). Branch `feature/unified-rolling-playback-path`, HEAD before this slice `94725d69`.

## Scope and transition oracle

| Item | Value |
|---|---|
| Scope | `Track::seedRollingFrameFromBoundLaunch` (the seed's `deadlineTick`) and `Track::preparePlaybackLaunchForSlot` (a `Bound` preparation is not cancelled). No resolver branch, no `boundLaunchView`/DEC-147 change, no qualification-build accounting change. |
| Transition oracle | A frame seeded `Serving` on a two-bar launch window must, at `bind + 2 bars`, end as **`Expired` → `Idle`** and be followed by a **`win_requested`** for the successor span — not by `win_blocked`. |
| Observable (device) | `win_gate` rows: `win_adopted_launch` → `win_serving` → (`win_seed_refused` with `rq=5` = `expired`) + `win_requested`, with **no** `win_blocked`/`win_repositioned` between them. |
| Out of scope | The successor demand's build cost (3.7h) and 3.7d's terminal `win_blocked_by_request` (still needs a route whose demand can finish). |

## RED → GREEN (native)

The paired case `test_3_7f_a_served_launch_span_expires_and_the_lane_opens_the_next_one` drives the production caller
(the read path's `resolveRollingPlaybackWindow`) at `boundAt + 2 bars` after an adoption and then one service turn.

- **RED** (before the seed wrote the bind tick as `deadlineTick`):
  `Expected 7 Was 8. a served span ends by expiring; blocking the frame strands the lane for the whole loop`
  (`Expired` = 7, `Rejected` = 8), i.e. the resolver sized the zero-deadline frame as `frame.loopLength` and rejected
  its two-bar coverage.
- **GREEN** after the one-line seed change: focused suite `226 test cases: 226 succeeded` (before 3.7i's case).
- The blocker's case `test_3_7f_a_bound_launch_survives_the_provision_owner_turn`:
  with the guard disabled → `Expected FALSE Was TRUE` (the provision turn restarted the launch); with the guard →
  green.

## Suites, builds, images

| Gate | Result |
|---|---|
| Focused native `pio test -e native -f test_runtime_working_set_ownership` | **227 / 227** |
| Full native `pio test -e native` | **3349 test cases: 7 skipped, 3342 succeeded** (baseline 3347 → +2 cases) |
| Host `python -m unittest scripts.test_windowed_content_parity_verify` | **Ran 56 tests … OK** |
| Build `teensy41-capture-serial` | **SUCCESS**, hex `d415934a448a26c3…` |
| Build `teensy41-capture-serial-window-qualification` | **SUCCESS**, hex `b4a73e2950d9a3654…` |

The guard is production code (both images); the seed's deadline change is compiled only where
`PLAYBACK_WINDOW_ROLLING_ENABLED=1`, i.e. the qualification image, which is why the default hex moves only for 3.7i.

## Device

Harness (unchanged; the board is flashed with the qualification image first):

```
.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity \
  --build-env teensy41-capture-serial-window-qualification --track 8 --loop-slot 4 --midi-channel 8 \
  --record-bars 32 --boot-reset --switch-target-slot 5 --switch-settle-s 170 --out-dir captures/<run>
```

### `captures/i5-s37f-device-32bar-03` — 3.7f observed

| capstamp (µs) | row |
|---|---|
| 234 756 560 | `win_gate,t=7,s=4,x=win_adopted_launch,…,sv=1,rt=1,rl=1,ow=0,dm=0` |
| 234 771 268 | `win_gate,t=7,s=4,x=win_serving,…` |
| 238 746 655 | `win_gate,t=7,s=4,x=win_adopted_launch,…` (the frame was `Idle` a few ticks before its end and the seed re-adopted the same window; `elapsed < 2 bars`) |
| 238 758 000 | `win_gate,t=7,s=4,x=win_seed_refused,…,rq=5` (**expired**) |
| 238 758 000 | `win_gate,t=7,s=4,x=win_requested,…,rq=12` (the successor demand, same turn) |
| 239 467 336 | `win_gate,t=7,s=4,x=win_blocked_by_budget,…` after 12 `win_repositioned` (the demand's cost — 3.7h) |

Counts: `win_adopted_launch` 2, `win_serving` 1, `win_requested` 12, `win_repositioned` 12, `win_blocked_by_budget` 1,
`win_blocked` 2 (one is the pre-existing slot-3 row at 131.75 s, one is the budget terminal at 239.478 s).
The adopted frame is **never** `win_blocked`: the span ends by expiring and the lane asks for the next one.

Contrast (`captures/i5-s37e-device-32bar-04`, pre-3.7f): `win_adopted_launch` 247.782404 → `win_serving` 247.801447 →
`win_blocked` 251.793824 (3.992 s = the two-bar interval) and `win_requested` **0** — the same span, blocked instead of
expired, with no successor demand.

### `captures/i5-s37f-device-32bar-01` — the 3.7i interleaving

| capstamp (µs) | row |
|---|---|
| 246 699 | `All Notes Off sent @ tick 49152` (the commit's branch) |
| 246 734 590 | `launch_gate,t=7,s=4,x=launch_requested,…,prep=0,root=1,role=1,own=0` |
| 246 735 | `LoopEnd playback commit track=7 3->4 start=0 len=18432` (drained) |
| 246 768 000 | `win_gate,…,x=win_busy,…,sv=0` |
| 246 786 785 | `win_frozen_readiness,7,4,build_drop,launch_cancelled,…` |
| 246 807 423 | `win_gate,…,x=win_seed_refused,…,sv=0,rq=3` (**identity**) |
| 246 807 487 | `win_gate,…,x=win_requested,…,rq=12` (the pre-3.7e whole-loop demand) |

Reading: the commit bound the launch, the provision owner's turn then read the `Bound` preparation as stale and
restarted it, and `cancelPlaybackLaunchForSlot(slot, true)` reset the adopted **current** view's metadata. The seed's
bind witness survived (`launchBoundTick != 0`) but the view was no longer servable (`sv=0`), so it refused on identity
and the lane fell back to the whole-loop demand (12 re-positions, `win_blocked_by_budget`). `DEC-154` makes that restart
report `launch_bound_owned` and leave the window alone.

The run's lane report (`host_midi_hitl_windowed_content_parity_20261020_193248.json` equivalent for `-01`) has the same
single failing check as every run since 3.7c: `capture_evidence_ring_intact`, `critical_evidence_loss_rows=1`
(`tier_a_evicted: 1`) — pre-existing ring pressure, not this slice.

## What this slice does **not** claim

- **Windows do not resume.** The successor demand is refused for cost, not for correctness: `qualification_started`
  5.86 ms per grain (the whole-loop page), `handoff_unreachable` at 24.6 ms builds, 12 re-positions,
  `win_blocked_by_budget`. Task **3.7h**.
- **3.7d is still open.** `win_blocked_by_request` is a *demand's* terminal term; no demand in these runs reaches a
  terminal without the budget block, so the read needs 3.7h (or a slot played without a switch).
- The device read for 3.7i is the absence of the restart; a run that lands the interleaving should show
  `launch_bound_owned`.
