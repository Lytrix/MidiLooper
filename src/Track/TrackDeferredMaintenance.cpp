//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "Globals.h"
#include "Logger.h"
#include "LoopEventStore.h"
#include "SlotLoadSession.h"
#include "StorageManager.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"
#include "VisualCache.h"

void Track::validateAndCleanupMidiEvents(uint32_t openTailCloseTick) {
    (void)openTailCloseTick;
    Loop& loop = getActiveLoop();
    MidiEventVec materializedEvents;
    loop.mergeActiveCapturePasses(materializedEvents);
    if (materializedEvents.empty()) return;

    const LoopEventValidation::LoopEventValidationResult invariantResult =
        LoopEventValidation::validateLoopEvents(materializedEvents, loop.loopLengthTicks,
                                                LoopEventValidation::kCanonicalInvariantMask);
    if (!invariantResult.passed) {
        logger.log(CAT_MIDI, LOG_WARNING,
                   "MIDI idle validate: non-canonical storage (check=%u); orphan repair log-only",
                   static_cast<unsigned>(invariantResult.firstFailure));
    }

    MidiEventVec repairProbe = materializedEvents;
    const LoopEventValidation::OrphanRepairResult repair =
        LoopEventValidation::repairOrphanNoteEvents(repairProbe, loop.loopLengthTicks,
                                                    Config::TICKS_PER_BAR);

    if (repair.orphanedRemoved > 0) {
        logger.log(CAT_MIDI, LOG_INFO,
                  "MIDI idle validate: would remove %d orphaned events (log-only v1), %d events remaining",
                  static_cast<int>(repair.orphanedRemoved),
                  static_cast<int>(repairProbe.size()));
    } else {
        logger.log(CAT_MIDI, LOG_INFO,
                  "MIDI validation complete: no orphaned events found, %d events total",
                  static_cast<int>(materializedEvents.size()));
    }
}

namespace {

// Wrap-pair verification is quadratic, so it only runs on loops small enough to afford it.
constexpr size_t kMaxWrapPairVerifyEvents = 512;
constexpr size_t kMaxDisplayNoteVerifyLines = 32;

#if defined(SESSION_CAPTURE)
// One stored-MIDI verification dump per boot — enough for a single HITL/evidence capture, then
// off so manual overdub retests are not blocked by merge+SEVT cost on every stop. Reboot to re-arm.
bool sStoredMidiVerificationArmed = true;

void logStoredMidiVerificationArmState(bool armed) {
  Serial.printf("#CAP,DIAG,stored_verify,armed,%u\n", armed ? 1u : 0u);
}
#endif

enum StoredVerificationPhase : uint8_t {
  kStoredVerificationNotes = 0,
  kStoredVerificationWrapPairs = 1,
  kStoredVerificationDisplayNotes = 2,
};

}  // namespace

TRACK_COLD_MEM void Track::resetDeferredStoredMidiVerification() {
  deferredStoredVerificationPending = false;
  deferredStoredVerificationPhase = kStoredVerificationNotes;
  deferredStoredVerificationCursor = 0;
  deferredStoredVerificationEvents.clear();
}

TRACK_COLD_MEM void Track::queueDeferredStoredMidiVerification() {
#if !defined(SESSION_CAPTURE)
  // Every emission in the drain compiles out without SESSION_CAPTURE; queueing would only buy a
  // flatten, a wrap-pair scan and a reconstruct that produce nothing.
  return;
#else
  resetDeferredStoredMidiVerification();
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
    return;
  }
  if (!sStoredMidiVerificationArmed) {
    return;
  }
  deferredStoredVerificationPending = true;
  sStoredMidiVerificationArmed = false;
  logStoredMidiVerificationArmState(false);
#endif
}

