## Purpose

Audible-first boot Commit and on-demand deferred slot hydration so interactive ready does not
require full-set SD drain. Shipped in **unified-commit-lazy-slot-load** (device gate
`session_20260718_224607.log`; A.7 confirm `session_20260718_230145.log`).

## Requirements

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

The system SHALL NOT implement unbounded speculative fill without priority. Background fill SHALL use focus-adjacent then focus-track then forward-track ordering (`computeDeferredRestorePriority`).

#### Scenario: Select unloaded slot while stopped

- **WHEN** the user selects an unloaded SD slot while transport is idle
- **THEN** the system requests deferred load for that slot
- **AND** the slot reaches COMMITTED without reboot
- **AND** display MAY paint from committed passes

The system SHALL background-fill remaining HEADER_READY SD payloads after interactive ready, ordered by:

1. Focus selected slot
2. Left and right neighbors on the focus track (wrap)
3. Other slots on the focus track
4. Other tracks in forward cycle order from the selected track

While any track is recording or overdubbing, the system MAY queue an explicit slot-load request and SHALL defer SD load work until capture is idle.

After interactive ready, deferred restores SHALL proceed as deferred job steps (`LoadLoopJob`) executed through `DeferredJobScheduler::runFrame`: SD payload bytes MAY be read in chunks across multiple main-loop iterations, then parsed under a time budget, then Committed by `StorageManager`. Parse SHALL be time-sliced; finalize / commit-id headroom SHALL NOT walk the PSRAM pool via `sm_malloc_stats_pool`. Loop length SHALL NOT gate whether a slot may restore while PLAYING.

Focus High `LoadLoopJob` work (focused / explicitly requested slot) SHALL advance while a track is PLAYING. Background Low `LoadLoopJob` work SHALL be skipped while any track is PLAYING (`canRunBackgroundLoadLoopNow`); skipped frames SHALL leave active/parked/queue progress intact. Device gate evidence: PASS on `session_20260718_224607.log` and A.7 confirm on `session_20260718_230145.log`.

#### Scenario: Select unloaded during PLAYING hydrates via deferred job steps

- **WHEN** the user selects an unloaded slot while a track is PLAYING (no record/overdub)
- **THEN** the load request is retained and remaining payloads are enqueued for priority fill
- **AND** focus High `LoadLoopJob` for that slot MAY advance one chunk (or parse/commit) per `DeferredJobScheduler::runFrame` while PLAYING
- **AND** background Low `LoadLoopJob` fill does not advance while any track is PLAYING (`canRunBackgroundLoadLoopNow`)
- **AND** display MAY update from COMMITTED passes without requiring transport stop

#### Scenario: Select unloaded during RECORDING defers

- **WHEN** the user selects an unloaded slot while any track is RECORDING or OVERDUBBING
- **THEN** the load request is retained
- **AND** SD restore for that slot does not run until capture is idle
