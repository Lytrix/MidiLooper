## ADDED Requirements

### Requirement: Long-press slot triggers IMPORT LOOP

A long-press gesture on a loop slot button SHALL enter **IMPORT LOOP** mode for that slot's
**LoopLocation** `{ track, slot }`.

#### Scenario: Import targets pressed slot

- **WHEN** the user long-presses slot button for track T slot S
- **THEN** IMPORT LOOP mode targets **LoopLocation** `{ T, S }`
- **AND** no track-selection step is shown in UX

### Requirement: Import source navigation

IMPORT LOOP SHALL offer source navigation at minimum:

- Current CurrentSet (`Sets/_current/`)
- Last 3 SavedSets (by created timestamp)
- Browse All SavedSets (catalog excludes `_current` and `checkpoints/`)

Favorites section is a future capability (non-goal v1).

#### Scenario: Last three SavedSets listed

- **WHEN** IMPORT LOOP source picker opens
- **AND** at least three SavedSets exist
- **THEN** the three most recent SavedSets appear in the quick list

### Requirement: Import selection flow

The user SHALL select **source container → source loop → confirm load into target slot**. Track
is internal metadata only; UX does not expose track picker during import.

#### Scenario: Import copies loop blob only

- **WHEN** the user selects a loop from SavedSet `Sets/260625_001/loop_02_03.bin` and confirms import into target `{ T, S }`
- **THEN** target slot S on track T receives the source loop MIDI and geometry
- **AND** other slots on track T are unchanged
- **AND** `invalidateCaches()` runs on track T

#### Scenario: Import from CurrentSet

- **WHEN** the user selects a loop from `Sets/_current/loop_01_04.bin` for import
- **THEN** the source loop is read from CurrentSet without modifying CurrentSet until import commits

#### Scenario: Import requests CurrentSet save

- **WHEN** import completes successfully
- **THEN** `requestDeferredSaveState` is queued for CurrentSet
- **AND** active loop indices on other tracks are unchanged unless user separately switches slot

### Requirement: LoopLocation struct

Slot import and persistence helpers SHALL use **LoopLocation** with `uint8_t track` and
`uint8_t slot` fields to identify slot addresses.

#### Scenario: Valid LoopLocation bounds

- **WHEN** import targets **LoopLocation** `{ track, slot }`
- **THEN** `track` is in `0 .. NUM_TRACKS-1`
- **AND** `slot` is in `0 .. MAX_LOOPS_PER_TRACK-1`
