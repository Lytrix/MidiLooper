# Host-side MIDI automation baseline

Goal: run a repeatable local hardware-in-the-loop baseline where a Mac script drives the connected Teensy over USB MIDI to:
- select each track,
- record dense chromatic notes + CC data,
- stop recording (firmware returns to playing),
- start overdub,
- send dense overdub data,
- stop overdub,
- emit a pass/fail report.

This is the first automation layer for future scenarios (for example slot switching and new loop workflows).

## Script

`scripts/host_midi_automation_baseline.py`

## Dependencies

```bash
python3 -m pip install mido python-rtmidi pyserial
```

## Firmware build to use

Use capture build when you want `#CAP` state assertions (`ST`, `RECA`, `RECS`):

```bash
pio run -e teensy41-capture-serial -t upload
```

Mode A (`--serial-port`) and Mode B (`--follow-current-session`) both enable serial transport proxy for bar pacing when USB MIDI clock is absent on the host input port.

## List ports first

```bash
python3 - <<'PY'
import mido
print("MIDI outputs:")
for p in mido.get_output_names():
    print(" -", p)
print("MIDI inputs:")
for p in mido.get_input_names():
    print(" -", p)
PY
```

Optionally list serial devices:

```bash
pio device list
```

## Run baseline (all 8 tracks)

With serial assertions enabled:

```bash
python3 scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" \
  --midi-in "Teensy" \
  --serial-port "/dev/cu.usbmodemXXXX" \
  --start-transport \
  --track-count 8 \
  --record-seconds 3.0 \
  --overdub-seconds 2.0
```

Without serial assertions:

```bash
python3 scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" \
  --midi-in "Teensy" \
  --start-transport \
  --track-count 8
```

Target a single track directly (example: track 5):

```bash
python3 scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" \
  --midi-in "Teensy" \
  --track-number 5 \
  --start-transport
```

Use bar-based durations (`2|4|8|16|32|64|128`) instead of seconds:

```bash
python3 scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" \
  --midi-in "Teensy" \
  --track-number 5 \
  --midi-channel 5 \
  --tempo-bpm 120 \
  --record-bars 8 \
  --overdub-bars 8
```

By default bar-based mode follows incoming MIDI clock (`24 PPQN`) so 8 bars means 8 real bars at the current running tempo.  
When USB MIDI clock is not echoed to `--midi-in` but serial capture is enabled (`--serial-port` or `--follow-current-session`), the script uses a **wall-clock tempo proxy** when recent `#CAP,BPM` or `#CAP,BAR` lines show transport running. Proxy tempo prefers the latest serial BPM, then `--tempo-bpm` (default `120`).
Disable bar sync with `--no-bar-sync-from-midi-clock` if you explicitly want tempo-derived seconds only.
If no MIDI clock pulses are observed and serial proxy is inactive, bar-synced phases may record `0` clocks and fail (`phase_not_activated_or_no_clock`).
Use `--max-run-seconds` to enforce a hard timeout for the full run. On timeout, the script exits with failure and still writes a report (`assertions.timed_out = true`).

Default musical pattern in bar-sync mode:
- **Record phase:** 16th-note stream across `C3..C5`
- **Overdub phase:** 8th-note stream across `C1..B2`

You can override the ranges with:
- `--record-low-note` / `--record-high-note`
- `--overdub-low-note` / `--overdub-high-note`

For deterministic drift checks, fixed-note grid mode is enabled by default:
- record phase sends one fixed note on each 16th (`--record-fixed-note`, default `C4`)
- overdub phase sends one fixed note on each 8th (`--overdub-fixed-note`, default `C2`)
- disable with `--no-fixed-grid-notes`

JSON report fields for drift assertion:
- `record_max_abs_grid_jitter_clocks`
- `overdub_max_abs_grid_jitter_clocks`
- `record_mean_abs_grid_jitter_clocks`
- `overdub_mean_abs_grid_jitter_clocks`

To reduce dropped tail notes at phase boundaries, the script waits briefly after each stream before sending stop:
- `--post-stream-settle-ms` (default `120`)

To align stop actions with the intended musical boundary despite button short-press deferral, the bar stream ends early by:
- `--stop-press-advance-clocks` (default `12` clocks)

## Important behavior assumptions

- Control button events use channel 16 and notes from `include/MidiConfig.h`:
  - track select base note `60`,
  - record/overdub button note `36`,
  - global transport note `39` (optional `--start-transport`).
- Dense musical input uses a recordable channel (`--midi-channel`, default `1`); channel 16 is rejected by the script because firmware excludes channel 16 from recording.
- `--midi-channel` chooses the recorded MIDI data channel; `--track-number` chooses which looper track row button gets pressed.
- Before each record start, the script sends a long-press clear command on the selected track by default (to avoid unintended overdub on existing content). Use `--no-clear-before-record` to disable.
- Default post-press wait is tuned close to the short-press window (`--phase-wait-ms 320`) so recording/overdub input starts near phase boundaries while still allowing button resolution.
- Overdub note streaming defaults to a one-beat delayed start:
  - `--overdub-start-delay-bars 0`
  - `--overdub-start-delay-beats 1`
  This keeps overdub entry from landing too early in the loop while preserving repeatable bar-synced capture.
- The script adds a final settle delay before exit (`--final-wait-ms 1200`) so deferred button actions can complete.
- In bar mode, the script records clock pulses seen in the JSON report (`*_clock_pulses_seen`) so you can verify phase length. Counts come from host MIDI in or serial transport proxy pacing — not from `#CAP` clock lines.
- Record stop returns to playback (`STOPPED_RECORDING -> PLAYING`), a second record short press enters overdub (`PLAYING -> OVERDUBBING`), and a third press exits overdub (`OVERDUBBING -> PLAYING`).
- First-note timing validation (serial verification path):
  - `--record-first-note-max-clocks` (default `12`)
  - `--overdub-first-note-max-clocks` (default `12`)
  - Offset is derived from microsecond delta between phase `ST,Track` entry and first `#CAP,MI,U` note-on, converted using serial BPM (or `--tempo-bpm`).

## Output

Each run writes JSON output into `captures/`:

- `host_midi_automation_baseline_<timestamp>.json`

The report includes:
- per-track sent note/CC counts,
- serial capture marker counts (`RECA`, `RECS`) when enabled,
- track transition counts for `ARMED->RECORDING`, `RECORDING->STOPPED_RECORDING`, `STOPPED_RECORDING->PLAYING`, `PLAYING->OVERDUBBING`, `OVERDUBBING->PLAYING`,
- serial first-note offsets (`record_first_note_offset`, `overdub_first_note_offset`) and timing issues when out of threshold,
- `overall_ok` pass/fail summary.

## Next extension points

- Add scenario mode for slot switching while playback is running.
- Add scenario mode for new loop creation flows.
- Keep dense input generator shared so cross-scenario comparisons stay stable.
