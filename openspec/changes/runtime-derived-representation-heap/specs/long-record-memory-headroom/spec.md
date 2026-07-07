## MODIFIED Requirements

### Requirement: RAM2 safety-floor admission guard

Growth-heavy and persistence operations SHALL consult internal heap free bytes before allocating,
and SHALL defer rather than exhaust internal heap when below the floor. The MIDI clock, note-out,
and playback path SHALL be exempt and never blocked by this guard.

**Admission at dispatch:** `processDeferredSaveState` SHALL gate new deferred-save dispatch using
**current** `MemoryMonitor::getInternalHeapFreeBytes()`, not a frozen stop-path snapshot alone.
The optional `admissionHeap` passed to `requestDeferredSaveState` SHALL be used for telemetry
(`PERS,request`, `RECS` stages) only.

Once deferred save is `inProgress`, slice execution SHALL NOT re-gate on heap (unchanged).

#### Scenario: Below-floor non-critical work defers

- **WHEN** current internal heap is below the safety floor and a non-time-critical persistence
  dispatch is requested
- **THEN** the step defers with `PERS,defer,...,heap_floor` and retries on a later idle iteration
- **AND** the firmware does not reach the allocator's both-exhausted `abort()` path

#### Scenario: Dispatch proceeds when heap recovers after stop

- **WHEN** record stop sampled internal heap below the floor
- **AND** current internal heap rises above the floor before dispatch on a later idle tick
- **THEN** deferred save SHALL dispatch without indefinite `heap_floor` deferral

#### Scenario: Time-sensitive data is never blocked by the guard

- **WHEN** internal heap is below the safety floor
- **THEN** MIDI clock handling, note output, and loop playback continue without being gated

### Requirement: Length-scaling buffers reside in PSRAM first

Length-scaling, non-time-critical buffers SHALL be allocated PSRAM-first so that record length
does not consume the internal `malloc` heap before the external memory pool is used.

This applies at minimum to the per-loop note cache, playback order vector, **`passesMaterializedStore_`
published flat cache**, `materializeToFlat` output buffers, undo snapshot length-scaling vectors,
NoteEditFocus maps, NoteEditSessionUndoStack entries, session flat cache, DisplayManager live
display event buffer, MemoryPool::globalMidiEventPool`, and UIP batch temporaries.

#### Scenario: 64-bar record stop-path nadir vs post-seal telemetry

- **WHEN** a 64-bar record pass is stopped
- **THEN** firmware MAY report internal heap below the safety floor at stop-path nadir
- **AND** serial telemetry SHALL include `heap_before`, `heap_after`, and min-ever watermark where
  `SESSION_CAPTURE` is enabled
- **AND** stop→PLAY SHALL complete without USB disconnect within the HITL heartbeat window

#### Scenario: 64-bar record stop→PLAY completes

- **WHEN** a 64-bar record-only run transitions `STOPPED_RECORDING → PLAYING`
- **THEN** USB serial heartbeat continues for the configured long-run timeout
- **AND** overdub arming remains available without requiring stop-entry heap above the floor

#### Scenario: Hot path stays in fast RAM

- **WHEN** length-scaling buffers are re-targeted to external memory pool
- **THEN** MIDI clock, note-out, and playback servicing are not moved to PSRAM-resident hot buffers
  solely as a side effect
