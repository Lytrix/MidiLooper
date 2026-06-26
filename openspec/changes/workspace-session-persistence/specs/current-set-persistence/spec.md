## ADDED Requirements

### Requirement: CurrentSet is always-active live loop inventory

The system SHALL treat **CurrentSet** (`Sets/_current/` on SD) as the single always-active live
loop inventory. **CurrentSet** occupies conceptual sequence **000** (reserved — not a SavedSet
folder suffix). SavedSet snapshots start at sequence **001**. CurrentSet SHALL load on every boot,
SHALL be continuously writable via the deferred save path, and SHALL NOT be user-deletable or
listed as a SavedSet entry.

#### Scenario: Boot loads CurrentSet

- **WHEN** the device boots and `Sets/_current/meta.bin` is valid
- **THEN** runtime RAM mirrors the CurrentSet content
- **AND** transport and slot state are restored from CurrentSet meta

#### Scenario: CurrentSet is not a SavedSet

- **WHEN** the user browses SavedSet list
- **THEN** `Sets/_current/` does not appear as a numbered SavedSet entry
- **AND** CURRENT status is shown separately

### Requirement: CurrentSet SD layout under single Sets root

CurrentSet SHALL persist under `Sets/_current/` with:

- `meta.bin` — versioned global transport, track headers, slot metadata, active indices, global undo
- `loop_TT_SS.bin` — per-slot loop blob using existing `StorageLoopIo` wire (2-digit zero-padded track and slot, e.g. `loop_00_07.bin`)

All set containers SHALL live under the single SD root `/Sets/`.

#### Scenario: Meta excludes inline loop bodies

- **WHEN** CurrentSet meta is written
- **THEN** loop MIDI payloads are not embedded in `meta.bin`
- **AND** each slot has a corresponding `loop_TT_SS.bin` file in `Sets/_current/`

#### Scenario: Two-digit loop filename supports future caps

- **WHEN** track 15 slot 15 is persisted
- **THEN** the file name is `loop_15_15.bin`
- **AND** lexicographic directory listing matches numeric track/slot order

### Requirement: Atomic per-slot CurrentSet write

Each CurrentSet loop file SHALL be written using temp → verify → rename. The system SHALL NOT
overwrite `loop_TT_SS.bin` in place without a staged temp file.

#### Scenario: Power loss during slot write

- **WHEN** power is lost during `loop_00_00.bin.tmp` write
- **THEN** on next boot either the previous `loop_00_00.bin` remains valid or that slot load fails independently
- **AND** other slot files in CurrentSet are unaffected

#### Scenario: Successful slot write commits

- **WHEN** a deferred save slice completes a CurrentSet slot file write
- **THEN** the temp file is renamed to `loop_TT_SS.bin`
- **AND** the file ends with `STORAGE_COMPLETE_MAGIC`

### Requirement: CurrentSet uses central deferred writer

Runtime persistence to CurrentSet SHALL route through `requestDeferredSaveState` and
`processDeferredSaveState`. The system SHALL NOT call synchronous `saveState()` on record/overdub
stop or other hot paths.

#### Scenario: Capture active defers CurrentSet write

- **WHEN** any track is RECORDING or OVERDUBBING
- **THEN** `processDeferredSaveState` does not advance CurrentSet write slices

#### Scenario: CurrentSet write completes with PERS ok

- **WHEN** a full CurrentSet flush completes
- **THEN** serial telemetry reports `PERS,result,...,ok`
- **AND** `meta.bin` records updated `lastActiveUnix` from RTC

### Requirement: v5 monolith migration to CurrentSet

The system SHALL migrate v5 monolith state to CurrentSet on first v6 boot: when `Sets/_current/`
is absent and a valid v5 `/midilooper_state.raw` exists, load the monolith once, write
`Sets/_current/` with 2-digit loop filenames, and quarantine the legacy file.

#### Scenario: First boot after v6 upgrade

- **WHEN** `/midilooper_state.raw` v5 exists and `Sets/_current/` is absent
- **THEN** all tracks and loops appear in RAM as before migration
- **AND** `Sets/_current/` is created
- **AND** the legacy monolith is quarantined to `/state.bad.{millis}`

### Requirement: CurrentSet meta tracks provenance and dirty anchor

CurrentSet `meta.bin` SHALL include fields for load/save coordination at minimum:

- `uint32_t loadedFromSequence` — SavedSet sequence last loaded into CurrentSet (0 if never loaded from a SavedSet)
- `uint32_t lastAnchoredSequence` — sequence of the last SavedSet that captured CurrentSet state (manual, auto-before-load, or failsafe); 0 if none since boot
- `uint32_t lastMaterialChangeUnix` — RTC timestamp of last material change to CurrentSet (0 when RTC invalid)
- `bool hasMaterialChangesSinceAnchor` — true when CurrentSet differs from `lastAnchoredSequence` snapshot

Material changes SHALL include record, overdub, edit commit, clear, and slot loop import into
CurrentSet. Deferred writes alone SHALL NOT clear the dirty flag — only **saveNewSet**,
**loadSetIntoCurrent** completion, or boot load of an anchored state SHALL update the anchor.

#### Scenario: Dirty flag set after record without saveNewSet

- **WHEN** the user records into CurrentSet without **saveNewSet**
- **THEN** `hasMaterialChangesSinceAnchor` is true
- **AND** `lastMaterialChangeUnix` is updated when RTC is valid

#### Scenario: saveNewSet clears dirty anchor

- **WHEN** **saveNewSet** completes for sequence N
- **THEN** `lastAnchoredSequence` = N
- **AND** `hasMaterialChangesSinceAnchor` is false

#### Scenario: loadSetIntoCurrent updates provenance

- **WHEN** **loadSetIntoCurrent** completes from SavedSet sequence B
- **THEN** `loadedFromSequence` = B
- **AND** `lastAnchoredSequence` = B
- **AND** `hasMaterialChangesSinceAnchor` is false
