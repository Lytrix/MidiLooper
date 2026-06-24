# note-edit-length-mode Specification (delta)

## Purpose

Length-edit mode SHALL be an explicit, bounded state: when disabled, faders 2 and 3 control note **start** only and MUST NOT apply length changes to any note — including after fader 1 reselect or scheduled fader feedback.

## ADDED Requirements

### Requirement: Length mode OFF routes faders to position edit

When `lengthEditingMode` is false, coarse and fine fader handlers SHALL treat fader input as **position edit** (note START / span start) only.

#### Scenario: Toggle length mode off

- **WHEN** the user toggles length editing mode from ON to OFF
- **THEN** serial SHALL log `Length editing mode DISABLED`
- **AND** subsequent coarse fader movement SHALL log `POSITION EDIT` (not `LENGTH EDIT`) for the selected note

#### Scenario: Fader 2 feedback after select in position mode

- **WHEN** length mode is OFF and fader 1 select schedules fader 2 position feedback
- **THEN** the feedback pitchbend SHALL map to the selected note **start** tick
- **AND** SHALL NOT invoke length-edit end mutation on any note

### Requirement: Reselect must not stretch unrelated fixture notes

After overlap editing that temporarily deletes a same-pitch inner note (e.g. P0 @ fixture step 12), fader 1 reselect on the moving note SHALL NOT change P0 gate length when length mode is OFF.

#### Scenario: P0 gate preserved after M0 reselect post-overlap

- **WHEN** long M0 has been lengthened, moved over P0, pitch-changed, and returned home; length mode is OFF; user operates fader 1 on M0 without touching fader 2
- **THEN** P0 (pitch 60 at fixture step 12) SHALL retain ~2-step record gate length in reconstruction
- **AND** serial SHALL NOT contain `LENGTH EDIT` targeting P0 start tick
- **AND** serial SHALL NOT contain `Active note at loop end` for P0 start tick

### Requirement: Length mode ON applies only to selected note

When `lengthEditingMode` is true, length edits SHALL apply only to the currently selected note identity (`movingNote` / selected index), not to other same-pitch notes sharing an off tick.

#### Scenario: M0 length extend does not mutate P0 off tick pairing

- **WHEN** the user lengthens M0 end to step 14 while P0 remains at step 12 with gate length
- **THEN** P0 gate length in reconstruction SHALL remain ~2 steps
- **AND** M0 end SHALL extend to the target step without stealing P0 note-off pairing (store invariant; display refresh tracked separately)

## MODIFIED Requirements

*(none — new capability)*
