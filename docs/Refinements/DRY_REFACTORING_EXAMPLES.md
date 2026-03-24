# DRY Refactoring Examples

This document shows how to use the new utility functions to eliminate code duplication in the MIDI looper codebase.

## 🔧 **1. Loop Length Validation Pattern**

### Before (Duplicated 16+ times):
```cpp
// In MidiButtonManager.cpp, EditStates/*.cpp, etc.
void someFunction(Track& track) {
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;  // Repeated everywhere!
    
    // ... rest of function
}
```

### After (Using ValidationUtils):
```cpp
#include "ValidationUtils.h"

void someFunction(Track& track) {
    uint32_t loopLength = track.getLoopLength();
    if (!ValidationUtils::validateLoopLength(loopLength)) return;
    
    // ... rest of function
}

// Or even better, combined validation:
void someFunctionWithNoteIndex(Track& track, int noteIdx) {
    const auto& notes = track.getCachedNotes();
    if (!ValidationUtils::validateBasicParams(track.getLoopLength(), noteIdx, notes.size())) {
        return;
    }
    
    // ... rest of function - guaranteed valid params
}
```

## 🔧 **2. DeletedNote Creation Pattern**

### Before (Duplicated in 3+ files):
```cpp
// Repeated in NoteMovementUtils.cpp, EditStartNoteState.cpp, MidiButtonManager.cpp
EditManager::MovingNoteIdentity::DeletedNote deleted;
deleted.note = dn.note;
deleted.velocity = dn.velocity;
deleted.startTick = dn.startTick;
deleted.endTick = dn.endTick;
deleted.originalLength = calculateNoteLength(dn.startTick, dn.endTick, loopLength);
deleted.wasShortened = false;
deleted.shortenedToTick = 0;
manager.movingNote.deletedNotes.push_back(deleted);
```

### After (Using MidiEventUtils):
```cpp
#include "MidiEventUtils.h"

// One line replaces 8+ lines of duplication!
auto deleted = MidiEventUtils::createDeletedNote(dn, loopLength, false, 0);
manager.movingNote.deletedNotes.push_back(deleted);

// For shortened notes:
auto shortened = MidiEventUtils::createDeletedNote(dn, loopLength, true, newEndTick);
manager.movingNote.deletedNotes.push_back(shortened);
```

## 🔧 **3. Duplicate Position Removal Pattern**

### Before (Duplicated in 3+ locations):
```cpp
// Repeated in EditSelectNoteState.cpp and MidiButtonManager.cpp
for (int i = allPositions.size() - 1; i > 0; i--) {
    if (allPositions[i] == allPositions[i-1]) {
        allPositions.erase(allPositions.begin() + i);
    }
}
```

### After (Using ValidationUtils):
```cpp
#include "ValidationUtils.h"

// One line replaces entire loop!
ValidationUtils::removeDuplicates(allPositions);
```

## 🔧 **4. MIDI Event Finding Pattern**

### Before (Multiple similar patterns):
```cpp
// Finding NoteOn event - repeated logic across files
auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
    return evt.type == midi::NoteOn &&
           evt.data.noteData.note == movingNotePitch &&
           evt.tick == currentStart &&
           evt.data.noteData.velocity > 0;
});

// Finding NoteOff event - similar pattern repeated
auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
    bool isOff = (evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0);
    return isOff && evt.data.noteData.note == movingNotePitch &&
           evt.tick == currentEnd;
});
```

### After (Using MidiEventUtils):
```cpp
#include "MidiEventUtils.h"

// Clean, readable, no duplication
auto onIt = MidiEventUtils::findNoteOnEvent(midiEvents, movingNotePitch, currentStart);
auto offIt = MidiEventUtils::findNoteOffEvent(midiEvents, movingNotePitch, currentEnd);

// Or find both at once:
auto [onIt, offIt] = MidiEventUtils::findNoteEventPair(
    midiEvents, movingNotePitch, currentStart, currentEnd);
```

## 🔧 **5. Complete Function Refactoring Example**