TRACK_COLD_MEM void Track::processDeferredStoredMidiVerification(size_t maxEventsPerSlice) {
#if !defined(SESSION_CAPTURE)
  (void)maxEventsPerSlice;
  return;
#else
  if (!deferredStoredVerificationPending) {
    return;
  }

  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
    resetDeferredStoredMidiVerification();
    return;
  }

  const uint32_t heapBeforeMerge = MemoryMonitor::getInternalHeapFreeBytes();
  if (!LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(heapBeforeMerge)) {
    resetDeferredStoredMidiVerification();
    return;
  }

  // Flatten once, on the first idle slice — never on the stop path.
  if (deferredStoredVerificationEvents.empty() && deferredStoredVerificationCursor == 0 &&
      deferredStoredVerificationPhase == kStoredVerificationNotes) {
    loop.mergeActiveCapturePasses(deferredStoredVerificationEvents);
    if (deferredStoredVerificationEvents.empty()) {
      resetDeferredStoredMidiVerification();
      return;
    }
  }

  const SessionMidiEventVec& flat = deferredStoredVerificationEvents;

  if (deferredStoredVerificationPhase == kStoredVerificationNotes) {
    size_t emitted = 0;
    while (deferredStoredVerificationCursor < flat.size() && emitted < maxEventsPerSlice) {
      const MidiEvent& evt = flat[deferredStoredVerificationCursor++];
      if (evt.isNoteOn()) {
        SC_STORED_NOTE_EVENT('N', evt.tick, evt.channel, evt.data.noteData.note);
      } else if (evt.isNoteOff()) {
        SC_STORED_NOTE_EVENT('F', evt.tick, evt.channel, evt.data.noteData.note);
      } else {
        continue;
      }
      ++emitted;
    }
    if (deferredStoredVerificationCursor < flat.size()) {
      return;
    }
    deferredStoredVerificationPhase = kStoredVerificationWrapPairs;
    return;
  }

  if (deferredStoredVerificationPhase == kStoredVerificationWrapPairs) {
    if (flat.size() <= kMaxWrapPairVerifyEvents) {
      for (size_t i = 0; i < flat.size(); ++i) {
        const MidiEvent& on = flat[i];
        if (!on.isNoteOn()) {
          continue;
        }
        for (const MidiEvent& off : flat) {
          if (!off.isNoteOff() || off.channel != on.channel ||
              off.data.noteData.note != on.data.noteData.note) {
            continue;
          }
          if (NoteUtils::isWrappedLoopNotePair(on.tick, off.tick, loop.loopLengthTicks)) {
            SC_STORED_WRAP_PAIR(on.tick, off.tick, on.channel, on.data.noteData.note);
            break;
          }
        }
      }
    }
    deferredStoredVerificationPhase = kStoredVerificationDisplayNotes;
    return;
  }

  const auto reconstructed = NoteUtils::reconstructNotes(flat, loop.loopLengthTicks, false);
  size_t reconLogged = 0;
  for (const NoteUtils::DisplayNote& note : reconstructed) {
    const uint32_t displayStart = IntervalProjection::noteRelativeTick(
        note.startTick, loop.loopStartTick, loop.loopLengthTicks);
    // DisplayNote.endTick is a display boundary tick; when a note ends at loop wrap its tail
    // segment ends at loopLength-1, but its exclusive end is loopLength.
    uint32_t length = 0;
    if (loop.loopLengthTicks > 0 && note.endTick >= note.startTick) {
      const uint32_t endExclusive =
          (note.endTick == loop.loopLengthTicks - 1) ? loop.loopLengthTicks : note.endTick;
      length = endExclusive > note.startTick ? (endExclusive - note.startTick) : 0;
    }
    SC_DNTE(note.note, note.startTick, displayStart, length, static_cast<int>(reconLogged));
    ++reconLogged;
    if (reconLogged >= kMaxDisplayNoteVerifyLines) {
      break;
    }
  }
  resetDeferredStoredMidiVerification();
#endif  // SESSION_CAPTURE
}

