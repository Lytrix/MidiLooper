# The host-grid record phase: the script dictates tick 0

**Status: implemented, host-verified and reviewed; the device leg is owed.** Host tooling only — no
firmware line, no build, no upload, no SD action, no device run. Nothing here declares a limit, an
envelope or a qualification, and **S7.5a / S7.5b / S7.5 stay unchecked**.

This executes the bounded next step recorded in
[TEMPO-FADER-AND-300BPM-LEG.md](TEMPO-FADER-AND-300BPM-LEG.md) §14/§17 ("anchor the base record
stream's start on the clock grid … the in-stream stop press should land on the 2-bar boundary"), and
answers the owner's report that *"the MIDI notes sent are always one bar too late, making the loop
length 3 bars instead of 2"*.

## 1. The two defects, measured from the captures this packet already holds

| Capture | Clock | Requested | Committed | `raw_length` | Record first note |
|---|---|---|---|---|---|
| `envdist-t8s4-01` | device | 2 bars (1536) | **1536** | 1906 | 16 clocks (16 latency) |
| `base-300bpm-preseam-probe-t8s4-07` | device | 2 bars (1536) | **2304** | 2479 | **10 clocks** (no advisory) |
| `base-300bpm-bootreset-t8s4-05` | device | 2 bars (1536) | **2304** | 2683 | **37 clocks** |
| `base-300bpm-hostclock-after-intent-t8s4-03` | host | 2 bars (1536) | **2304** | 2312 | **33 clocks** |

Both symptoms are **host-side pacing**, and both come from not owning tick 0.

**Defect A — the length.** A record stop is applied at the tick the press is *processed*
(`MidiButtonActions::handleToggleRecordForSlot` → `TrackManager::stopRecordingTrack(currentTick)`),
and the committed length is that raw tick quantised to the nearest bar
(`RecordStopLength::quantizeTransportRecordLength`: `raw % TICKS_PER_BAR <= TICKS_PER_BAR / 2` floors,
otherwise it ceilings). So the stop's *effect* must land within **± half a bar** of the requested
boundary — at 300 BPM ± 400 ms. The device does not act on the note-on at all: a MIDI button short
press is resolved when the double-tap window expires (`MidiButtonProcessor::onShortRelease` →
`tapStateExpireTime = release + doubleTapWindow`, 300 ms by default), i.e. `press_ms + 300 ms` after
the note-on — the same `record_button_lead_seconds` the repo's stop helpers already model. The base
preset emitted **no in-stream stop at all** (`_stream_pattern_for_bars` was called without
`stop_press_note`, so `stop_press_trigger_clock` stayed `None`) and the press came afterwards from
wall time: stream end + `--post-stream-settle-ms` + the press lead. At 120 BPM that lateness still
fits inside half a bar (a 1000 ms grace), which is why `envdist-t8s4-01` committed 1536/1536; at
300 BPM the grace is 400 ms and the same lateness lands in the next bucket → 2304.

**Defect B — the first note.** In host mode the phase was anchored on the host's *observation* of the
device's serial `RECORDING` row, not on the realtime `Start` that produced tick 0. The measured
offsets are that observation delay (10–37 clocks, growing with the serial round trip) — exactly the
"pacing term" §14 named and refused to declare as intent.

## 2. The fix: the python script owns the grid

Firmware contract this rides on (cited, not invented):

* `ClockManager::onMidiStart` sets `sequencerRunning`, `assignCurrentTickSilently(0)` and
  `firstPulseAfterStart = true`; `onMidiClockPulse` **skips the first advance** after Start, so the
  first pulse after `Start` holds tick 0 and pulse `origin + n` is tick `n * TICKS_PER_CLOCK`
  (`Config::TICKS_PER_CLOCK` = 8, one 24-PPQN pulse). While the device is slaved it advances no other
  way (`updateInternalClock` returns early for `CLOCK_EXTERNAL`), so the pulse count *is* the tick.
* `TrackManager::handlePendingRecordStart` starts an armed, reference-free record at
  `currentTick == 0` or on a bar boundary — so **arm (transport stopped) + realtime `Start` =
  recording from tick 0**, with no serial round trip in the loop.

What landed:

1. **`hitl/midi_clock.py`** — `MidiClockMaster.start()` retires any previous pump, sends the realtime
   `Start`, then records the grid origin (`grid_origin_pulse()`: the next pulse the pump sends, i.e.
   device tick 0) and `tick_for_pulse(pulse)`. Before a `Start` the origin is `None`: there is no grid
   to anchor on and a caller must not invent one.
2. **`hitl/transport_clock.py`** — `stream_pattern_for_bars(..., grid_origin_pulse=…)` makes the
   phase's grid position 1 be that pulse (grid position `c` → device tick
   `(c - 1) * TICKS_PER_CLOCK`), so step 0 lands on tick 0 and step `s` on tick `48 * s` exactly, and
   the device's own `BAR` probe is deliberately **not** an end condition in that mode (it arrives
   through the lagging serial ring and could only truncate the phase). A masterless or pumpless
   anchor is refused by name; an anchor the host has already passed is **not** an exception — the
   phase starts on the next pulse and reports `grid_anchor_missed_pulses`, so nothing can raise out
   of a phase whose take is already armed. The timing evidence also carries `grid_origin_pulse`,
   `grid_first_note_pulse` and `grid_first_note_tick`, so "the script dictated tick 0" is a
   **reading** (`grid_first_note_tick == 0`), and the pulse field is host-only so a device-clock run
   cannot be misread as an anchored host run.
