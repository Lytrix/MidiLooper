# Rolling playback ON + the 2-bar base recipe (record, 2× overdub, undo): the A/B that isolates the stall (2026-10-17)

**Requested:** turn rolling playback on and run the registered 2-bar HITL (`STIM-BASE-2-2-01@v1` — 2-bar record, 2-bar
overdub, default second overdub, post-stop undo/redo) on track 8 / slot 4, then evaluate the MIDI-boundary timings.

**Answer: on the rolling-enabled image the run does not complete** — the first overdub never stops, so there is no
boundary verdict at all. **On the identical source with rolling off, the same recipe PASSES**, including both deferred
overdub-stop boundaries. The two runs differ only by the build flag, so the stall is attributable to
`PLAYBACK_WINDOW_ROLLING_ENABLED` on a **2-bar** loop. Nothing was fixed; the named hypothesis and the next diagnostic
are below.

## Identity

| | Rolling run | Control |
|---|---|---|
| Build env | `teensy41-capture-serial-window-qualification` (`-D PLAYBACK_WINDOW_ROLLING_ENABLED=1`) | `teensy41-capture-serial` (rolling off) |
| Firmware HEX | `32f6501d2051bc38af046c204d2e38e6773987f2cc3ee36714ada7069e13acaa` | `b67684e4…` (current tree) |
| Source | `26125ceb` + the branch's dirty tree (identical for both) | same |
| RAM1 | `variables 96,192 / code 392,280 + padding 936` ≥ 840 | unchanged line reported by the guard |
| Stimulus | `STIM-BASE-2-2-01@v1` (base preset), track 8 / slot 4, channel 8, device clock | same |
| Capture | `captures/i5-rolling-base-2bar-01` | `captures/i5-rollingbase-control-off-01` |

Both runs used `--boot-reset` (verdict-grade fresh boot) and the managed capture.

## The rolling run: INCONCLUSIVE, the first overdub never stops

- verdict **`INCONCLUSIVE_CAPTURE`**, `capture_complete: false`, `setup_transitions_confirmed: false`; issues
  `missing_phase_boundaries`, `record_unmatched_open_notes`, `overdub_first_note_missing`,
  `record_stored_revt_missing`, `second_overdub_missing_phase_boundaries`, `second_overdub_first_note_missing`;
  abort reason `second overdub transition not confirmed`.
- **Boundary evidence is absent, not failed:** `interval_counts.OVERDUB_STOPPING = 0`,
  `overdub_stopping_segments = []`, and the verifier's own diagnostic is **`overdub_without_stop`**.
- **The stop press did reach the device**: `#CAP,44405662,MI,U,144,8,39,98` (the transport/record button, note 39) at
  device time 44.4 s. The track's last state transition is `ST,Track,PLAYING,OVERDUBBING` (40.1 s) and it never leaves
  it.
- **The record leg itself completed**: `RECS,stop,3,357,0,1893,1536,0` (raw 1893 → final 1536, the 2-bar bucket), and its
  stages completed (`record_stop 9 µs`, `visual_cache_request 609`, `revt_queue 623`, `save_request 649`,
  `state_advance 636`, `finalize 592`).
- **A deferred stop job stays pending at `phase=16`**: `Record stop retarget: phase=16 dest=1` six times between 80.8 s
  and 130.9 s, and the run-end allocator observation is **`#HEAP_STATS,refused=track_work`** (the job holds track work).
  `RING,overflow` rows: **0**. Canonical shutdown still completed (`complete`, 2 drains, `ok`).
- The lane's own gate rows show why it never served: `x=win_window_ineligible` ×5, `x=win_track_not_playing` ×19,
  `x=win_not_servable` ×2 — on a 2-bar loop the lane is not a servable route for this target (the two
  `win_readiness` rows read `not_servable`).

## The control (identical source, rolling off): PASS

- verdict **`PASS`**, `capture_complete: true`, `setup_transitions_confirmed: true`, `issues: []`.
- **Both deferred overdub-stop boundaries are present and tight**:
  `interval_counts.OVERDUB_STOPPING = 2`, with
  `overdub_stopping_segments = [{overdub_start 33805457 → stopping 39123634 → destination 39123653 PLAYING},
  {47760628 → 52950194 → 52950213 PLAYING}]` — the stop request reaches its PLAYING destination in
  **19 µs** on both legs.
- Stop-phase samples are valid with real slack (`max_duration_us` 92 µs, `slack_us` 503 µs on phase 0; all `valid: true`).
- Content/consumer checks pass: stored overdub span `16/15` notes, second overdub `20/7`, both `count_ok/span_ok/bars_ok`
  true; display `overdub_ok`/`transport_ok` true, and across undo/redo the frame notes go **60 → 45 → 54**;
  capture cleanup `dedup=0 minlen_pairs=0 wrap_synth=0`; persistence handoff `ok`.

