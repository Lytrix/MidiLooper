//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/CommittedMidiScratch.h"

#include "Loop.h"

namespace CommittedMidiScratch {

RefreshKind refresh(MidiEventVec& scratch, uint32_t& scratchRevision,
                    const void*& scratchIdentity, Loop& loop) {
  const uint32_t revision = loop.playbackRevision;
  const void* matchIdentity = loop.publishedSnapshotIdentity();
  if (isFresh(scratchRevision, revision, scratchIdentity, matchIdentity)) {
    return RefreshKind::Reused;
  }

  SessionMidiEventVec sessionEvents;
  const void* observedIdentity = nullptr;
  if (loop.tryCopyCurrentSnapshotEditViewEvents(sessionEvents, observedIdentity)) {
    scratch.assign(sessionEvents.begin(), sessionEvents.end());
    scratchRevision = revision;
    scratchIdentity = observedIdentity;
    return RefreshKind::FromSnapshot;
  }

  loop.materializeEditViewFromPasses();
  const SessionMidiEventVec& committedLoopMidi = loop.midiEvents();
  scratch.assign(committedLoopMidi.begin(), committedLoopMidi.end());
  scratchRevision = revision;
  scratchIdentity = nullptr;
  return RefreshKind::FromPasses;
}

}  // namespace CommittedMidiScratch
