---
name: Reduce undo and lazy Loop
overview: Reduce MAX_UNDO_HISTORY from 99 to 25, then make Loop's undo/cache containers allocate lazily so we can restore MAX_LOOPS_PER_TRACK to 8 without heap/crash issues.
todos: []
isProject: false
---

# Reduce Undo to 25 and Lazy Loop Allocation

## Part 1: Reduce MAX_UNDO_HISTORY to 25

**File:** [include/Globals.h](../../include/Globals.h)

- Change line 60: `MAX_UNDO_HISTORY = 99` to `MAX_UNDO_HISTORY = 25`
- No other changes needed; `TrackUndo.cpp` already uses `Config::MAX_UNDO_HISTORY` for all history caps

---

## Part 2: Lazy Allocation for Loop

### Problem

Each `Loop` has 12 deque members and 3 cache/container members that are constructed up-front. With 64 Loops (8 tracks × 8 loops), this uses a large amount of memory and can trigger heap exhaustion or crash during construction.

### Approach

Wrap the heavy containers in `std::unique_ptr` and add accessors that allocate on first use. Callers use the accessors instead of direct members.

### Containers to make lazy


| Member                 | Type                       | Allocate when                |
| ---------------------- | -------------------------- | ---------------------------- |
| midiHistory            | dequePooledMidiEventVector | first overdub undo push      |
| midiRedoHistory        | dequePooledMidiEventVector | first overdub redo           |
| clearMidiHistory       | dequePooledMidiEventVector | first clear undo push        |
| clearMidiRedoHistory   | dequePooledMidiEventVector | first clear redo             |
| clearStateHistory      | dequeTrackState            | first clear undo push        |
| clearStateRedoHistory  | dequeTrackState            | first clear redo             |
| clearLengthHistory     | dequeuint32_t              | first clear undo push        |
| clearLengthRedoHistory | dequeuint32_t              | first clear redo             |
| clearStartHistory      | dequeuint32_t              | first clear undo push        |
| clearStartRedoHistory  | dequeuint32_t              | first clear redo             |
| loopStartHistory       | dequeuint32_t              | first loop start undo push   |
| loopStartRedoHistory   | dequeuint32_t              | first loop start redo        |
| playbackOrder          | vectorsize_t               | first `rebuildPlaybackOrder` |
| noteCache              | CachedNoteList             | first `getCachedNotes`       |
| cachedEventIndex       | EventIndex                 | first `getCachedEventIndex`  |


Keep as direct members (required for `hasData()` and basic state):

- `midiEvents`, `startLoopTick`, `loopLengthTicks`, `loopStartTick`
- `lastTickInLoop`, `nextEventIndex`, `playbackOrderDirty`
- `midiEventCountAtLastSnapshot`, `eventIndexValid`

### Implementation steps

**1. Update [include/Loop.h](../../include/Loop.h)**

- Add `#include <memory>`
- Replace each lazy member with `std::unique_ptr<...>`
- Add accessor methods that allocate on first use, e.g.:

```cpp
std::deque<MemoryPool::PooledMidiEventVector>& getMidiHistory() {
  if (!midiHistory_) midiHistory_ = std::make_unique<std::deque<...>>();
  return *midiHistory_;
}
const std::deque<...>& getMidiHistory() const {
  return const_cast<Loop*>(this)->getMidiHistory();  // or separate const impl
}
```

- Provide both const and non-const accessors where needed (e.g. for `empty()`, `size()`).

**2. Update [src/TrackUndo.cpp*](../../src/TrackUndo.cpp)*

- Replace `loop.midiHistory` with `loop.getMidiHistory()`
- Same for all other undo/cache members
- `TrackUndo::getMidiHistory(Track&)` remains; implementation returns `track.getActiveLoop().getMidiHistory()`

**3. Update [src/Track.cpp](../../src/Track.cpp)**

- `clear()`: use accessors (e.g. `getMidiHistory().clear()`) and only call if the container exists, or always use accessor (it will allocate when needed)
- `rebuildPlaybackOrder()`: use `getPlaybackOrder()` accessor
- Playback: use `getPlaybackOrder()` for read access

**4. Update [include/Track.h](../../include/Track.h)**

- `getCachedNotes()` and `getCachedEventIndex()`: use Loop accessors so `noteCache` and `cachedEventIndex` are lazy
- `invalidateCaches()`: call `noteCache.invalidate()` only if `noteCache` exists; set `eventIndexValid = false`

**5. Restore MAX_LOOPS_PER_TRACK to 8**

- In [include/Globals.h](../../include/Globals.h), change `MAX_LOOPS_PER_TRACK` from 4 back to 8

### Memory impact

- Before (per Loop): 12 deques + 1 vector + 2 cache objects ≈ 1–2 KB
- After (per Loop, idle): 12 pointers + primitives ≈ 150 bytes
- Savings: ~1.5 KB × 64 loops ≈ 96 KB

### Risk / validation

- After implementation: build, upload, and run
- Confirm: no crash on startup, 8 loop slots work
- Test: overdub undo, clear undo, loop start undo on multiple slots
- Check that StorageManager load/save still works with the new accessors

