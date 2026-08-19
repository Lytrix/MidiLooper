//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Stage 1–2 content-only editing derivation (DEC-035). Reads LoopPasses fields only.
// Builds GUS-shaped entries for load-time fill. Not a persist owner.

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "GlobalUndoStack.h"
#include "LoopPasses.h"
#include "PendingNoteChange.h"
#include "Utils/LoopMem.h"

enum class ContentUndoUnitKind : uint8_t {
  RecordPassAdded = 0,
  OverdubPassAdded = 1,
  NoteEditPassClosed = 2,
  ControlChangeEditPassClosed = 3,
  LoopBoundaryChange = 4,
};

struct ContentUndoUnit {
  ContentUndoUnitKind kind = ContentUndoUnitKind::RecordPassAdded;
  PassId primaryPassId = kInvalidPassId;
  uint8_t editPassIndex = 0;
  uint8_t overdubSessionIndex = kUngroupedOverdubSessionIndex;
  PassIdList passIds;
  EditPassIdList editPassIds;
};

/// Group persisted content records into undo units using only Loop file fields.
/// When \p effectiveOnly is true, Disabled records are omitted (load-time tip).
#if defined(__IMXRT1062__)
#define LOOP_CONTENT_HISTORY_MEM LOOP_COLD_MEM
#else
#define LOOP_CONTENT_HISTORY_MEM
#endif

LOOP_CONTENT_HISTORY_MEM void deriveContentUndoUnits(const LoopPasses& passes,
                                                     std::vector<ContentUndoUnit>& out,
                                                     bool effectiveOnly = false);
LOOP_CONTENT_HISTORY_MEM void deriveEffectiveContentUndoUnits(const LoopPasses& passes,
                                                              std::vector<ContentUndoUnit>& out);
LOOP_CONTENT_HISTORY_MEM void buildContentUndoEntries(const LoopPasses& passes, uint8_t slotIndex,
                                                      LoopId loopId, UndoEntryVec& out);
