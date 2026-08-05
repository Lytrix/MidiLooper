# Move Note Logic Documentation

## Overview

The move note system in this MIDI looper handles the complex task of moving notes while managing overlaps, maintaining note integrity, and supporting dynamic pitch changes during movement. The system is designed to ensure that the moving note always remains intact while intelligently handling conflicts with other notes.

**Current Architecture (2026-08):**
- **Edit-session geometry pipeline**: Move, length, and active-session pitch route through [`runEditSessionGeometryPipelineForCausingNote`](../../src/RunEditSessionGeometryPipeline.cpp) from [`NoteMovementUtils.cpp`](../../src/Utils/NoteMovementUtils.cpp)
- **Declarative overlap**: Analysis → constrained geometry → `buildEditSessionActions` → `applyEditSessionActions` — not imperative restore-first chains
- **Stable note identity**: `EditorSelection.primaryNote` + `NoteEditFocus` transaction baseline
- **Commit**: `commitAllPendingNoteEditActions` serializes baseline vs canonical session store (see [`openspec/specs/edit-session-action-geometry/spec.md`](../../openspec/specs/edit-session-action-geometry/spec.md))

**Legacy (retired from `src/`):** `restoreOverlapNotesNoLongerOverlapping`, `findOverlaps` on move/length overlap paths, `movingNoteRange` restore-first authority.

## Key Concepts

### Moving Note Identity
The system maintains a `MovingNoteIdentity` in `EditManager` that tracks:
- **Original position** (`origStart`, `origEnd`) - The note's position when movement began
- **Current position** (`lastStart`, `lastEnd`) - The note's current position during movement
- **Pitch** (`note`) - The current pitch (can change during movement)
- **Movement direction** - Whether moving left (-1) or right (+1)
- **Deleted notes** - List of notes that were temporarily removed or shortened
- **Active flag** - Whether a note is currently being moved

### Cached Note Performance
The system uses **cached note reconstruction** for optimal performance:
- **`Track::getCachedNotes()`** - Returns cached notes with hash-based invalidation
- **Hash-based invalidation** - Cache invalidated automatically when MIDI events change
- **95% performance improvement** - Eliminates expensive note reconstruction calls

### Overlap Handling Philosophy
The core principle is: **The moving note is never modified - other notes adapt to it.**

When overlaps occur:
1. **Complete containment**: If an overlapping note is entirely within the moving note's new position, it's deleted
2. **Partial overlap**: The overlapping note is shortened to avoid conflict
3. **Minimum length**: Notes shortened to less than 49 ticks are deleted instead
4. **Restoration**: Previously deleted/shortened notes may be restored when the moving note moves away

## System Architecture

### Core Components

#### 1. Geometry pipeline (primary path)

**Orchestrator:** [`NoteMovementUtils`](../../src/Utils/NoteMovementUtils.cpp) — `moveNoteWithOverlapHandling`, `changeLengthWithOverlapHandling`, `applyPitchChange` (active session) delegate to:

```
EditedGeometry → Edit projection (D20) → analyzeEditSessionInteractions
  → groupEditSessionInteractionsByTarget → resolveConstrainedGeometry
  → buildEditSessionActions → applyEditSessionActions → live store
```

**Pipeline implementation:** [`RunEditSessionGeometryPipeline.cpp`](../../src/RunEditSessionGeometryPipeline.cpp)

**Post-apply UI sync:** `finalReconstructAndSelect` — selection index + display refresh (not overlap authority).

**Non-session pitch:** `applySimplePitchChange` when no active NOTE_EDIT session (loop view / pre-session path).

#### 2. NoteMovementUtils (legacy section — overlap helpers)

**Location**: `src/Utils/NoteMovementUtils.cpp`

**Retired from live geometry paths:** `findOverlaps`, `restoreOverlapNotesNoLongerOverlapping`, `applyShortenOrDelete` as the primary move/length overlap engine.

**Still used:** orchestrator entry points, `finalReconstructAndSelect`, wrap/position helpers, non-session `applySimplePitchChange`.

#### 3. EditManager (state management)
**Location**: `src/EditManager.cpp`
**Responsibilities**:
- Maintains `MovingNoteIdentity` state
- Manages edit state transitions
- Handles bracket positioning
- Provides stable note identity during movement

#### 3. EditStates (Movement Triggers)
**Location**: `src/EditStates/`
**Key States**:
- `EditStartNoteState` - Handles note position movement
- `EditLengthNoteState` - Handles note length changes
- `EditPitchNoteState` - Handles pitch changes
- `EditSelectNoteState` - Handles note selection with cached notes

