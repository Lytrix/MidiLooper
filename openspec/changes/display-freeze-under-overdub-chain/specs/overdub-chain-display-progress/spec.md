## Purpose

Preserve responsive, coherent piano-roll feedback while rapid overdub stops retire, without sacrificing accepted recording interactions or MIDI service.

**Status 2026-10-29 (DEC-193):** this capability delta is frozen with the display packet. The first
requirement below records the historical eventual-recovery behavior; it is **not** playback acceptance and is
not the active contract. Active playback work is
[`playback-regression-after-retained-start-fix`](../../../playback-regression-after-retained-start-fix/specs/playback-servicing-regression/spec.md).

## ADDED Requirements

### Requirement: Playing passes-only slots have a source recovery producer

*Historical / eventual-recovery only. Does not satisfy startup or continuous playback; see the status note above.*

A playing enabled slot with committed content but no published playback root SHALL NOT remain
permanently silent solely because no publication request exists. With no live capture, pending
stop, publication or session-exit claim owner, the existing deferred Snapshot owner SHALL request
on-demand publication from the committed inventory. Existing publication work SHALL NOT be restarted.
No re-record, slot removal, bulk inactive-slot hydration or synchronous full gather on the MIDI tick
is required or permitted by this recovery.

#### Scenario: Multiple playing restored slots lack roots

- **WHEN** enabled active or layered slots on playing Tracks have committed passes but no root or publisher
- **THEN** their existing Snapshot owner requests and completes source recovery
- **AND** playback emits their committed MIDI identities through the existing readers
- **AND** disabled or stopped slots do not acquire a recovery request merely because they are loaded

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
