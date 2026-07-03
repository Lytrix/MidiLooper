# DROID motorfader pitchbend — scale, NOTE_EDIT arm, HITL probe

Non-obvious behavior for motorized faders on the DROID patch (`droid/midilooper_v1.ini`) and the host
HITL probe (`scripts/hitl/fader_motor_probe.py`). Firmware constants live in `include/MidiConfig.h`
(`MidiConfig::Pitchbend`).

---

## Logical vs wire pitchbend

| Endpoint | Signed (logical) | Unsigned (14-bit wire) |
|----------|------------------|-------------------------|
| 0 % | −8192 | 0 |
| 50 % | 0 | 8192 |
| 100 % | +8192 | 16383 (+8191 on Teensy `sendPitchBend`) |

- **Logical range** is **−8192 … +8192** (both endpoints valid). `MidiFaderManager::isValidPitchbend`
  and `MidiConfig::Pitchbend::isValidLogical` accept +8192.
- **Wire send** maps +8192 → +8191 (`logicalToWireSigned`) because the Teensy MIDI library accepts
  −8192 … +8191 only; unsigned **16383** is the same physical deflection.
- **Do not** cap motor travel at unsigned **12288** (signed +4096). That is **75 %** of full span
  (\(12288 / 16384 = 0.75\)) and was the source of “100 % sends but motor stops at ~75 %” reports.

Inbound USB host pitchbend uses `logicalToUnsigned` / `unsignedToLogical` in `MidiHandler` so +8192
encodes as 16383 and does not overflow `pitch + 8192`.

---

## DROID patch mapping (NOTE_EDIT)

From `droid/midilooper_v1.ini` (fader 1 / note select):

```ini
startvalue = _SET_FADER1_POSITION * 32 + 0.5
[midiout]
    pitchbend = _SEND_FADER1_POSITION * 2 - 1
[midiin]
    notegate1 = _TRIGGER_FADER1_UPDATE
    pitchbend = _SET_FADER1_POSITION
    program = _EDIT_NOTE_STATE
```

- Internal fader position is **0.0 … 1.0**.
- Outbound MIDI pitchbend is bipolar: `position × 2 − 1` (−1 … +1).
- Inbound pitchbend updates `_SET_FADER1_POSITION`; **notegate1** (note 0) triggers the motor to move
  to that stored value.

Fader 2 (coarse / ch14) uses the same `× 2 − 1` pattern on its motor channel.

---

## NOTE_EDIT arm — extra move to ~50 %

Entering NOTE_EDIT (PC **1** on ch16, same as `EditManager::sendEditSessionChange`) switches DROID
motorfaders with `selectat = 1`. On that switch, `startvalue = _SET_FADER1_POSITION * 32 + 0.5` runs.

Until a pitchbend has been received in NOTE_EDIT mode, `_SET_FADER1_POSITION` is typically **0.5**
(center) → the motor can snap to **~50 %** once. This is **not** part of a 0 → 25 → 50 → 75 → 100 %
sweep; it is DROID mode-entry behavior.

**Note 100** on ch15 is the **LOOP_EDIT** LED trigger only (`triggerNote = 100` in
`sendEditSessionChange` for `EditSessionType::Loop`). NOTE_EDIT uses **note 0** on ch15.

---

## Motor trigger timing (probe and firmware)

DROID applies position on **notegate** (note 0). Order matters:

| Order | Name | Effect |
|-------|------|--------|
| Note on → pitchbend → note off | `note_pitch_off` | Motor moves to **stored** position on note on (often 50 %), then pitchbend updates value — visible extra stop before target. |
| Pitchbend → note on → note off | `pitch_note_off` | **Preferred.** Value is set before the motor trigger. |

The HITL probe defaults to **`pitch_note_off`**.

Firmware `SELECT_SYNC` and outbound pipeline use `NoteEditFaderMotorTiming` (three position sends at 12-tick gaps, notegate on with the third, 24-tick note length) from `test/test_faders/fader{2,3,4}_*.mid`. Verified by `scripts/test_note_edit_fader_motor_timing.py` and native `test_note_edit_fader_feedback`.

**Capture analysis:** ch13 motor acks on USB often appear ~**11 ms** after the corresponding notegate MO in host recordings (`kCh13AckCorrelationWindowMs`). Use that window in HITL verifiers only.

**Live note select (`SELECT_SYNC`):** display and `EditorSelection` apply **immediately** on each F1 crossing. F2/F3/F4 motors are **debounced**: pending sync coalesces to the latest `NoteId` / bracket and flushes after **300 ms** F1 idle (`kSelectFaderMotorIdleMs`; temporary test value). One **parallel interleaved** timed burst (~250 ms total) sends all dependent faders per round (12-tick gaps, 24-tick notegate) via `NoteEditFaderMotorTiming::runParallelMotorFaderBursts`. SessionOpen outbound uses the same parallel helper (one blocking pass, not three sequential bursts). No program change on fader updates — NOTE_EDIT PC + `selectat=1` runs once in `EditManager::sendEditSessionChange` when entering note edit.

