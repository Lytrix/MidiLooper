## ADDED Requirements

### Requirement: ChangeLength commits linear NoteOff

When ChangeLength commits a moving note end tick, the system SHALL set `NoteOff.tick = NoteOn.tick + newLength` (linear, no `% loopLength`) and SHALL normalize at the **edit pass commit boundary** before rematerialize readers observe the change.

#### Scenario: ChangeLength across loop boundary

- **WHEN** ChangeLength extends a note past `loopLength - 1` and commits
- **THEN** canonical storage has linear `NoteOff.tick` beyond `loopLength`
- **AND** rematerialize parity uses the same linear span
