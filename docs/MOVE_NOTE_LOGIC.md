# Move Note Logic Documentation

## Overview

The move note system in this MIDI looper handles the complex task of moving notes while managing overlaps, maintaining note integrity, and supporting dynamic pitch changes during movement. The system is designed to ensure that the moving note always remains intact while intelligently handling conflicts with other notes.

## Key Concepts

### Moving Note Identity
The system maintains a `MovingNoteIdentity` that tracks:
- **Original position** (`origStart`) - The note's position when movement began
- **Current position** (`lastStart`, `lastEnd`) - The note's current position during movement
- **Pitch** (`note`) - The current pitch (can change during movement)
- **Movement direction** - Whether moving left (-1) or right (+1)
- **Deleted notes** - List of notes that were temporarily removed or shortened

### Overlap Handling Philosophy
The core principle is: **The moving note is never modified - other notes adapt to it.**

When overlaps occur:
1. **Complete containment**: If an overlapping note is entirely within the moving note's new position, it's deleted
2. **Partial overlap**: The overlapping note is shortened to avoid conflict
3. **Minimum length**: Notes shortened to less than 49 ticks are deleted instead
4. **Restoration**: Previously deleted/shortened notes may be restored when the moving note moves away

## Step-by-Step Flow

### 1. Initial Setup and Validation
```cpp
void moveNoteWithOverlapHandling(Track& track, EditManager& manager, 
                                const NoteUtils::DisplayNote& currentNote, 
                                uint32_t targetTick, int delta)
```

- Validates loop length and movement delta
- Updates movement direction in the moving note identity
- Calculates new positions with wrap-around handling

### 2. Note Filtering and Classification
- Reconstructs all notes from MIDI events
- Creates a filtered list containing only other notes of the same pitch (excluding the moving note)
- This prevents confusion about which note is being moved

### 3. Overlap Detection and Planning
Using `findOverlaps()`:
- Checks each other note against the moving note's new position
- Determines if overlapping notes are completely contained or partially overlapping
- Plans which notes should be shortened vs. deleted

#### Overlap Detection Algorithm
```cpp
bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2, uint32_t loopLength)
```
- Handles both wrapped and unwrapped notes around loop boundaries
- Uses unwrapped position comparison for accurate overlap detection
- Accounts for loop-around scenarios

#### Containment Logic
- **Complete containment**: `note.startTick >= newStart && note.endTick <= displayNewEnd`
- **Wrap-around containment**: `note.startTick >= newStart || note.endTick <= displayNewEnd`

### 4. MIDI Event Location and Pitch Change Detection
- Locates the actual MIDI events for the moving note
- **Critical timing**: Pitch change detection happens AFTER finding events using the stored pitch
- If events aren't found, checks if the moving note was accidentally deleted and restores it
- Detects pitch changes by comparing stored pitch with actual event pitch
- Updates moving note identity and reindexes deleted notes if pitch changed

### 5. Note Movement Execution
- Moves the MIDI events to their new positions
- Updates the moving note identity with new positions
- Rebuilds event indices for efficient subsequent operations

### 6. Overlap Resolution
Using `applyShortenOrDelete()`:

#### Shortening Notes
- Shortens notes that start before the moving note
- Sets new end position to `newStart - 1` (or `loopLength - 1` for wrap-around)
- Checks if shortened length would be ≥ 49 ticks
- Deletes notes that would be too short after shortening
- Updates existing shortened note entries if already modified

#### Deleting Notes
- Uses two-phase deletion to avoid deleting wrong events
- Finds specific NoteOn/NoteOff pairs using length validation
- Stores deleted notes in the moving note identity for potential restoration

### 7. Note Restoration Logic
Using `restoreNotes()`:

#### Restoration Criteria
Notes are restored when:
- They belong to the same pitch as the moving note
- They don't overlap with the new position
- The moving note is moving away from them:
  - **Moving right**: Restore notes to the left (`deletedNote.endTick <= newStart`)
  - **Moving left**: Restore notes to the right (`deletedNote.startTick >= newStart + noteLen`)

#### Phantom Note Prevention
- **Critical safety**: Never restore notes that match the moving note's original start position
- These are artifacts from tracking issues and would create duplicates
- Validates note length to prevent invalid restorations

### 8. Final Reconstruction and Selection
Using `finalReconstructAndSelect()`:
- Sorts all MIDI events by tick
- Reconstructs the final note list
- Locates the moved note in the reconstructed list
- Updates the selected note index to maintain selection
- Updates the bracket tick position

## Special Cases and Edge Handling

### Wrap-Around Boundaries
- All position calculations use modulo arithmetic with loop length
- Overlap detection handles notes that span the loop boundary
- Length calculations account for wrapped notes: `(loopLength - start) + end`

### Pitch Changes During Movement
- System detects when note pitch changes mid-movement
- Updates moving note identity with new pitch
- Reindexes all related deleted notes to the new pitch
- Ensures overlap detection uses the correct current pitch

### Accidental Deletion Recovery
- If the moving note's MIDI events are missing, checks the deleted notes list
- Restores accidentally deleted moving notes before proceeding
- Prevents loss of the selected note during complex movements

### Movement Direction Awareness
- Tracks whether moving left (-1) or right (+1)
- Uses direction to determine which notes to restore
- Prevents thrashing when oscillating between positions

## Data Structures

### MovingNoteIdentity::DeletedNote
```cpp
struct DeletedNote {
    uint8_t note;           // MIDI note number
    uint8_t velocity;       // Original velocity
    uint32_t startTick;     // Original start position
    uint32_t endTick;       // Original end position
    uint32_t originalLength;// Length before modification
    bool wasShortened;      // true if shortened, false if deleted
    uint32_t shortenedToTick; // New end position if shortened
};
```

### Event Indexing
- Uses `NoteUtils::EventIndexMap` for O(1) event lookup
- Key format: `(pitch << 32) | tick`
- Separate indices for NoteOn and NoteOff events
- Rebuilt after major changes to maintain accuracy

## Performance Optimizations

1. **Event Indexing**: O(1) lookup instead of O(n) search
2. **Filtered Processing**: Only processes notes of the same pitch
3. **Batch Operations**: Groups related operations together
4. **Early Termination**: Skips processing when no movement occurs
5. **Index Reuse**: Rebuilds indices only when necessary

## Error Handling and Logging

- Comprehensive logging at DEBUG level for troubleshooting
- Validates all critical operations (event finding, length calculations)
- Graceful handling of missing events or invalid states
- Warning logs for unexpected conditions without crashing

## Integration Points

- **EditManager**: Manages moving note identity and selection state
- **Track**: Provides MIDI events and loop length
- **NoteUtils**: Handles note reconstruction and display formatting
- **Logger**: Provides categorized logging for debugging

This system ensures robust note movement with intelligent conflict resolution while maintaining the integrity of the moving note and providing smooth user experience even during complex editing operations. 