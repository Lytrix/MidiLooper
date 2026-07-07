## MODIFIED Requirements

### Requirement: External-memory-first cold buffers

Length-scaling buffers that are not on the MIDI-clock or per-CC fader hot path SHALL use
`ExternalMemoryFirstAllocator` (or `SessionMidiEventVec`) so they prefer `extmem_malloc` before
internal `malloc`.

This applies at minimum to:

- `NoteEditFocus::baselineMap` and `overlapNotes`
- `NoteEditSessionUndoStack` entry storage
- `CowLoopEventStore` session flat cache during NOTE_EDIT
- **`Loop::passesMaterializedStore_` published flat cache**
- `DisplayManager::liveDisplayEventBuffer`
- `MemoryPool::globalMidiEventPool` reservation (after `init()` in `setup()`)
- `buildCanonicalSpansFromMidi` and `IntervalProjection` batch temporaries

#### Scenario: Published loop flat cache in external pool

- **WHEN** idle maintenance seeds or `Loop::midiEvents()` materializes the published pass view
- **THEN** the flat vector backing `passesMaterializedStore_` SHALL use `SessionMidiEventVec`
- **AND** internal heap SHALL not drop by a full-loop `MidiEventVec` copy solely from published flat

#### Scenario: Merge temporaries follow output allocator

- **WHEN** `mergeActiveCapturePassesInto` or `mergeMaterializedPassesWithCapture` writes into
  `SessionMidiEventVec`
- **THEN** intermediate `layer` and `merged` temporaries SHALL use the output vector's allocator
- **AND** internal heap SHALL not receive full-loop merge copies when output is extmem-backed

#### Scenario: NOTE_EDIT reconstruct does not malloc-first a full-loop span copy

- **WHEN** `buildCanonicalSpansFromMidi` runs during NOTE_EDIT display or overlap analysis
- **THEN** canonical span and projection batch vectors SHALL be backed by `ExternalMemoryFirstAllocator`
- **AND** internal heap free SHALL not drop by a full-loop `MidiEvent` vector copy per fader CC
  solely from span rebuild

## ADDED Requirements (M5 spike — follow-up, not yet implemented)

### Requirement: Pass clone and SD restore use external-memory flat

Length-scaling temporaries on SD load, undo restore, and pass snapshot clone SHALL NOT use
`MidiEventVec` for full-loop flatten when the operation scales with loop event count.

#### Scenario: deepCloneChunkRefs uses SessionMidiEventVec

- **WHEN** `deepCloneChunkRefs` materializes chunk refs for undo restore or `sharePassesSnapshot`
- **THEN** the flatten buffer SHALL use `SessionMidiEventVec` (or equivalent extmem-first allocator)
- **AND** internal heap SHALL not drop by one full-loop copy per pass solely from pass clone

#### Scenario: SD load adopts chunk refs without deep clone

- **WHEN** `applySnapshotToLoop` runs after `readPersistedLoopSnapshot` from SD
- **THEN** the live loop SHALL adopt snapshot `chunkRefs` via move (`adoptPersistedSnapshot`)
- **AND** pool chunk count SHALL NOT double from an additional `deepClonePasses` on load
- **AND** undo restore SHALL continue to use `restorePassesSnapshot` with deep clone

#### Scenario: Visual cache rebuild deferred on load

- **WHEN** `Loop::adoptPersistedSnapshot` or `restorePassesSnapshot` completes after SD load
- **THEN** display visual cache MAY remain dirty until idle maintenance rebuilds it
- **AND** boot load SHALL NOT synchronously require full-loop internal-heap materialize for display
  when Phase C stale-while-revalidate policy applies

#### Scenario: Boot load heap budget

- **WHEN** boot recovery loads a current set containing a 64-bar loop with record and overdub passes
- **THEN** internal heap free after load SHALL remain above `INTERNAL_HEAP_SAFETY_FLOOR_BYTES` or
  persistence admission SHALL defer until idle maintenance frees headroom
- **AND** HITL clear-to-empty preconditions SHALL not fail solely because load exhausted internal heap

#### Scenario: Load current set bundle and active loop slots at boot

- **WHEN** `loadCurrentSetBundleAndActiveLoopSlots` restores the current workspace from SD
- **THEN** the firmware SHALL read the runtime bundle (transport + slot metadata + undo metadata)
- **AND** SHALL restore loop slot payloads only for enabled, active, and selected slots
- **AND** SHALL queue remaining slot payloads for `processDeferredLoopSlotRestore` in idle

#### Scenario: Defer undo snapshot bodies at boot

- **WHEN** the runtime bundle footer contains global undo stacks with loop snapshots
- **THEN** boot restore SHALL read undo entry metadata without loading snapshot bodies into RAM
- **AND** SHALL hydrate snapshot bodies per track via `processDeferredUndoSnapshots` in idle or before first undo
