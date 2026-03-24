---
name: Stuck note and Clock log fix
overview: "Fix two issues: (1) Orphaned note-ons when shortening the loop by sending all-notes-off when loop boundaries change, and (2) Reduce MIDI log noise by skipping Clock messages so notes and other events are visible."
todos: []
isProject: false
---

# Fix Stuck Notes and MIDI Clock Log Noise

## Problem 1: Orphaned note-on when shortening loop

**Root cause:** When loop length or start/end changes, events are remapped via `evt.tick % loopLengthTicks` during playback. A note-on that wraps around the old loop can end up with its note-off firing before the note-on (or outside the visible range), leaving the note sounding indefinitely.

**Current behavior:**

- [src/Track.cpp](src/Track.cpp) `setLoopLength`, `setLoopLengthWithWrapping`, `setLoopStartAndEnd`, and `setLoopStartTick` only update bounds and invalidate caches. They never call `sendAllNotesOff()`.
- `validateAndCleanupMidiEvents()` runs only on `stopRecording()`, not when loop bounds change during playback.
- `sendAllNotesOff()` exists and is used in `stopPlaying()` (line 412).

**Fix:** Call `sendAllNotesOff()` inside each loop-boundary mutator so any sounding notes are killed immediately when the loop is shortened or shifted. This is safe regardless of track state (sending CC 123 All Notes Off is idempotent).

**Files to change:**

- [src/Track.cpp](src/Track.cpp): Add `sendAllNotesOff()` in:
  - `setLoopLength` (line 661)
  - `setLoopLengthWithWrapping` (line 666, after loop length update)
  - `setLoopStartAndEnd` (line 712, after bounds update)
  - `setLoopStartTick` (line 692, after start tick update)

---

## Problem 2: MIDI notes hidden by Clock log spam

**Root cause:** Every incoming MIDI message is logged in `handleMidiMessage` (line 106), including Clock. At 24 PPQN, Clock produces 24 log lines per beat, drowning out NoteOn, NoteOff, CC, and PitchBend.

**Current behavior:** Unconditional log at [src/MidiHandler.cpp](src/MidiHandler.cpp) line 106:

```cpp
logger.log(CAT_MIDI, LOG_DEBUG, "%s MIDI: type=%s ch=%d d1=%d d2=%d", ...);
```

**Fix:** Skip logging for `type == midi::Clock` so only channel-voice and transport messages are logged. The note detail block (lines 110–116) already only runs for NoteOn/NoteOff, so no change there.

**Files to change:**

- [src/MidiHandler.cpp](src/MidiHandler.cpp): Wrap the main log call in `handleMidiMessage` with `if (type != midi::Clock)` so Clock messages are not logged.

