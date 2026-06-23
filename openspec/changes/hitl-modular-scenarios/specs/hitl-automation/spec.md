## ADDED Requirements

### Requirement: Composable HITL scenarios

The HITL runner SHALL register named **scenarios** that combine MIDI **phases** and serial
**verifiers**.

#### Scenario: Run subset by name

- **WHEN** the operator passes `--scenarios edit_minimal`
- **THEN** only phases and verifiers for those scenarios run
- **AND** unrelated verifiers from **`edit_full`** do not gate the run

### Requirement: Backward-compatible presets

Presets **`base`** and **`edit_full`** SHALL produce equivalent default MIDI sequences and
verifiers to [`host_midi_automation_baseline.py`](scripts/host_midi_automation_baseline.py) and
[`host_midi_automation_edit_baseline.py`](scripts/host_midi_automation_edit_baseline.py).

#### Scenario: Preset base

- **WHEN** the operator runs `host_midi_hitl.py run --preset base` with canonical args
- **THEN** record/overdub transition and reconciliation checks match the legacy baseline script

### Requirement: Verify-only replay

The runner SHALL support verify-only replay without opening MIDI ports.

#### Scenario: Verify-only replay without MIDI

- **WHEN** `--verify-serial-log` is set without opening MIDI ports
- **THEN** the runner executes verifiers for selected scenarios against the log
- **AND** does not send MIDI

### Requirement: edit_overdub_during_note_edit scenario

The runner SHALL provide scenario **`edit_overdub_during_note_edit`** covering:

1. Record and first overdub while **PLAYING**
2. Enter NOTE_EDIT; **add**, **delete**, **move**, and **length** commits (**saveNoteEditPass**)
3. Second overdub while still in note edit (**closeNoteEditPass** then **OverdubPassAdded**)
4. **EditSession** undo/redo that disables/restores the in-edit overdub **overdubPass**
5. Second edit pass actions; **exitEditMode** with **NoteEditPassClosed** and deferred save
6. Post-exit global undo/redo matching three-step **noteEditPass** / **overdubPass** order

#### Scenario: In-edit overdub session undo marker

- **WHEN** the scenario runs session undo after in-edit overdub stop
- **THEN** serial includes **`EditSession overdub pass undone`**
- **AND** **`EditSession undo`**

#### Scenario: Post-exit global undo sequence

- **WHEN** the scenario runs three global undos after **exitEditMode**
- **THEN** serial includes post-exit scoped edit pass undo markers in reverse commit order
