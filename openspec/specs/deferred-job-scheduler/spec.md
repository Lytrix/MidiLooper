## Purpose

Non-realtime deferred **load** job frame ownership (DEC-027 Phase B). Main loop spends
budget via `DeferredJobScheduler::runFrame`; `StorageManager` owns LoadLoopJob storage,
begin, step, and Commit. Device gates: B.1 [`231510`](../../../captures/session_20260718_231510.log);
B.3/B.4 [`022107`](../../../captures/session_20260719_022107.log).

## Requirements

### Requirement: Deferred job frame owner

The system SHALL execute non-realtime deferred **load** jobs through `DeferredJobScheduler::runFrame(budgetUs)` as the main-loop entry. Domain managers SHALL submit jobs and own domain step/Commit logic. The scheduler SHALL NOT partially modify published loop state.

#### Scenario: Main loop spends budget via scheduler

- **WHEN** the main loop runs a deferred load turn with budget `budgetUs`
- **THEN** it calls `DeferredJobScheduler::runFrame(budgetUs)`
- **AND** load progress advances only inside that call (or nested domain step invoked by it)

#### Scenario: Commit remains atomic in domain owner

- **WHEN** a `LoadLoopJob` completes parsing
- **THEN** Commit (`applySnapshotToLoop` / token publish) remains owned by `StorageManager`
- **AND** the scheduler does not write Loop passes directly

#### Scenario: Scheduler selects then steps

- **WHEN** `DeferredJobScheduler::runFrame` runs
- **THEN** it calls `StorageManager::selectSubmittedLoadJobs` before `stepSubmittedLoadJobs`
- **AND** active/parked admission (including focus High while Low is parked under PLAYING) is decided before the step budget is spent

### Requirement: Phase A load policies preserved

While load execution is owned by `DeferredJobScheduler`, the system SHALL preserve:

- Demote-on-focus (park in-flight non-focus load; promote focus)
- Focus High budget while PLAYING; Low skipped while any track is PLAYING (`canRunBackgroundLoadLoopNow`)
- Atomic Commit after budgeted parse
- No PSRAM `sm_malloc_stats_pool` walk on commit-id headroom

#### Scenario: Focus load while PLAYING still advances

- **WHEN** the user selects an unloaded focus slot while a track is PLAYING and capture is idle
- **THEN** focus High load steps may run under the focus budget
- **AND** background Low load does not run until transport is idle

#### Scenario: Smoothness gate vs Phase A baseline

- **WHEN** a representative interactive session runs under capture-serial after Phase B load migration
- **THEN** button triggers remain responsive with presses (no progressive starve)
- **AND** `#CAP,LLBG,parse_us` does not reintroduce ~295ms PSRAM-stats clusters (baseline [`230145`](../../../captures/session_20260718_230145.log); Phase B confirm [`022107`](../../../captures/session_20260719_022107.log))
