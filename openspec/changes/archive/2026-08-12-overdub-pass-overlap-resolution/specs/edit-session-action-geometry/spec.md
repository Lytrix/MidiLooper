## ADDED Requirements

### Requirement: Overdub reuses constrained geometry decisions

When overdub overlap resolution evaluates an incoming note against immutable source-pass geometry, the system SHALL obtain shorten / hide / min-length outcomes from the same constrained-geometry decision rules used by NOTE_EDIT (`resolveConstrainedGeometry` semantics and shared `noteMinLengthTicks` / `noteMinLengthRemoveEnabled` globals). The system MUST NOT maintain a second, contradictory capture-only overlap policy for those decisions. Encoding of resulting actions onto a pending `overdubPass` MAY use a different apply owner than `applyEditSessionActions` on `NoteEditSession.store`, provided the decided geometry outcomes match.

#### Scenario: Shared min-length hide decision

- **GIVEN** constrained geometry would hide a target note under NOTE_EDIT because residual length is below `noteMinLengthTicks` with remove enabled
- **WHEN** overdub overlap resolution evaluates an equivalent causing/target pair
- **THEN** overdub decides hide/remove for that target as well
- **AND** the decision is not overridden by capture-store duplicate detection alone
