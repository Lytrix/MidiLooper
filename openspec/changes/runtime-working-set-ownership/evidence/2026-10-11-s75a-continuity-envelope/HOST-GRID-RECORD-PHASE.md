# The host-grid record phase: the script dictates tick 0

**Status: implemented, host-verified, reviewed and **device-verified** (the fix's three readings are
measured on the board, §3b).** Host tooling only — no firmware line, no build, no upload, no SD action.
Nothing here declares a limit, an envelope or a qualification, and **S7.5a / S7.5b / S7.5 stay
unchecked**.

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

## 3b. Device leg — the fix is measured on the board

Owner-authorized run on the canonical entry point, target track 8 / slot 4, **no upload and no
flash** (the image on the board is the one the earlier legs ran):

```
host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 \
    --boot-reset --capture-boot-wait 30 --clock-source host --tempo-bpm 300 \
    --out-dir captures/host-grid-record-t8s4-01
```

**Preflight (`captures/host-grid-record-preflight-01`, read-only).** A non-reset attach
(`hitl_boot_census.py --no-reset --settle-s 6 --heap-stats`) shows the device healthy and quiescent:
transport stopped, `deferred_prewarm_idle=1`, admission/interactive/undo ready, `!HEAP_STATS`
**granted** (the previous session's `track_work` refusal is gone), `free_chunks=406`, no reconcile
warnings, `DISP,3,STOPPED` (LOOP_EDIT, stopped — no leftover `ARMED`). **A stale latch was found and
discharged in the documented order:** `captures/.device-shutdown-incomplete.json` held
`incomplete_disconnected: transport stop press failed: RuntimeError("No MIDI output port matching
'Teensy'")` with `drains: 0` — a **host-side** failure from an earlier *test* sweep in which no MIDI
port existed, so no device save was ever in flight. The device was verified first (above), the payload
preserved as `captures/host-grid-record-preflight-01/latch-before.json`, and only then was
`hitl.device_shutdown.clear_latch()` called.

**The three readings this leg exists for, all three from the device's own rows:**

| Reading | Before this change (same preset/tempo/clock) | This leg |
|---|---|---|
| committed length | `2304/1536`, `raw 2312` | **`1536/1536`, `raw 1536`**, `bar_aligned 1536` |
| record first note | **33 clocks** (bound 12, `record_first_note_too_late`) | **1 clock (4.613 ms)**, `timing_warnings []` |
| grid anchor evidence | (no such field) | `origin_pulse=76`, `first_note_pulse=76`, **`first_note_tick=0`** |

* The device's own stop row: `RECS,stop,3,1536,0,1536,1536,0` with `Recording stopped @ tick 1536
  (recStart=0 length=1536)` — the press's *effect* landed exactly on the 2-bar boundary (`raw ==
  1536`, not one tick past), which is the bucket centre the schedule aims at.
* `stored_record_grid`: 32 note-ons, `first_tick 0`, `last_tick 1488`, `min_delta = max_delta = 48`,
  `bad_delta_count 0/2` — the recorded content sits exactly on the 1/16 grid from tick 0.
* `record_phase`: 32 on / 32 off, 0 unmatched, 0 out of range, 0 sequence mismatch; `record_note_span`
  `raw_span_ok=true`; `stored_overdub_span` and `stored_second_overdub_span` `bars 2/2 span_ok=true`;
  MIDI-boundary timing `overflow_free=true call_sites=true`; display `overdub_ok=true
  transport_ok=true`.
* **Shutdown `complete / drains 2 / ok true`, and no latch was written** — the device is left drained.

**The leg also measures the press lead, which was the modelled term.** The schedule emitted the press at
grid position `192 - 50 = 142`, i.e. device tick `141 * 8 = 1128`, and the device's own row says the stop
was applied at tick **1536** — so the bench's lead was **408 ticks = 425 ms** against the modelled
`press_ms + 300 ms` = **420 ms** (403 ticks, +5 ticks / +5 ms of USB and processing). That is one run's
observation, not a calibration: it turns "the constant is the repo's model" into "the model was accurate to
about a percent on this run", and it is also why the effect landed *on* the boundary rather than 5 ms past
it (`raw == 1536` exactly).

**Named residuals, unchanged by this work and not part of its claim.** The preset's own verdict is
still `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` with `overall_ok=false`:
`overdub_unmatched_open_notes` (the **overdub** phase, 1 unmatched note — the dense-generator class the
skill records as "setup verdict only, unrelated to snapshot content"; the record phase itself is 0/32)
and `persistence_stage_missing:result` (a report-window gap: three `PERS,result,…,ok` rows are in the
full session log). Neither is evidence about this change, and no content or timing qualification is
claimed.

**Named limit of the fix.** It is **host-clock only** (`--clock-source host`): the record stop is
scheduled on the host grid, so a *device-clock* base run at 300 BPM still ends its stream on the device
`BAR` probe and presses afterwards, and would still mis-bucket. The generic mechanism for that shape
already exists (`stop_press_note` + a positive `stop_press_advance_clocks`, which the occupy and
overdub call sites use), so extending it is a bounded follow-up with its own leg rather than part of
this request.

## 3c. The run's **final** transport stop is the host's realtime `Stop`

Two gestures, kept apart on purpose: a **record or overdub stop stays a record-button press**
in-stream on the grid (§2 — that is the edges' mechanism and what the phases' receipts are keyed to),
while the stop that ends the whole run is the host's realtime `Stop` when the host owns the clock.
Nothing is timing-critical after it, and one byte has no double-tap window and no toggle risk; the
phase stops would lose their settled bounds if they were converted, which is why they are not.

Code: `_run_end_transport_stop` (the run-end site, `--stop-after-overdub`) and
`MidiClockMaster.send_realtime_stop` (the `Stop` byte **without** retiring the pump — a transport
stop is not a clock-source change, so the drain below is measured under exactly the conditions the
press-based end stop produced; `stop()` stays the teardown shape, and pump retirement stays where it
already was — the pre-arm `master.stop()` inside `_ensure_transport_stopped_before_record_arm` and the
loop teardown). The **last leg** owns the gesture: a multi-track run hands the master only for the
leg that ends the run (`is_final_leg`), so an earlier leg's stop remains the ordinary mid-run
transport transition it always was. The run writes one other literal, `"transport_press_after_replay"`,
from the temporary `--post-save-play-wait-ms` replay branch, which restarts the transport *after* this
stop; the field is then not left claiming the realtime `Stop` was the run's last gesture.

Device evidence, same preset/tempo/clock, target track 8 / slot 4, no upload and no flash —
`captures/host-grid-record-t8s4-03` after `…-t8s4-01` (the run this packet's §3b measured). Both legs
are single-track runs (`--track` sets `track_count = 1`), so the last-leg gate below leaves them
exactly as measured:

| Reading (report-level, attributed by caller) | `…-t8s4-01` (press) | `…-t8s4-03` (this change) |
|---|---|---|
| `assertions.run_end_transport_stop` | (no such field) | **`host_realtime_stop`** |
| `assertions.transport_press_count` | **2** | **1** |
| transport presses, with `caller` | `_prepare_deferred_save_before_clear`, **`run`** | **`_prepare_deferred_save_before_clear` only** |

So the run no longer ends with a transport *toggle*: the last transport gesture on the wire is the
realtime `Stop`, and the device's own last transitions agree — `ST,Track,PLAYING,STOPPED` closes the
session (`#CAP,59148370`) and the post-stop display frame is `DISP,3,STOPPED,1536,58,58,58,58` with
`transport_ok=true`. Shutdown is `complete/2/ok` ("2 stop+drain cycle(s) reached verified persistence
idle", `captures/host-grid-record-t8s4-03/shutdown-result.json`), no latch.

**The record phase is unchanged by this leg, which is the point of running it:**
`Recording stopped @ tick 1536 (recStart=0 length=1536)`, `RECS,stop,3,1536,0,1536,1536,0`,
`actual_final_length 1536/1536` (`raw 1536`), `record_grid_first_note_tick == 0`
(`origin_pulse = first_note_pulse = 76`), `record_stop_stages first_missing=None`,
`persistence handoff first_missing=None result=ok`, `stored_record_grid` 32 ons `bad 0/2`.
The preset's verdict stays inconclusive for its own residuals — `…-t8s4-03` reports
`INCONCLUSIVE_CAPTURE (EVIDENCE_MISSING)` with the single gap
`persistence_active_snapshot_unavailable_during_save` (a report-window gap; the `PERS,result,…` rows
are in the session log), a different residual from §3b's `SETUP_UNCONFIRMED` pair. Neither is a
content or timing qualification of this change.

**One observed host-side flake, named rather than smoothed over.** The first repeat of this leg
(`captures/host-grid-record-t8s4-02`, identical invocation) aborted *before* the changed code path —
`overdub start refused: no predecessor stop-completion receipt`, `abort_reason` set,
`run_end_transport_stop == ''` — with `RING,shed,2` inside the record-stop window and the record pass
publishing no `Record pass undo reset for slot 3` receipt (leg 01 and leg 03 both log it ~780 ms /
~1.4 s after `Recording stopped`). Its record stop also landed one clock early (`raw 1528`, still the
2-bar bucket, committed 1536). The identical invocation then completed in `…-t8s4-03`. The flake is
an evidence/admission one at a gate upstream of this change, not a reading about the end stop.

**Still a transport press in the run, and it is not a phase stop.** `…-t8s4-03`'s single remaining
press is `_prepare_deferred_save_before_clear` — the fixture's *setup* gesture that stops a transport
already running before the clear long press (guarded by `serial_global_transport_running`, present in
§3b's accepted leg as well). This change deliberately leaves it alone: it is neither a record/overdub
stop nor the run's end, and converting it would move the clear precondition's own path. Named here so
the next reader can decide it on its own evidence.

**One focused adversarial read-only review (APPROVE), and what it changed.** It confirmed that the
replaced site *is* the run's only end stop, that leaving the pump running is correct (the drain is
device-side and keys on the device's own `DIAG,stored_verify,drain_complete` rows; a stopped device
ignores pulses, so its `CLOCK_INTERNAL` timeout and the `DISP,…STOPPED` frame the report reads behave
as before), that the gesture local cannot be unbound before the report is built, and that the phase
path cannot emit a realtime stop (one `send_realtime_stop()` call site; the only production
`master.stop()` is the pre-arm stop). It raised two latent defects — with `--track-count N > 1` every
leg would have ended the run with a realtime `Stop`, and the `--post-save-play-wait-ms` replay branch
would have left the field naming a gesture that was no longer the run's last — plus three nits
(non-discriminating tripwire assertions, an inaccurate assertion message, and a comment that
described a now-unreachable case). All are repaired, and the repairs are re-proved: **six mutations**,
each killing exactly its own test, restored byte-identically.

**Residual named, not fixed (device-side logging).** The realtime path logs nothing on the device,
where the press path logged `Transport stopped` in `ClockManager`'s stop handler, so two host readers
cannot see a realtime `Stop` inside a measured window: `serial_global_transport_running` and the
accepted-sequence probe's `continuous_transport` check. Harmless *here* (host mode short-circuits
`clock_present`, record pacing is forced to the host clock, and the drain keys on the device's own
rows), and the positive side is that the gesture is now its own reported field rather than something
inferred from `control_actions`. Closing it needs a firmware marker, i.e. outside this change. The
field also reads `""` when the run never reached the end stop (`--record-only` continues before the
site, `--no-stop-after-overdub`, or an abort) — honest, but the report cannot tell those apart.

## 4. Owed, named rather than claimed

1. **~~The device leg~~ — RUN, and all three readings met** (§3b): `actual_final_length 1536/1536`
   (`raw 1536`), `record_grid_first_note_tick == 0`, first note 1 clock, `timing_warnings []`,
   `RECS,stop,3,1536,0,1536,1536,0`, shutdown `complete/2/ok` with no latch. The preset's own verdict
   stays `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` for its two pre-existing residuals (an overdub
   unmatched note and a report-window gap), so this is a fix verification rather than a baseline pass.
1b. **The device-clock shape is still unfixed** (named in §3b): a 300 BPM base run on
   `--clock-source device` keeps the old stop edge (no in-stream press on the host grid). The generic
   `stop_press_note` + `stop_press_advance_clocks` mechanism already exists for it; extending the base
   device-clock branch to use it is a bounded follow-up with its own leg.
2. **The press lead: modelled, and now measured once.** `press_ms + 300 ms` is the repo's own model of
   the MIDI-button path and the schedule tolerates a large error (the effect may land anywhere in ± 384
   ticks — ± 0.4 s at 300 BPM); §3b's leg measures the bench at **408 ticks / 425 ms against the modelled
   403 ticks / 420 ms** on that run. One observation is not a calibration, and a leg whose committed
   length mis-buckets at the top tempo would still be the evidence that qualifies the constant.
3. **The phase's own end condition is the host grid, not the device's bar counter.** That is
   deliberate here (the host knows the tick while slaved), but it means a host that loses slaving
   mid-phase would keep streaming its own grid; the device's `BAR` rows stay in the capture as the
   cross-check the leg should read.
4. **The overdub phases keep their stop on the record button — deliberately, not by omission.** Their
   stop cannot change the loop length, and a phase stop is the gesture the device's control surface
   owns; the run's *final* stop is the one carried by the host's realtime `Stop` (§3c). The same
   "press after the stream" lateness is still there for the *stored span* checks, unchanged.
5. **The device image confound named in §16 is unchanged.** Any before/after on this bench must pin
   the image as well as the tempo; the board's image is newer than the 2026-10-10 300 BPM legs'.

6. **One non-phase transport press stays in the run** (§3c): `_prepare_deferred_save_before_clear`'s
   stop-before-clear *setup* gesture, which §3b's accepted leg also carried. It is neither a
   record/overdub stop nor the run's end; converting it means moving the clear precondition's own
   path, so it is named rather than folded into this change.
