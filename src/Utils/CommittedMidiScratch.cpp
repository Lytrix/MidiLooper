//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/CommittedMidiScratch.h"

#include "Loop.h"

namespace CommittedMidiScratch {

RefreshType refresh(MidiEventVec& scratch, uint32_t& scratchRevision,
                    const void*& scratchIdentity, Loop& loop) {
  const uint32_t revision = loop.playbackRevision;
  const void* matchIdentity = loop.publishedSnapshotIdentity();
  if (isFresh(scratchRevision, revision, scratchIdentity, matchIdentity)) {
    return RefreshType::Reused;
  }

  SessionMidiEventVec sessionEvents;
  const void* observedIdentity = nullptr;
  if (loop.tryCopyCurrentSnapshotEditViewEvents(sessionEvents, observedIdentity)) {
    scratch.assign(sessionEvents.begin(), sessionEvents.end());
    scratchRevision = revision;
    scratchIdentity = observedIdentity;
    return RefreshType::FromSnapshot;
  }

  scratch.clear();
  scratchRevision = revision;
  scratchIdentity = nullptr;
  return RefreshType::HoldEmpty;
}

}  // namespace CommittedMidiScratch