3. **`hitl/serial_tick_anchor.py`** — `record_stop_press_advance_clocks(bpm, press_ms)` converts the
   button lead (`press_ms + 300 ms`) into the clocks before the boundary at which the in-stream stop
   press must be emitted, minus the grid-position/pulse off-by-one: 20 clocks (160 ticks) at 120 BPM,
   50 (400 ticks) at 300. The press tick is then `boundary - lead`, so the effect lands on the
   boundary (at worst one clock before it) — the centre of the requested bucket, ± 384 ticks of slack.
4. **`hitl/legacy_record_baseline.py`** — in host mode the base record branch anchors the phase on the
   Start grid (and refuses by name when that anchor has already gone), takes its stop lead from the
   helper above (`requested <= 0` → the button latency; a *smaller* explicit
   `--stop-press-advance-clocks` is raised to it with a printed warning, because obeying a lead inside
   the button's own latency re-creates the one-bar-long take; a lead that cannot fit the phase is
   refused), passes the in-stream stop press on that grid, and **skips the serial `RECORDING`
   confirmation** — the phase must start at the Start, not after a serial round trip — asserting that
   confirmation *after* the phase from the rows the device emitted during it instead.
   `_restart_transport_after_clear`'s callers no longer apply the modelled button debounce to a
   realtime `Start`, and one `_record_phase_sent_its_own_stop` helper (reading each stream shape's own
   statement) replaces the inline composition, so the caller cannot press a second time on a track
   that is already `PLAYING` again.

**An honest note on what the anchor does and does not buy.** Whenever the guard passes, the anchored
phase origin equals the pulse the unanchored path would have used — so the *schedule arithmetic* is
the same in that state, and the first-note win comes from removing the serial `RECORDING` wait. What
the anchor adds is that grid position 1 is **provably** the tick-0 pulse (reported as
`grid_first_note_tick == 0`), that a host stall longer than one pump period becomes a named refusal
instead of a silent shift, and that the stop press is scheduled against the same grid as the notes.
The 300 BPM host leg's 33-clock first note was the `RECORDING` wait, not the origin convention.

## 3. Verification

* **New suite `scripts/test_hitl_host_grid_record.py` — 19/19.** The Start's grid origin and tick
  mapping against the *real* `MidiClockMaster`; a second `Start` rebasing the grid; the arm's anchor
  being the Start instant and not `press_ms + 300 ms` (both the serial-capture and no-serial sites,
  through `_run_record_arm_until_capturing`); a grid-anchored phase emitting step 0 on the tick-0
  pulse and step `s` on tick `48 * s`; the device `BAR` probe not consulted in that mode (**and**, as
  the control, still truncating an unanchored phase); a masterless/pumpless anchor refused by name and
  a passed anchor degrading with `grid_anchor_missed_pulses` (no exception); the press lead as the
  button model in clocks; a caller lead inside that latency raised, one that cannot fit refused; the
  stop effect inside the ± half-bar bucket at 20/60/120/200/300 BPM; the emitted press on the grid
  pulse the schedule names, exactly once; and the composition reading every stream shape. A
  **source-scan tripwire** pins the call sites themselves (which no unit can drive without the bench),
  in the spirit of this repo's compatibility-inventory gates.
