## Why

Capture stop paths need bounded pair sanity (incremental during record/overdub + light verify at hot stop) without running full-loop `validateAndCleanupMidiEvents` on stop. Q16 **NoteMinLength** and orphan repair belong on the capture tier, separate from NOTE_EDIT **EditSessionAction** geometry.

Q9 **boundary split** at capture pass edges is deferred; v1 ships incremental sanity + Q16 + verify.

## What Changes

- **`CaptureIncrementalSanity`** — pair-close repair, wrap-window slice, budget slice during capture
- **Hot stop** — `removePairsShorterThanNoteMinLength` + `verifyCaptureHotStop` after `LoopStopFinalize`
- **`LoopEventValidation::repairOrphanNoteEvents`** — shared orphan repair for incremental + idle fallback
- **Tests** — `test_capture_incremental_sanity`, `test_capture_note_min_length`

## Impact

- **Files:** `src/Utils/CaptureIncrementalSanity.cpp`, `src/Track.cpp`, `src/Loop.cpp`, `src/main.cpp`, `src/Utils/LoopEventValidation.cpp`
- **Guide:** [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- **Plan:** [`docs/Plans/capture_pass_note_min_length_refinement.md`](../../../docs/Plans/capture_pass_note_min_length_refinement.md)

## Out of scope (v1)

- Q9 boundary split at pass materialization
- Full-loop sort/repair at stop
- SD-load validate wiring
- Synthetic open-tail offs during capture
