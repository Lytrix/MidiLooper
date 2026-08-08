# internal-heap-external-memory-routing

Cold NOTE_EDIT and projection buffers SHALL use the external memory pool first so internal heap headroom remains for admission gates and hot paths.

Shipped 2026-07-06. Guide: [`docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](../../../docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md).

## Requirements

### Requirement: External-memory-first cold buffers

Length-scaling buffers that are not on the MIDI-clock or per-CC fader hot path SHALL use `ExternalMemoryFirstAllocator` (or `SessionMidiEventVec`) so they prefer `extmem_malloc` before internal `malloc`.

This applies at minimum to:

- `NoteEditCurrentState` row storage and current-state snapshot/delta payloads,
- `NoteEditFocus::baselineMap` and `overlapNotes`,
- `NoteEditSessionUndoStack` entry storage
- `CowLoopEventStore` session flat cache during NOTE_EDIT
- `DisplayManager::liveDisplayEventBuffer`
- `MemoryPool::globalMidiEventPool` reservation (after `init()` in `setup()`)
- `buildCanonicalSpansFromMidi` and `IntervalProjection` batch temporaries

#### Scenario: NOTE_EDIT current state does not malloc-first full-loop rows

- **WHEN** note edit current state is built or restored during NOTE_EDIT
- **THEN** row storage SHALL be backed by `ExternalMemoryFirstAllocator` when PSRAM is available
- **AND** internal heap free SHALL not drop by a full-loop current-state row copy solely from current-state construction

#### Scenario: NOTE_EDIT reconstruct does not malloc-first a full-loop span copy

- **WHEN** `buildCanonicalSpansFromMidi` runs during NOTE_EDIT display or overlap analysis
- **THEN** canonical span and projection batch vectors SHALL be backed by `ExternalMemoryFirstAllocator`
- **AND** internal heap free SHALL not drop by a full-loop `MidiEvent` vector copy per fader CC solely from span rebuild

#### Scenario: Session undo stack entries in external pool

- **WHEN** `NoteEditSessionUndoStack::pushEntry` appends a `SessionUndoEntry`
- **THEN** the stack's `entries_` vector SHALL live in the external memory pool when PSRAM is available
- **AND** entry count SHALL not be trimmed solely because internal heap dropped while external pool still has space

#### Scenario: Current-state debug verification is cold path

- **WHEN** debug verification checks current-state projection parity
- **THEN** the check SHALL NOT run from MIDI-clock or per-CC fader hot paths
- **AND** it SHALL NOT walk external-pool diagnostics from hot edit geometry code

### Requirement: Baseline map edit-closure scope

`NoteEditFocus::baselineMap` SHALL hold baselines for the edit closure only (moving note and overlap notes), not every note in the materialized loop.

`populateBaselineMapForEditClosure` SHALL run after focus rebuild on note select paths that rebind the moving note.

#### Scenario: F1 reselect after geometry move

- **WHEN** the user reselects a note via fader1 after a geometry edit
- **THEN** `baselineMap` SHALL include the moving note and overlap closure entries
- **AND** SHALL NOT repopulate baselines for all notes in the loop

### Requirement: Session undo snapshot trims focus maps

`snapshotFocusForSessionUndo` SHALL copy only moving-note and overlap-related `baselineMap` entries required to restore undo, not the full pre-trim map.

#### Scenario: Undo push at geometry kind boundary

- **WHEN** `pushSessionUndoOnKindChange` or `buildSessionUndoEntry` snapshots focus
- **THEN** the stored `SessionUndoEntry.focus.baselineMap` SHALL contain trimmed closure baselines
- **AND** overlap baselines SHALL be recoverable from `overlapNotes` when absent from the map

### Requirement: Split-tier session undo admission

`canHeapAdmitSessionUndoEntry` SHALL estimate internal and external bytes separately. Internal admission SHALL use `MemoryMonitor::getInternalHeapFreeBytes()` against `HEAP_RESERVE_BYTES` plus internal payload only. When the external memory pool is available, external payload SHALL be checked against `getExternalMemoryPoolFreeBytes()` and SHALL NOT be folded into the internal threshold.

When the external memory pool is unavailable, external estimates SHALL be required from internal heap (native / no PSRAM).

#### Scenario: Admit with low internal but sufficient PSRAM

- **WHEN** internal free is above `HEAP_RESERVE_BYTES` plus internal entry estimate
- **AND** external pool free is above external map estimate
- **THEN** `pushEntry` SHALL succeed
- **AND** undo depth SHALL not be trimmed solely for internal pressure from external-map storage

#### Scenario: Reject when external pool exhausted

- **WHEN** PSRAM is available but `getExternalMemoryPoolFreeBytes()` is below the entry's external estimate
- **THEN** `pushEntry` SHALL fail with a logged warning
- **AND** stack size SHALL be unchanged

### Requirement: Hot path stays non-allocating

Playback sort and MIDI send gates SHALL NOT call allocating UIP batch APIs per event or inside sort comparators.

#### Scenario: Playback order rebuild

- **WHEN** `rebuildPlaybackOrder` sorts events
- **THEN** the comparator SHALL use `playbackEventPhase` (scalar)
- **AND** SHALL NOT call `generateEquivalentIntervals` or allocate per comparison

### Requirement: Flat cache discard on failed undo push

When `NoteEditSessionUndoStack::pushEntry` fails after pass reclaim, firmware SHALL call `editSession.store.discardFlatCache()` and retry once before logging failure.

#### Scenario: Push succeeds after flat cache discard

- **WHEN** first `pushEntry` fails and reclaim runs
- **THEN** firmware SHALL discard the session flat cache and retry push once

### Requirement: Diagnostics trace on capture-serial builds

When built with `SESSION_CAPTURE`, firmware SHALL emit versioned binary diagnostic records via the existing `DebugSessionCapture` PSRAM ring without allocating on the record append path.

Records SHALL use `Diagnostics::kTraceFormatVersion` and `DiagTraceRecord` layout defined in [`include/Utils/DiagnosticsTypes.h`](../../../include/Utils/DiagnosticsTypes.h).

A fixed `DiagLastRecordSlot` in external memory SHALL preserve the last record across hard fault for boot `DIAGCHK` export.

#### Scenario: NOTE_EDIT open trace without heap perturbation

- **WHEN** `openNoteEditSession` runs on a heap-critical transition
- **THEN** inner steps MAY emit `DIAG_EVENT` (event ID + context snapshot only)
- **AND** SHALL NOT call `MemoryMonitor::logStatus`, `getLargestFreeBlock`, or `Serial.printf` inside the open path
- **AND** heap snapshots SHALL occur at most once at enter or exit, or from main loop after return

#### Scenario: Ring flush defers Serial I/O

- **WHEN** diagnostic records are appended during FLASHMEM producers
- **THEN** append SHALL write to the PSRAM ring only
- **AND** USB serial export SHALL occur from `flushCaptureBuffer` in the main loop

#### Scenario: Parser reads format version

- **WHEN** `scripts/parse_diag_trace.py` parses `#CAP,...,DIAG,...` or `DIAGCHK` lines
- **THEN** it SHALL read `formatVersion` from the record
- **AND** unknown versions SHALL be reported for manual migration
