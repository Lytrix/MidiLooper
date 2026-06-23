## Why

The shipped **timeline-passes** model reserves **EditPassKind** for note vs control-change edit rows, but the next edit family needs clearer nesting before CC or future audio edits are added. The current **EditChange** vocabulary is note-shaped; extending it with CC fields would blur scope, action, property, and target.

## What Changes

- Replace the future-facing **EditPassKind** model with a scoped **EditPass** model:
  - **EditSessionType** identifies the edited domain (`Note`, `ControlChange`, future `Audio`).
  - **EditActionType** identifies the stored action (`Create`, `Update`, `Delete`).
  - **EditPropertyType** identifies the updated property (`Pitch`, `Length`, `StartTick`, `EndTick`, `Tick`, `Value`, or `None`).
- Keep one ordered **editPasses[]** array on **LoopPasses** unless a later change proves separate arrays are required.
- Keep capture unified: record/overdub continue storing one time-ordered MIDI event stream including notes, CC, pitch bend, and program change.
- Keep undo/redo separate from stored edit action: undo disables pass rows; redo re-enables pass rows. Undo/redo does not create new `Delete` or `Create` edits.
- Treat current **EditChange** / **EditChangeType** as shipped legacy note-edit storage during migration, not as the future generic edit row.
- Define naming rules for **Property** rather than **Parameter**: `parameter` already means controller/action argument in button/fader config.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- **timeline-passes**: replace the future **EditPassKind** discriminant model with scoped **EditPass** rows using **EditSessionType**, **EditActionType**, and **EditPropertyType**.

## Impact

- **Code:** `EditPass.h`, `LoopPasses.*`, `Loop.*`, `TrackUndo.*`, `StorageLoopIo.*`, native tests around edit materialization and SD persistence.
- **Docs:** `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`, `README.md`, `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`.
- **Tests:** native `pio test -e native`; targeted `test_edit_apply`, `test_storage_loop_io`, and undo tests when implementation starts.
- **Non-goals:** no CC edit UI, no audio edit implementation, no capture split, no Teensy upload.