* **Six mutations red-proved, each restored byte-identically** (sha256 re-checked after every one):
  the serial-capture anchor expression reverted to the modelled `press_ms + 300 ms` lead; the
  no-serial anchor expression reverted; the composition dropping the base stream's evidence; the
  stop lead obeying a caller value below the button latency; `grid_anchor_missed_pulses` forced to 0;
  and the first-note pulse reported as the anchor instead of the pulse actually used. Each killed
  exactly the test that pins it.
* **Full host sweep: 1,426 tests, 6 failures + 1 error.** That is the pristine `HEAD` set (1,407
  tests, the same 6F+1E) plus exactly the 19 new tests. The A/B was run directly: the six failing
  suites (`test_6_3_machinery_closeout_gate`, `test_compatibility_consumer_inventory`,
  `test_edit_session_state_serial_verify`, `test_foundation_runner`, `test_layered_verifiers`,
  `test_occupy_paint_serial_verify`) were executed in a pristine `git worktree` at `3caa2bfb` and
  reproduce the same counts; their causes are pre-existing (`Namespace` objects without
  `overdub_step_clocks` in the verifier path, and source-inventory scans). No new failure.
* **One adversarial read-only review pass** (independent, over the diff plus the firmware touchpoints)
  raised one blocker, three real defects and several nits; all are repaired above or recorded here:
  the past-anchor guard that would have raised out of an armed take (now a degradation plus a
  caller-side refusal), an explicit `--stop-press-advance-clocks` below the button latency
  re-creating the one-bar take (now raised/refused), the dropped `RECORDING` confirmation with no
  replacement (now asserted after the phase), three non-discriminating tests plus an untested wiring
  (retargeted/extracted, with the mutations above), the stray-pulse race at `Start` (the pump is now
  retired *before* the `Start`), the dead `pulse_for_tick` inverse (deleted), the mixed meaning of
  `grid_first_note_pulse` (host-only now), and a pre-existing duplicated device-bar key triple in the
  dict the anchor keys were added to (removed). It also confirmed the tick/pulse mapping, the `- 1`
  convention and the stop-latency model against the firmware, and that the non-host paths are
  unchanged.

## 4. Owed, named rather than claimed

1. **The device leg.** Nothing here has run against the board: the anchor, the in-stream press and the
   committed length are host-pinned only. The leg that proves them is the canonical entry point —
   `host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 --clock-source host
   --tempo-bpm 300 …` — and it **overwrites the content of its target slot**, so it needs the same
   per-session authorization the earlier legs used. Its readings to check:
   `record_loop_length.actual_final_length == 1536`, `record_grid_first_note_tick == 0`, and no
   `record_first_note_too_late`.
2. **The press lead is a modelled constant.** `press_ms + 300 ms` is the repo's own model of the
   MIDI-button path; the schedule tolerates a large error (the effect may land anywhere in ± 384 ticks
   — ± 0.4 s at 300 BPM), but the *measured* lead on this bench is not in this packet because no
   device leg was run. A leg whose committed length still mis-buckets at the top tempo is the evidence
   that would turn the model into a measurement.
3. **The phase's own end condition is the host grid, not the device's bar counter.** That is
   deliberate here (the host knows the tick while slaved), but it means a host that loses slaving
   mid-phase would keep streaming its own grid; the device's `BAR` rows stay in the capture as the
   cross-check the leg should read.
4. **The overdub phases keep their old stop shape.** Their stop cannot change the loop length, so they
   are outside this defect's blast radius, but the same "press after the stream" lateness is still
   there for the *stored span* checks. Not touched.
5. **The device image confound named in §16 is unchanged.** Any before/after on this bench must pin
   the image as well as the tempo; the board's image is newer than the 2026-10-10 300 BPM legs'.
