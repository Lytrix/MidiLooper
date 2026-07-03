## ADDED Requirements

### Requirement: Persist linear canonical ticks

When saving loop slot data, the system SHALL persist NoteOn and NoteOff ticks in canonical linear form per `linear-loop-tick-storage` invariants. NoteOff ticks MAY exceed `loopLength` up to `maxPersistedEventTick`.

#### Scenario: Save round-trip linear off

- **WHEN** a loop with `NoteOn.tick = 1344`, `NoteOff.tick = 1536`, `loopLength = 1536` is saved and loaded after dev reset
- **THEN** restored ticks match exactly

### Requirement: Load validates canonical ticks

On SD load, the system SHALL run `validateLoopEvents` on loaded loop events. Non-canonical storage SHALL fail the load with a logged reject; the system SHALL NOT normalize legacy data on load.

#### Scenario: Reject wrapped storage on load

- **WHEN** a slot file contains a NoteOff stored at wrapped tick `0` paired with tail NoteOn at `1345` without linear span semantics
- **THEN** load fails with `LOAD_REJECT non-canonical tick storage`

#### Scenario: Canonical load succeeds

- **WHEN** a slot file saved under linear-tick rules is loaded
- **THEN** load succeeds and `validateLoopEvents` passes
