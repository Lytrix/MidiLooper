# Spec delta — note edit modification session

**Change:** `note-edit-modification-session`  
**Applies to:** note edit overlap + commit behavior during **NoteEditSession**

---

## ADDED Requirements

### Requirement: Single overlap owner for move / length / pitch

During an active **NoteEditSession**, the system SHALL route move, length, and pitch mutations
through one overlap engine (`applyNoteEditChange`) that owns overlap note hide, shorten, and
restore logic.

#### Scenario: Pitch and move share restore pass

- **WHEN** the user changes pitch then moves the moving note away from a hidden overlap note
- **THEN** the overlap note SHALL be restored using the same restore pass as move-only edits

---

### Requirement: Baseline at note select

When the user selects a note with fader 1 during note edit, the system SHALL capture a read-only
baseline map of all notes from **NoteEditSession.store** and SHALL set the focus **commitBaseline**
from the selected note's baseline entry.

#### Scenario: Overlap notes from Take included

- **WHEN** the selected moving note has other notes present in the materialized store from a Record **Take**
- **THEN** those notes SHALL appear in the baseline map without a separate Take read

---

### Requirement: Commit baseline vs overlap footprint (A1)

The system SHALL maintain separate **commitBaseline** and **overlapFootprint** on the note edit
focus. Length edit SHALL update **overlapFootprint.end** to match live end tick and SHALL NOT update
**commitBaseline.end** until commit.

#### Scenario: Pending length commit after lengthen

- **WHEN** the user lengthens the moving note without committing
- **THEN** `commitPendingLengthAction` SHALL detect a pending **ChangeLength** against **commitBaseline**
- **AND** inner overlap note tests SHALL use **overlapFootprint.end** equal to the lengthened end

---

### Requirement: Live source of truth

During note edit, **NoteEditSession.store** SHALL be the sole mutable MIDI source for
`editAwareMidiEvents()`. Hiding an overlap note SHALL remove only impacted note pairs from the
store while retaining geometry in the baseline map and **overlapNotes** entry.

#### Scenario: Incremental store mutation

- **WHEN** overlap hides one overlap note
- **THEN** only that note's events and the moving note's events SHALL be modified in the store

---

### Requirement: Pre-commit resolve (B1)

Before **saveEdit** at fader-1 reselect or commit boundary, the system SHALL resolve
**overlapNotes** into the store for impacted **NoteRef**s only, emit **EditChange** entries only
for notes whose committed state differs from **commitBaseline** / baseline map, and rebuild the
session store via **applyEdits**.

#### Scenario: Rematerialize matches live session

- **WHEN** the user completes lengthen → move over overlap note → pitch → move back and commits
- **THEN** `applyEdits(takes, edits)` SHALL produce the same note inventory as the live store after
  pre-commit resolve

---

### Requirement: CC and velocity out of overlapNotes

Velocity and control-change edits SHALL NOT use **overlapNotes**. They SHALL apply only to the
focus moving note (or future **ControlChangeEditSession** scope).

#### Scenario: Velocity change during note edit

- **WHEN** the user changes velocity on the selected note
- **THEN** no **NoteRef** SHALL be added to **overlapNotes**

---

## MODIFIED Requirements

None — new capability; child bug specs remain as scenario evidence until archived into
`openspec/specs/` after implementation.
