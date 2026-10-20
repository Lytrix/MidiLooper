# 3.7g — the long-loop leg takes the host's clock (device evidence)

Slice: `windowed-playback-generation` task **3.7g — host-clock source for the long-loop HITL leg**. Branch
`feature/unified-rolling-playback-path`, HEAD before this slice `6bb34096` (3.7f). **Host tooling only**: nothing under
`src/` or `include/` changed, so both firmware images are untouched (no rebuild; the board stays on the qualification
hex `b4a73e2950d9a3654…` from 3.7f).

## Scope and transition oracle

| Item | Value |
|---|---|
| Scope | `scripts/hitl/scenarios/long_loop_display_window.py` only: `--clock-source {device,host}` + `--tempo-bpm`, the host-mode transport/clock branch, the take's grid anchor, and the host-mode stop lead. Plus its host tests. No firmware, no lane oracle, no 6.4 change, no `boundLaunchView`/DEC-147 change. |
| Why | The windowed evaluation's clock was the device's own transport, so "the two-bar window ends here" could only be named in seconds and every boundary had to be inferred from the device's clock. With the host owning 24 PPQN at a stated BPM the clock is **known before** the evaluation. |
| Transition oracle | A 32-bar take recorded with `--clock-source host --tempo-bpm 90` commits a length that is a whole number of bars — `len % 768 == 0`, `768 = TICKS_PER_BAR` — while the device runs at the host's tempo, not its own 120 BPM. |
| Out of scope | The successor demand's build cost (3.7h), the invalidation-term exit (3.7d), and the lane's own gates. |

## RED → GREEN (device, three runs of the same preset)

Harness (unchanged entry point, `--boot-reset`, slot 4, 32 bars; only the clock flags are new):

```
HITL_MANAGED_RUN_TIMEOUT_S=600 .venv/bin/python scripts/host_midi_hitl.py run \
  --preset windowed_content_parity --build-env teensy41-capture-serial-window-qualification \
  --track 8 --loop-slot 4 --midi-channel 8 --record-bars 32 --boot-reset \
  --switch-target-slot 5 --switch-settle-s 170 --clock-source host --tempo-bpm 90 \
  --out-dir captures/<run>
```

| Run | Leg shape | Committed take |
|---|---|---|
| `captures/i5-s37g-host-clock-01` | host clock, but the transport was started **before** the clear/record leg (host Start twice; stream paced from wherever it arrived) | `RECS,stop,3,25136,152,24984,25344,0` → **25 344 (33 bars)** |
| `captures/i5-s37g-host-clock-02` | Start **after the record arm** (device tick 0) and the stream anchored on `grid_origin_pulse`, but the leg still pressed the stop ~0.9 s after the stream | `RECS,stop,3,25088,96,24992,25344,0` → **25 344 (33 bars)** |
| `captures/i5-s37g-host-clock-03` | the above **and** the stop pressed from inside the stream, `HOST_STOP_LEAD_SECONDS = 0.9` (32 clocks at 90 BPM) | `RECS,stop,3,24648,96,24552,24576,0` → **24 576 (32 bars)** |

`24576 = 32 × 768`, so `len % TICKS_PER_BAR == 0`. The device's own debug line agrees:
`Recording stopped @ tick 24648 (recStart=96 length=24576)` (`session_20261020_210231.log`, `[126.470]`).

### Why the two early runs were one bar long

Two independent latenesses, both of which a 120 BPM bar (2.0 s, quantised ± half a bar) hides and a 90 BPM bar
(2.67 s, ±1.33 s) does not:

1. **Start-before-arm** left the take's first bar ahead of the stream's anchor, so the stream's own 32 bars ended one
   bar into the take. Fixed by sending the realtime `Start` **after** the record arm — that `Start` is the device's
   tick 0 (`ClockManager` resets the tick on it and holds 0 through the next pulse) — and by handing
   `grid_origin_pulse` to the streamer, so the bar budget is counted from the take's origin, not from when the process
   got around to streaming.
2. **Stop pressed after the stream** loses ~0.9 s to the leg's transition-baseline wait (0.5 s) plus the device's
   short-press double-tap window (~0.3 s). At 90 BPM that is past the half-bar quantum. Fixed by handing the press to
   the streamer one lead early (`stop_press_note` + `stop_press_advance_clocks`) and skipping the leg's own press;
   the leg then only watches for `STOPPED_RECORDING → PLAYING`.

