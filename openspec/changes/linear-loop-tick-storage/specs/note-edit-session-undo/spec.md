## ADDED Requirements

### Requirement: Undo snapshots after normalize at commit

Note-edit undo snapshots pushed at **macro** commit boundaries SHALL capture **canonical** session store events (post-`normalizeAll`). Restore SHALL reinstate linear spans without wrap-pair storage.

#### Scenario: Undo restores linear span

- **WHEN** undo restores a snapshot taken after `commitAllPendingNoteEditActions` and `normalizeAll`
- **THEN** restored NoteOn and NoteOff ticks match the snapshot
- **AND** invariant 7 holds on the restored pair

### Requirement: Global undo snapshots are canonical

Global loop undo snapshots (`restoreFromSnapshot`) SHALL store and restore **canonical** linear ticks. Snapshots are taken only after normalize at commit boundaries on the materialized loop. Restore SHALL NOT run normalize (data is already canonical).

#### Scenario: Global undo restore preserves linear off

- **WHEN** global undo restores a snapshot containing `NoteOn@1344`, `NoteOff@1536`
- **THEN** restored flat store matches snapshot ticks exactly
- **AND** `validateLoopEvents` passes canonical mask

### Requirement: Materialize reads canonical pass data

`LoopPasses::materialize` and `EditApply` replay SHALL assume pass-committed event ticks are **canonical** (normalized at `saveNoteEditPass` / capture stop commit). Materialize SHALL NOT re-normalize except when applying live edit geometry that changes ticks before commit.

#### Scenario: saveNoteEditPass writes canonical ticks

- **WHEN** `saveNoteEditPass` commits note geometry with changed ticks
- **THEN** `normalizeWindow` runs before the pass row is stored
- **AND** materialize replays those ticks without further conversion

#### Scenario: EditApply replay parity

- **WHEN** `EditApply` replays edit pass rows onto flat store
- **THEN** resulting ticks match pass-stored canonical values
- **AND** rematerialize view equals session store for the same geometry
