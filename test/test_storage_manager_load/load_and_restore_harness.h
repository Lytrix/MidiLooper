//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet A / A1.1 of `deferred-slot-restore-liveness` — the controllable restore harness.
//
// The pre-existing suite exercised the queue's pop/peek helpers directly, which is exactly what the
// change's acceptance fixtures may not do: the defect is in the *scheduling* between the queue and the
// load job, so a fixture that calls `stepSubmittedLoadJobs()` (or a pop helper) by hand cannot observe
// it. This header supplies the two controllable inputs those fixtures need and nothing else:
//
//   * an SD payload set the test owns (`writeEmptyCommittedPayloadToSd`,
//     `writePayloadWithOneNoteToSd`), built with the **production** body writer rather than
//     hand-assembled bytes, so a fixture cannot pass against a format the device never writes;
//   * a bounded turn driver over the production entry points
//     (`StorageManager::selectSubmittedLoadJobs` + `stepSubmittedLoadJobs`), so a fixture states a
//     turn budget instead of stepping internals;
//   * transport state (`sequencerRunning`) and the track's capture/audible flags, which the harness
//     already controls through the production `Track` surface — see the per-fixture setup.
//
// Transport, capture and audible state are *inputs* here, never assertions: a fixture that observes
// "nothing happened" must also show which gate denied, which is the point of the red-acceptance set
// (tasks.md A1.11 — the named exclusion flag).
//
// **Harness state note (measured during A1.1).** `trackManager`'s default track state in this suite is
// `TRACK_PLAYING`, i.e. `isAudiblePlaybackActive()` is already true before a fixture does anything.
// That is convenient for transport-active cases but silently gates every background restore, so each
// fixture below sets its state explicitly through `setTrackPlaybackState` / `setRuntimeTransportIdle`
// rather than inheriting it.

#pragma once

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "LoadLoopBudget.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "LoopPasses.h"
#include "SlotLoadSession.h"
#include "StorageLoopIo.h"
#include "Track.h"
#include "TrackManager.h"
#include "TrackState.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

// The suite's own in-memory SD card (see test/test_support/firmware/SD.h).
#include "../test_support/firmware/SD.h"

namespace RestoreHarness {

/// Set the transport global the production gate reads (`extern bool sequencerRunning`).
inline void setSequencerRunning(bool running) { sequencerRunning = running; }

/// Put every track into `TRACK_STOPPED`: the whole-runtime eligibility the load gate derives from
/// `isRecording() || isAudiblePlaybackActive()` across all tracks.
inline void setRuntimeTransportIdle() {
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    trackManager.getTrack(i).forceSetState(TRACK_STOPPED);
  }
  setSequencerRunning(false);
}

/// Make one track the reason the runtime is timing-critical, leaving the others stopped. This is the
/// "transport running with one track audibly playing" shape, and the shape a CLEAR on *another* track
/// leaves behind.
inline void setTrackAudiblyPlaying(uint8_t trackIndex) {
  setRuntimeTransportIdle();
  trackManager.getTrack(trackIndex).forceSetState(TRACK_PLAYING);
}

/// The capture half of the same gate: `isRecording()` is true and the runtime is timing-critical.
inline void setTrackRecording(uint8_t trackIndex) {
  setRuntimeTransportIdle();
  trackManager.getTrack(trackIndex).forceSetState(TRACK_RECORDING);
}

