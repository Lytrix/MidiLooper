## ADDED Requirements

### Requirement: validateAndCleanupMidiEvents uses check registry

Deferred idle **`validateAndCleanupMidiEvents`** SHALL invoke the shared **`LoopEventValidation`** check registry. Orphan removal MAY remain as an allowed repair action. Canonical invariant failures SHALL be logged; repair MUST NOT rewrite NoteOn/NoteOff geometry or reintroduce wrap-pair storage.

#### Scenario: Idle cleanup orphan only

- **WHEN** idle maintenance finds an orphan NoteOff with no matching NoteOn
- **THEN** the orphan event MAY be removed
- **AND** paired note spans are unchanged

#### Scenario: Idle cleanup does not canonicalize

- **WHEN** idle maintenance finds non-canonical wrapped storage
- **THEN** it logs failure via the check registry
- **AND** does not run normalize or rewrite ticks
