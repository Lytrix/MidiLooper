//  Copyright (c)  2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "TrackPreparedUndo.h"
#include "PendingNoteChange.h"
#include "Utils/TrackMem.h"

TRACK_COLD_MEM bool TrackPreparedUndo::prepareOverdubSessionEntry(
    Loop& loop, Loop::SessionUndoCollection& collection, uint8_t slotIndex,
    const ImmutableLoopSnapshotRef& beforePublished, UndoEntry& entry,
    uint32_t beforePublishedCoverage) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK ||
      !loop.collectedOverdubSessionUndoPassesCurrent(collection) || collection.passIds->empty() ||
      entry.id != kInvalidUndoEntryId || entry.passId != kInvalidPassId || !entry.passIds.empty() || !entry.editPassIds.empty() ||
      entry.beforeSnapshot || entry.afterSnapshot || entry.beforePublishedSnapshot || entry.afterPublishedSnapshot ||
      collection.passIds == &entry.passIds || collection.passIds == &entry.editPassIds ||
      collection.companionIds == &entry.passIds || collection.companionIds == &entry.editPassIds) return false;
  // All fallible checks precede mutation. Empty destination vectors may retain
  // capacity: swapping keeps those allocations in the collection outputs.
  entry.kind = UndoEntryKind::OverdubPassAdded;
  entry.slotIndex = slotIndex; entry.loopId = loop.loopId;
  entry.passId = collection.passIds->back();
  entry.editPassType = EditPassType::Note; entry.editPassIndex = kOverdubCompanionEditPassIndex;
  entry.beforeGeometry = {}; entry.afterGeometry = {};
  entry.beforeLoopStartTick = entry.beforeLoopLengthTicks = 0;
  entry.afterLoopStartTick = entry.afterLoopLengthTicks = 0;
  entry.beforeTrackState = entry.afterTrackState = TRACK_EMPTY;
  entry.hasTrackState = entry.beforeSlotEnabled = entry.beforeSlotMuted = false;
  entry.hasSlotFlags = entry.hasRedoPayload = false;
  entry.beforePublishedSnapshot = beforePublished; // Destination was empty: retain only, no release.
  entry.beforePublishedCoverage = beforePublishedCoverage; // DEC-073: coverage of that root.
  if (collection.passIds->size() > 1) entry.passIds.swap(*collection.passIds);
  entry.editPassIds.swap(*collection.companionIds);
  collection.phase = Loop::SessionUndoCollection::Phase::Cold; // Explicitly consumed, including single-pass/empty edits.
  return true;
}
