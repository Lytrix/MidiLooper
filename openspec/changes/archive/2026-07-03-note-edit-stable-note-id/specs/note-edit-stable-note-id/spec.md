## ADDED Requirements

### Requirement: Entity id type aliases on public API

The firmware SHALL define **`NoteId`** and **`TrackId`** as **`uint32_t`** aliases with documented invalid sentinels in a shared public header (**`EntityIds.h`**). **`kInvalidNoteId`** SHALL be **0**. **`kInvalidTrackId`** SHALL be **`UINT32_MAX`**, matching **`kInvalidLoopId`**. Phase 0 SHALL introduce types and includes only — no **`MidiEvent.noteId`** field or behavior change until Phase B.

#### Scenario: Invalid sentinels match design

- **WHEN** native tests compile **`EntityIds.h`**
- **THEN** **`kInvalidNoteId`** equals **0**
- **AND** **`kInvalidTrackId`** equals **`kInvalidLoopId`**

#### Scenario: Public headers expose entity id types

- **WHEN** note-edit public headers (**`NoteEditSessionState.h`**, **`EditPass.h`**, **`EditManager.h`**, **`NoteEditFocus.h`**, **`Loop.h`**, **`MidiEvent.h`**) are included
- **THEN** **`NoteId`** and **`TrackId`** are available without adding selection or storage fields

### Requirement: Loop owns NoteId allocation

Each **Loop** SHALL maintain **`nextNoteId_`** and SHALL expose **`allocateNoteId()`** as the **only** allocator of **`NoteId`** values within that loop. Allocated ids SHALL be monotonic, unique within the loop, and SHALL NOT be reused for the lifetime of the loop slot.

#### Scenario: Allocate returns monotonic ids

- **WHEN** **`allocateNoteId()`** is called three times on the same loop without reset
- **THEN** the returned values are strictly increasing
- **AND** no two returned values are equal

#### Scenario: Persisted counter restores on load

- **WHEN** a v6 loop snapshot with **`nextNoteId`** = N is loaded
- **THEN** the next **`allocateNoteId()`** returns N or greater per persisted state
- **AND** no newly allocated id collides with note-ons already in the loaded store

### Requirement: NoteId on note-on MidiEvent only

Each logical note SHALL be identified by **`uint32_t noteId`** stored on its **note-on** **`MidiEvent`** in **`LoopEventStore`**, capture buffers, session store, and **`addedEvents`** on Create edit rows. **`0`** SHALL mean invalid / unassigned. **Note-off** events SHALL NOT carry **`noteId`**; pairing SHALL use channel + pitch + LIFO from the note-on.

#### Scenario: Reconstruct propagates noteId to DisplayNote

- **WHEN** **`NoteUtils::reconstructNotes`** processes a note-on with **`noteId`** = N and a paired note-off
- **THEN** the resulting **`DisplayNote`** has **`noteId`** = N

#### Scenario: Wrap split shares one noteId

- **WHEN** a note wraps the loop boundary and reconstruct emits tail and head **DisplayNote** rows
- **THEN** both rows SHALL have the same **`noteId`** as the source note-on

### Requirement: NoteId assigned only at note birth

The firmware SHALL call **`allocateNoteId()`** and assign the result on note-on **only** at note birth boundaries: record capture append, overdub capture append, manual Add (**EditActionType::Create**), and **`assignMissingNoteIds`** guard rail for existing note-ons with **`noteId == 0`**. Move, pitch, length, velocity, quantize, overlap restore, undo, and redo SHALL preserve existing **`noteId`** values and SHALL NOT allocate new ids.

#### Scenario: Record assigns id on each note-on

- **WHEN** recording appends note-on events to capture
- **THEN** each appended note-on has **`noteId != 0`** before or at record stop
- **AND** all assigned ids in that pass are unique

#### Scenario: Move preserves noteId

- **WHEN** a move edit changes start and end ticks of a note with **`noteId`** = N
- **THEN** the note-on event still has **`noteId`** = N after apply

#### Scenario: Overlap restore preserves noteId

- **WHEN** an overlap note is restored after hide or shorten
- **THEN** the restored note-on carries the same **`noteId`** as before the overlap operation
- **AND** **`allocateNoteId()`** is not invoked for that restore

### Requirement: Assign missing ids on session open

