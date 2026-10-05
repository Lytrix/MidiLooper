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

/// Which loop the `Snapshot` deferred owner serves this turn. The active loop wins **ties**, but not absolutely: a
/// pending publication the owner's own gate can refuse (queued pass ids with no session exit, no wrap journal and
/// nothing in flight, e.g. while PLAYING) must not hold the turn against a demoted slot whose accepted republish request
/// the owner *can* start (windowed-playback-generation: a switch target's source publication waited behind such a loop).
/// Rank 0 = construction already in flight (finish what was started), 1 = startable (session exit queued or a wrap
/// journal), 2 = any other pending publication. Lowest rank wins; ties go to the active loop, then slot order. Stateless
/// and bounded by `MAX_LOOPS_PER_TRACK`. While recording, the active loop keeps absolute priority: live-record
/// progress is its own clause of the owner's gate.
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
  return best != nullptr ? *best : active;
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