void Track::processDeferredIdleMaintenance(uint32_t nowMs) {
  // REVT and stored-MIDI verification: emit only when transport is fully idle (58d6c08 reference).
  // Verification is one-shot per boot; it must not drain during PLAYING — the first slice still
  // does a full merge and blocks MIDI for seconds on grown loops (session_20260813_020631).
  if (!isPlaying() && !isRecording() && !isOverdubbing() && !isStoppedRecording()) {
    size_t revtSlice = 64;
    size_t verificationSlice = 64;
    if (StorageManager::hasDeferredSaveWork()) {
      revtSlice = 8;
      verificationSlice = 16;
    }
    processDeferredRecordRevts(revtSlice);
    processDeferredStoredMidiVerification(verificationSlice);
  }

  const bool deferredDerivedViewMaintenance =
      (isPlaying() || isStoppedRecording() || isOverdubbing()) && !isRecording();
  if (deferredDerivedViewMaintenance) {
    Loop& loop = getActiveLoop();
    if (loop.hasCommittedPasses() && loop.visualCacheDirty) {
      uint8_t barsPerSlice = 4;
      if (StorageManager::hasDeferredSaveWork()) {
        barsPerSlice = 2;
      }
      uint32_t playheadBar = 0;
      if (loop.lastTickInLoop != UINT32_MAX) {
        playheadBar = visualBarForTick(loop.lastTickInLoop, Config::TICKS_PER_BAR);
      }
      const uint32_t totalBars =
          (loop.loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
      // One slice per maintenance: alternate playhead vs loop-tail priority so tail fills
      // over time without doubling gather cost in one main-loop iteration (122003 MIDI lag).
      static bool sPrioritizeLoopTailVisualCache = false;
      uint32_t priorityBar = playheadBar;
      if (totalBars > 0 && sPrioritizeLoopTailVisualCache) {
        priorityBar = totalBars - 1;
      }
      sPrioritizeLoopTailVisualCache = !sPrioritizeLoopTailVisualCache;
      // PLAYING/OVERDUB: backfill near the playhead/paint window so idle reconstruct does not
      // race the OLED path across a full long loop (session_20260811_030614).
      constexpr uint32_t kPlayingVisualCacheNeighborhoodBars =
          DisplayWindowUtils::kMaxDetailedWindowBars + 4u;
      uint32_t maxBarDistanceFromPriority = kPlayingVisualCacheNeighborhoodBars;
      // Partial adopt (RC-E): some bars clean and some dirty — the neighborhood cap leaves a
      // dead zone on long loops (bars 36–62 on an 84-bar loop). Allow any dirty bar while mixed.
      if (loop.visualCache.dirtyBars.size() == totalBars && totalBars > 0) {
        bool sawCleanBar = false;
        bool sawDirtyBar = false;
        for (uint8_t flag : loop.visualCache.dirtyBars) {
          if (flag != 0) {
            sawDirtyBar = true;
          } else {
            sawCleanBar = true;
          }
          if (sawCleanBar && sawDirtyBar) {
            maxBarDistanceFromPriority = UINT32_MAX;
            break;
          }
        }
      }
      loop.rebuildVisualCacheIdleSlice(barsPerSlice, priorityBar, maxBarDistanceFromPriority);
    }
    // R1A: when PLAYING visual cache is clean, prebuild overdub source view off the button path.
    if (isPlaying() && !isOverdubbing() && loop.hasCommittedPasses()) {
      loop.stepOverdubSourceViewPrebuild();
    }
  }

  if (!isPlaying() && !isRecording() && !isOverdubbing() && !isStoppedRecording()) {
    Loop& loop = getActiveLoop();
    if (loop.hasCommittedPasses()) {
      // Phase 3: queued background restores must not block idle visual work.
      const bool bootHydrateActive =
          SlotLoadSession::isActive() || StorageManager::hasPendingUndoSnapshotHydrate();
      const bool deferHeavyDerivedView =
          bootHydrateActive || StorageManager::hasDeferredSaveWork();
      const bool avoidFullVisual =
          loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks) || deferHeavyDerivedView;
      if (!loop.isPassesMaterializedStoreFresh() && !deferHeavyDerivedView && !avoidFullVisual) {
        loop.materializeEditViewFromPasses();
      }
      if (loop.visualCacheDirty) {
        // Budget-driven: one idle slice per call (bars), never full ensure when avoidFullVisual.
        uint8_t barsPerSlice = StorageManager::hasDeferredSaveWork() ? 2 : 4;
        if (avoidFullVisual) {
          loop.rebuildVisualCacheIdleSlice(barsPerSlice, 0);
        } else {
          loop.ensureVisualCacheBuilt();
        }
      }
    }
  }

  if (!DeferredValidatePolicy::shouldRunDeferredFullValidate(
          deferredFullMidiValidate, isPlaying(), isRecording(), isOverdubbing(),
          deferredValidateQueuedAtMs, nowMs)) {
    if (deferredFullMidiValidate && deferredValidateQueuedAtMs == 0) {
      deferredValidateQueuedAtMs = nowMs;
    }
    return;
  }
  deferredFullMidiValidate = false;
  deferredValidateQueuedAtMs = 0;
  validateAndCleanupMidiEvents();
}

