## MODIFIED Requirements

### Requirement: Published events live in active takes

The system SHALL store committed loop MIDI from record and overdub as one or more **Active**
**Takes** with chunk-backed storage, ordered by `mergeSequence`. Live capture SHALL use
**Capture** on the loop until **commitTake** appends a **Take**.

#### Scenario: Playback reads takes without full flatten

- **WHEN** the transport plays a loop with committed takes
- **THEN** playback uses take/chunk indexed access
- **AND** does not flatten the full loop on each tick

#### Scenario: Record stop commits capture into a take

- **WHEN** recording stops with non-empty capture
- **THEN** capture is committed as a new Active take
- **AND** capture is cleared for the next capture

### Requirement: Note edit uses NoteEditSession until saveEdit

Note-edit paths SHALL mutate **NoteEditSession** storage. The flat materialize bridge SHALL
be limited to session open and post-overdub rematerialize.

#### Scenario: Store mutations before saveEdit

- **WHEN** the user is mid-edit in note edit mode before **saveEdit**
- **THEN** mutations apply to **NoteEditSession** storage only
- **AND** no **Edit** is appended until **saveEdit**

### Requirement: NoteEditSession undo stack

While note edit mode is active, undo and redo SHALL operate on **NoteEditSessionUndoStack**
for edit actions within the current span only, before **saveEdit**.

#### Scenario: Session undo does not pop global stack

- **WHEN** the user presses undo while in note edit mode before **saveEdit**
- **THEN** **NoteEditSessionUndoStack** restores the prior store snapshot
- **AND** `GlobalUndoStack` cursor is unchanged

#### Scenario: Session redo

- **WHEN** the user presses redo while in note edit mode before **saveEdit**
- **THEN** **NoteEditSession** storage restores from the session stack

## REMOVED Requirements

### Requirement: Note edit uses materialized flat bridge until M8

**Reason**: Replaced by **NoteEditSession** + persisted **Edit** entries.
**Migration**: Remove `editFlat_` collapse paths.

### Requirement: Edit overlay epoch preserves prior Active epochs

**Reason**: Edits are not Takes; use `edits[]` with **EditChange** lists.
**Migration**: `applyEdits(takes, edits)`.

### Requirement: Global undo commits whole edit session on exit

**Reason**: **saveEdit** plus **NoteEditSessionCommitted** at span boundaries.
**Migration**: Use **NoteEditSessionCommitted** undo kind.

## ADDED Requirements

### Requirement: Persisted Edit with EditChange list per saveEdit

Each **saveEdit** that changes loop content SHALL append one **Edit** entry containing an
ordered **EditChange** list and a unique **EditId**. **Edit** entries SHALL NOT be stored as Takes.

#### Scenario: saveEdit after delete

- **WHEN** the user completes a delete-note edit and **saveEdit** runs
- **THEN** an **Edit** with delete **EditChange** entries is appended to `edits[]`
- **AND** Takes remain unchanged

#### Scenario: No-op saveEdit

- **WHEN** **saveEdit** would leave **NoteEditSession** storage unchanged
- **THEN** no **Edit** is appended
- **AND** SD dirty state is unchanged

#### Scenario: NoteRef not list index

- **WHEN** multiple **saveEdit** calls delete or add notes
- **THEN** each **EditChange** references targets by **NoteRef**
- **AND** later changes do not use display note indices

### Requirement: NoteEditSessionCommitted global undo

When a **NoteEditSession** span closes, the system SHALL push a **NoteEditSessionCommitted**
global undo entry that reverses all **Edit** ids saved during that span.

#### Scenario: closeNoteEditSpan on note edit exit

- **WHEN** the user exits note edit mode after **saveEdit** left committed edits in the span
- **THEN** **closeNoteEditSpan** pushes **NoteEditSessionCommitted** for that span
- **AND** one global undo reverses all **Edits** from that span

### Requirement: Edit SD autosave is change-gated with configurable interval

Edit-driven SD writes SHALL occur only when loop state is dirty, SHALL schedule an urgent
flush on note edit exit when dirty (including while recording or overdubbing) that completes in
the post-MIDI main-loop slice without blocking MIDI event processing, and SHALL use the
globally configured autosave interval for periodic flush while still in note edit mode (default
five minutes, `autosaveIntervalMs = 300000`). Periodic edit autosave SHALL defer only while
any track is recording or overdubbing and SHALL NOT be blocked solely because a track is playing.

#### Scenario: Dirty mark without immediate SD write

- **WHEN** **saveEdit** commits an **Edit** while still in note edit mode
- **THEN** edit state is marked dirty
- **AND** SD is not written immediately

#### Scenario: Urgent flush on note edit exit in post-MIDI slice

- **WHEN** the user exits note edit mode with dirty state
- **THEN** an urgent SD flush runs after the current MIDI batch in the main-loop tail
- **AND** clears the dirty flag when complete

#### Scenario: Autosave during playback when dirty

- **WHEN** edit state is dirty and autosave interval elapsed and no capture active
- **THEN** the system writes state to SD

### Requirement: Overdub allowed during note edit mode

The system SHALL allow starting and completing overdub while note edit mode is active.

#### Scenario: Span close before overdub

- **WHEN** the user starts overdub while in note edit mode
- **THEN** **closeNoteEditSpan** pushes **NoteEditSessionCommitted** for the pre-overdub span
- **AND** overdub capture proceeds on **Capture**

#### Scenario: Rematerialize NoteEditSession after overdub stop

- **WHEN** overdub stops while note edit mode is still active
- **THEN** a **TakeCommitted** global entry is pushed
- **AND** **NoteEditSession** storage is rematerialized from takes and active edits
- **AND** a new span begins with a fresh **NoteEditSessionUndoStack**

### Requirement: Span-level global undo after note edit exit

After note edit mode exits, global undo SHALL allow reversing note-edit spans and overdub in
order: post-overdub span, overdub take, pre-overdub span.

#### Scenario: Three-step undo after edit plus overdub

- **WHEN** the user edited, overdubbed in note edit mode, edited again, then exited note edit mode
- **THEN** the first global undo reverses the post-overdub span (**NoteEditSessionCommitted**)
- **AND** the second global undo reverses the overdub take (**TakeCommitted**)
- **AND** the third global undo reverses the pre-overdub span (**NoteEditSessionCommitted**)

### Requirement: Edit types covered by NoteEditSession undo and tests

The note edit model SHALL support **NoteEditSessionUndoStack** undo/redo for select, add,
delete, move coarse and fine, pitch, and length before **saveEdit**.

#### Scenario: Move note coarse session undo

- **WHEN** the user moves a note with coarse positioning and undoes before **saveEdit**
- **THEN** the note returns to its pre-move position in **NoteEditSession** storage