When **`openNoteEditSession`** materializes the session store, the firmware SHALL scan note-on events with **`noteId == 0`**, call **`allocateNoteId()`** for each, assign the result, and log **WARNING** per assignment. After assignment, no selectable note-on in the session store SHALL remain with **`noteId == 0`** unless the session store is empty of note-ons.

#### Scenario: Guard rail assigns stragglers

- **WHEN** session open finds two note-ons with **`noteId == 0`** and **`nextNoteId_`** = 5
- **THEN** those note-ons receive ids 5 and 6
- **AND** **`nextNoteId_`** advances to 7
- **AND** serial or debug log includes WARNING for each assignment

### Requirement: EditorSelection stores NoteIds only

**EditManager** SHALL own **`EditorSelection`** replacing **`NoteEditSelection`**. **`EditorSelection`** SHALL store **`trackId`**, **`loopId`**, **`selectedNotes`** (vector of **`NoteId`**), **`primaryNote`**, and **`bracketTick`**. It SHALL NOT store list indices, **`DisplayNote`** pointers, or geometry as selection identity. List index for OLED draw SHALL be derived at use time from **`primaryNote`** and the sorted selectable inventory.

#### Scenario: Single select populates primaryNote

- **WHEN** the user selects one note via fader-1 navigation
- **THEN** **`selectedNotes`** contains exactly one **`NoteId`**
- **AND** **`primaryNote`** equals that **`NoteId`**
- **AND** **`isSingle()`** is true

#### Scenario: Chord slot first selected is primary

- **WHEN** fader-1 enters a 16th-step slot with multiple notes sorted pitch-low to pitch-high
- **THEN** **`primaryNote`** is the **`NoteId`** of the lowest-pitch note in that slot
- **AND** sibling navigation can change **`primaryNote`** without inventory index drift causing false selection change

### Requirement: Resolve NoteId at mutation time

Edit apply, session undo replay, delete, and overlap helpers SHALL resolve **`NoteId`** to live note-on events via **`findNoteOnById`** (or equivalent) at mutation time. Committed undo rows and **`EditorSelection`** snapshots SHALL store **`NoteId`s**, not global event indices or geometry keys, as note identity.

#### Scenario: Delete by NoteId removes pair

- **WHEN** **`deleteNoteById(N)`** runs on a note with paired note-off
- **THEN** the note-on with **`noteId`** = N and its paired note-off are removed from the store
- **AND** no new **`NoteId`** is allocated

#### Scenario: EditApply uses targetNoteId

- **WHEN** an Update edit row with **`targetNoteId`** = N and **propertyType = Length** is replayed
- **THEN** apply locates the note-on by id N
- **AND** updates paired note-off/end geometry without changing **`noteId`**

### Requirement: Fader feedback gates on primaryNote

After Phase A selection refactor is stable, NOTE_EDIT fader motor sync and dependent outbound refresh SHALL treat selection as changed when **`EditorSelection.primaryNote`** changes or when the **`selectedNotes`** set changes. Motor sync SHALL NOT gate solely on **`displayIdx`** or **`selectedNoteIdx`** delta.

#### Scenario: Inventory rebuild without id change does not retrigger sync

- **WHEN** the selectable inventory is rebuilt and derived list index for **`primaryNote`** changes
- **AND** **`primaryNote`** is unchanged
- **THEN** fader note-select dependent refresh is not scheduled solely due to list index change

#### Scenario: primaryNote change triggers sync

- **WHEN** fader-1 navigation changes **`primaryNote`** from id A to id B
- **THEN** note-select outbound feedback runs for the note with id B per fader feedback spec

### Requirement: Cache does not own identity

Rebuilt caches (**`baselineMap`**, overlap maps, **`CachedNoteList`**, ephemeral selectable inventory, **`SelectNavigation`** slots) SHALL be derivable from **`LoopEventStore`** and **`NoteId`** keys. Invalidating or destroying caches SHALL NOT lose note identity information durably held on note-on events.

#### Scenario: Invalidate caches preserves noteId on events

- **WHEN** **`invalidateCaches()`** runs after a flat edit
- **THEN** note-on events in the session store retain their **`noteId`** values
- **AND** reconstructed **`DisplayNote`** list reports the same **`noteId`s** for the same logical notes
