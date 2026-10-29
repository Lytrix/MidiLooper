//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "Globals.h"
#include "TrackManager.h"
#include "Utils/TrackMem.h"

extern TrackManager trackManager;

void Track::syncSlotRefsFromPool() {
  for (uint8_t i = 0; i < Config::MAX_LOOPS_PER_TRACK; ++i) {
    slots_[i].loopId = loopPool_.loopIdAt(i);
  }
}

void Track::ensureLoopsAllocated() {
  if (loopPool_.initialized()) {
    return;
  }
  loopPool_.ensureInitialized();
  if (!loopPool_.initialized()) {
    while (1) {
      delay(1);
    }  // Out of heap - should not happen
  }
  syncSlotRefsFromPool();
}

Loop& Track::loopForSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  const uint8_t idx = slotIndex < Config::MAX_LOOPS_PER_TRACK ? slotIndex : 0;
  const LoopId id = slots_[idx].loopId;
  if (id != kInvalidLoopId) {
    Loop* found = loopPool_.findById(id);
    if (found != nullptr) {
      return *found;
    }
  }
  return loopPool_.at(idx);
}

const Loop& Track::loopForSlot(uint8_t slotIndex) const {
  return const_cast<Track*>(this)->loopForSlot(slotIndex);
}

LoopId Track::loopIdForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return kInvalidLoopId;
  }
  return slots_[slotIndex].loopId;
}

const Slot& Track::slotRef(uint8_t slotIndex) const {
  static const Slot kEmpty{};
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return kEmpty;
  }
  return slots_[slotIndex];
}

Loop& Track::getLoop(uint8_t index) {
  return loopForSlot(index);
}

const Loop& Track::getLoop(uint8_t index) const {
  return loopForSlot(index);
}

bool Track::hasDataInSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return loopForSlot(slotIndex).hasData();
}

TRACK_COLD_MEM bool Track::hasPendingSnapshotPublishOnAnySlot() const {
  if (!loopsAllocated()) {
    return false;
  }
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (getLoop(s).hasPendingSnapshotPublish()) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM bool Track::hasInFlightSnapshotConstructionOnAnySlot() const {
  if (!loopsAllocated()) {
    return false;
  }
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (getLoop(s).hasInFlightSnapshotConstruction()) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM bool Track::hasSnapshotPublicationBacklogOnAnySlot() const {
  if (!loopsAllocated()) {
    return false;
  }
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    const Loop& loop = getLoop(s);
    if (loop.hasPendingSnapshotPublish() &&
        loop.publishedSnapshotRevision() < loop.playbackRevision) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM bool Track::hasPendingSessionExitSnapshotOnAnySlot() const {
  if (!loopsAllocated()) {
    return false;
  }
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (getLoop(s).pendingSnapshotSessionExitQueued()) {
      return true;
    }
  }
  return false;
}

/// Task 4.20 device census. Every class is walked at its **resident capacity**, not its live size: capacity is what
/// the allocator took. The two totals are separated on purpose, because the decline is measured in the *internal*
/// heap and only some of these owners take it from there:
///   internal-first — `EditPass::addedEvents` (`InternalHeapFirstAllocator`), the undo entries' nested
///                    `passIds`/`editPassIds`, and the live overdub session's identity lists;
///   external-first — each pass's `committedChunkIds` (`CommittedChunkIdAllocator` tries `extmem_malloc` first) and
///                    the pass vectors themselves, so a large census with a quiet internal heap is not a
///                    contradiction.
/// Read-only, allocation-free, bounded by `MAX_LOOPS_PER_TRACK` slots and the pass caps; FLASH-resident
/// (`TRACK_COLD_MEM`) because its only caller is the rate-limited diagnostics turn, never the interval path.
TRACK_COLD_MEM Track::AllocationCensus Track::allocationCensus() const {
  AllocationCensus out;
  if (!loopsAllocated()) {
    return out;
  }
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (getLoop(s).passes.capturePassCount() != 0) ++out.slotsWithPasses;
  }
  const Loop& loop = getActiveLoop();
  out.capturePasses = static_cast<uint16_t>(loop.passes.capturePassCount());
  out.editPasses = static_cast<uint16_t>(loop.passes.editPasses.size());
  // External-first on the device: the pass vectors and every pass's chunk directory.
  out.externalBytes =
      static_cast<uint32_t>(loop.passes.overdubPasses.capacity() * sizeof(OverdubPass) +
                            loop.passes.editPasses.capacity() * sizeof(EditPass));
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    out.externalBytes +=
        static_cast<uint32_t>(pass.committedChunkIds.capacity() * sizeof(uint16_t));
  }
  // Internal-first: each edit pass's own note payload.
  for (const EditPass& pass : loop.passes.editPasses) {
    out.editPassBytes += static_cast<uint32_t>(pass.addedEvents.capacity() * sizeof(MidiEvent));
  }
  // Internal-first: the undo entries' nested id lists (the entry vector itself is external-first).
  const GlobalUndoStack& stack = getGlobalUndoStack();
  out.undoEntries = static_cast<uint16_t>(stack.entries.size());
  for (const UndoEntry& entry : stack.entries) {
    out.undoIdBytes += static_cast<uint32_t>(entry.passIds.capacity() * sizeof(PassId) +
                                            entry.editPassIds.capacity() * sizeof(EditPassId));
  }
  // Internal-first: the live overdub session's identity lists, which a settled close empties.
  const Loop::OverdubSessionRetention session = loop.overdubSessionRetention();
  out.sessionBytes = static_cast<uint32_t>(session.passCapacity * sizeof(PassId) +
                                           session.companionNestedCapacity * sizeof(EditPassId));
  out.internalBytes = out.editPassBytes + out.undoIdBytes + out.sessionBytes;
  return out;
}

