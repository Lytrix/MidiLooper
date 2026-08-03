# Track stop DRY refinement

Behavior-preserving extraction of shared stop helpers on `Track`. Ownership boundaries unchanged; `Loop::commitCapturePass` remains sole seal authority.

## Helpers

### `commitCaptureForStop`

Owns exactly:

1. `Loop::commitCapturePass` (once)
2. `finalizeCommitSideEffects` (once)

Must never: change transport/playback/editor state; modify pending capture buffers; absorb dirty/save/display/`startPlaying`/`setState`.

### `prepareRecordStop`

Exact statement order (not interchangeable):

```
compute rawLength / clamp warning
        ↓
loop.loopLengthTicks = computeRecordStopLengthTicks(...)
        ↓
if length > 0:
  finalizePendingNotes(currentTick)
        ↓
  dropEventsAtOrBeyondTick(loop.loopLengthTicks)
```

### `handleNoteEditFold`

Returns `true` when stop handling is fully completed and the caller must return immediately.

## Order checklist (four entry points)

| Path | Order |
|------|--------|
| `stopRecording` | `prepareRecordStop` → `pendingNotes.clear` → `commitCaptureForStop` (seal→finalize) → playback transition |
| `stopRecordingToStopped` | `prepareRecordStop` → `pendingNotes.clear` → `commitCaptureForStop` → stopped transition |
| `stopOverdubbing` | `handleNoteEditFold` (complete→return) **or** `finalizePendingNotes` → `commitCaptureForStop` → PLAYING |
| `stopOverdubbingToStopped` | `handleNoteEditFold` (complete→return) **or** `finalizePendingNotes` → `commitCaptureForStop` → STOPPED |

## Out of scope

Deferred commit FSM, cancel-site consolidation, Phase 5 renames, growing `commitCaptureForStop` beyond seal+finalize.
