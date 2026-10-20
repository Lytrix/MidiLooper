# 3.7e — the rolling frame adopts the committed switch target's bound launch window

Date: 2026-10-20. Task: **3.7e — adoption seed** (prerequisite of 3.7d's owed device read, which stays open). Branch:
`feature/unified-rolling-playback-path`, HEAD `2149fcd4` + the uncommitted 3.7e change. Type: firmware + host verifier
names + device run.

**Verdict: 3.7e is device-verified.** On the qualification image the switch's commit is followed **31 ms later** by
`win_adopted_launch` and then `win_serving` for the target slot, with **zero** `win_requested`, `handoff_unreachable`,
`win_repositioned` or `win_blocked_by_budget` afterwards — the qualification storm of
`captures/i5-s37d-device-32bar-10` is gone. **3.7d's exit is still owed**, for a new and now measured reason: with the
seed adopting, the switch route opens **no** rolling demand at all, so a demand's terminal term
(`win_blocked_by_request`) cannot appear on it. The adopted frame's own end-of-window term is `win_blocked`, which is the
next independent RED (named below, not fixed here).

## The correction that made this run meaningful

Runs `captures/i5-s37e-device-32bar-02` and `-03` reported *no seed row at all*, which the previous reading took as
"the seed returned `NoLaunch` on the device, so `launchPreparation` must already be `None`". That reading was wrong:
**both runs executed the default image, where the rolling lane is compiled out.** `platformio.ini` sets
`PLAYBACK_WINDOW_ROLLING_ENABLED=1` only in `[env:teensy41-capture-serial-window-qualification]`, so
`seedRollingFrameFromBoundLaunch` returns `NoLaunch` at its `#if` on every turn and can never emit a row. The metadata
pins it:

| Run | `build_env` | `firmware_hex_sha256` (16) | Seed compiled in? |
|---|---|---|---|
| `-02` | `teensy41-capture-serial` | `5dbbae91ce5dfdf7` | **no** |
| `-03` (rebuilt tree, after the seed existed) | `teensy41-capture-serial` | `5dbbae91ce5dfdf7` | **no** |
| `-04` | `teensy41-capture-serial-window-qualification` | `b3cdb73ffeb09d64` | **yes** |

The default hex is **byte-identical** before and after the seed change, which is the proof: a change that is compiled
into an image cannot leave that image's hex unchanged. The rule this restores is the one the lane already documents —
the rolling lane is a **qualification image** feature, so `--build-env
teensy41-capture-serial-window-qualification` is part of the run's configuration, not an optimisation.

## What was run

Registered preset only (`windowed_content_parity`, track 8 / slot 4 / channel 8, 32 bars); no stimulus edited, no
captured `command.json` executed, no oracle term relaxed, no qualification-build accounting changed.

```
.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity \
  --build-env teensy41-capture-serial-window-qualification \
  --track 8 --loop-slot 4 --midi-channel 8 --record-bars 32 --boot-reset \
  --switch-target-slot 5 --switch-settle-s 170 \
  --out-dir captures/i5-s37e-device-32bar-04
```

The board was flashed with the qualification hex from the same build (`pio run -e
teensy41-capture-serial-window-qualification -t upload`). Clock: the device's own transport clock at the stimulus's
120 BPM (`--tempo-bpm` default), i.e. 768 ticks/bar and 2 s/bar, so ticks are computable from the log's own timestamps;
the tick rate measured in the run (`record_length` growing 768 ticks per ~2.02 s) matches it. Nothing about the run's
clock was inferred from wall time.

## The device rows

```
[247.751] [INFO] LoopEnd playback commit track=7 3->4 start=0 len=18432
#CAP,247782404,DIAG,win_gate,t=7,s=4,x=win_adopted_launch,len=18432,...,sv=1,rt=1,rl=1,ow=0,dm=0,bld=0,gr=0,rp=0,rq=0
#CAP,247801447,DIAG,win_gate,t=7,s=4,x=win_serving,      len=18432,...,sv=1,rt=1,rl=1,ow=0,dm=0,bld=0,gr=0,rp=0,rq=0
#CAP,251793824,DIAG,win_gate,t=7,s=4,x=win_blocked,      len=18432,...,sv=1,rt=1,rl=1,ow=0,dm=0,bld=0,gr=0,rp=0,rq=0
```

- **Adoption is on the commit's own turn+1**: `win_adopted_launch` at 247.782404 s, **31 ms** after the commit line at
  247.751 s, with `dm=0` — the frame was positioned on the window it already had, and opened no demand.
- **It serves**: `win_serving` at 247.801447 s, `dm=0`, `bld=0` — no build was started for an interval the launch
  already covered.
- **The storm is absent**: over the whole run, `win_requested` 0, `handoff_unreachable` 0, `win_repositioned` 0,
  `win_blocked_by_budget` 0, `win_seed_refused` 0. Compare `captures/i5-s37d-device-32bar-10`, where the commit was
  followed by a 12-span `win_requested rq=12` → `handoff_unreachable` → `win_repositioned` → `win_blocked_by_budget`
  storm.
- **It ends at the window's own interval**: `win_blocked` at 251.793824 s, **3.992 s** after adoption — exactly the
  bound window's two bars (1536 ticks at 768 ticks/bar and 2 s/bar = 4.0 s). Nothing after it for the rest of the
  170 s settle.

Verifier: `captures/i5-s37e-device-32bar-04/host_midi_hitl_windowed_content_parity_20261020_183248.json` — the lane's
only failing check is the pre-existing evidence-ring gate
(`capture_evidence_ring_intact: critical_evidence_loss_rows=1 kinds={'tier_a_evicted': 1}`), which exercises the ring,
not this slice. Lane demand exits: `win_adopted_launch` 1, `win_serving` 1, `win_blocked` 2, `win_requested` 0.

## The rule that changed, and its red proof

`seedRollingFrameFromBoundLaunch` now takes the **commit's own bind** (`launchBoundTick != 0`) as its witness instead of
the handoff view's `launchPreparation == Bound`. `boundLaunchView` makes its expiry/invalidation **sticky**: it clears
`launchPreparation` and leaves the bound view published and `launchBoundTick` intact, because it answers "may the
*handoff* still serve this view" — a different question from "did this slot's playback start inside the window the
commit bound". A live preparation (`Requested`/`Building`/`Ready`/`Fallback*`) still refuses by name (`NotBound`). The
seed also now refuses a bind whose traversal has ended (`Expired`, new ordinal), so `coverage.contains(phase)` alone can
never re-serve a stale window one loop later. `boundLaunchView`, its `phase == elapsed` proof and DEC-147 are untouched.

Four natively pinned cases (all in `test_runtime_working_set_ownership`):

| Case | Shape | Red reading |
|---|---|---|
| `test_3_7e_a_bound_launch_seeds_the_rolling_frame_instead_of_re_requesting` | commit-shaped bind at the loop boundary, then the idle turn | pre-seed: `Expected 6 (Serving) Was 3 (Requested)` |
| `test_3_7e_a_launch_whose_handoff_view_expired_still_seeds_the_rolling_frame` | **the device's shape** — bind intact, `launchPreparation` cleared to `None` | pre-DEC-152 gate: `Expected 6 (Serving) Was 3 (Requested)` (measured this session by restoring the `Bound`-only gate and re-running: 1 failed / 224 succeeded, then reverted) |
| `test_3_7e_a_bound_launch_past_its_interval_is_not_seeded` | tick moved one full window past the bind | green on arrival; the guard the device's next RED depends on |
| `test_3_7e_a_stale_bound_launch_is_not_seeded` | identity moved (revision) after the bind | pre-seed: `Expected 6 Was 0` (served from a stale window) |

## Suites and builds

- Focused native (`pio test -e native -f test_runtime_working_set_ownership`): **225/225**.
- Full native (`pio test -e native`): **3347 cases, 7 skipped, 3340 succeeded** (baseline before this slice: 3345 / 7 /
  3338; +2 cases).
- Host verifier suite (`.venv/bin/python -m unittest scripts.test_windowed_content_parity_verify`): **56/56**, including
  the new `expired` term in `SEED_REFUSALS`.
- `teensy41-capture-serial`: SUCCESS, `code 392,216`, RAM1 `variables 96,192 + padding 1,000`, RAM2 `168,384`
  (hex `5dbbae91…`, unchanged by this slice — the seed is compiled out of this image).
- `teensy41-capture-serial-window-qualification`: SUCCESS, `code 392,376`, RAM1 `variables 96,192 + padding 840`, RAM2
  `168,384` (hex `b3cdb73f…`; RAM1 identical to the default image, RAM2 unchanged).

## What is not claimed

- **No 3.7d read.** `win_blocked_by_request` did not appear, and cannot appear on this route now: the seed removes the
  demand that would carry it.
- **No post-adoption continuation.** The frame serves its two bars and then blocks; the loop's remaining 22 bars are
  served by the ordinary path, not by rolling windows. Named next RED below.
- **No qualification-build accounting change** (the slice's own instruction), no DEC-147 change, no timing/envelope
  claim, no `ExecutionBounds.md` row, and no change to any registered stimulus.
- **No host-clock run.** The run used the device's transport clock at the stimulus's 120 BPM, which the log's own tick
  cadence confirms; `--clock-source host` for this leg does not exist yet (see `tasks.md` 3.7g).

## The next independent RED, in one line

`resolveRollingPlaybackWindow` rejects a `Serving` frame whose `coverage.contains(phase)` fails and never returns it to
`Idle`, so an adopted zero-deadline frame cannot roll on to a successor span: `win_serving` 247.80 s → `win_blocked`
251.79 s on device, and no demand afters. That is the post-adoption route, and it is the slice that must run before
3.7d's exit evidence can be taken on the switch route.