## Timing rows (device-side, from each run's own log)

`DIAG,env_dist,<channel>,p50Lo,p50Hi,p99Lo,p99Hi,samples` — quantiles are **bucket bounds**, not invented microseconds;
`DIAG,late_route,<type>,<ch>,<maxUs>,<overCount>,<samples>,<spill>` — for `late_*` families `overCount` counts **every
lateness > 0** (no threshold, per `RuntimeTimingTelemetry`).

| Row | Rolling run (stalled) | Control (PASS) |
|---|---|---|
| `late_on` p50 / p99 | 0–255 µs / 0–255 µs (28) | **20.7–21.0 ms / 20.7–21.0 ms** (61) |
| `late_off` p50 / p99 | 0–255 µs / 0–255 µs (22) | **20.7–21.0 ms / 20.7–21.0 ms** (58) |
| `late_route` NoteOn | max **0 µs**, over 0/41 | max **20,969 µs**, over 451/495 |
| `late_route` NoteOff | max **0 µs**, over 0/34 | max **20,929 µs**, over 421/457 |
| `midi_gap` p99 | 1.0–1.3 ms (15,998) | 2.6–2.8 ms (7,710) |
| `midi_input` p99 | 0–255 µs (15,998) | 0–255 µs (7,710) |

**Read carefully, not comparatively:** the rolling run aborted early, so its `late_*` numbers describe only the record
leg (~40-70 emitted notes); they are not the same flow as the control's full recipe. The control's **~20.8 ms on every
note** is exactly one MIDI clock pulse at the shared device tempo (24 PPQN ⇒ 8 internal ticks ⇒ 8 × 2.60 ms), i.e. that
image emits note-on/off on the clock edge rather than at the exact tick. Recorded as an observation; it is **not**
attributed here and the verifier treats it as advisory (its boundary checks are about phase scheduling, not µs lateness).

## Attribution and the named hypothesis (not a diagnosis, nothing fixed)

The runtime, stimulus, target, channel, clock source and tempo are identical; the only difference is the rolling build
flag — and the control passes. So the stall is a property of the rolling-enabled build on a **2-bar** take.

Hypothesis, stated as such: with the lane compiled in, the Record stop's retirement (`Track::stepDeferredRecordRetirement`
— the S7.5b.5 release) waits for a **root-return successor** (DEC-136) that this loop's lane demand can never produce
(`win_window_ineligible`/`win_not_servable` on a 2-bar loop), while DEC-136's hold is counted only while *no adoption
boundary can come* — and on a 2-bar loop the boundaries keep coming. That would leave the job parked at `phase=16`, which
is exactly what the run shows (`Record stop retarget: phase=16`, `#HEAP_STATS,refused=track_work`).

Named next diagnostic, not done here: a **native reproducer** with `PLAYBACK_WINDOW_ROLLING_ENABLED` and a 2-bar take
(the DEC-139 shape, one bar/two bars, no lane window possible), asserting that the record job retires and the following
overdub stop lands. Until that exists, no fix is proposed — the previous round's lesson (DEC-139/140) is that guessing
here costs device runs.

## What this does and does not change

- **No 6.4/3.15f checkoff**: this run does not qualify a 2-bar rolling route and it does not retract the DEC-140/141
  `rolling_record` PASS, which covers an 8-bar take on its own dedicated preset.
- **No firmware, no limit, no decision entry**, no SD cleanup: the control run cleared and re-recorded the **authorized
  scratch slot** (track 8 / slot 4) through the stimulus's own pre-record clear; no other slot was addressed.
- **Device restored**: pinned default HEX `08f43e450ee327546b844af95a8b9ac57be46fcb8e84ad982ddda10e357fc964` uploaded
  no-build from the retained file (hash verified before and after; one upload attempt failed while the board was
  re-enumerating and succeeded on retry), then a read-only postflight
  (`captures/i5-rollingbase-restored-postflight-01`): boot banner, reconcile 0, `free_chunks=394`,
  admission/interactive/undo/stopped/prewarm-idle all 1, **S2b.3 met**, library observation complete over 64 slots
  (`initial_playback=8`, `restore_completions=0`, `committed_at_checkpoint=13`).

## Reproduce

```bash
# rolling ON
pio run -e teensy41-capture-serial-window-qualification -t nobuild -t upload
.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 \
    --build-env teensy41-capture-serial-window-qualification --boot-reset --out-dir captures/<rolling>

# control: rolling OFF, same tree
pio run -e teensy41-capture-serial -t nobuild -t upload
.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 \
    --build-env teensy41-capture-serial --boot-reset --out-dir captures/<control>
```
