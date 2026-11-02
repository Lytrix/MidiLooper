# C5.3b — device owner-turn cadence: how many ticks are 16 and 128 window-lane turns?

Date: 2026-11-02. This is the device half of the successor-lead measurement (DEC-246 amendment 24). The host measured
a successor window in **owner turns**:
- 14–16 turns for a dense window with one owner;
- 112–128 turns under the worst arbitration (seven denials per grain, or eight real tracks).

This run converts those turn counts into ticks on the device.

## Instrument

`PlaybackLaneMeasurement` (capture images only) keeps a ring of the tick and microsecond stamps of the last 128
**consecutive running dispatches of one epoch**:
- The ring lives in RAM2 (`DMAMEM`, +1,088 B); the code is flash.
- A dispatch is one `stepPlaybackViewMaintenance` opportunity of the window lane in `main.cpp`: the host model's owner
  turn.
- A stopped dispatch, a new epoch or a tick rewind restarts the run, so no span crosses one.

Each `win_gap` row now carries, per block:
- `span16`: the most ticks any 16 consecutive dispatches spanned;
- `span128`: the most ticks any 128 consecutive dispatches spanned;
- `span128 us`: the microseconds of that 128 span.

**Row format.** Schema 4 appends these three fields after the schema number, so fields 4–19 keep their positions for
existing readers. `scripts/hitl/verify/windowed_lane.py` reads them.

**Test.** `test_clock_transport_master` `test_lane_measurement_spans_consecutive_running_dispatches` covers a stall
inside the spans, a restart on stop and on a new epoch, and the edge where N turns is N intervals (N + 1 dispatches).

## Run

- **Image:** `teensy41-capture-serial-revstore-reader` (flag 1, the C5.4 A/B's heavier side), HEX `6d7bb3afcd5666b6…`.
  - RAM1 unchanged: ITCM 392,280 + 936 (floor 840), variables 96,224 / locals 34,848.
- **Upload:** `pio run -t upload` (teensy-gui). The first attempts found Teensy Loader busy with another session's
  programming cycle; it succeeded once that finished.
- **Boot:** `#CAP,2160004,DIAG,boot_reset,crash=0`, then `BOOT,load,start` / `BOOT,load,ok`. The 917 schema-4 rows prove
  the new image is running (only this build writes schema 4).
- **Stimulus:** `STIM-WINDOW-CONTENT-01` (registered), driven by `host_midi_hitl.py run --scenarios windowed_content_parity
  --track 8 --loop-slot 4 --midi-channel 1 --boot-reset`, with `HITL_MANAGED_RUN_TIMEOUT_S=900`.
  - It records a 24-bar take on the HITL slot, plays it, enters and exits `NOTE_EDIT`, and does the play/stop gestures.
  - The scenario's own display verification reported `ok=True`.
- **Capture:** `captures/c5-3b-cadence-01/session_20261102_112059.log`, 130 s of running transport. 906 running blocks;
  786 of them hold a complete 128-turn span.
- **Tempo:** about 125 BPM, 400 ticks/s (one row every ~25 ms with 9–10 ticks between rows).

## Result

| Span | p50 | p90 | p99 | max |
|---|---|---|---|---|
| 16 consecutive lane turns (ticks) | 93 | 159 | 168 | **285** |
| 128 consecutive lane turns (ticks) | 718 | 1,255 | 1,259 | **1,265** |
| 128 consecutive lane turns (µs) | 1.85 s | 3.23 s | 3.24 s | **3.26 s** |

Other fields in the same blocks:
- the worst gap after an eligible turn is 23 ticks (p99 14), with 0 overruns of the declared 64;
- the worst worker span is 7,990 µs.

**Reading.** In the worst stretch a window-lane turn comes about every **25 ms (about 10 ticks at 125 BPM)**. The
typical rate is about 14 ms per turn.
- **Physical dense successor** (14–16 turns): at most **285 ticks**, a fifth of one span (1,536). It fits with a wide
  margin.
- **Successor under the worst arbitration** (112–128 turns): at most **1,265 ticks**, which fits one span with 18 %
  margin at this tempo. Two caveats:
  - the lane's cadence is wall time (about 25 ms per turn), so the tick cost grows with tempo. 128 turns (3.26 s)
    fit a two-bar span (480 / BPM seconds) only below about **147 BPM**;
  - this run sounded **one track**. With eight tracks playing, each main-loop turn does more playback and display work,
    so the per-turn time can only grow.
- **The declared DEC-147 price is far above all of these:** 7 grains x 8 x 64 + 768 = 4,352 ticks at the floor, and
  28,928 for the measured dense window.

**Correction.** An earlier reading of older captures, given in chat only, claimed a lane turn "every 36–103 µs" with
0.02 ticks per turn. That was wrong. `opportunities` is a running total that `clearBlock` does not reset, and dividing
by it under-states the time per turn by about a thousandfold.

## What this settles, and what not

- **Settled:** at about 125 BPM with one sounding track, one successor window prepared from N's start is ready within
  one span. This holds even under the host's worst arbitration turn count: 1,265 of 1,536 ticks.
- **Not settled:**
  - higher tempos: the cost grows linearly with BPM, and the limit is about 147 BPM at 128 turns;
  - eight tracks actually sounding on the device;
  - display and save owners at their worst.
- **For the owner's decision (amendment 24, "afterwards"):**
  - an evidence-based successor price in **owner turns**, independent of tempo, would be about 16 turns idle and about
    128 under eight-way arbitration;
  - its tick price comes from the device's measured worst turn time multiplied by the tempo;
  - at high tempo or full load the bound reaches the span, which is the case for a deeper lookahead (option C).

## Gates

- **FAST:** `test_clock_transport_master` 8/8.
- **AFFECTED:** `test_clock_transport_master`, `test_capture_ring`, `test_capture_line_tier`,
  `test_runtime_working_set_ownership`, `test_revision_manifest_lane`: 349/349.
- **FULL:** not run for this slice (the owner's three-tier cadence). The last full run was at `c327abcb`: 3,931 cases,
  3,923 passed, 8 skipped.
- **Verifier Python tests:** 111/112. One failure,
  `test_snapshot_progress_requires_live_and_post_stop_owner_work`, already fails at `HEAD` with the unmodified
  verifier.
