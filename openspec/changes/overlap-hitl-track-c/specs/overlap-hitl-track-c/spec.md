## ADDED Requirements

### Requirement: Insert restore after move-back off inserted note

When the moving note (pitch 60) moves over an inserted note and then moves back without fader-1 reselect, the inserted note SHALL be present in **NoteEditSession.store** after the move-back completes.

#### Scenario: HITL insert reorder move-back

- **WHEN** the edit baseline runs move-over-insert then move-back-off-insert
- **THEN** `_verify_delay_move_insert_reorder` reports `insert_restored_after_move_back` true
- **AND** reconstruction or overlap restore logs show the insert at fixture insert step tick

#### Scenario: Insert survives in-edit session redo

- **WHEN** in-edit session redo runs after the insert/reorder scenario (before note edit exit)
- **THEN** `_verify_delay_move_insert_reorder` reports `insert_present_after_in_edit_redo` true
- **AND** post-redo **NoteEditSession.store** reconstruction includes the inserted note at fixture insert tick
- **AND** global **NoteEditPassClosed** undo is not invoked for in-edit double/triple record presses

### Requirement: M0 at home after overlap round-trip

After the overlap round-trip sequence (move over inner notes, pitch M0 to 67, move past A, return home), the moving note SHALL appear at fixture step 0 (`m0_tick`) at pitch 67 with length at least record gate.

#### Scenario: Long-over-short home verifier

- **WHEN** `_verify_long_over_short_pitch_restore` runs on a passing capture
- **THEN** `round_trip_home` and `m0_home_ok` are true
- **AND** inner overlap notes B, A, P0 remain restored per existing AC

#### Scenario: Change-length store native parity at overlap home

- **WHEN** `_verify_change_length_store_rebuild` evaluates the overlap round-trip home checkpoint
- **THEN** `after_overlap_round_trip_home:native_m0_count` is not reported
- **AND** `after_overlap_round_trip_home:native_m0_pitch_67_count` is not reported
- **AND** native parity assertions use post-move inventory at `m0_tick`, not pre-move scratch reconstruction

### Requirement: Split-overlap mover at home

After the split-overlap round-trip (M0 pitch+position without fader-1 reselect), the mover SHALL be at fixture home with length greater than record gate; inner overlap note A SHALL remain recaptured at home.

#### Scenario: Split overlap round-trip verifier

- **WHEN** `_verify_split_overlap_note_round_trip` runs on a passing capture
- **THEN** `inner_a_recaptured_at_home` and `mover_at_home` are true
- **AND** `split_overlap_note_mover_not_home` is not listed in issues

## MODIFIED Requirements

*(none — new capability spec only)*

## REMOVED Requirements

*(none)*
