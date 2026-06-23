## Purpose

Composable hardware-in-the-loop (**HITL**) automation: named **scenarios** combine MIDI **phases**
and serial **verifiers**. Entry point: [`scripts/host_midi_hitl.py`](../../scripts/host_midi_hitl.py).

## Requirements

### Requirement: Composable HITL scenarios

The HITL runner SHALL register named **scenarios** that combine MIDI **phases** and serial
**verifiers**.

#### Scenario: Run subset by name

- **WHEN** the operator passes `--scenarios edit_minimal`
- **THEN** only phases and verifiers for those scenarios run
- **AND** unrelated verifiers from **`edit_full`** do not gate the run

### Requirement: Backward-compatible presets

Presets **`base`** and **`edit_full`** SHALL produce equivalent default MIDI sequences and
verifiers to the legacy baseline scripts.

#### Scenario: Preset base

- **WHEN** the operator runs `host_midi_hitl.py run --preset base` with canonical args
- **THEN** record/overdub transition and reconciliation checks match the legacy baseline script

### Requirement: Verify-only replay

- **WHEN** `--verify-serial-log` is set without opening MIDI ports
- **THEN** the runner executes verifiers for selected scenarios against the log
- **AND** does not send MIDI

### Requirement: edit_overdub_during_note_edit scenario

The runner SHALL provide scenario **`edit_overdub_during_note_edit`** covering record, first
overdub, note edit commits, in-edit overdub, **EditSession** undo for overdub removal, second edit
pass, **exitEditMode**, and post-exit global undo sequence.

#### Scenario: In-edit overdub session undo marker

- **WHEN** the scenario runs session undo after in-edit overdub stop
- **THEN** serial includes **`EditSession overdub pass undone`**
- **AND** **`EditSession undo`**

#### Scenario: Post-exit global undo sequence

- **WHEN** the scenario runs global undos after **exitEditMode**
- **THEN** serial includes post-exit scoped edit pass undo markers
