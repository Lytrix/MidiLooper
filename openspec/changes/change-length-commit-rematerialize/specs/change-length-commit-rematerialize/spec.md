# Spec delta — ChangeLength commit rematerialize

**Change:** `change-length-commit-rematerialize`  
**Applies to:** **ChangeLength** commit and **Takes + Edits** replay during **NoteEditSession**

---

## ADDED Requirements

### Requirement: ChangeLength commit updates persisted moving note end

The system SHALL persist the moving note's new end tick in **Loop** **Edits** on **ChangeLength** commit so **applyEdits** replay on **NoteEditSession.store** matches the live end tick previewed before commit.

#### Scenario: Post-commit reconstruction shows lengthened M0

- **WHEN** the user lengthens M0 on the standard edit fixture and commits **ChangeLength** with
  `newEnd` at the long fixture step
- **THEN** the next reconstruction snapshot SHALL show M0 `end` equal to `newEnd` within edit tick tolerance
- **AND** SHALL NOT leave M0 at the record gate end tick

#### Scenario: Serial commit line matches store

- **WHEN** firmware logs `Edit committed ChangeLength` with `baselineEnd` and `newEnd`
- **THEN** the inventory after the post-commit snapshot line SHALL reflect `newEnd` for that moving note

---

### Requirement: Rematerialize parity at HITL checkpoints

Materialized inventory from **Takes + Edits** replay SHALL match the live **NoteEditSession.store** at each HITL checkpoint after **ChangeLength** commit and subsequent overlap edits in the same session.

#### Scenario: Post length commit native parity

- **WHEN** `_verify_change_length_store_rebuild` runs on the capture log
- **THEN** `after_change_length_commit:*` issues SHALL be empty

#### Scenario: Post pitch change native parity

- **WHEN** the overlap round-trip includes **ChangePitch** after lengthen and move-over-P0
- **THEN** `after_overlap_pitch_change:native_m0_count:0!=1` SHALL NOT appear
- **AND** M0 at pitch 67 SHALL appear exactly once in the native parity inventory at the post-pitch checkpoint

#### Scenario: Post reselect B native parity

- **WHEN** the user reselects fixture note B after the overlap segment
- **THEN** `after_reselect_b:native_m0_count:0!=1` SHALL NOT appear
- **AND** stray pitch-60 note-on at M0 start SHALL NOT appear

---

### Requirement: M0 home after overlap round-trip

The moving note SHALL return to fixture home start tick after move-over-P0 → pitch → move past → move home without fader-1 reselect mid-segment when Track A rematerialize gates pass.

#### Scenario: Long over short pitch restore home

- **WHEN** `_verify_long_over_short_pitch_restore` runs on a passing Track A capture
- **THEN** `m0_home_ok` SHALL be true

#### Scenario: Split overlap note round-trip mover home

- **WHEN** `_verify_split_overlap_note_round_trip` runs on the same capture
- **THEN** `mover_at_home` SHALL be true
- **AND** inner overlap note A SHALL remain recaptured at home per existing AC

---

### Requirement: Investigation artifacts before archive

The change SHALL document root cause and capture id in [BUG.md](./BUG.md) patch history before archive.

#### Scenario: Hypothesis closed in BUG.md

- **WHEN** Track A fix merges
- **THEN** BUG.md SHALL record which hypothesis (H1–H5) was confirmed and the capture id used for sign-off

---

## MODIFIED Requirements

None — parent [note-edit-modification-session](../note-edit-modification-session/specs/note-edit-modification-session/spec.md) B1 requirements stand; this delta closes the device gap.

## REMOVED Requirements

None.