Run `-03` also shows the anchor doing its job rather than being lucky: the stream arrived 27 pulses (`first_note_tick=216`)
after tick 0, and the budget still closed on bar 32.

## Run `-03` — the readings

| Source | Reading |
|---|---|
| leg stdout | `clock source host: transport stays stopped until the record arm (90.0 BPM, continuous 24 PPQN pump after it)` |
| leg stdout | `host Start after record arm (device tick 0), grid origin pulse 1` |
| leg stdout | `host clock anchor: source=host_clock missed_pulses=27 first_note_tick=216` |
| leg stdout | `record stop was sent from inside the stream (host clock lead)` |
| leg stdout | `serial verification ok=True` |
| `session_20261020_210231.log` | 32 × `DIAG,record_length,ticks=` rows, 768 → 24 576, first at µs 41 517 924 and last at 125 225 435: **31 bars in 83.7 s ≈ 2.70 s/bar ≈ 89 BPM** |
| same | `RECS,stop,3,24648,96,24552,24576,0` and `Recording stopped @ tick 24648 (recStart=96 length=24576)` |
| same | `[184.015] MIDI clock lost, switching to internal at 89.8 BPM` — the **only** clock-lost row, 58 s after the take ended: the device was slaved to the host's 90 BPM for the whole take, and falls back to its own clock at 89.8 (the tempo it was measuring from the host) once the pump is retired at the leg's end |
| leg JSON | `leg.ok: true`, `expected_loop_len_ticks: 24576`, `disp_window_row_count: 1438`, `playing_bounded_window_rows: 39` |

Run `-01` (which started the host transport early) is the contrast for the clock ownership reading: it produced **two**
`MIDI clock lost` rows — `19.139 (118.9 BPM)` after the clear's realtime `Stop`, and `171.705 (90.1 BPM)` at the leg's
end — i.e. the early Start had to be retired and re-started, which is exactly the shape that also lost the take's
alignment.

## Lane observations (not this slice's claims)

The run's lane verdict is `ok: false` on two rows, neither of which is a 3.7g acceptance:

- `capture_evidence_ring_intact: critical_evidence_loss_rows=1 kinds={'tier_a_evicted': 1}` — the pre-existing ring
  pressure that also failed 3.7f's run (unchanged firmware).
- `no_windowed_overrun_under_transport: overrun_rows=1 under_transport=1 worst=72` — **new relative to 3.7f's run** and
  observed once, at µs 43 931 169 (`DIAG,win_gap_overrun,64,1,72,1,4`), i.e. during the 32-bar host-clocked
  recording. The firmware image is identical to 3.7f's, so this is a run-condition observation (a single permitted-block
  overrun of 72 µs at a tempo the device is being clocked at externally), recorded here rather than smoothed. It belongs
  with the lane work (3.7h/3.7d), not with the host-clock leg.

## Gates

| Gate | Result |
|---|---|
| New host module `scripts/test_long_loop_display_host_clock.py` | **Ran 21 tests … OK** (flags and their defaults, bar length per source, stop lead per tempo, and a wiring scan for the master's creation/how it is started/stopped, the anchor, the in-stream stop, the report fields) |
| `scripts/test_long_loop_display_serial_verify.py` | **Ran 30 tests … OK** |
| `scripts/test_windowed_content_parity_verify.py` | **Ran 56 tests … OK** |
| `scripts/test_hitl_host_grid_record.py` (the reference shape's own suite) | **Ran 23 tests … OK** |
| `scripts/test_baseline_canonical_args.py` | **Ran 3 tests … OK** |
| `pio test -e native` | **3349 test cases: 7 skipped, 3342 succeeded** (unchanged — no firmware file changed) |
| `openspec validate windowed-playback-generation --strict` | valid |

## What this does not claim

- The lane's gates are not green (above); 3.7h and 3.7d remain open.
- No new stimulus was registered and no canonical stimulus was edited: the run reuses the `windowed_content_parity`
  preset and its existing fixtures.
- Device mode (`--clock-source device`, the default) is byte-for-byte the same flow it was: every host branch is
  behind `clock_master is None`, and the new stop lead is `0` clocks there.