/// A payload slot file, in the byte layout `LoadLoopJob` restores from:
/// `[epoch header][PersistedLoopSnapshot body][kSaveFileToken]`. `storageIoFromFileWrite` and
/// `writePersistedLoopSnapshot` are the production writer and the production write boundary.
inline bool writeSlotPayloadToSd(const PersistedLoopSnapshot& snapshot, uint8_t trackIndex,
                                 uint8_t slotIndex) {
  char finalPath[64];
  char tempPath[64];
  if (!CurrentSetStorage::formatLoopSlotPath(finalPath, sizeof(finalPath), trackIndex, slotIndex) ||
      !CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex, slotIndex)) {
    return false;
  }

  File file = CurrentSetStorage::openForRewrite(tempPath);
  if (!file) {
    return false;
  }
  const StorageIo io = StorageManagerInternal::storageIoFromFileWrite(file);
  bool ok = CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(file, 0u) &&
            writePersistedLoopSnapshot(io, snapshot) &&
            StorageManagerInternal::writeRaw(file, &CurrentSetStorage::kSaveFileToken,
                                             sizeof(CurrentSetStorage::kSaveFileToken));
  file.close();
  return ok && SD.rename(tempPath, finalPath);
}

/// An empty-but-committed payload: structurally valid, restores to a slot that reports
/// `hasCommittedPasses() == false` for the pass set but a real `loopLengthTicks`. Used where the
/// fixture needs a payload that opens and parses, without asserting content.
inline bool writeEmptyCommittedPayloadToSd(uint8_t trackIndex, uint8_t slotIndex) {
  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = 1000u + static_cast<uint32_t>(trackIndex) * 16u + slotIndex;
  snapshot.loopLengthTicks = 0;
  snapshot.startLoopTick = 0;
  snapshot.loopStartTick = 0;
  snapshot.nextPassId = 1;
  snapshot.nextNoteId = 1;
  return writeSlotPayloadToSd(snapshot, trackIndex, slotIndex);
}

/// A payload with exactly one committed record pass holding one note pair, so a fixture can assert
/// that the restore reproduced *its own* content (not merely that a slot became non-empty).
///
/// `loopLengthTicks` is fixed at 768 (one bar at the suite's 96-tick grid) and the note pair sits at
/// ticks 8..16, well inside it.
inline bool writePayloadWithOneNoteToSd(uint8_t trackIndex, uint8_t slotIndex, uint8_t note = 60) {
  TEST_ASSERT_TRUE_MESSAGE(LoopEventStore::usedChunkCount() == 0,
                           "the payload builder owns the pool: release previous fixtures first");

  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(8, 1, note, 100)));
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOff(16, 1, note, 0)));
  CaptureChunkIdList captureIds;
  capture.detachChunksTo(captureIds);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(committedChunkIds,
                                                                              captureIds));

  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = 1000u + static_cast<uint32_t>(trackIndex) * 16u + slotIndex;
  snapshot.loopLengthTicks = 768;
  snapshot.startLoopTick = 0;
  snapshot.loopStartTick = 0;
  snapshot.nextPassId = 2;
  snapshot.nextNoteId = 2;
  snapshot.lastCommittedPassId = 1;
  snapshot.passes.recordPass.id = 1;
  snapshot.passes.recordPass.state = CapturePassState::Active;
  snapshot.passes.recordPass.sealedAtTick = 768;
  snapshot.passes.recordPass.committedChunkIds = committedChunkIds;
  return writeSlotPayloadToSd(snapshot, trackIndex, slotIndex);
}

/// The queue-side half of the production path. `StorageManager::requestLoopSlotRestoreFromSd` is the
/// runtime's own explicit-request entry (focus path: it prioritizes the slot and its neighbors and
/// enqueues only through the payload cache), so a fixture arming a request uses the same call the
/// select/focus path uses rather than a pop helper.
inline void armRestoreRequestThroughProductionQueue(uint8_t trackIndex, uint8_t slotIndex) {
  StorageManager::requestLoopSlotRestoreFromSd(trackIndex, slotIndex);
}

/// One production service turn: select (which may begin a job) then step it under the background
/// slice budget. This is the *production* deferred-load shape from
/// `StorageManager::processDeferredLoopSlotRestore` minus the scheduler wrapper, so a fixture drives
/// admission as the runtime does instead of stepping the job by hand.
inline void runLoadServiceTurn() {
  (void)StorageManager::selectSubmittedLoadJobs();
  StorageManager::stepSubmittedLoadJobs(LoadLoopBudget::BackgroundRestoreUs, false);
}

