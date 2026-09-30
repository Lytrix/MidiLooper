# HITL MIDI clock over USB (read and drive)

The host needs a musical timebase to place notes, stop presses and bar counts. This guide is
the contract for the one the board actually provides: its **USB device-port MIDI clock**. The
module is [`scripts/hitl/midi_clock.py`](../../scripts/hitl/midi_clock.py); its host tests are
[`scripts/test_midi_clock.py`](../../scripts/test_midi_clock.py); the device check is
[`scripts/hitl_midi_clock_probe.py`](../../scripts/hitl_midi_clock_probe.py).

> **Corrected premise (2026-09-30).** Several HITL comments, the baseline guide and a test
> docstring stated "this board emits no USB MIDI clock out". That is **false**. Measured on
> device: the clock is present and clean, and the host can also *drive* it. What was true is
> narrower: the clock exists only while the device's own transport runs, so a readiness probe
> that waits for clock *before* starting the transport can only ever report "no clock" — and
> the serial fallback then latched that verdict for the rest of the run. The claim is corrected
> in place; see [The three failure modes this replaces](#the-three-failure-modes-this-replaces).

## Measured facts

All figures from the connected board, `teensy41-capture-serial` family, 2026-09-30, through
`hitl/midi_clock.py` (`captures/midi-clock-probe/midi_clock_probe.json`):

| Quantity | Measurement |
|---|---|
| Device clock rate | 48.00 Hz = exactly 24 PPQN at 120.00 BPM; 384/384 pulses per 8 s window |
| Interval jitter | 0.72–1.55 ms stdev; occasional short dropouts on a busy run (busy playback plus a save) |
| Device transport messages | realtime `Start` with the downbeat clock, ~0.31 s after a transport press; `Stop` on the toggle off |
| Host-driven clock, device reaction | device **slaves**: it emits no clock of its own (0 echo pulses) while driven |
| Host pacing quality | 2.8 ms mean / 9.2–19.0 ms max per-pulse error, **≤3 ms cumulative drift over 4 bars** |
| Device clock resume after release | **0.53 s** (the 500 ms `ClockManager::midiClockTimeout`) |
| Tempo effect of driving | the device adopts the host rate through `ClockManager::setBpmFloat`; measured residual **±1–3 BPM** because the device averages its own 24-pulse window |
| Bar-boundary error on the device grid | constant offset within ±1 ms, drift ≤7 ms over 6 bars |

## Read: the device is clock master (default)

This needs no device state change. Press the transport, then pace off the device's own grid.

```python
from hitl.midi_clock import MIDI_CLOCKS_PER_BAR, MidiClockReader

reader = MidiClockReader(in_port)
send_short_press(out_port, GLOBAL_TRANSPORT_NOTE)   # the clock only exists while running
if not reader.wait_pulse(12, timeout_s=2.0):        # verify the press landed — it is a toggle
    raise RuntimeError("no device clock after the transport press")
print(reader.bpm())                                 # the device's real tempo, learned not guessed

reader.wait_bars(8, timeout_s=30.0)                 # 8 bars on the device's grid, exactly
```

* One bar is `MIDI_CLOCKS_PER_BAR` pulses; a 16th note is `MIDI_CLOCKS_PER_BEAT / 4` (6).
* `bpm()` is the device's tempo from its own clock — the replacement for `--tempo-bpm`
  guessing. `#CAP,BPM` cannot serve this: `SC_BPM` is called only from
  `ClockManager::onMidiClockPulse`, i.e. only while the device *receives* external clock, and
  `SESSION_CAPTURE_BPM` defaults to 0, so the mandated build drops the family at the sink.
* `index_now()` is **phase-locked**, not a counter: it advances on a monotonic clock and each
  pulse re-anchors it to the nearest grid position. That is what survives a dropout (the phase
  keeps moving with time instead of sitting a quarter beat behind) and a burst after the host
  was descheduled (the extra pulses do not double-count a phase that time already advanced
  past). `observed_pulses()` is the raw count for evidence; `gap_count()` counts late pulses
  that implied lost grid positions.

## Drive: the host is clock master (pinned tempo)

Only when a run needs the *host* to own the tempo.

```python
from hitl.midi_clock import MidiClockMaster

master = MidiClockMaster(out_port)
master.start()                    # realtime Start: the device begins at tick 0
master.bars(8, bpm=120.0)         # absolute-deadline pacing; returns the pacing report
# release by simply stopping: do NOT send a realtime Stop unless the transport must stop too
```

The device slaving to the host is *observable*: while it is slaved it emits no clock of its
own. `bars(..., echo_reader=reader)` reports both totals, and the tail count (after the first
quarter of the drive) is the verdict — a device that is truly slaving still emits for the
brief window before `ClockManager::checkClockSource` applies `CLOCK_EXTERNAL`.

## The three failure modes this replaces

1. **Clock probed before the transport exists.** `ensure_midi_clock`/`_clock_seen_within` wait
   for pulses to decide whether to press. Because `updateInternalClock` emits clock only while
   `sequencerRunning`, that wait expires and the verdict "no clock" is recorded — and
   `use_serial_transport_proxy` then latches, so the clock is never consulted again. Press
   first, then confirm with `wait_pulse`.
2. **The serial wall-clock proxy cannot know the device tempo.** `#CAP,BPM` is never emitted in
   the mandated build (see above), so `resolve_wall_tempo_bpm` falls back to `--tempo-bpm`
   (default 120). A device running at 85.4 BPM then mis-times every bar — the DEC-086 problem.
   `MidiClockReader.bpm()` measures it instead.
3. **Raw pulse counting stalls on a dropout.** One ~0.5 s dropout is a quarter beat; a counter
   never catches up and an 8-bar request lands wrong. The phase-locked index does not stall,
   and `wait_bars` is the primitive that expresses the requirement directly.

## Hazards (each measured)

| Hazard | Detail |
|---|---|
| Bare `Start`/`Continue` without clock | Puts the device in `CLOCK_EXTERNAL`, where it emits no clock: the device clock collapsed to ~0.75 Hz. Always follow a realtime Start with clock, or use the transport press. |
| A transport press is a **toggle** | Press-and-verify (`wait_pulse`), never re-press blind. Two history captures show a confirm-press stopping a transport that had just started. |
| Driving moves the device's live tempo | Residual ±1–3 BPM. When a run needs an exact figure, pin it with the documented tempo control instead: `.venv/bin/python scripts/hitl_tempo_fader_probe.py --set-tempo 120` (ch16 CC102, 1 BPM per step, DEC-086). |
| `Stop` is not a "release" | A realtime `Stop` stops the device's transport. To release a host drive while keeping the transport, just stop sending clock (resume latency 0.53 s). |
| Closing the serial fd early | Keep the port open through the stop/drain protocol (see the HITL content-verification skill's recovery table). |

## Running the device check

```bash
.venv/bin/python scripts/hitl_midi_clock_probe.py --observe-bars 6
.venv/bin/python scripts/hitl_midi_clock_probe.py --observe-bars 6 --drive-bars 4 \
    --serial-port /dev/cu.usbmodemXXXX
```

It writes `captures/midi-clock-probe/midi_clock_probe.json` and prints PASS/FAIL. It reads the
device's clock, times bar boundaries on the device grid, and (with `--drive-bars`) verifies the
device slaves to a host-driven clock and returns to its own clock after release. It restores
the transport state it found; it never records, overdubs, clears or writes loop content.

A run is a pass when: the tempo was derived from the clock, all observed bars were reached, the
bar-boundary drift is small, and (when driving) the device's own clock stopped while driven and
resumed afterwards. `#CAP,BPM` rows and `DIAG,clockrate` are reported as *advisory device
verdicts*: the first needs a build with `SESSION_CAPTURE_BPM=1` (only
`teensy41-capture-serial-investigation-diag`), the second is emitted on the telemetry window.

## Not yet wired

The legacy streamers still pace by the wall-clock proxy: `stream_pattern_for_bars` is called
with `wall_clock_tempo_bpm`, so note streaming is not yet pulse-paced, and an earlier attempt to
prefer the clock there (`captures/pacing-fix-01`, an 8-bar request recorded 20 bars) failed
before the device-BAR end condition existed. Converting that path to `wait_pulse`/`wait_bars`
is the next slice; this guide plus the module is what it should be built on, and the reason the
premise correction matters is that the old comment read as evidence that the clock was
unavailable.
