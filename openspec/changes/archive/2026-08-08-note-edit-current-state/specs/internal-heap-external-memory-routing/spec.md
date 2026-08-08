## MODIFIED Requirements

### Requirement: External-memory-first cold buffers

Length-scaling buffers that are not on the MIDI-clock or per-CC fader hot path SHALL use `ExternalMemoryFirstAllocator` (or `SessionMidiEventVec`) so they prefer `extmem_malloc` before internal `malloc`.

This applies at minimum to:

- `NoteEditCurrentState` row storage and current-state snapshot/delta payloads,
- `NoteEditFocus::baselineMap` and `overlapNotes`,
- `NoteEditSessionUndoStack` entry storage,
- `CowLoopEventStore` session flat cache during NOTE_EDIT,
- `DisplayManager::liveDisplayEventBuffer`,
- `MemoryPool::globalMidiEventPool` reservation (after `init()` in `setup()`),
- `buildCanonicalSpansFromMidi` and `IntervalProjection` batch temporaries.

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
