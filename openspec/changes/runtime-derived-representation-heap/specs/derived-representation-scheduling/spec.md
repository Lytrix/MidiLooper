## ADDED Requirements

### Requirement: M6 completes DEC-016 migration without new playback architecture

M6 SHALL treat remaining playback and display pressure as **legacy callsite migration** to the
deferred/windowed derived-representation policy already defined in DEC-016. M6 SHALL NOT introduce
a parallel streaming or iterator-based playback model. A Phase 0 callsite audit SHALL complete before
runtime behaviour changes.

#### Scenario: Audit gate before firmware edits

- **WHEN** M6 implementation begins
- **THEN** a materialization callsite matrix is completed and signed off
- **AND** each callsite has documented expected behaviour (deferred, windowed, or remain materialized)

#### Scenario: Primary implementation gap closed

- **WHEN** a non-capture PLAYING track rebuilds its playback window on `playbackRevision` change
- **THEN** playback window construction follows the approved DEC-016 derived-representation policy
- **AND** does not perform unintended full materialization on the hot path

### Requirement: Derived-view layer owns representation selection

Runtime consumers (playback, display, LED lookup) SHALL NOT choose their own data representation
by calling `Loop::midiEvents()` or full flatten helpers for convenience on hot paths.
Representation selection SHALL remain owned by the derived-view layer (`gatherPublishedFlatForDerivedView`,
`visualCache`, `capturePreview`, edit session store).

#### Scenario: Hot path uses derived-view policy

- **WHEN** firmware builds playback or display data during PLAYING, RECORDING, or OVERDUBBING
- **THEN** the callsite uses deferred, windowed, or incremental derived views per DEC-016
- **AND** any full materialization is limited to documented intentional paths (edit active, stop-path, idle seed)

### Requirement: M6 exit criteria

M6 SHALL be considered complete when: all playback-related callsites are audited; every intentional
eager materialization is documented; no unintended `Loop::midiEvents()` remains on PLAYING or RECORDING
hot paths; DEC-016 policy is consistently followed; and 64-bar multi-track HITL regression passes without
architectural regression.

#### Scenario: M6 archive gate

- **WHEN** Phases A–3 merge and verification completes
- **THEN** exit criteria in the M6 plan are checked off
- **AND** architecture regression counters show hot-path legacy materialize at zero during stress
- **AND** further streaming or iterator playback designs are not started unless post-M6 measurement requires them

### Requirement: Architecture regression counters (M6 Phase A)

On SESSION_CAPTURE builds, firmware SHALL maintain lightweight permanent counters verifying
DEC-016 path usage, including at minimum: legacy hot-path materialize, deferred reuse, playback
window rebuild vs reuse, display full vs incremental rebuild. Counters SHALL NOT require the
cancelled heavy RAM1 heap telemetry approach (~50 KB).

#### Scenario: Legacy path detectable after M6

- **WHEN** multi-track PLAYING/OVERDUBBING stress runs after M6 Phase 1–3
- **THEN** hot-path legacy materialize counters read zero
- **AND** deferred reuse counters increase relative to pre-M6 baseline

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

### Requirement: Multi-track playback uses chunk-ref merge on hot path

When building the playback window for a loop that is not in live capture and has no active edit passes,
firmware SHALL use chunk-reference merge (`mergeActiveCapturePasses`) and SHALL NOT call
`Loop::midiEvents()` full materialize on every `playbackRevision` change per playing track.

#### Scenario: Background tracks playing during capture

- **WHEN** one track is RECORDING or OVERDUBBING and other tracks are PLAYING long loops
- **THEN** each background track's playback window build uses chunk-ref merge
- **AND** internal heap at overdub enter is not lower solely due to per-track full materialize on
  every clock tick

### Requirement: Display defers full rebuild during OVERDUBBING

During OVERDUBBING, DisplayManager SHALL NOT call synchronous full-loop `ensureVisualCacheBuilt`
on every display frame. Committed notes MAY be stale until idle maintenance or incremental overlay
catches up.

#### Scenario: Overdub with multi-track MO load

- **WHEN** overdub runs with multiple tracks emitting MO and loop length ≥ 64 bars
- **THEN** firmware does not enter a sustained `RING,overflow`-only serial tail (>10 s) while
  transport is active
- **AND** user can complete or stop overdub without USB reboot attributable to main-loop stall
