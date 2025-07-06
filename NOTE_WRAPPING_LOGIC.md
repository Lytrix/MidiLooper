# Note Wrapping and Loop Shortening Logic

## Overview

This document explains the note reconstruction logic used in the MIDI looper when loop lengths are dynamically changed. The system handles both loop extension and shortening while preserving musical integrity.

## Core Concepts

### Display Notes vs MIDI Events

- **MIDI Events**: Raw note-on/note-off events stored with absolute tick positions
- **Display Notes**: Reconstructed note objects with start/end positions for UI display
- **Key Principle**: Original MIDI events are never modified; wrapping is calculated dynamically

### Loop Boundary Handling

The system distinguishes between two scenarios:

1. **Loop Extension** (e.g., 3 bars → 5 bars): All existing notes remain valid
2. **Loop Shortening** (e.g., 5 bars → 2 bars): Notes beyond the new boundary need special handling

## Algorithm Flow

### 1. Note-On Event Processing

```cpp
if (noteOnTick >= loopLength) {
    // CRITICAL: Discard notes that start beyond current loop boundary
    logger.log("Discarding note-on beyond loop boundary");
    continue;
}
```

**Design Decision**: When shortening loops, notes that start beyond the new loop boundary are completely discarded rather than wrapped. This prevents overcrowding and maintains musical coherence.

### 2. Note-Off Event Processing

```cpp
if (noteOffTick >= loopLength) {
    // Wrap note-off position for notes that extend beyond loop
    noteOffTick = noteOffTick % loopLength;
    logger.log("Wrapped note-off");
}
```

**Design Decision**: Note-off events are wrapped because they belong to notes that started within the valid loop range but extend beyond it.

### 3. Deduplication

```cpp
std::set<std::tuple<uint8_t, uint32_t, uint32_t>> seenNotes;
auto key = std::make_tuple(note.note, note.startTick, note.endTick);
```

**Purpose**: Removes exact duplicates that might occur during complex wrapping scenarios.

## Examples

### Scenario 1: Loop Extension (3 bars → 5 bars)

```
Original: 3 bars (2304 ticks)
New:      5 bars (3840 ticks)

All notes remain unchanged:
- Note at tick 1000 → stays at tick 1000
- Note at tick 2000 → stays at tick 2000
- No wrapping occurs
```

### Scenario 2: Loop Shortening (5 bars → 2 bars)

```
Original: 5 bars (3840 ticks)
New:      2 bars (1536 ticks)

Note Processing:
- Note-on at tick 1000 → kept (within new boundary)
- Note-on at tick 2000 → DISCARDED (beyond new boundary)
- Note-off at tick 2500 → wrapped to tick 964 (2500 % 1536)
```

### Scenario 3: Extreme Shortening (5 bars → 1 bar)

```
Original: 5 bars (3840 ticks)
New:      1 bar (768 ticks)

Note Processing:
- Note-on at tick 100 → kept
- Note-on at tick 900 → DISCARDED (900 >= 768)
- Note-on at tick 1500 → DISCARDED
- Note-on at tick 2200 → DISCARDED
- Note-on at tick 3000 → DISCARDED

Result: Only notes that originally started in the first bar remain
```

## Key Design Decisions

### 1. Discard vs Wrap for Note-On Events

**Decision**: Discard notes that start beyond the new loop boundary.

**Rationale**: 
- Prevents musical overcrowding
- Maintains temporal relationships between notes
- Avoids creating artificial overlaps

**Alternative Considered**: Wrap all note-on events using modulo
- **Problem**: Would cram multiple bars worth of notes into a single bar
- **Result**: Unmusical dense note clusters

### 2. Wrap Note-Off Events

**Decision**: Always wrap note-off events using modulo.

**Rationale**:
- Completes notes that legitimately started within the loop
- Preserves the "cut-off" behavior for notes that extend beyond the loop
- Maintains note pairing integrity

### 3. Preserve Original MIDI Data

**Decision**: Never modify the original MIDI events.

**Rationale**:
- Allows dynamic loop length changes without data loss
- Enables "undo" functionality
- Maintains data integrity for future loop extensions

## Implementation Details

### Data Structures

```cpp
struct DisplayNote {
    uint8_t note;        // MIDI note number
    uint8_t velocity;    // Note velocity
    uint32_t startTick;  // Start position in ticks
    uint32_t endTick;    // End position in ticks
};
```

### LIFO Note Pairing

The system uses Last-In-First-Out (LIFO) pairing for note events:
- Multiple note-on events for the same pitch create a stack
- Note-off events complete the most recent note-on for that pitch
- Handles overlapping notes of the same pitch correctly

### Error Handling

```cpp
if (activeNoteStacks[pitch].empty()) {
    logger.log("Note-off without matching note-on");
    continue;
}
```

Gracefully handles malformed MIDI data where note-off events don't have corresponding note-on events.

## Performance Considerations

### Complexity
- **Time**: O(n) where n is the number of MIDI events
- **Space**: O(k) where k is the number of simultaneously active notes

### Optimization Opportunities
- **Caching**: Results are cached based on MIDI hash and loop length
- **Early Termination**: Processing stops for events beyond relevance
- **Minimal Allocations**: Reuses data structures where possible

## Testing Scenarios

### Critical Test Cases

1. **Empty Loop**: No MIDI events
2. **Single Note**: One note-on/note-off pair
3. **Overlapping Notes**: Multiple notes of same pitch
4. **Notes Without Ends**: Note-on without corresponding note-off
5. **Extreme Shortening**: Loop reduced to minimum size
6. **Multiple Shortenings**: Repeated loop length reductions

### Expected Debug Output

```
[DEBUG] Reconstructing notes with loop length: 1536 ticks
[DEBUG] Note-on: pitch=52, tick=112
[DEBUG] Discarding note-on beyond loop boundary: pitch=52, tick=2416, loop=1536
[DEBUG] Wrapped note-off: pitch=52, original_tick=1713 -> wrapped_tick=177
[DEBUG] Final note: pitch=52, start=112, end=177
[DEBUG] Reconstruction complete: 14 notes total (0 duplicates removed)
```

## Integration Points

### Display System
- **File**: `DisplayManager.cpp`
- **Method**: `drawNoteBar()`
- **Responsibility**: Renders wrapped notes as two visual segments when they cross loop boundaries

### Track Management
- **File**: `Track.cpp`
- **Method**: `setLoopLength()`
- **Responsibility**: Triggers note reconstruction when loop length changes

### Caching Layer
- **File**: `NoteUtils.h`
- **Class**: `CachedNoteList`
- **Responsibility**: Avoids expensive recalculation for unchanged data

## Future Enhancements

### Potential Improvements

1. **Quantization**: Snap wrapped notes to musical boundaries
2. **Velocity Scaling**: Adjust velocity for wrapped note segments
3. **Smart Wrapping**: Preserve musical phrases when possible
4. **Undo Stack**: Track changes for complex undo operations

### Backwards Compatibility

The current implementation maintains backwards compatibility with existing MIDI files and doesn't require migration of stored data.

## Conclusion

This note wrapping system provides a robust foundation for dynamic loop length changes while maintaining musical integrity and system performance. The key insight is distinguishing between note-on events (which should be discarded when beyond boundaries) and note-off events (which should be wrapped to complete legitimate notes). 