### Before (MidiButtonManager.cpp - highly duplicated):
```cpp
void MidiButtonManager::handleSomeOperation(Track& track) {
    // Validation duplication
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    const auto& notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    if (selectedIdx >= (int)notes.size()) return;
    
    // MIDI event finding duplication  
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn &&
               evt.data.noteData.note == notes[selectedIdx].note &&
               evt.tick == notes[selectedIdx].startTick &&
               evt.data.noteData.velocity > 0;
    });
    
    // DeletedNote creation duplication
    EditManager::MovingNoteIdentity::DeletedNote deleted;
    deleted.note = notes[selectedIdx].note;
    deleted.velocity = notes[selectedIdx].velocity;
    deleted.startTick = notes[selectedIdx].startTick;
    deleted.endTick = notes[selectedIdx].endTick;
    deleted.originalLength = calculateNoteLength(deleted.startTick, deleted.endTick, loopLength);
    deleted.wasShortened = false;
    deleted.shortenedToTick = 0;
    
    // Position building duplication...
    // (more duplicated code)
}
```

### After (Using utilities):
```cpp
#include "ValidationUtils.h"
#include "MidiEventUtils.h"

void MidiButtonManager::handleSomeOperation(Track& track) {
    // Clean validation - one line
    const auto& notes = track.getCachedNotes();
    int selectedIdx = editManager.getSelectedNoteIdx();
    if (!ValidationUtils::validateBasicParams(track.getLoopLength(), selectedIdx, notes.size())) {
        return;
    }
    
    // Clean MIDI event finding - one line
    auto onIt = MidiEventUtils::findNoteOnEvent(
        midiEvents, notes[selectedIdx].note, notes[selectedIdx].startTick);
    
    // Clean DeletedNote creation - one line
    auto deleted = MidiEventUtils::createDeletedNote(notes[selectedIdx], track.getLoopLength());
    
    // ... rest of function much cleaner
}
```

## 📊 **Impact Analysis**

### Lines of Code Reduced:
- **Guard clauses**: 16+ locations × 2 lines = 32+ lines → 16+ locations × 1 line = 16+ lines saved
- **DeletedNote creation**: 3+ locations × 8 lines = 24+ lines → 3+ locations × 1 line = 21+ lines saved  
- **Duplicate removal**: 3+ locations × 5 lines = 15+ lines → 3+ locations × 1 line = 12+ lines saved
- **MIDI event finding**: 10+ patterns × 5 lines = 50+ lines → 10+ patterns × 1 line = 40+ lines saved

### **Total Estimated Savings**: 100+ lines of code reduced

### Maintainability Benefits:
1. **Single Source of Truth**: Validation logic in one place
2. **Consistent Behavior**: All code uses same validation/creation patterns
3. **Easier Testing**: Utility functions can be unit tested independently
4. **Reduced Bug Surface**: Fewer places for logic errors to hide
5. **Better Readability**: Intent is clearer with well-named utility functions

## 🚀 **Implementation Strategy**

### Phase 1: Create Utilities (✅ Done)
- `ValidationUtils.h` - Guard clause patterns
- `MidiEventUtils.h` - MIDI event operations

### Phase 2: Refactor Hot Paths (Recommended)
1. **MidiButtonManager.cpp** - 15+ validation patterns
2. **EditStates/*.cpp** - Note index validation  
3. **NoteMovementUtils.cpp** - DeletedNote creation

### Phase 3: Refactor Remaining Files
1. **DisplayManager.cpp** - Loop validation
2. **EditManager.cpp** - Note validation
3. **Track.cpp** - Parameter validation

### Phase 4: Add Tests
1. Unit tests for utility functions
2. Integration tests to verify behavior unchanged

## 💡 **Future DRY Opportunities**

After implementing these utilities, consider:

1. **Position Building Pattern**: Extract navigation position logic
2. **Overlap Detection Pattern**: Standardize overlap checking algorithms  
3. **Cache Management Pattern**: Centralize cache invalidation logic
4. **Logging Patterns**: Standardize debug/info logging formats
5. **Error Handling Patterns**: Consistent error reporting and recovery

---

**Result**: Significantly more maintainable codebase with consistent patterns and reduced duplication! 🎵 