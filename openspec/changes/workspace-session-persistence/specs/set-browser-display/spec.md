## ADDED Requirements

### Requirement: CURRENT status line

The display SHALL show **CURRENT** as the always-active working set, separate from SavedSet list
entries. CURRENT SHALL remain visually selected/highlighted while the user plays or edits. SavedSet
rows SHALL NOT be highlighted as the live working set after **loadSetIntoCurrent**.

CURRENT status SHALL include `Last active: {date} {time}` from CurrentSet meta. When
`loadedFromSequence` is non-zero, display SHALL show `From: {folderId}` as provenance (e.g.
`From: 260625_003`) — subtitle only, not as the active row.

#### Scenario: CURRENT status on boot

- **WHEN** the device finishes boot load from CurrentSet
- **THEN** info area shows `CURRENT` as the active row
- **AND** last-active timestamp comes from `MidiLooper/current/workspace.bin`

#### Scenario: From provenance after load set

- **WHEN** **loadSetIntoCurrent** completes from SavedSet sequence 3
- **THEN** CURRENT row shows `From: 260625_003` (or UID equivalent)
- **AND** SavedSet row `260625_003` is not highlighted as live

### Requirement: SavedSet list view

The set browser SHALL list SavedSets by `sequence` (newest first). Each entry SHALL show a primary
label and folder ID:

- When user label is set: show user label (e.g. "Night Jam"); folder name as secondary (e.g.
  `260625_003` or `00003`).
- When user label is empty and `createdAtUnix` is valid: show full date text (e.g.
  `25 June 2026`) as primary label; folder name as secondary.
- When user label is empty and `createdAtUnix` is 0: show UID folder name (e.g. `00003`) as primary
  label.

`_current` and `index.bin` SHALL NOT appear in this list.

#### Scenario: List with date default labels

- **WHEN** SavedSets `260625_003` and `260625_002` exist with no user labels and valid
  `createdAtUnix`
- **THEN** list shows full dates derived from each save time
- **AND** `260625_003` (sequence 3) appears above `260625_002` (sequence 2)

#### Scenario: List with UID folder after RTC loss

- **WHEN** SavedSet `00003` exists with no user label and `createdAtUnix` = 0
- **THEN** list primary label shows `00003`

#### Scenario: Auto-saved set appears after load with dirty current

- **WHEN** user loads SavedSet 3 over dirty CurrentSet and auto **saveNewSet** creates sequence 4
- **THEN** list shows `260625_004` above `260625_003`
- **AND** CURRENT remains the highlighted active row

### Requirement: SavedSet detail view

Detail view for a selected SavedSet SHALL show:

- User label if set; otherwise full date from `createdAtUnix` when valid, otherwise UID folder name
- Created date and time (from `createdAtUnix` when RTC valid at save)
- Folder ID (`260625_003` or `00003`)
- Bars (master loop length)
- Track count
- Filled slot count
- Per-track filled-slot bar graph (8 segments per track row)

#### Scenario: Detail stats match meta

- **WHEN** the user opens detail for a SavedSet with 4 tracks and 18 filled slots
- **THEN** display shows `Tracks: 4` and `Filled: 18`
- **AND** per-track bar rows reflect filled slots per track from SavedSet meta

### Requirement: saveNewSet and auto-save-before-load display feedback

After **saveNewSet** completes (manual or auto before **loadSetIntoCurrent**), the display SHALL
confirm the new SavedSet folder name. Auto-save before load SHALL use a non-blocking message (1–2 s),
not a blocking confirm dialog.

#### Scenario: Save confirmation with date folder

- **WHEN** **saveNewSet** succeeds with valid RTC creating `260625_004`
- **THEN** display shows confirmation including `260625_004`

#### Scenario: Auto-save toast before load

- **WHEN** auto **saveNewSet** runs before **loadSetIntoCurrent** creating `260625_004`
- **THEN** display shows brief non-blocking text including `Saved 260625_004`
- **AND** load proceeds without blocking user confirmation

### Requirement: Button mapping TBD before ship

Exact DROID button combinations for SAVE NEW, **loadSetIntoCurrent**, set browse SHALL be
documented in implementation tasks before M2 UX ships. This spec does not mandate a specific
note/CC map.

#### Scenario: Gesture map documented

- **WHEN** M2 implementation begins
- **THEN** `MidiButtonActions` gesture map for **saveNewSet** and **loadSetIntoCurrent** is recorded in design or tasks artifact
