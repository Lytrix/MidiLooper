## Purpose

Keep the silent-playback defect scoped as a regression at the DEC-191 image boundary: restore the playback
servicing that the last-good image provided, keep the retained-Start refusal cheap, and refuse to substitute a
new playback architecture for the missing producer.

## ADDED Requirements

### Requirement: Playback servicing returns to last-good behavior for the pinned slot shape

For a slot with committed content on a playing, enabled Track that played on the last-good image, production
Start plus ordinary deferred owner turns SHALL make the first due MIDI event available at its declared
deadline, and the following intervals SHALL continue without a second Start, Stop/restart, frozen clock, or
new whole-loop publication as a precondition. The repair SHALL be demonstrated as a difference from the
first-bad behavior on the same fixture, not accepted because eventual recovery later produces audio.

#### Scenario: Passes-only slot on a pre-regression body

- **WHEN** a playing enabled slot with committed content is started through the production admission path while the clock advances
- **THEN** the first due On/Off is emitted with its exact NoteId, tick, pitch, channel and velocity
- **AND** the fixture reds when the producing opportunity is removed and greens when it is restored
- **AND** satisfying the fixture only through the historical on-demand whole-root recovery does not count as a pass

#### Scenario: Regression attributed to a boundary

- **WHEN** the first-bad behavior is compared with the last-good image
- **THEN** the evidence names the first missing producer or owner opportunity and its refusing predicate
- **AND** an unreproduced or data-path-only outcome is reported as such instead of being repaired speculatively

### Requirement: An unadmittable retained Start stays cheap

While a retained Start cannot be admitted and its reviewed refusal identity is unchanged, the system SHALL NOT
re-enter the expensive preparation body on every main-loop turn. The accepted request SHALL be retained, the
wake SHALL follow the reviewed refusal identity, and playback on other sounding slots SHALL continue.

#### Scenario: Unchanged refusal across repeated owner turns

- **WHEN** the refusal tuple stays unchanged for many consecutive owner turns
- **THEN** the preparation body entry counter does not advance for that request
- **AND** the exact accepted Start is still retained and can be admitted when the tuple moves
- **AND** other sounding slots keep emitting their due events

### Requirement: The regression repair does not become a playback redesign

The change SHALL modify only the boundary that is demonstrated missing. It SHALL NOT introduce a new playback
reader, window producer, standby pool, root-independent emitter, source-lifetime contract or persistence
representation as a way to make the fixture pass. If the narrow repair proves impossible because last-good
playback relied on unsafe behavior, the change SHALL stop and request an explicit decision rather than
proceeding.

#### Scenario: Patch exceeds the demonstrated boundary

- **WHEN** a proposed repair adds a new playback owner, source type or scheduling contract
- **THEN** the change is rejected as out of scope even if the playback oracle passes
- **AND** the parked root-independent direction is reopened only by an explicit owner decision
