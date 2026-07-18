## ADDED Requirements

### Requirement: Slot hydration lifecycle

The system SHALL model slot hydration as the architectural lifecycle:

`UNLOADED → HEADER_READY → COMMITTED → DERIVED_READY`

- **UNLOADED** — no committed passes; may still need SD (`needsSlotLoad`)
- **HEADER_READY** — metadata (length, bars, …) without committed passes
- **COMMITTED** — immutable passes are canonical; playback, editor, and display MAY use the slot
- **DERIVED_READY** — optional derived reconstructions finished

The system SHALL NOT require a dedicated stored enum field for this lifecycle; implementation MAY use flags, computed properties, or explicit state.

#### Scenario: Interactive ready does not require DERIVED_READY

- **WHEN** audible boot slots reach COMMITTED
- **THEN** interactive UI (including piano roll) MAY proceed
- **AND** DERIVED_READY MAY still be pending

### Requirement: Audible-first boot Commit

At cold boot, the system SHALL sync-commit the audible boot set (`isAudibleBootSlot`) before declaring interactive ready. The system SHALL NOT require every SD payload slot to reach COMMITTED before interactive ready. Boot SHALL NOT use the runtime deferred load scheduler for audible restore.

MVP MAY perform audible sync Commit via the existing `loadLoopSlotFromCurrentSetSd` path.

#### Scenario: Non-audible slots stay unloaded after boot ready

- **WHEN** boot completes interactive ready for a set with SD payloads beyond the audible set
- **THEN** audible slots are COMMITTED
- **AND** non-audible payload slots MAY remain UNLOADED or HEADER_READY
- **AND** those slots are NOT auto-enqueued for background full-set drain

#### Scenario: Boot ready faster than full-set drain baseline

- **WHEN** a representative multi-slot set boots under capture-serial
- **THEN** time from load start to interactive ready is significantly shorter than the full-set drain baseline (`session_20260718_020628.log`)
- **AND** all audible tracks produce sound on first Play

### Requirement: On-demand deferred slot load priorities

After interactive ready, deferred slot load SHALL honor only:

1. Audible slots still needing Commit (normally empty after boot)
2. Explicitly requested slots (select, focus, or equivalent user/request path)

The system SHALL NOT implement speculative adjacent or Priority-3 background fill in this change.

#### Scenario: Select unloaded slot while stopped

- **WHEN** the user selects an unloaded SD slot while transport is idle
- **THEN** the system requests deferred load for that slot
- **AND** the slot reaches COMMITTED without reboot
- **AND** display MAY paint from committed passes

### Requirement: Load while transport active deferred in MVP

While any timing-critical track is playing, recording, or overdubbing, the system MAY queue an explicit slot-load request and SHALL defer SD load work until transport is idle. Interactive mid-play SD hydration is out of scope for this change’s MVP.

#### Scenario: Select unloaded during PLAYING queues

- **WHEN** the user selects an unloaded slot while a track is PLAYING
- **THEN** the load request is retained
- **AND** SD restore for that slot does not run on the timing-critical path until idle (MVP)
