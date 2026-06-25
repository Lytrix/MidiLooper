# HITL test scenarios — overview

Hardware-in-the-loop (HITL) tests drive a connected Teensy over USB MIDI and (when required) USB serial
from the host Mac. Entry point: [`scripts/host_midi_hitl.py`](../../scripts/host_midi_hitl.py).

**Firmware:** use `teensy41-capture-serial` so `#CAP`, `ST`, and track undo/redo lines are available.

**List scenario IDs:**

```bash
.venv/bin/python scripts/host_midi_hitl.py run --list-scenarios
```

**Verify-only** (replay a captured serial log without MIDI):

```bash
.venv/bin/python scripts/host_midi_hitl.py run \
  --scenarios <scenario_id> \
  --verify-serial-log captures/<log>.log --verify-only
```

Replace `/dev/cu.usbmodem154944801` with your port (`pio device list`).

---

## Presets vs scenarios

| Kind | Meaning |
|------|---------|
| **Preset** | Named bundle of one or more scenarios (`--preset base`) |
| **Scenario** | Single composable phase script (`--scenarios two_overdub_undo_redo`) |

Presets are defined in [`scripts/hitl/registry.py`](../../scripts/hitl/registry.py) (`PRESET_SCENARIOS`).

---

## Scenario catalog

| ID | Preset | What it exercises | Serial required | Host verifier |
|----|--------|-------------------|-----------------|---------------|
| `base` | `base` | Clear → record → overdub(s) → optional global undo/redo after last overdub | Recommended | Legacy baseline JSON report |
| `edit_record_prelude` | `edit_full`, `edit_minimal` | 2-bar fixture record for edit suites | Recommended | Record transitions |
| `edit_minimal` | `edit_minimal` | Prelude → enter edit → add/delete/move/length → exit | Recommended | Session enter subset |
| `edit_full` | `edit_full` | Prelude → full note-edit overlap suite | Recommended | Legacy edit baseline gates |
| `edit_overdub_during_note_edit` | `edit_overdub_during_note_edit` | Record → overdub → edit → in-edit overdub → **E:** session undo/redo → exit → global undo | **Yes** | `verify_edit_overdub_during_note_edit` |
| `long_loop_display_window` | `long_loop_display_window` | 24+ bar record → NOTE_EDIT window freeze → play/stop long-press snap → hold-to-track | **Yes** | `verify_long_loop_display_window` |
| `two_overdub_undo_redo` | `two_overdub_undo_redo` | Record → **2** overdub passes → global undo ×3 (display **empty**) → global redo ×3 | **Yes** | `verify_two_overdub_undo_redo` |

### `base` (record/overdub baseline)

Default second overdub pass is on (2 bars, C0–B1). Undo/redo after last overdub stop is on by default.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

Legacy entry: `scripts/host_midi_automation_baseline.py` (delegates to preset `base`).

### `two_overdub_undo_redo` (global undo/redo + EMPTY)

Stack: **record + 2 overdubs**. Global undo ×3 disables both overdubs then the record pass (piano roll shows **no notes**). Global redo ×3 restores all layers (redo branch preservation).

| Overdub | MIDI range | Notes |
|---------|------------|-------|
| 1 | 24–39 | C1–D#2 |
| 2 | 12–35 | C0–B1 |

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset two_overdub_undo_redo \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --start-transport --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

Pass criteria (serial): `Overdub undone` ×3, `Overdub redone` ×3, `PLAYING→OVERDUBBING` ×2, first `#CAP DISP` after undo chain has **frame_notes=0**, after redo chain **frame_notes>0**.

Host unit test: `scripts/test_two_overdub_undo_redo_serial_verify.py`.

### `long_loop_display_window`

Loop must be **> 16 bars** for bounded detailed window.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset long_loop_display_window \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 24 --start-transport \
  --freeze-wait-ms 8000 --hold-track-ms 4000 --long-press-ms 700 \
  --phase-wait-ms 500 --press-ms 120
```

Host unit test: `scripts/test_long_loop_display_serial_verify.py`.

### `edit_full` / `edit_minimal`

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_full \
  --midi-out "Teensy" --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --record-bars 2 --start-transport
```

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_minimal \
  --midi-out "Teensy" --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --record-bars 2 --start-transport
```

Legacy entry: `scripts/host_midi_automation_edit_baseline.py`.

### `edit_overdub_during_note_edit`

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_overdub_during_note_edit \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --record-bars 2 --overdub-bars 2 \
  --start-transport --phase-wait-ms 500 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

**E:** session undo after in-edit overdub targets the in-edit overdub layer only. Pre-edit overdub stays.

---

## Native (host) tests vs HITL

| Suite | Command | Teensy |
|-------|---------|--------|
| Firmware logic | `pio test -e native` | Not required |
| HITL verifier scripts | `.venv/bin/python scripts/test_*_serial_verify.py` | Not required |
| Full HITL run | `host_midi_hitl.py run …` | **Required** |

Run `pio test -e native` before push/merge. Run relevant HITL preset when changing capture, undo, display, or edit flows covered above.

---

## Related docs

- Implementation notes: [`docs/plans/hitl_modular_scenarios_enhancement.md`](../plans/hitl_modular_scenarios_enhancement.md)
- Cursor rule (canonical `base` command): [`.cursor/rules/HITL-Test-Flow.mdc`](../../.cursor/rules/HITL-Test-Flow.mdc)
- Undo/redo storage rules: [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](LOOP_MIDI_STORAGE_AND_VALIDATION.md)