/// Which loop the `Snapshot` deferred owner serves this turn. The active loop wins **ties**, but not absolutely: a
/// pending publication the owner's own gate can refuse (queued pass ids with no session exit, no wrap journal and
/// nothing in flight, e.g. while PLAYING) must not hold the turn against a demoted slot whose accepted republish request
/// the owner *can* start (windowed-playback-generation: a switch target's source publication waited behind such a loop).
/// Rank 0 = construction already in flight (finish what was started), 1 = startable (session exit queued or a wrap
/// journal), 2 = any other pending publication. Lowest rank wins; ties go to the active loop, then slot order. Stateless
/// and bounded by `MAX_LOOPS_PER_TRACK`. While recording, the active loop keeps absolute priority: live-record
/// progress is its own clause of the owner's gate.
TRACK_COLD_MEM bool Track::playbackSourceRecoveryNeededForSlot(uint8_t slotIndex) const {
  if (!isPlaying() || !loopsAllocated() || slotIndex >= Config::MAX_LOOPS_PER_TRACK ||
      hasPendingDeferredOverdubStop() || hasPendingSnapshotPublishOnAnySlot()) return false;
  const Loop& loop = getLoop(slotIndex);
  return trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slotIndex) &&
      loop.loopLengthTicks != 0 && loop.hasCommittedPasses() &&
      loop.publishedSnapshotIdentity() == nullptr && !loop.captureActive() &&
      !loop.hasOverdubSession() &&
      loop.sessionExitPublishClaimState() == Loop::SessionExitPublishClaim::Empty;
}

TRACK_COLD_MEM Loop& Track::loopWithPendingSnapshotPublish() {
  Loop& active = getActiveLoop();
  const bool activePending = active.hasPendingSnapshotPublish();
  if (activePending && isRecording()) {
    return active;
  }
  const auto rank = [](const Loop& l) -> uint8_t {
    if (l.hasInFlightSnapshotConstruction()) return 0;
    if (l.pendingSnapshotSessionExitQueued() || l.hasPendingWrapJournal()) return 1;
    return 2;
  };
  Loop* best = nullptr;
  uint8_t bestRank = 3;
  if (activePending) {
    best = &active;
    bestRank = rank(active);
  }
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK && bestRank > 0; ++s) {
    Loop& slotLoop = getLoop(s);
    if (&slotLoop == &active || !slotLoop.hasPendingSnapshotPublish()) {
      continue;
    }
    const uint8_t r = rank(slotLoop);
    if (r < bestRank) {
      best = &slotLoop;
      bestRank = r;
    }
  }
  if (best != nullptr) return *best;
  // A passes-only restore has no queued publisher. Select a sounding slot only:
  // the existing Snapshot turn must produce its missing source, not hydrate the library.
  if (playbackSourceRecoveryNeededForSlot(activeLoopIndex)) return active;
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (playbackSourceRecoveryNeededForSlot(s)) return getLoop(s);
  }
  return active;
}

TRACK_COLD_MEM bool Track::hasAnySlotData() const {
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (hasDataInSlot(s)) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM void Track::reconcileTransportStateAfterSlotMutation() {
  if (isRecording() || isOverdubbing() || hasPendingDeferredOverdubStop() || isStoppedRecording()) {
    return;
  }
  if (trackState == TRACK_ARMED && getActiveLoop().hasCommittedPasses()) {
    armedPreRollNotes.clear();
    for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
      if (&trackManager.getTrack(i) == this) {
        trackManager.cancelPendingRecordArm(i);
        break;
      }
    }
    return;
  }
  if (hasAnySlotData()) {
    if (trackState == TRACK_EMPTY || trackState == TRACK_ARMED) {
      setState(TRACK_STOPPED);
    } else if (trackState == TRACK_PLAYING && !getActiveLoop().hasData()) {
      // Cleared the playing slot while other slots still have data — do not stay PLAYING
      // on empty active (blocks re-arm; session_20260803_170251).
      setState(TRACK_STOPPED);
    }
  } else if (trackState == TRACK_ARMED || trackState == TRACK_STOPPED ||
             trackState == TRACK_PLAYING || trackState == TRACK_OVERDUBBING ||
             trackState == TRACK_OVERDUB_STOPPING) {
    setState(TRACK_EMPTY);
  } else if (trackState != TRACK_EMPTY) {
    setState(TRACK_EMPTY);
  }
}