**Geometry edit (F2/F3/F4 → F1):** note move/length/pitch updates `EditorSelection` immediately (`syncSelectionFromGeometryEdit` uses **moving-note `NoteId`** from focus when active). F1 bracket motor uses a **separate** debounced queue (`pendingGeometryDriverMotorSync_`): coalesce on bracket tick changes, flush after **300 ms** geometry-fader idle (`lastMotorSyncDriverInputMs_`). Flush sends F1 via `sendFader1MotorTimedBurst` (three ch16 position sends + notegate — same timing pattern as F2). Select-driver and geometry-driver pendings **do not merge**; each driver input cancels the opposite pending queue.

**Geometry F1 inbound (geometry edit kinds):** Geometry-driven F1 bracket motor is **outbound-only** (`sendFader1MotorTimedBurst` after geometry-fader idle). Motor echo within `FEEDBACK_IGNORE_PERIOD` (1500 ms) after that send is ignored via `selectFaderFeedbackIgnoreUntilMs_` (and value-echo when within threshold). **User-driven** F1 select during Move/Length/Pitch commits pending geometry and runs `applyNoteSelectFromFader1Pitchbend` → `applySelectNav` (returns to **Select** kind). `applySelectionFromGeometryEdit` calls `syncGeometrySelectionToUi` (bracket + display refresh only) so geometry **outbound** does not recompute `selectedNoteIdx` or exit `EditStartNoteState`.

After NOTE_EDIT arm, the probe sends a **prime** pitchbend (no note) to the first sweep target so the
first notegate does not use the 0.5 center position.

---

## HITL fader motor probe

| Item | Value |
|------|--------|
| Scenarios | `fader_motor_probe`, `fader_motor_sweep` (quarter steps 0, 25, 50, 75, 100) |
| Firmware env | `teensy41-capture-serial-fader-probe` (`MIDI_USB_FADER_PROBE_PASSTHROUGH`) — mirrors host USB ch14/ch16 (+ ch15 note 0 arm) to DROID USB host |
| Script | `scripts/hitl/fader_motor_probe.py` |
| Registry | `scripts/hitl/registry.py` |

```bash
# Quarter sweep (default timing: pitch_note_off)
.venv/bin/python scripts/host_midi_hitl.py run --preset fader_motor_sweep \
  --midi-out "Teensy" --midi-in "Teensy" \
  --settle-ms 2500

# Both faders — same pitchbend + notegate on ch16 and ch14 each step
.venv/bin/python scripts/host_midi_hitl.py run --preset fader_motor_sweep \
  --midi-out "Teensy" --midi-in "Teensy" --fader both \
  --settle-ms 2500

# With serial capture (separate terminal)
.venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodem154944801
```

Build/upload probe firmware:

```bash
pio run -e teensy41-capture-serial-fader-probe
teensy_loader_cli --mcu=TEENSY41 -w -v \
  ~/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial-fader-probe/firmware.hex
```

Normal NOTE_EDIT feedback (no host passthrough) uses `teensy41-capture-serial` and
`NoteEditManager` fader outbound — same `MidiConfig::Pitchbend` scale.

---

## DROID USB host pacing (LED + motor)

Teensy → DROID uses USB host. Bursts (bar LED refresh + fader motor on ch14/ch16) can drop packets on
DROID. `MidiHandler` applies:

| Constant | Default | Role |
|----------|---------|------|
| `DroidUsbHost::MIN_PACKET_GAP_MICROS` | 1000 µs | Min gap between any USB-host packet |
| `DroidUsbHost::LED_DRAIN_MAX_PER_LOOP` | 4 | LED packets sent per main-loop drain |
| `DroidUsbHost::LED_PENDING_MAX` | 64 | LED queue; coalesces duplicate notes |

- **ch15 LED** (`sendLedFeedback*`): queued, coalesced per note, drained from `main` loop.
- **ch14/ch16 motor** and **ch15 CC**: sent immediately with the same min gap.
- USB device / DIN serial paths are unchanged (no queue).

At 120 BPM a 16th is ~125 ms; draining 4 LEDs every ~8 ms clears a 24-LED bar refresh in ~50 ms.

---

## Symptom → cause (quick)

| What you see | Likely cause |
|--------------|--------------|
| Motor goes to ~50 % before sweep step A | NOTE_EDIT arm `startvalue` at stored 0.5; and/or `note_pitch_off` triggering before pitchbend |
| Sweep order looks like 50 %, 0 %, 25 % … | Arm 50 % + wrong timing on step A (fixed by prime + `pitch_note_off`) |
| Step E (100 %) sent but no extra travel | Value on wire is 16383; if motor already at DROID’s effective max from 75 % step, no visible move — check DROID motorfader scaling vs full 14-bit span |
| `isValidPitchbend` dropped full travel | Old cap at +8191 rejected +8192; fixed in `MidiConfig::Pitchbend` |
| One fader stops after heavy LED / edit traffic | DROID USB host overload; firmware queues ch15 LED and paces all host packets (`MidiConfig::DroidUsbHost`) |

---

## Related

- [`HITL_TEST_SCENARIOS.md`](HITL_TEST_SCENARIOS.md) — scenario catalog
- [`MIDI_CONFIG_GUIDE.md`](MIDI_CONFIG_GUIDE.md) — channel/note remap
- [`FADER_STATE_SYSTEM.md`](FADER_STATE_SYSTEM.md) — Teensy fader feedback / deadband
- `openspec/changes/note-edit-fader-feedback-regression/` — fader2 feedback regression work
