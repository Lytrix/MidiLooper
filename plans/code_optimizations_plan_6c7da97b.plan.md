---
name: Code Optimizations Plan
overview: Performance optimizations and technical debt reduction for the MIDI looper. Includes Option B (primitives-only) for EditStartNoteState/NoteMovementUtils consolidation to avoid high-risk refactoring.
todos: []
isProject: false
---

# Code Optimizations and Technical Debt

## Priority Summary


| Priority | Item                                                               | File(s)                                 | Effort |
| -------- | ------------------------------------------------------------------ | --------------------------------------- | ------ |
| Critical | Replace per-event sort in recordMidiEvents                         | Track.cpp                               | Medium |
| Critical | Reduce O(n) duplicate check in recordMidiEvents                    | Track.cpp                               | Low    |
| High     | Avoid full event scans in MidiLedManager                           | MidiLedManager.cpp                      | Medium |
| High     | Simplify CachedNoteList cache validation                           | NoteUtils.cpp                           | Low    |
| Medium   | Remove/reduce logging in hot paths                                 | main.cpp, MidiHandler, Track, NoteUtils | Low    |
| Medium   | Remove String allocation in MidiLedManager                         | MidiLedManager.cpp                      | Low    |
| Medium   | **Option B:** Replace only primitive helpers in EditStartNoteState | EditStartNoteState.cpp                  | Low    |
| Low      | Consolidate findOverlaps/applyShortenOrDelete                      | (deferred - high risk)                  | Medium |
| Low      | Consolidate EditManager state-exit logic                           | EditManager.cpp                         | Low    |


---

## Option B: EditStartNoteState Primitives-Only (Point 5)

**Scope:** Replace only the three static helper functions in [EditStartNoteState.cpp](src/EditStates/EditStartNoteState.cpp) with calls to [NoteMovementUtils](include/Utils/NoteMovementUtils.h). Do NOT touch `findOverlaps`, `applyShortenOrDelete`, or `restoreNotes`.

**Changes:**

1. Add `#include "Utils/NoteMovementUtils.h"` to EditStartNoteState.cpp
2. Remove the static definitions of `wrapPosition`, `calculateNoteLength`, and `notesOverlap` (lines 41-90)
3. Replace all usages with `NoteMovementUtils::wrapPosition`, `NoteMovementUtils::calculateNoteLength`, and `NoteMovementUtils::notesOverlap`

**Why Option B:**

- The three primitives are byte-for-byte identical in both files
- No semantic changes; zero behavioral risk
- Reduces duplication without touching the complex overlap/restore logic that has caused refactor failures
- Full unification (Option C) deferred: findOverlaps, applyShortenOrDelete, restoreNotes have subtle behavioral differences that would require careful reconciliation

**Files affected:** [EditStartNoteState.cpp](src/EditStates/EditStartNoteState.cpp) only