void Track::resetDeferredRecordRevts() {
  deferredRecordRevtsPending = false;
  deferredRecordRevtChunkScan = false;
  deferredRecordRevtCursor = 0;
  deferredRecordRevtEvents.clear();
  deferredRecordRevtChunkRefs.clear();
  deferredRecordRevtChunkCursor = 0;
  deferredRecordRevtChunkEvents.clear();
  deferredRecordRevtChunkEventCursor = 0;
}

void Track::queueDeferredRecordRevts() {
  const Loop& loop = getActiveLoop();
  if (!loop.hasCommittedPasses()) {
    resetDeferredRecordRevts();
    return;
  }

  resetDeferredRecordRevts();
  deferredRecordRevtsPending = true;

  const bool hasActiveRecordPass =
      loop.passes.hasRecordPass() &&
      loop.passes.recordPass.state == CapturePassState::Active &&
      !loop.passes.recordPass.committedChunkIds.empty();
  bool hasActiveOverdubPass = false;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      hasActiveOverdubPass = true;
      break;
    }
  }

  // Fast path: record-stop baseline has one active record pass and no active overdub passes.
  if (hasActiveRecordPass && !hasActiveOverdubPass) {
    deferredRecordRevtChunkScan = true;
    if (!LoopEventStore::tryCopyCommittedChunkIds(deferredRecordRevtChunkRefs,
                                                  loop.passes.recordPass.committedChunkIds)) {
      resetDeferredRecordRevts();
    }
  }
}

void Track::processDeferredRecordRevts(size_t maxEventsPerSlice) {
  if (!deferredRecordRevtsPending) {
    return;
  }

  const Loop& loop = getActiveLoop();
  if (!loop.hasCommittedPasses()) {
    resetDeferredRecordRevts();
    return;
  }

  if (deferredRecordRevtChunkScan) {
    size_t queued = 0;
    while (queued < maxEventsPerSlice) {
      if (deferredRecordRevtChunkEventCursor >= deferredRecordRevtChunkEvents.size()) {
        deferredRecordRevtChunkEvents.clear();
        deferredRecordRevtChunkEventCursor = 0;
        if (deferredRecordRevtChunkCursor >= deferredRecordRevtChunkRefs.size()) {
          break;
        }
        const uint16_t chunkId = deferredRecordRevtChunkRefs[deferredRecordRevtChunkCursor++];
        LoopEventStore::appendChunkRefEvent(chunkId, deferredRecordRevtChunkEvents);
        continue;
      }

      const MidiEvent& evt =
          deferredRecordRevtChunkEvents[deferredRecordRevtChunkEventCursor++];
      if (!evt.isNoteOn()) {
        continue;
      }
      SC_REC_QUEUE_STORED_NOTE_ON(evt.tick, evt.channel, evt.data.noteData.note);
      ++queued;
    }

    const bool done =
        deferredRecordRevtChunkCursor >= deferredRecordRevtChunkRefs.size() &&
        deferredRecordRevtChunkEventCursor >= deferredRecordRevtChunkEvents.size();
    if (done) {
      logger.log(CAT_TRACK, LOG_DEBUG, "Queued REVT note-ons (deferred): %d",
                 static_cast<int>(queued));
      resetDeferredRecordRevts();
    }
    return;
  }

  if (deferredRecordRevtEvents.empty() && deferredRecordRevtCursor == 0) {
    loop.mergeActiveCapturePasses(deferredRecordRevtEvents);
  }

  size_t queued = 0;
  while (deferredRecordRevtCursor < deferredRecordRevtEvents.size() &&
         queued < maxEventsPerSlice) {
    const MidiEvent& evt = deferredRecordRevtEvents[deferredRecordRevtCursor++];
    if (evt.isNoteOn()) {
      SC_REC_QUEUE_STORED_NOTE_ON(evt.tick, evt.channel, evt.data.noteData.note);
      ++queued;
    }
  }

  if (deferredRecordRevtCursor >= deferredRecordRevtEvents.size()) {
    logger.log(CAT_TRACK, LOG_DEBUG, "Queued REVT note-ons (deferred): %d",
               static_cast<int>(queued));
    resetDeferredRecordRevts();
  }
}
