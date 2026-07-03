## MODIFIED Requirements

### Requirement: Session undo stores EditorSelection with NoteIds

Note edit session undo entries SHALL snapshot **EditorSelection** (**trackId**, **loopId**,
**selectedNotes**, **primaryNote**, **bracketTick**) and session **editRows** with **`targetNoteId`**
on note rows. Restore SHALL reinstate the same **NoteId** values on **EditorSelection** and on
note-on events without allocating new ids.

#### Scenario: Undo round-trip preserves primaryNote

- **WHEN** the user edits a note, pushes session undo, then restores from undo
- **THEN** **EditorSelection.primaryNote** matches the pre-edit snapshot
- **AND** the targeted note-on retains the same **noteId**

#### Scenario: Session undo diff emits targetNoteId rows

- **WHEN** **buildSessionStoreEditPasses** diffs baseline vs session flat events
- **THEN** Delete rows carry **targetNoteId** from the removed baseline note-on
- **AND** Create rows carry assigned **noteId** on the **addedEvents** note-on
- **AND** Update rows carry **targetNoteId** plus payload fields

## ADDED Requirements

### Requirement: applySessionEditRows resolves by NoteId

**applySessionEditRows** / **applyNoteEditPassSequence** SHALL replay note rows using
**findNoteOnById** (or equivalent) on **targetNoteId** at apply time, not cached global indices or
**NoteRef** geometry search.

#### Scenario: Update row replay after move changes ticks

- **WHEN** an Update row with **targetNoteId** = N is replayed after intervening geometry changes
- **THEN** apply locates the current note-on by id N
- **AND** applies payload fields to that note-on and paired note-off
