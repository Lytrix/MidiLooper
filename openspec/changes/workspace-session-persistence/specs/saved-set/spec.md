## ADDED Requirements

### Requirement: SavedSet is immutable user snapshot

A **SavedSet** SHALL be an explicit user-intent snapshot stored under `Sets/{folderName}/` where
`folderName` is derived from **SetIndex** `nextSequence` and RTC validity (see hybrid naming
requirement below). SavedSet sequence numbers SHALL start at **001**; **000** is reserved for
**CurrentSet** (`Sets/_current/` — the live set, not a SavedSet folder). Once created, a SavedSet
folder SHALL NOT be overwritten except by explicit **updateLastSet** (v2 — not required in v1).

#### Scenario: saveNewSet with valid RTC uses date folder name

- **WHEN** the user invokes **saveNewSet**
- **AND** `Sets/index.bin` reports `nextSequence` = 3
- **AND** RTC is set to a calendar date on or after 2026-01-01
- **THEN** a new `Sets/260625_003/` folder is created (YYMMDD from RTC at save time, `_` + sequence)
- **AND** CurrentSet content is copied into the new folder

#### Scenario: saveNewSet with invalid RTC uses UID folder name

- **WHEN** **saveNewSet** runs with `nextSequence` = 3
- **AND** RTC is unset, reset, or before 2026-01-01
- **THEN** the new SavedSet folder is `Sets/00003/` (5-digit zero-padded sequence; sequence 3 →
  `00003`, first save would be `00001`)
- **AND** ordering still follows sequence 3 in catalog sort

### Requirement: Hybrid SavedSet folder naming

Folder naming SHALL use **SetIndex** sequence for allocation and ordering. RTC SHALL affect folder
name format only, not sequence value.

When RTC reads a valid calendar date **on or after 2026-01-01**:

- Folder name SHALL be `YYMMDD_NNN` where `YYMMDD` is from RTC at save time and `NNN` is
  `nextSequence` zero-padded to 3 digits (e.g. `260625_001`, `260625_003`, `260625_042`).

When RTC is unset, failed, reset, or before 2026-01-01:

- Folder name SHALL be UID-only: 5-digit zero-padded sequence starting at `00001` (e.g. `00001`,
  `00003`, `00042`). Sequence **000** SHALL NOT be allocated — it is reserved for **CurrentSet**.

Existing folders SHALL NOT be renamed when RTC later becomes valid; only new **saveNewSet** calls
use the updated naming rule.

#### Scenario: RTC recovery does not rename old UID folders

- **WHEN** SavedSet `Sets/00003/` was created with invalid RTC
- **AND** RTC is later set correctly
- **THEN** `Sets/00003/` remains unchanged
- **AND** the next **saveNewSet** may create `Sets/260625_004/` (date form with next sequence)

### Requirement: SetIndex registry is source of truth for SavedSet sequence

The system SHALL maintain `Sets/index.bin` containing **SetIndex** with a single field
`uint32_t nextSequence` — the next sequence number to assign on **saveNewSet** (initial value **1**
for first SavedSet `_001` / `00001`). SavedSet allocation and catalog ordering SHALL use
`sequence` from **SetIndex**, not RTC date arithmetic. Sequence **0** SHALL NOT be used for SavedSets.

#### Scenario: First save creates index

- **WHEN** `Sets/index.bin` is absent and **saveNewSet** runs with valid RTC on 2026-06-25
- **THEN** the first SavedSet folder is `Sets/260625_001/`
- **AND** after success `Sets/index.bin` records `nextSequence` = 2

#### Scenario: saveNewSet increments registry after successful copy

- **WHEN** **saveNewSet** completes copying CurrentSet into `Sets/260625_005/`
- **THEN** `Sets/index.bin` is updated to `nextSequence` = 6
- **AND** the index write uses temp → verify → rename

### Requirement: Boot reconciles SetIndex with existing folders