/// Drive up to \p maxTurns production service turns, stopping early when \p stop reports true.
/// Returns the number of turns actually run (a fixture asserts on it when a bounded wait matters).
inline uint32_t runLoadServiceTurns(uint32_t maxTurns, bool (*stop)() = nullptr) {
  uint32_t turns = 0;
  for (; turns < maxTurns; ++turns) {
    runLoadServiceTurn();
    if (stop != nullptr && stop()) {
      ++turns;
      break;
    }
  }
  return turns;
}

/// The observable "no restore work left" predicate: no queued request and neither job instance
/// active. Deliberately does not consult the terminal record the change will add later, so Packet A
/// fixtures can use it before that record exists.
inline bool restoreWorkIdle() {
  return StorageManagerInternal::pendingLoopSlotRestoreCount() == 0 &&
         !StorageManagerInternal::anyLoadLoopJobActive() && !SlotLoadSession::isActive();
}

/// One observable snapshot of the request's outcome, taken from the production state the runtime
/// already exposes. Packet A fixtures need to distinguish three things without the named-wait record
/// that Packet B adds:
///
///   * `served`     — the request reached the slot (terminal in Packet B's vocabulary);
///   * `progressed` — the job did measurable preparation work (bytes read or a phase advance);
///   * `stalled`    — none of the above: the request is still owed and nothing advanced.
struct RestoreOutcomeObservation {
  bool served = false;
  bool progressed = false;
  bool stalled = false;
  uint32_t turnsRun = 0;
  uint16_t pendingAtEnd = 0;
  uint8_t phaseAtEnd = 255;
  bool jobActiveAtEnd = false;
};

/// Drive production turns while observing whether the request is served or its preparation advances.
/// `progressed` is true only for real work: a phase advance, or a positive `bytesRead`/parsed payload
/// on either job instance. Being selected into a job is deliberately **not** progress — that is the
/// parked-job shape C2 describes.
inline RestoreOutcomeObservation observeRestoreOutcome(uint8_t trackIndex, uint8_t slotIndex,
                                                      uint32_t maxTurns) {
  RestoreOutcomeObservation observation{};
  uint8_t lastPhase = 255;
  for (uint32_t turn = 0; turn < maxTurns; ++turn) {
    runLoadServiceTurn();
    observation.turnsRun = turn + 1;

    bool active = false;
    uint8_t track = 0;
    uint8_t slot = 0;
    uint8_t phase = 0;
    uint8_t isFocus = 0;
    StorageManager::probeActiveLoadLoopJob(active, track, slot, phase, isFocus);
    if (active && phase != lastPhase) {
      observation.progressed = true;
      lastPhase = phase;
    }
    if (active) {
      size_t bytesRead = 0;
      size_t payloadSize = 0;
      StorageManager::probeLoadLoopJobProgress(bytesRead, payloadSize);
      if (bytesRead > 0) {
        observation.progressed = true;
      }
    }
    const bool committed =
        trackManager.getTrack(trackIndex).getLoop(slotIndex).hasCommittedPasses();
    const bool attempted = StorageManagerInternal::isLoopSlotRestoreAttempted(trackIndex, slotIndex);
    if (committed || attempted || restoreWorkIdle()) {
      observation.served = committed;
      break;
    }
  }

  bool active = false;
  uint8_t track = 0;
  uint8_t slot = 0;
  uint8_t phase = 0;
  uint8_t isFocus = 0;
  StorageManager::probeActiveLoadLoopJob(active, track, slot, phase, isFocus);
  observation.jobActiveAtEnd = active;
  observation.phaseAtEnd = phase;
  observation.pendingAtEnd = StorageManagerInternal::pendingLoopSlotRestoreCount();
  observation.stalled = !observation.served && !observation.progressed;
  return observation;
}

}  // namespace RestoreHarness
