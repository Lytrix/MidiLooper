## MODIFIED Requirements

### Requirement: noteEditPass batch vs editPass row

A `noteEditPass` SHALL denote NOTE_EDIT open/close boundary in `NoteEditSession`.
`saveNoteEditPass` SHALL remain the merge boundary into `passes.editPasses[]`.

Leaving NOTE_EDIT via `exitEditMode` SHALL evaluate pending NOTE_EDIT actions and, when edit
changes exist, SHALL append scoped `editPass` rows before `closeNoteEditPass`.

The NOTE_EDIT/LOOP_EDIT mode toggle path (`cycleMainEditMode`) SHALL have explicit, testable
behavior for whether it is a commit boundary or session-only UI transition.

#### Scenario: Full NOTE_EDIT exit appends and closes pass

- **WHEN** NOTE_EDIT leaves through `exitEditMode`
- **AND** pending note-edit changes exist
- **THEN** `saveNoteEditPass` appends one or more rows to `passes.editPasses[]`
- **AND** `closeNoteEditPass` emits `NoteEditPassClosed` for that closed batch

#### Scenario: NOTE_EDIT mode toggle behavior is explicit

- **WHEN** NOTE_EDIT transitions through `cycleMainEditMode`
- **THEN** the system behavior is explicitly verified as either:
  - commit boundary with pass close, or
  - session-only transition without pass close
- **AND** test coverage proves the selected behavior

### Requirement: Deferred save boundary after note edit exit

NOTE_EDIT exit persistence SHALL use deferred save handoff. Runtime behavior SHALL preserve MIDI
timing during playback by avoiding blocking save work in playback hot paths.

The system SHALL provide evidence markers from save request to deferred-save completion for
NOTE_EDIT exit persistence.

#### Scenario: Exit triggers deferred save request and completion markers

- **WHEN** NOTE_EDIT exits with `isEditStateDirty() == true`
- **THEN** the serial trace includes deferred save request markers (`PERS,request`)
- **AND** completion markers include `PERS,result,ok` when deferred save completes

#### Scenario: Replay checks after exit align with persisted boundary

- **WHEN** NOTE_EDIT workflows perform in-edit undo/redo and post-exit undo/redo
- **THEN** marker sequence includes `NoteEditPassClosed` and scoped post-exit undo/redo markers
- **AND** replay verification does not report missing insertions caused by exit persistence boundary loss