On boot or before **saveNewSet**, the system SHALL reconcile `nextSequence` to at least one greater
than the highest `sequence` extracted from any existing SavedSet folder (date form or UID form).

#### Scenario: Orphan folder after power loss

- **WHEN** `Sets/260625_007/` exists but `Sets/index.bin` still records `nextSequence` = 7
- **THEN** reconciliation advances `nextSequence` to at least 8 before the next **saveNewSet**

#### Scenario: Reconcile reads both folder name formats

- **WHEN** `/Sets/` contains `00003/` and `260625_004/`
- **THEN** reconciliation treats highest sequence as 4
- **AND** `nextSequence` becomes at least 5

### Requirement: saveNewSet excludes checkpoints

**saveNewSet** SHALL copy `MidiLooper/current/workspace.bin` (and interim runtime bundle / slot files under `MidiLooper/current/`) and all loop slot files to the
new SavedSet folder. It SHALL NOT copy `Sets/_current/checkpoints/` or any RecoveryPoint content.

#### Scenario: SavedSet has no checkpoints subfolder

- **WHEN** **saveNewSet** completes
- **THEN** the new SavedSet folder contains `set.bin` and `loop_*.bin` only
- **AND** no `checkpoints/` directory exists under the SavedSet folder

### Requirement: SavedSet metadata

Each SavedSet `set.bin` SHALL include at minimum:

- `uint32_t sequence` — from **SetIndex** at save time (authoritative ordering key)
- `uint8_t folderNamingMode` — date form vs UID form (for catalog display)
- User label (optional string; empty when unset)
- `createdAtUnix` — RTC timestamp at save when valid; 0 when RTC invalid at save
- Summary stats: master loop length in bars, track count, filled slot count, per-track filled-slot bar indicators for browser display

SavedSet meta SHALL NOT include jam fields until M10. Catalog sort order SHALL use `sequence`
descending regardless of folder name format.

#### Scenario: Default list label uses full date when no user label

- **WHEN** SavedSet has no user label
- **AND** `createdAtUnix` is non-zero
- **THEN** browser list shows full date text (e.g. `25 June 2026`) as the primary label
- **AND** folder name (e.g. `260625_003`) may appear as secondary ID

#### Scenario: Default list label falls back when RTC was invalid at save

- **WHEN** SavedSet has no user label
- **AND** `createdAtUnix` is 0
- **THEN** browser list shows UID folder name (e.g. `00003`) as the primary label

### Requirement: saveCopySet duplicates SavedSet

**saveCopySet** SHALL duplicate an existing SavedSet tree into a new registry-allocated folder
(using current hybrid naming rules) without modifying the source SavedSet.

#### Scenario: Copy preserves source

- **WHEN** **saveCopySet** runs on `Sets/260625_001/`
- **THEN** `Sets/260625_001/` content is unchanged
- **AND** a new folder with the next sequence from **SetIndex** contains an equivalent copy

### Requirement: SavedSet includes global undo

SavedSet snapshots SHALL include global undo stack content with parity to the CurrentSet meta
undo block at save time.

#### Scenario: Undo stack round-trip in SavedSet

- **WHEN** CurrentSet has global undo entries and **saveNewSet** runs
- **THEN** the new SavedSet meta contains equivalent undo data
- **AND** loading that SavedSet for recovery restores undo depth consistent with save time

### Requirement: SavedSet catalog filters reserved entries

The set catalog SHALL enumerate SavedSet folders matching date form (`YYMMDD_NNN`) or UID form
(5-digit sequence). It SHALL NOT list `Sets/_current/`, `Sets/index.bin`, or
`Sets/_current/checkpoints/`.

#### Scenario: Catalog scan excludes CurrentSet and index

- **WHEN** the firmware scans `/Sets/` for SavedSet list
- **THEN** `_current` and `index.bin` are excluded
- **AND** SavedSets are ordered by `sequence` newest first

### Requirement: Eight-hour CurrentSet failsafe SavedSet

