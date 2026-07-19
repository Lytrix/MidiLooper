# HITL modular scenarios — navigation

> **Canonical scenario list and run commands:** [`docs/Guides/HITL_TEST_SCENARIOS.md`](../Guides/HITL_TEST_SCENARIOS.md)

Composable hardware-in-the-loop automation lives under [`scripts/hitl/`](../../scripts/hitl/) with entry point
[`scripts/host_midi_hitl.py`](../../scripts/host_midi_hitl.py).

## Layout

| Path | Role |
|------|------|
| `scripts/host_midi_hitl.py` | CLI entry (`run`, `--preset`, `--scenarios`, `--verify-only`) |
| `scripts/hitl/registry.py` | Preset → scenario IDs, lazy scenario registry |
| `scripts/hitl/scenarios/*.py` | Run handlers (MIDI phases) |
| `scripts/hitl/verify/*.py` | Serial log verifiers |
| `scripts/test_*_serial_verify.py` | Host unit tests for verifiers (no Teensy) |

Legacy `host_midi_automation_baseline.py` and `host_midi_automation_edit_baseline.py` are thin CLI shims
over `hitl.legacy_record_baseline` / `hitl.legacy_edit_baseline` and still accept presets
`base` and `edit_full` via `host_midi_hitl.py`.

## Adding a scenario

1. Add `scripts/hitl/scenarios/<id>.py` (`run_<id>`) and optional `scripts/hitl/verify/<id>.py`.
2. Register in `scripts/hitl/registry.py` (`PRESET_SCENARIOS` + `_lazy_registry`).
3. Add host verifier test `scripts/test_<id>_serial_verify.py` when serial gates are non-trivial.
4. **Update [`docs/Guides/HITL_TEST_SCENARIOS.md`](../Guides/HITL_TEST_SCENARIOS.md)** — this is the required catalog row for every new scenario.

## `edit_overdub_during_note_edit` pitch bands

Two overdub passes use **distinct ranges** (same as baseline first/second overdub) so layers are visually separable on the piano roll:

| Pass | MIDI range | Notes |
|------|------------|-------|
| Pre-edit overdub | 24–39 | C1–D#2 (baseline first overdub band) |
| In-edit overdub | 12–35 | C0–B1 (baseline second overdub band) |

**E:** session undo/redo after in-edit overdub targets the **in-edit overdub pass only** (C0–B1 layer). The pre-edit overdub layer (C1–D#2) stays visible through undo/redo.

Reference capture template: [`captures/host_midi_hitl_edit_overdub_during_note_edit_reference.json`](../../captures/host_midi_hitl_edit_overdub_during_note_edit_reference.json).

## Firmware dependency

In-edit overdub removal uses **session undo** (`SessionUndoEntry.overdubPassIdAtPush`). Serial markers:
`EditSession overdub pass undone` / `EditSession overdub pass redone`.
