## Purpose

Preserve responsive, coherent piano-roll feedback while rapid overdub stops retire, without sacrificing accepted recording interactions or MIDI service.

## ADDED Requirements

### Requirement: Overdub chains preserve display service

During an admitted overdub chain, the system SHALL continue servicing due display phases and completing frames without waiting for the chain or the entire stop backlog to end. Coalescing while a framebuffer transfer is busy SHALL preserve the newest pending frame rather than queue stale frames.

#### Scenario: Backlogged stops and a moving playhead

- **WHEN** repeated overdub stops create a backlog while a successor records and the display is due
- **THEN** display processing continues during the chain
- **AND** the visible playhead and qualified note content advance before the chain ends
- **AND** accepted starts and stops remain serviceable without losing their content

### Requirement: Display freshness preserves source qualification

The system SHALL compose each frame from sources qualified for its track, slot and covered interval. A missing replacement source SHALL retain the existing coherent frame without suppressing future display or bounded source opportunities. The system SHALL NOT obtain apparent freshness by dropping recorded identities or mixing unrelated generations.

#### Scenario: Publication is deferred during a successor capture

- **WHEN** committed snapshot publication is unavailable while a successor capture is live
- **THEN** existing qualified committed content and permitted capture overlays remain available to the display
- **AND** newly qualified content can replace the held frame without requiring transport Stop

#### Scenario: Focus or committed content changes

- **WHEN** focus, loop geometry, clear, or undo invalidates a source
- **THEN** stale content is not advertised as current content for the new identity
- **AND** display servicing remains eligible while the appropriate replacement is prepared

### Requirement: Regression evidence distinguishes service from transitions

Verification SHALL distinguish display admission, resolution, frame submission and completion from content or viewport transitions. Absence of transition-only telemetry SHALL NOT be accepted as evidence of a display freeze. Display and overdub-throughput acceptance SHALL be recorded separately and both SHALL pass before closeout.

#### Scenario: Repeated fresh frames in the same viewport

- **WHEN** consecutive completed frames have an unchanged published viewport and transition telemetry is suppressed
- **THEN** the verification oracle still recognizes frame progress
- **AND** it does not report a freeze solely because the transition receipt is absent

#### Scenario: Visible regression is reproduced

- **WHEN** a repeatable chain produces a display hang
- **THEN** the evidence identifies the last progressing display boundary and the blocking condition or unresolved evidence gap
- **AND** a repair is not accepted solely because stop throughput passes or burst limits were reduced
