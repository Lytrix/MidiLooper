# HITL modular scenarios — navigation

Composable hardware-in-the-loop automation lives under [`scripts/hitl/`](../scripts/hitl/) with entry point
[`scripts/host_midi_hitl.py`](../scripts/host_midi_hitl.py).

## Presets (backward compatible)

| Preset | Replaces |
|--------|----------|
| `base` | `host_midi_automation_baseline.py` |
| `edit_full` | `host_midi_automation_edit_baseline.py` |
| `edit_minimal` | Fast edit smoke (prelude + add/delete/move/length + exit) |
| `edit_overdub_during_note_edit` | Record → overdub → edit → in-edit overdub → session undo → exit → global undo |

Legacy scripts delegate to the same presets via `hitl run --preset …`.

## Canonical commands

Record/overdub baseline:

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

Edit overlap suite:

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_full \
  --midi-out "Teensy" --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --record-bars 2 --start-transport
```

Edit + overdub during note edit (firmware session undo for in-edit overdub):

```bash
.venv/bin/python scripts/host_midi_hitl.py run \
  --scenarios edit_overdub_during_note_edit \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --record-bars 2 --overdub-bars 2 \
  --start-transport --phase-wait-ms 500 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

Verify-only replay:

```bash
.venv/bin/python scripts/host_midi_hitl.py run \
  --scenarios edit_overdub_during_note_edit \
  --verify-serial-log captures/session.log --verify-only
```

## Scenario matrix

| Scenario ID | Phases | Verifiers |
|-------------|--------|-----------|
| `base` | clear → record → overdub(s) → optional global undo/redo | baseline transitions + reconciliation |
| `edit_record_prelude` | 2-bar fixture record | record transitions |
| `edit_minimal` | prelude → enter edit → add/delete/move/length → exit | session_state_enter subset |
| `edit_full` | prelude → full overlap suite | all edit `_verify_*` gates (via legacy script) |
| `edit_overdub_during_note_edit` | prelude → overdub → edit → in-edit overdub → E: undo → second edit → exit → global undo ×3 | `_verify_edit_overdub_during_note_edit` |

### `edit_overdub_during_note_edit` pitch bands

Two overdub passes use **distinct ranges** (same as baseline first/second overdub) so layers are visually separable on the piano roll:

| Pass | MIDI range | Notes |
|------|------------|-------|
| Pre-edit overdub | 24–39 | C1–D#2 (baseline first overdub band) |
| In-edit overdub | 12–35 | C0–B1 (baseline second overdub band) |

**E: session undo/redo** after in-edit overdub targets the **in-edit overdub pass only** (C0–B1 layer). The pre-edit overdub layer (C1–D#2) stays visible through undo/redo.

Reference capture template: [`captures/host_midi_hitl_edit_overdub_during_note_edit_reference.json`](../captures/host_midi_hitl_edit_overdub_during_note_edit_reference.json).

## Firmware dependency

In-edit overdub removal uses **session undo** (`SessionUndoEntry.overdubPassIdAtPush`). Serial markers:
`EditSession overdub pass undone` / `EditSession overdub pass redone`.
