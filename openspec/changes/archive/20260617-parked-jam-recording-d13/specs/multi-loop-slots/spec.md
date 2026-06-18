## MODIFIED Requirements

### Requirement: Per-slot record and overdub lifecycle

Each slot SHALL support the same record/overdub/clear semantics as the historical
single-loop track model, scoped to that slot's `Loop`, **including** arrangement
capture where view slot and `recordTargetSlot` may diverge.

#### Scenario: Active slot drives default playback and capture

- **WHEN** a slot is selected as active outside arrangement capture
- **THEN** playback and default record/overdub target that slot's loop data

#### Scenario: Arrangement capture overrides default target

- **WHEN** arrangement capture is armed with `recordTargetSlot` = S
- **THEN** new captured events are stored in slot S regardless of view slot
