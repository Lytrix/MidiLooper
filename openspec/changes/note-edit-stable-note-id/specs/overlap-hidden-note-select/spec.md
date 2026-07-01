## MODIFIED Requirements

### Requirement: Delete targets selected note by stable identity

When delete is invoked on a selected note during **NoteEditSession**, the firmware SHALL commit
pending edits attributable to that selection, then SHALL apply **DeleteNote** to that note's
**`NoteId`** in **NoteEditSession.store** (resolved to note-on + paired note-off at apply time).

#### Scenario: Delete hidden overlap note not in selectable inventory

- **WHEN** delete is invoked on a selected note that is visible in **filterSelectableDisplayNotes**
- **THEN** delete removes the note-on and paired note-off for that note's **NoteId**
- **AND** hidden overlap notes remain excluded from the selectable inventory

#### Scenario: Delete does not mutate unrelated note

- **WHEN** delete targets note B by **NoteId**
- **THEN** the firmware SHALL NOT apply **ChangeLength** or **MoveNote** for a different **NoteId**
  unless **focus.movingNoteId** matches the delete target

### Requirement: Delete captures NoteId before commit boundary

When delete is invoked, the system SHALL resolve the delete target **`NoteId`** from
**filterSelectableDisplayNotes** / **EditorSelection.primaryNote** before any pre-commit or
**saveNoteEditPass** operation.

#### Scenario: Delete after select switch uses new primaryNote

- **WHEN** the user selects note B then invokes delete
- **THEN** delete SHALL target B's **NoteId** after scoped focus rebuild on B

## REMOVED Requirements

### Requirement: Delete captures NoteRef before commit boundary

**Reason**: **NoteRef** geometry removed as note identity; replaced by **NoteId** capture requirement above.

**Migration**: All delete and select paths use **EditorSelection** / **primaryNote**; dev wipe for SD v6.