The system SHALL automatically invoke **saveNewSet** as a failsafe memory anchor when **CurrentSet**
has material changes since the most recent SavedSet (or since boot when no SavedSet exists), and
**8 hours** have elapsed since the last material modification to **CurrentSet** without a new
SavedSet being created. This is consolidation for revert-to-earlier-set safety, not a replacement
for continuous deferred writes to `Sets/_current/`. Each new SavedSet (manual or failsafe) SHALL
allocate the next sequence from **SetIndex** (+1).

#### Scenario: Manual save increments sequence

- **WHEN** the user invokes **saveNewSet** and `nextSequence` is 3
- **THEN** SavedSet sequence 3 is created (`260625_003` or `00003`)
- **AND** `nextSequence` becomes 4

#### Scenario: Failsafe save after 8 hours of modified CurrentSet

- **WHEN** CurrentSet has changes since the last SavedSet
- **AND** wall-clock time since last material CurrentSet modification exceeds 8 hours
- **AND** no SavedSet was created covering those changes
- **THEN** the system runs **saveNewSet** automatically
- **AND** a new SavedSet receives the next sequence from **SetIndex**
- **AND** CurrentSet continues as the live set unchanged in role

#### Scenario: Failsafe does not run when CurrentSet unchanged

- **WHEN** CurrentSet has no material changes since the last SavedSet
- **THEN** the eight-hour failsafe SHALL NOT create a duplicate SavedSet

#### Scenario: Failsafe runs during idle maintenance only

- **WHEN** the eight-hour failsafe condition is met
- **AND** any track is RECORDING or OVERDUBBING
- **THEN** **saveNewSet** defers until capture is inactive (same gating as deferred CurrentSet save)

### Requirement: loadSetIntoCurrent replaces live set from SavedSet

**loadSetIntoCurrent** SHALL copy a selected SavedSet tree into `Sets/_current/` and apply it to
RAM, making that content the new live **CurrentSet**. The source SavedSet folder SHALL remain
immutable. SavedSet rows SHALL NOT be highlighted as the live working set after load — **CURRENT**
remains the active row.

#### Scenario: Load with dirty CurrentSet auto-preserves via saveNewSet

- **WHEN** the user confirms **loadSetIntoCurrent** from SavedSet sequence 3 (`260625_003`)
- **AND** CurrentSet has material changes since the last SavedSet anchor (or since boot when no anchor exists)
- **AND** less than 8 hours have elapsed since last material modification
- **THEN** the system runs **saveNewSet** first without user confirmation
- **AND** a new SavedSet sequence 4 is created (e.g. `260625_004`) preserving the previous CurrentSet
- **AND** SavedSet sequence 3 is then copied into `Sets/_current/` and RAM
- **AND** CurrentSet meta records `loadedFromSequence` = 3
- **AND** the dirty anchor is cleared after load completes

#### Scenario: Load with clean CurrentSet skips auto-save

- **WHEN** the user loaded SavedSet sequence 3 and made no material changes
- **AND** the user confirms **loadSetIntoCurrent** from SavedSet sequence 2
- **THEN** **saveNewSet** SHALL NOT run before load
- **AND** SavedSet sequence 2 replaces `Sets/_current/` content
- **AND** CurrentSet meta records `loadedFromSequence` = 2

#### Scenario: Auto-save before load is not gated on eight-hour failsafe

- **WHEN** CurrentSet has material changes for 2 hours without manual **saveNewSet**
- **AND** the user loads another SavedSet into CurrentSet
- **THEN** auto **saveNewSet** runs before overwrite regardless of the 8-hour failsafe timer

#### Scenario: Brief UI feedback after auto-save before load

- **WHEN** auto **saveNewSet** runs before **loadSetIntoCurrent**
- **THEN** display MAY show a non-blocking confirmation including the new SavedSet folder name for 1–2 seconds
- **AND** load proceeds without a blocking confirm dialog
