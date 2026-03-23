---
name: Fix wrapped note playback
overview: Fix the root cause of stuck notes by introducing a playback-order index that sorts events by their wrapped tick, so `playMidiEvents` fires wrapped note-offs at the correct time in the loop.
todos:
  - id: track-h-members
    content: Add playbackOrder, playbackOrderDirty, and rebuildPlaybackOrder() declaration to Track.h private section
    status: completed
  - id: invalidate-caches
    content: Add playbackOrderDirty = true to invalidateCaches() in Track.h
    status: completed
  - id: rebuild-impl
    content: Implement rebuildPlaybackOrder() in Track.cpp
    status: completed
  - id: play-midi-events
    content: Update playMidiEvents() to use playbackOrder instead of direct midiEvents iteration
    status: completed
isProject: false
---

# Fix Wrapped Note-Off Playback (Root Cause)

## Problem

`playMidiEvents()` in [Track.cpp](src/Track.cpp) iterates `midiEvents` in original-tick order (ascending by `evt.tick`). For each event, it computes `evTick = evt.tick % loopLengthTicks`. When a note-off has an original tick beyond loop length (e.g., tick 2585 with loop length 2304), its `evTick` is 281 — but the event is at the **end** of the array. The `break` condition (`evTick > tickInLoop`) fires on earlier events with larger wrapped ticks, so the wrapped note-off is never reached.

`NoteUtils::reconstructNotes()` (used for display) handles wrapping correctly because it pairs events using stacks rather than linear iteration. `NoteMovementUtils::wrapPosition()` also correctly handles wrapping. The playback loop is the only code path that doesn't.

```mermaid
flowchart TD
    A["midiEvents sorted by original tick"] --> B["Event at index 11: tick=2585, evTick=281"]
    A --> C["Event at index 0: tick=901, evTick=901"]
    D["playMidiEvents iterates from index 0"] --> E["index 0: evTick=901 > tickInLoop=300 → BREAK"]
    E --> F["index 11 never reached → NoteOff skipped → STUCK NOTE"]
```

## Solution: Playback Order Index

Add a **playback order index** — a `std::vector<size_t>` of indices into `midiEvents`, sorted by wrapped tick (`evt.tick % loopLengthTicks`). This follows the existing cache/invalidation pattern already in the codebase (`invalidateCaches()`).

```mermaid
flowchart TD
    A["playbackOrder sorted by evTick"] --> B["playbackOrder[0] → index 11 (evTick=281)"]
    A --> C["playbackOrder[1] → index 0 (evTick=901)"]
    D["playMidiEvents iterates playbackOrder"] --> E["pos 0: evTick=281 ≤ 300 → FIRES NoteOff"]
    E --> G["pos 1: evTick=901 > 300 → break"]
    G --> H["Note-off sent correctly"]
```

## Changes

### 1. [include/Track.h](include/Track.h) — Add playback order members

In the private section (near line 240, alongside existing caches):

```cpp
std::vector<size_t> playbackOrder;
bool playbackOrderDirty = true;
```

Add a private method declaration:

```cpp
void rebuildPlaybackOrder();
```

### 2. [include/Track.h](include/Track.h) — Extend `invalidateCaches()`

In the existing `invalidateCaches()` inline method (line 182), add:

```cpp
playbackOrderDirty = true;
```

### 3. [src/Track.cpp](src/Track.cpp) — Implement `rebuildPlaybackOrder()`

New method that builds a vector of indices `[0..midiEvents.size()-1]` sorted by `midiEvents[i].tick % loopLengthTicks` (using `NoteMovementUtils::wrapPosition` for consistency with existing patterns):

```cpp
void Track::rebuildPlaybackOrder() {
  playbackOrder.resize(midiEvents.size());
  for (size_t i = 0; i < midiEvents.size(); i++) {
    playbackOrder[i] = i;
  }
  uint32_t ll = loopLengthTicks;
  std::sort(playbackOrder.begin(), playbackOrder.end(),
    [&](size_t a, size_t b) {
      uint32_t ta = midiEvents[a].tick % ll;
      uint32_t tb = midiEvents[b].tick % ll;
      return ta < tb;
    });
  playbackOrderDirty = false;
}
```

### 4. [src/Track.cpp](src/Track.cpp) — Update `playMidiEvents()` (line 563)

- At the top: if `playbackOrderDirty`, call `rebuildPlaybackOrder()`
- Replace `midiEvents[nextEventIndex]` with `midiEvents[playbackOrder[nextEventIndex]]`
- Replace `midiEvents.size()` loop bound with `playbackOrder.size()`

The rest of the logic (wrap detection, window check, `nextEventIndex` management) stays identical.