#### 4. ControlSurfaceManager (Fader Integration)
**Location**: `src/ControlSurfaceManager.cpp`
**Responsibilities**:
- Integrates fader input with movement system
- Maintains stable note identity during fader operations
- Delegates to `NoteMovementUtils` for overlap handling
- Schedules **geometry→F1** motor sync (`pendingGeometryDriverMotorSync_`) after F2/F3/F4 moves. User F1 select during geometry kinds commits geometry and applies navigation; motor echo after geometry F1 send is gated by `selectFaderFeedbackIgnoreUntilMs_` (see [`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md))

**Selection UI during geometry moves:** `EditManager::applySelectionFromGeometryEdit` calls `syncGeometrySelectionToUi` (bracket + display refresh only) so `selectedNoteIdx` and `EditStartNoteState` stay on the moving note — not full `syncNoteEditSessionStateToUi`.

## Step-by-Step Flow

### 1. Movement Initiation
**Entry Points**:
- **EditState movements**: `EditStartNoteState::moveNoteToPosition()`
- **Fader movements**: `ControlSurfaceManager::handleCoarseFaderInput()`
- **Direct calls**: `EditManager::moveNoteToPosition()` (via surface → edit operation)

**Initialization**:
```cpp
// Activate moving note identity if not already active
if (!manager.movingNote.active) {
    const auto& notes = track.getCachedNotes();  // Use cached notes
    auto& note = notes[manager.getSelectedNoteIdx()];
    manager.movingNote.note = note.note;
    manager.movingNote.origStart = note.startTick;
    manager.movingNote.origEnd = note.endTick;
    manager.movingNote.lastStart = note.startTick;
    manager.movingNote.lastEnd = note.endTick;
    manager.movingNote.active = true;
}
```

### 2. Stable Note Identity Usage
**Critical Pattern**: Always use moving note identity instead of reconstructing from selection:

```cpp
// CORRECT: Use stable identity
if (editManager.movingNote.active) {
    currentNote.note = editManager.movingNote.note;
    currentNote.startTick = editManager.movingNote.lastStart;
    currentNote.endTick = editManager.movingNote.lastEnd;
} else {
    // First movement - use selected note from cache
    const auto& notes = track.getCachedNotes();
    currentNote = notes[selectedIdx];
}
```

### 3. Pipeline movement processing

**Main path:** `NoteMovementUtils` builds `EditedGeometry` and calls `runEditSessionGeometryPipelineForCausingNote`.

**Process (per geometry tick):**
1. **Edit driver boundary** — refresh transaction baseline when `EditorSelection.primaryNote` changes (D19)
2. **Edit projection** — linear spans for analyze (`buildEditProjectionContext`, D20)
3. **Analyze** — `analyzeEditSessionInteractions` (positive interaction graph only)
4. **Group by target** — `groupEditSessionInteractionsByTarget`
5. **Resolve** — `resolveConstrainedGeometry` (hide/shorten/restore policy)
6. **Build actions** — `buildEditSessionActions` (minimal diff vs live store)
7. **Apply** — `applyEditSessionActions` (sole live-store writer for geometry)
8. **Invalidate** — `track.invalidateCaches()` for playback preview
9. **UI sync** — `finalReconstructAndSelect` when needed

**Commit (F1 select / session exit):** `commitAllPendingNoteEditActions` → `buildPreCommitEditPasses` (baseline vs canonical store diff).

### 4. Cached note integration (display / selection)
**Performance Optimization**: All note access uses cached notes:

```cpp
// High-performance cached access
const auto& notes = track.getCachedNotes();  // O(1) if cached

// Legacy expensive reconstruction (avoided)
// auto notes = NoteUtils::reconstructNotes(midiEvents, loopLength);  // O(n)
```

**Cache Invalidation**: Automatic when MIDI events change:
- Hash-based detection of MIDI event changes
- Automatic cache rebuild only when necessary
- Maintains cache across multiple operations

## Overlap detection and resolution (pipeline)

Overlap policy lives in **`resolveConstrainedGeometry`** and **`buildEditSessionActions`** — not in imperative `findOverlaps` / `restoreNotes` chains.

**Hide / shorten:** `OverlapNoteOn`, `CompleteCover` → hide; `OverlapNoteOff` → restrictive shorten combine; minimum note edit length hide when `noteMinLengthRemoveEnabled`.

**Restore:** Omitted interaction pairs + baseline-equivalent constrained geometry → `RestoreNote` actions when live store differs from transaction baseline.

See [`openspec/specs/edit-session-action-geometry/spec.md`](../../openspec/specs/edit-session-action-geometry/spec.md) for normative precedence tables.

### Legacy overlap section (historical)
**Function**: `NoteMovementUtils::findOverlaps()`

**Process**:
1. **Filtered Processing**: Only processes notes of same pitch (excluding moving note)
2. **Wrap-around Handling**: Correctly handles notes spanning loop boundaries
3. **Containment Analysis**: Determines complete vs partial overlaps
4. **Restoration Candidates**: Identifies notes that can be restored

### Intelligent Restoration Logic
**Function**: `NoteMovementUtils::restoreNotes()`

**Restoration Criteria**:
- Note belongs to same pitch as moving note
- Note doesn't overlap with new position
- Moving note is moving away from the note:
  - **Moving right**: Restore notes to the left
  - **Moving left**: Restore notes to the right
- **Phantom Prevention**: Never restore notes matching original start position

### Overlap Resolution Execution
**Function**: `NoteMovementUtils::applyShortenOrDelete()`

**Two-Phase Process**:
1. **Planning Phase**: Categorizes overlaps without modifying MIDI events
2. **Execution Phase**: Applies changes with proper event pair validation

## Special Cases and Advanced Features

### Pitch Changes During Movement
**Dynamic Pitch Handling**:
- Detects pitch changes by comparing stored vs actual event pitch
- Updates moving note identity with new pitch
- Reindexes deleted notes to new pitch
- Maintains overlap detection accuracy

### Accidental Deletion Recovery
**Robust Error Handling**:
- Detects when moving note MIDI events are missing
- Searches deleted notes list for accidental removal
- Restores accidentally deleted moving notes
- Prevents loss of selected note during complex operations

### Wrap-Around Boundary Handling
**Loop Boundary Logic**:
- All calculations use modulo arithmetic
- Handles notes that span loop start/end boundary
- Correct length calculation for wrapped notes
- Proper overlap detection across boundaries

### Multi-State Integration
**State Machine Integration**:
- Works seamlessly with all EditStates
- Maintains consistency across state transitions
- Handles fader-driven movements
- Preserves moving note identity across operations

## Performance Optimizations

### 1. Cached Note Access
- **95% performance improvement** over reconstruction
- Hash-based cache invalidation
- Automatic cache management
- Consistent O(1) access patterns

### 2. Filtered Processing
- Only processes notes of same pitch
- Excludes moving note from overlap detection
- Reduces computational complexity
- Prevents identity confusion

### 3. Stable Identity Management
- Maintains moving note identity across operations
- Prevents expensive note searches
- Eliminates redundant reconstructions
- Ensures consistent behavior

### 4. Batch Operations
- Groups related MIDI event changes
- Minimizes cache invalidations
- Reduces reconstruction overhead
- Optimizes display updates

## Integration Points

### Current System Integration
- **EditManager**: Manages moving note identity and state
- **EditStates**: Trigger movements and maintain state consistency
- **ControlSurfaceManager**: Integrates fader input with movement logic (delegates geometry to `EditManager`)
- **Track**: Provides cached notes and MIDI event access
- **NoteMovementUtils**: Centralized overlap handling and movement logic
- **DisplayManager**: Uses cached notes for efficient rendering

### API Usage Patterns
```cpp
// Correct usage pattern for note movement
void moveNote(Track& track, uint32_t targetTick, int delta) {
    // Get current note using cached access
    const auto& notes = track.getCachedNotes();
    
    // Use stable identity if available
    NoteUtils::DisplayNote currentNote;
    if (editManager.movingNote.active) {
        currentNote.note = editManager.movingNote.note;
        currentNote.startTick = editManager.movingNote.lastStart;
        currentNote.endTick = editManager.movingNote.lastEnd;
    } else {
        currentNote = notes[selectedIdx];
    }
    
    // Delegate to centralized logic
    NoteMovementUtils::moveNoteWithOverlapHandling(track, editManager, currentNote, targetTick, delta);
}
```

## Error Handling and Logging

### Comprehensive Logging
- **Category-based logging**: `CAT_MIDI` for MIDI operations
- **Debug level details**: Movement parameters, overlap detection, restoration
- **Performance metrics**: Cache hit rates, reconstruction counts
- **Error conditions**: Missing events, invalid states, boundary conditions

### Robust Error Recovery
- **Missing event detection**: Identifies and recovers missing MIDI events
- **State validation**: Ensures moving note identity consistency
- **Boundary checking**: Validates all position calculations
- **Graceful degradation**: Continues operation even with unexpected conditions

## Current Status (2026-08)

✅ **Pipeline wire**: Move / length / pitch / add / delete through `runEditSessionGeometryPipelineForCausingNote`  
✅ **Canonical commit**: Baseline vs session store diff; apply-owned rows diagnostic only  
✅ **Display projection**: `projectNoteEditDisplayNotes` sole active display producer  
✅ **F1 select display-first motors**: Dependent settle gates F2–F4; paint epoch before motor flush (`adf9209`)  
✅ **Native + HITL**: Phases 1–4.10 complete; full regression matrix (Phase 5) open  

**Further reading:** [`openspec/specs/edit-session-action-geometry/spec.md`](../../openspec/specs/edit-session-action-geometry/spec.md), [`FADER_STATE_SYSTEM.md`](FADER_STATE_SYSTEM.md) § Select-dependent motor sync.
