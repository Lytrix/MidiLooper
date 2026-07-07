## ADDED Requirements

### Requirement: Playback window avoids lazy internal flat on PLAYING entry

When building the playback window after record or overdub stop, firmware SHALL use chunk-ref merge
or `LoopPasses::materializeToEventVector(SessionMidiEventVec)` when the published store is fresh.
Firmware SHALL NOT call `Loop::midiEvents()` lazy internal-heap flatten on the first PLAYING tick
after stop.

#### Scenario: 16-bar record stop→PLAY without hang

- **WHEN** a 16-bar record stops and transport enters PLAYING
- **THEN** the first playback window build completes without allocating a full-loop internal-heap
  flat vector
- **AND** USB serial heartbeat continues within 20 s

#### Scenario: Playback window uses extmem materialize when store fresh

- **WHEN** `isPassesMaterializedStoreFresh()` is true at PLAYING entry
- **THEN** `ensurePlaybackWindowBuilt` materializes into `SessionMidiEventVec`
- **AND** does not call `discardPublishedFlatCache()` immediately before reading flat

### Requirement: One full materialize per playback revision off hot path

At most one full-loop materialize per `playbackRevision` SHALL run outside the MIDI-clock hot path
(idle maintenance). Hot-path consumers SHALL read existing derived caches or window-bounded merges.

#### Scenario: Idle seeds published flat once per revision

- **WHEN** `processDeferredIdleMaintenance` runs after record stop with a bumped playback revision
- **THEN** `passesMaterializedStore` is seeded at most once until the revision changes
- **AND** PLAYING entry does not trigger a second full materialize for the same revision

### Requirement: Display may serve stale visual cache during PLAYING

During PLAYING, display MAY serve stale `visualCache.notes` while idle maintenance rebuilds
bar-slices. Full-loop visual rebuild SHALL NOT run synchronously on every frame.

#### Scenario: Stale-while-revalidate on PLAYING

- **WHEN** transport is PLAYING and `visualCacheDirty` is set
- **THEN** DisplayManager MAY paint the last valid visual cache
- **AND** `rebuildVisualCacheIdleSlice` advances rebuild in idle maintenance only

### Requirement: Tier-A capture lines do not block USB

On `SESSION_CAPTURE` builds, state transitions and persistence stages (`#CAP,ST`, `#CAP,PERS`,
`#CAP,RECS`) SHALL use ring-first capture and SHALL NOT block USB with synchronous `Serial.printf`
on the producer hot path.

#### Scenario: ST line preserved after long overdub

- **WHEN** a 64-bar overdub stops and transitions to PLAYING
- **THEN** serial capture includes `#CAP,...,ST,...,OVERDUBBING,PLAYING` or equivalent firmware
  transition text
- **AND** heartbeat is not lost due to MO burst alone
