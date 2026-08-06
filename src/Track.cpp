//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Track.h"
#include <new>
#include "Logger.h"
#include "MidiHandler.h"
#include "ClockManager.h"
#include "StorageManager.h"
#include "SlotLoadSession.h"
#include "stdint.h"
#include <unordered_map>
#include <utility>
#include <algorithm>
#include "Globals.h"
#include "TickPhase.h"
#include "TrackStateMachine.h"
#include "TrackUndo.h"
#include "LooperState.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/NoteUtils.h"
#include "TrackInternal.h"

#include "Utils/IntervalProjection.h"
#include "Utils/PlaybackCursorAdvance.h"
#include "Utils/RecordStopLength.h"
#include "Utils/TrackMem.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "LoopEditManager.h"
#include "TrackManager.h"

extern TrackManager trackManager;

namespace {

ProjectionContext makePlaybackContext(const Track& track, const Loop& loop, uint32_t currentTick) {
  return IntervalProjection::buildPlaybackProjectionContext(
      loop.loopLengthTicks,
      IntervalProjection::makeFullLoopPlaybackWindow(loop.loopLengthTicks),
      static_cast<int32_t>(currentTick), track.getProjectionCycleStartTick(),
      static_cast<int32_t>(loop.loopStartTick), track.hasQueuedPlaybackStart(),
      track.getQueuedStartTick());
}

/// After a mid-pass playback-order rebuild, point nextEventIndex at the current playhead.
void reanchorPlaybackIndex(Loop& loop, const SessionMidiEventVec& mergedEvents, const PlaybackOrderVec& order,
                           const ProjectionContext& playbackContext) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.nextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < order.size()) {
    const MidiEvent& e = mergedEvents[order[idx]];
    const uint32_t evPhase =
        IntervalProjection::playbackEventPhase(e.tick, playbackContext.loopLength);
    if (evPhase > loop.lastTickInLoop) break;
    ++idx;
  }
  loop.nextEventIndex = static_cast<uint16_t>(idx);
}

void ensurePlaybackMergedMidiEventsBuilt(Track& track, Loop& loop, LoopPlaybackRuntime& runtime,
                               bool /*allowHeavyBuild*/, uint32_t currentTick) {
  // Projection boundary (linear-loop-tick-storage): mergedEvents are read-only input to
  // playback order + MIDI send. NOTE_EDIT uses session store (Tier 2) — full replace, no
  // materialized underlay. Outside NOTE_EDIT and live capture, chunk-ref merge only (DEC-016).
  // Long loops use windowed gather — full-loop gather after LoadLoopJob Commit hard-faults
  // (session_20260718_210532 / 210001).
  static bool playbackWindowBuildInProgress = false;
  if (playbackWindowBuildInProgress) {
    return;
  }
  const bool noteEditPreview = editManager.isNoteEditActive() &&
                               &track == &trackManager.getSelectedTrack();
  const uint32_t windowRevision = noteEditPreview ? editManager.sessionPlaybackPreviewRevision()
                                                    : loop.playbackRevision;
  const bool longLoop = loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks);
  uint32_t playhead = 0;
  if (loop.loopLengthTicks > 0) {
    playhead = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks);
  }

  if (!longLoop) {
    if (runtime.mergedMidiEvents.builtFromRevision == windowRevision) {
      DIAG_COUNTER_INC(PlaybackDeferredReuse);
      return;
    }
  } else if (runtime.mergedMidiEvents.builtFromRevision == windowRevision &&
             !runtime.mergedMidiEvents.empty() &&
             runtime.mergedMidiEvents.windowLengthTicks > 0) {
    const uint32_t winStart = runtime.mergedMidiEvents.windowStartTick;
    const uint32_t winEnd = winStart + runtime.mergedMidiEvents.windowLengthTicks;
    const uint32_t margin = Config::TICKS_PER_BAR / 2;
    if (playhead + margin >= winStart && playhead < winEnd) {
      DIAG_COUNTER_INC(PlaybackDeferredReuse);
      return;
    }
  }

  playbackWindowBuildInProgress = true;
  const uint32_t playbackBuildStartUs = micros();
  DIAG_COUNTER_INC(PlaybackMergedMidiEventsRebuild);
  if (noteEditPreview) {
    const MidiEventVec& preview = editManager.sessionMidiEvents();
    runtime.mergedMidiEvents.mergedEvents.assign(preview.begin(), preview.end());
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  } else if (longLoop) {
    // Two bars centered on playhead — enough for LoopEnd launch + clock catch-up.
    constexpr uint32_t kPlaybackWindowBars = 2;
    const uint32_t winLen = kPlaybackWindowBars * Config::TICKS_PER_BAR;
    uint32_t winStart = playhead > (winLen / 2) ? playhead - (winLen / 2) : 0;
    if (winStart + winLen > loop.loopLengthTicks) {
      winStart = loop.loopLengthTicks > winLen ? loop.loopLengthTicks - winLen : 0;
    }
    if (loop.captureActive()) {
      loop.gatherCommittedEventsInWindowWithCapture(runtime.mergedMidiEvents.mergedEvents, winStart,
                                                    winLen);
    } else {
      loop.gatherCommittedEventsInWindow(runtime.mergedMidiEvents.mergedEvents, winStart, winLen);
    }
    runtime.mergedMidiEvents.windowStartTick = winStart;
    runtime.mergedMidiEvents.windowLengthTicks = winLen;
  } else if (!loop.captureActive()) {
    loop.gatherCommittedEventsForDerivedView(runtime.mergedMidiEvents.mergedEvents);
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  } else {
    loop.gatherCommittedEventsWithCapture(runtime.mergedMidiEvents.mergedEvents);
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  }
  runtime.mergedMidiEvents.builtFromRevision = windowRevision;
  loop.playbackOrderDirty = true;
  DIAG_TIMING_RECORD(PlaybackBuild, micros() - playbackBuildStartUs);
  playbackWindowBuildInProgress = false;
}

void rebuildPlaybackOrder(Loop& loop, const SessionMidiEventVec& mergedEvents,
                          const ProjectionContext& playbackContext) {
  PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();
  playbackOrder.resize(mergedEvents.size());
  for (size_t i = 0; i < mergedEvents.size(); i++) {
    playbackOrder[i] = i;
  }
  std::vector<uint32_t, ExternalMemoryFirstAllocator<uint32_t>> sortPhases(mergedEvents.size());
  for (size_t i = 0; i < mergedEvents.size(); ++i) {
    sortPhases[i] =
        IntervalProjection::playbackEventPhase(mergedEvents[i].tick, playbackContext.loopLength);
  }
  std::sort(playbackOrder.begin(), playbackOrder.end(), [&](size_t a, size_t b) {
    if (sortPhases[a] != sortPhases[b]) {
      return sortPhases[a] < sortPhases[b];
    }
    return mergedEvents[a].tick < mergedEvents[b].tick;
  });
  loop.playbackOrderDirty = false;
}

void reanchorCaptureIndex(Loop& loop) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.captureNextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < loop.capture.store.size()) {
    const MidiEvent& e = loop.capture.store.at(idx);
    if (e.tick >= loop.loopLengthTicks || e.tick > loop.lastTickInLoop) break;
    ++idx;
  }
  loop.captureNextEventIndex = static_cast<uint16_t>(idx);
}

struct MergedPlaybackStreamCtx {
  const PlaybackOrderVec* order = nullptr;
  const SessionMidiEventVec* merged = nullptr;
};

size_t mergedPlaybackStreamSize(const void* ctx) {
  return static_cast<const MergedPlaybackStreamCtx*>(ctx)->order->size();
}

bool mergedPlaybackStreamValid(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const MergedPlaybackStreamCtx*>(ctx);
  if (static_cast<size_t>(cursor) >= stream->order->size()) {
    return true;
  }
  return (*stream->order)[cursor] < stream->merged->size();
}

const MidiEvent& mergedPlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const MergedPlaybackStreamCtx*>(ctx);
  return (*stream->merged)[(*stream->order)[cursor]];
}

uint32_t mergedPlaybackStreamPhase(const MidiEvent& evt, const ProjectionContext& playbackContext) {
  return IntervalProjection::playbackEventPhase(evt.tick, playbackContext.loopLength);
}

size_t capturePlaybackStreamSize(const void* ctx) {
  return static_cast<const Loop*>(ctx)->capture.store.size();
}

const MidiEvent& capturePlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  return static_cast<const Loop*>(ctx)->capture.store.at(cursor);
}

uint32_t capturePlaybackStreamPhase(const MidiEvent& evt, const ProjectionContext& playbackContext) {
  return IntervalProjection::projectPlaybackEventPhase(evt.tick, playbackContext);
}

PlaybackEventStream makeMergedPlaybackStream(MergedPlaybackStreamCtx& ctx) {
  return PlaybackEventStream{&ctx, mergedPlaybackStreamSize, mergedPlaybackStreamValid,
                             mergedPlaybackStreamEventAt, mergedPlaybackStreamPhase};
}

PlaybackEventStream makeCapturePlaybackStream(Loop& loop) {
  return PlaybackEventStream{&loop, capturePlaybackStreamSize, nullptr,
                             capturePlaybackStreamEventAt, capturePlaybackStreamPhase};
}

}  // namespace

// -------------------------
// Track class implementation
// -------------------------
Track::Track() :
  isPlayingBack(false),
  muted(false),
  midiChannel(1),
  activeLoopIndex(0),
  trackState(TRACK_EMPTY),
  jamStartTick(UINT32_MAX),
  jamLength(0),
  jamTick(0),
  jamPlaybackActive(false),
  alignLoopOriginOnNextStop(false),
  recordAddedNoteOnCount(0) {
}

Track::~Track() = default;

void Track::resetPlaybackState(uint32_t currentTick) {
  Loop& loop = getActiveLoop();
  loop.nextEventIndex = 0;
  invalidatePlaybackMergedMidiEvents(true);
  if (loop.loopLengthTicks == 0) {
    loop.lastTickInLoop = 0;
    projectionCycleStartTick = static_cast<int32_t>(currentTick);
    return;
  }
  const uint32_t phase =
      tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  projectionCycleStartTick = static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  loop.lastTickInLoop = phase;
}

void Track::resetPlaybackStateForSlot(uint8_t slotIndex, uint32_t currentTick) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  Loop& loop = getLoop(slotIndex);
  if (loop.loopLengthTicks == 0) return;
  loop.nextEventIndex = 0;
  const uint32_t phase =
      tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  if (slotIndex == activeLoopIndex) {
    projectionCycleStartTick = static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
  loop.lastTickInLoop = phase;
  invalidatePlaybackMergedMidiEvents(true);
}

void Track::invalidatePlaybackMergedMidiEvents(bool preserveLedger) {
  bumpPlaybackGeneration();
  playbackRuntime.resetAll(preserveLedger);
}

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

bool Track::handleNoteEditFold(bool endInPlaying, uint32_t currentTick, uint32_t closeTick,
                               uint32_t stopStartUs) {
  (void)closeTick;
  if (!editManager.isNoteEditActive()) {
    return false;
  }
  Loop& loop = getActiveLoop();
  finalizePendingNotes(currentTick);
  if (!loop.capture.store.empty() && loop.loopLengthTicks > 0) {
    loop.ensureCaptureEventsSorted();
    loop.assignMissingNoteIdsInStore(loop.capture.store);
    loop.finalizeCaptureWrapWindowAtStop(currentTick);
  }
  editManager.foldLiveCaptureIntoNoteEditSession(*this);
  pendingNotes.clear();
  if (endInPlaying) {
    const uint32_t stateHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
    const uint32_t stateStartUs = micros();
    sendAllNotesOff();
    resetPlaybackState(currentTick);
    setState(TRACK_PLAYING);
    logOverdubStopStage(loop, stopStartUs, "set_state", micros() - stateStartUs, stateHeapBefore,
                        MemoryMonitor::getInternalHeapFreeBytes(), "in_edit");
    const uint32_t flushHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
    const uint32_t flushStartUs = micros();
    SC_REC_FLUSH_PENDING_REVTS(256);
    logOverdubStopStage(loop, stopStartUs, "flush", micros() - flushStartUs, flushHeapBefore,
                        MemoryMonitor::getInternalHeapFreeBytes(), "ok");
    logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
    logger.logTrackEvent("Overdubbing stopped", currentTick);
    logger.info("Overdub stopped (in-edit fold): events=%d, undo_entries=%d",
                static_cast<int>(loop.displayEventCountHint()), TrackUndo::getUndoCount(*this));
    emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
    logOverdubStopStage(loop, stopStartUs, "display", 0, MemoryMonitor::getInternalHeapFreeBytes(),
                        MemoryMonitor::getInternalHeapFreeBytes(), "ok");
    HotPathTelemetry::requestDeferredSummary("overdub_stop");
  } else {
    logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
    setState(TRACK_STOPPED);
    resetPlaybackState(currentTick);
    displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
    logger.logTrackEvent("Overdubbing stopped (to STOPPED)", currentTick);
    HotPathTelemetry::requestDeferredSummary("overdub_stop_to_stopped");
  }
  return true;
}

void Track::emitStoredMidiVerification() const {
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
    return;
  }

  const uint32_t heapBeforeMerge = MemoryMonitor::getInternalHeapFreeBytes();
  if (!LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(heapBeforeMerge)) {
    return;
  }

  SessionMidiEventVec flat;
  loop.mergeActiveCapturePasses(flat);
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn()) {
      SC_STORED_NOTE_EVENT('N', evt.tick, evt.channel, evt.data.noteData.note);
    } else if (evt.isNoteOff()) {
      SC_STORED_NOTE_EVENT('F', evt.tick, evt.channel, evt.data.noteData.note);
    }
  }

  constexpr size_t kMaxWrapPairVerifyEvents = 512;
  if (flat.size() > kMaxWrapPairVerifyEvents) {
    return;
  }

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

#if defined(SESSION_CAPTURE)
  const auto reconstructed = NoteUtils::reconstructNotes(flat, loop.loopLengthTicks, false);
  size_t reconLogged = 0;
  for (const DisplayNote& note : reconstructed) {
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
    if (reconLogged >= 32) {
      break;
    }
  }
#endif
}

void Track::processDeferredIdleMaintenance(uint32_t nowMs) {
  // REVT: emit only when transport is not PLAYING (58d6c08 reference); ring-queue holds
  // note-ons until flush when idle. Skip during STOPPED_RECORDING stop tail.
  if (!isPlaying() && !isRecording() && !isOverdubbing() && !isStoppedRecording()) {
    size_t revtSlice = 64;
    if (StorageManager::hasDeferredSaveWork()) {
      revtSlice = 8;
    }
    processDeferredRecordRevts(revtSlice);
  }

  const bool deferredDerivedViewMaintenance =
      (isPlaying() || isStoppedRecording()) && !isRecording() && !isOverdubbing();
  if (deferredDerivedViewMaintenance) {
    Loop& loop = getActiveLoop();
    if (loop.hasCommittedPasses() && loop.visualCacheDirty) {
      uint8_t barsPerSlice = 4;
      if (StorageManager::hasDeferredSaveWork()) {
        barsPerSlice = 2;
      }
      uint32_t priorityBar = 0;
      if (loop.lastTickInLoop != UINT32_MAX) {
        priorityBar = visualBarForTick(loop.lastTickInLoop, Config::TICKS_PER_BAR);
      }
      loop.rebuildVisualCacheIdleSlice(barsPerSlice, priorityBar);
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

void Track::prewarmPlaybackForSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  Loop& loop = loopForSlot(slotIndex);
  (void)playbackRuntime.trySlot(slotIndex);
  (void)loop.getPlaybackOrder();
}

void Track::ensurePlaybackMergedEventsForSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  Loop& loop = loopForSlot(slotIndex);
  LoopPlaybackRuntime* runtime = playbackRuntime.trySlot(slotIndex);
  if (runtime == nullptr) {
    return;
  }
  (void)loop.getPlaybackOrder();
  // Build destination merged MIDI before LoopEnd commit so launch is a cache hit.
  if (loop.hasCommittedPasses() && loop.loopLengthTicks > 0) {
    const uint32_t currentTick = clockManager.getCurrentTick();
    ensurePlaybackMergedMidiEventsBuilt(*this, loop, *runtime, true, currentTick);
    if (loop.playbackOrderDirty) {
      const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
      ::rebuildPlaybackOrder(loop, runtime->mergedMidiEvents.mergedEvents, playbackContext);
    }
  }
}

bool Track::isPlaybackMergedMidiEventsReadyForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return false;
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return false;
  }
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  return runtime->mergedMidiEvents.builtFromRevision == loop.playbackRevision &&
         !runtime->mergedMidiEvents.mergedEvents.empty();
}

void Track::releasePlaybackMergedMidiEventsMemory() {
  playbackRuntime.resetAll(false);
}

TRACK_COLD_MEM bool Track::tryReleasePlaybackMergedMidiEventsMemory() {
  if (isRecording() || isOverdubbing() || getState() == TRACK_ARMED) {
    return false;
  }

  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  const bool selected = trackManager.isSelectedTrack(*this);
  const bool noteEditActive = editManager.isNoteEditActive();
  const bool transportActive = isPlaying();
  bool reclaimed = false;

  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    Loop& loop = loopForSlot(slot);
    if (!trackManager.isSlotEnabled(trackIndex, slot) && !loop.hasCommittedPasses()) {
      continue;
    }

    // Never allocate here — under Low pressure after a 57KB LoadLoopJob parse, trySlot can
    // fail and slot() null-derefs (session_20260719_000205: silence after parse_leave before
    // apply_enter on the next frame's reclaim).
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime == nullptr || runtime->mergedMidiEvents.empty()) {
      continue;
    }

    const bool noteEditPreview =
        noteEditActive && selected && slot == getActiveLoopIndex();
    if (noteEditPreview) {
      continue;
    }

    const uint32_t windowRevision = loop.playbackRevision;
    const bool windowStale = runtime->mergedMidiEvents.builtFromRevision != windowRevision;
    if (transportActive && !windowStale) {
      continue;
    }

    runtime->mergedMidiEvents.clear();
    reclaimed = true;
  }
  return reclaimed;
}

TRACK_COLD_MEM bool Track::tryClearCommittedMidiScratch() {
  if (editManager.isNoteEditActive() && trackManager.isSelectedTrack(*this)) {
    return false;
  }
  if (committedMidiScratch_.empty()) {
    return false;
  }
  committedMidiScratch_.clear();
  committedMidiScratchRevision_ = UINT32_MAX;
  return true;
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

// -------------------------
// Start playing
// -------------------------

void Track::reanchorPlaybackProjection(uint32_t currentTick, bool preserveLoopPhaseOrigin) {
  Loop& loop = getActiveLoop();
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = UINT32_MAX;
  if (!preserveLoopPhaseOrigin) {
    loop.startLoopTick = 0;
    // Fresh transport: play from storage tick 0; drop record-time display offset so
    // playhead, LEDs, and playbackEventPhase share one coordinate frame.
    loop.loopStartTick = 0;
    projectionCycleStartTick = static_cast<int32_t>(currentTick);
    for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
      getLoop(slotIndex).lastTickInLoop = UINT32_MAX;
    }
    // Keep LOOP_EDIT baseline aligned with the playback frame so preview depart cannot
    // restore a stale SD loopStartTick onto the live loop (session_20260717_234742).
    if (editManager.isLoopEditSession()) {
      loopEditManager.onGlobalGeometryRestored(*this);
    }
  } else if (loop.loopLengthTicks > 0) {
    const uint32_t phase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
    projectionCycleStartTick =
        static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
}

void Track::startPlaying(uint32_t currentTick, bool preserveLoopPhaseOrigin) {
  Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks > 0) {
    if (trackState == TRACK_EMPTY) {
      forceSetState(TRACK_STOPPED);
    }
    if (!setState(TRACK_PLAYING)) return;
    reanchorPlaybackProjection(currentTick, preserveLoopPhaseOrigin);
    logger.logTrackEvent("Playback started", currentTick);
  }
}

// -------------------------
// Start overdubbing
// -------------------------

void Track::startOverdubbing(uint32_t currentTick) {
  Loop& loopRef = getActiveLoop();
  if (trackState == TRACK_OVERDUBBING && loopRef.capture.phase == CapturePhase::Overdub) {
    return;
  }
  const uint32_t telemetryStartUs = micros();
  const uint32_t heapAtEnter = MemoryMonitor::getInternalHeapFreeBytes();
  SC_ODUB_STAGE("enter", 0, heapAtEnter, heapAtEnter, "ok");
  const Loop& active = loopRef;
  if (trackState == TRACK_EMPTY && active.loopLengthTicks > 0) {
    forceSetState(TRACK_STOPPED);
  }
  const uint32_t stateAdvanceStartUs = micros();
  if (!setState(TRACK_OVERDUBBING)) {
    SC_ODUB_STAGE("set_state", micros() - stateAdvanceStartUs, heapAtEnter,
                  MemoryMonitor::getInternalHeapFreeBytes(), "failed");
    return;
  }
  SC_ODUB_STAGE("set_state", micros() - stateAdvanceStartUs, heapAtEnter,
                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
  recordAddedNoteOnCount = 0;
  Loop& loop = getActiveLoop();
  loop.markDisplayCachesStale();
  const uint32_t captureStartUs = micros();
  loop.beginCapture(CapturePhase::Overdub);
  SC_ODUB_STAGE("begin_capture", micros() - captureStartUs, heapAtEnter,
                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
  if (loop.loopLengthTicks > 0) {
    const uint32_t phase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
    projectionCycleStartTick =
        static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
  const uint32_t undoStartUs = micros();
  TrackUndo::beginOverdubSession(*this);
  SC_ODUB_STAGE("undo_session", micros() - undoStartUs, heapAtEnter,
                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
  logger.info("Overdub session opened: events=%d, undo_entries=%d",
              static_cast<int>(loop.displayEventCountHint()),
              static_cast<int>(TrackUndo::getUndoCount(*this)));
  HotPathTelemetry::recordOverdubStart(micros() - telemetryStartUs,
                                       static_cast<uint32_t>(loop.displayEventCountHint()),
                                       static_cast<uint32_t>(TrackUndo::getUndoCount(*this)));
  SC_ODUB_STAGE("complete", micros() - telemetryStartUs, heapAtEnter,
                MemoryMonitor::getInternalHeapFreeBytes(), "ok");
  logger.logTrackEvent("Overdubbing started", currentTick);
}


void Track::stopOverdubbing() {
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
  const uint32_t stopStartUs = micros();
  const uint32_t heapAtEnter = MemoryMonitor::getInternalHeapFreeBytes();
  logOverdubStopStage(loop, stopStartUs, "enter", 0, heapAtEnter, heapAtEnter, "entered");
  SC_REC_FLUSH_PENDING_REVTS(8);
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = capturePhaseTick(currentTick);
  }
  if (handleNoteEditFold(true, currentTick, closeTick, stopStartUs)) {
    return;
  }
  finalizePendingNotes(currentTick);
  const uint32_t sealHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t sealStartUs = micros();
  const CommitResult sideEffectResult =
      commitCaptureForStop(CommitReason::OverdubStop, currentTick, closeTick);
  // Stage order seal → finalize preserved; duration covers seal+finalize together on seal.
  logOverdubStopStage(loop, stopStartUs, "seal", micros() - sealStartUs, sealHeapBefore,
                      MemoryMonitor::getInternalHeapFreeBytes(),
                      commitResultLabel(sideEffectResult));
  logOverdubStopStage(loop, stopStartUs, "finalize", 0, MemoryMonitor::getInternalHeapFreeBytes(),
                      MemoryMonitor::getInternalHeapFreeBytes(),
                      commitResultLabel(sideEffectResult));
  const uint32_t stateHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t stateStartUs = micros();
  // Wire-only silence before resuming loop playback; stored capture is unchanged.
  sendAllNotesOff();
  resetPlaybackState(currentTick);
  setState(TRACK_PLAYING);
  logOverdubStopStage(loop, stopStartUs, "set_state", micros() - stateStartUs, stateHeapBefore,
                      MemoryMonitor::getInternalHeapFreeBytes(), "ok");
  const uint32_t flushHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t flushStartUs = micros();
  SC_REC_FLUSH_PENDING_REVTS(256);
  logOverdubStopStage(loop, stopStartUs, "flush", micros() - flushStartUs, flushHeapBefore,
                      MemoryMonitor::getInternalHeapFreeBytes(), "ok");
  logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
  logger.logTrackEvent("Overdubbing stopped", currentTick);
  logger.info("Overdub stopped: events=%d, undo_entries=%d", static_cast<int>(loop.displayEventCountHint()),
              TrackUndo::getUndoCount(*this));

  emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
  logOverdubStopStage(loop, stopStartUs, "display", 0, MemoryMonitor::getInternalHeapFreeBytes(),
                      MemoryMonitor::getInternalHeapFreeBytes(), "ok");
  HotPathTelemetry::requestDeferredSummary("overdub_stop");
}

void Track::stopOverdubbingToStopped() {
  if (isEmpty()) return;
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = capturePhaseTick(currentTick);
  }
  sendAllNotesOff();
  if (handleNoteEditFold(false, currentTick, closeTick, /*stopStartUs=*/0)) {
    return;
  }
  finalizePendingNotes(currentTick);
  commitCaptureForStop(CommitReason::OverdubStopToStopped, currentTick, closeTick);
  logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
  setState(TRACK_STOPPED);
  resetPlaybackState(currentTick);
  displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
  logger.logTrackEvent("Overdubbing stopped (to STOPPED)", currentTick);
  HotPathTelemetry::requestDeferredSummary("overdub_stop_to_stopped");
}

// -------------------------
// Stop playing
// -------------------------

void Track::stopPlaying() {
  if (isEmpty()) return; // Nothing to stop, empty track
  sendAllNotesOff();  // first kill all sounding notes

  // then transition to the stopped state
  setState(TRACK_STOPPED);
  logger.logTrackEvent("Playback stopped", clockManager.getCurrentTick());
  displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, clockManager.getCurrentTick());
  HotPathTelemetry::requestDeferredSummary("playback_stop");
}

// -------------------------
// Toggle play/stop
// -------------------------

void Track::togglePlayStop() {
  isPlaying() ? stopPlaying() : startPlaying(clockManager.getCurrentTick());
}

// -------------------------
// Track Clear
// -------------------------

TRACK_COLD_MEM void Track::clear() {
    if (trackState == TRACK_EMPTY) {
        logger.debug("Track already empty; ignoring clear");
        return;
    }

    Loop& loop = getActiveLoop();
    loop.resetPassTimeline();
    loop.discardCapture();
    loop.startLoopTick = 0;
    loop.loopLengthTicks = 0;
    loop.loopStartTick = 0;

    reconcileTransportStateAfterSlotMutation();
    alignLoopOriginOnNextStop = false;
    invalidateCaches();
    editManager.revertNoteEditSessionForLoopClear(*this);
    logger.logTrackEvent("Track cleared", clockManager.getCurrentTick());
}

void Track::rebuildPlaybackOrder() {
  Loop& loop = getActiveLoop();
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(activeLoopIndex);
  const uint32_t currentTick = clockManager.getCurrentTick();
  ensurePlaybackMergedMidiEventsBuilt(*this, loop, runtime, true, currentTick);
  const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  ::rebuildPlaybackOrder(loop, runtime.mergedMidiEvents.mergedEvents, playbackContext);
}

void Track::queuePlaybackStartAtGrid(int32_t startTick, uint32_t queuedAtTick) {
  useQueuedStart = true;
  queuedStartTick = startTick;
  queuedStartQueuedAtTick = queuedAtTick;
  getActiveLoop().nextEventIndex = 0;
  getActiveLoop().lastTickInLoop = UINT32_MAX;
}

void Track::clearQueuedPlaybackStart() {
  useQueuedStart = false;
  queuedStartTick = 0;
  queuedStartQueuedAtTick = UINT32_MAX;
}

bool Track::shouldCommitQueuedPlaybackStart(uint32_t currentTick) const {
  if (!useQueuedStart) {
    return false;
  }
  if (currentTick == queuedStartQueuedAtTick) {
    return false;
  }
  if (queuedStartGridTicks == 0) {
    return false;
  }
  return (currentTick % queuedStartGridTicks) == 0;
}

void Track::commitQueuedPlaybackStart(uint32_t commitTick) {
  if (!useQueuedStart) {
    return;
  }
  Loop& loop = getActiveLoop();
  const uint32_t loopLength = loop.loopLengthTicks;
  if (loopLength == 0) {
    clearQueuedPlaybackStart();
    return;
  }
  const uint32_t startPhase = IntervalProjection::noteRelativeTick(
      static_cast<uint32_t>(queuedStartTick), loop.loopStartTick, loopLength);
  projectionCycleStartTick =
      static_cast<int32_t>(commitTick) - static_cast<int32_t>(startPhase);
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = UINT32_MAX;
  clearQueuedPlaybackStart();
}

struct PlaybackJamFilterCtx {
  const Track* track = nullptr;
  const Loop* loop = nullptr;
};

void playbackCursorAdvanceSend(void* ctx, const MidiEvent& evt, uint8_t slotIndex) {
  static_cast<Track*>(ctx)->sendMidiEvent(evt, slotIndex);
}

bool playbackCursorAdvanceJamFilter(void* ctx, uint32_t storageTick) {
  const auto* jam = static_cast<const PlaybackJamFilterCtx*>(ctx);
  return jam->track->isStorageTickInJamRegion(storageTick, *jam->loop);
}

bool Track::isStorageTickInJamRegion(uint32_t evTick, const Loop& loop) const {
  if (!jamPlaybackActive || jamLength == 0) {
    return true;
  }
  if (jamStartTick == UINT32_MAX) {
    return true;
  }
  const uint32_t jamEnd = jamStartTick + jamLength;
  if (jamEnd <= loop.loopLengthTicks) {
    return evTick >= jamStartTick && evTick < jamEnd;
  }
  return (evTick >= jamStartTick) || (evTick < jamEnd - loop.loopLengthTicks);
}

void Track::playCommittedLoopMidi(uint8_t slotIndex, uint32_t currentTick,
                                  PlaybackMidiTarget target) {
  const bool isActive = (target == PlaybackMidiTarget::ActiveSlot);
  Loop& loop = getLoop(slotIndex);
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(slotIndex);

  if (runtime.isStale(loop.playbackRevision, playbackGeneration)) {
    runtime.reset(true);
    loop.nextEventIndex = 0;
    if (isActive) {
      loop.captureNextEventIndex = 0;
    }
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
  }

  ensurePlaybackMergedMidiEventsBuilt(*this, loop, runtime, false, currentTick);
  const SessionMidiEventVec& mergedEvents = runtime.mergedMidiEvents.mergedEvents;
  if (mergedEvents.empty()) {
    return;
  }

  ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  if (loop.playbackOrderDirty) {
    ::rebuildPlaybackOrder(loop, mergedEvents, playbackContext);
    reanchorPlaybackIndex(loop, mergedEvents, loop.getPlaybackOrder(), playbackContext);
  }

  const uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);

  if (IntervalProjection::didDisplayPlayheadWrapBackward(tickInLoop, loop.lastTickInLoop,
                                                       loop.loopStartTick, loop.loopLengthTicks)) {
    if (isActive) {
      projectionCycleStartTick = IntervalProjection::advanceProjectionCycleStartTickOnWrap(
          projectionCycleStartTick, loop.loopLengthTicks);
      loop.nextEventIndex = 0;
      loop.captureNextEventIndex = 0;
      logger.trace("Loop wrapped, resetting index");
    } else {
      loop.nextEventIndex = 0;
    }
  }

  const uint32_t prevTickInLoop = loop.lastTickInLoop;
  loop.lastTickInLoop = tickInLoop;
  const bool atLoopStart =
      IntervalProjection::isPlaybackCatchUpWindow(prevTickInLoop, tickInLoop);

  if (prevTickInLoop == UINT32_MAX && tickInLoop > Config::TICKS_PER_BAR) {
    loop.nextEventIndex = 0;
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
    return;
  }

  PlaybackTickFrame frame{&playbackContext, tickInLoop, prevTickInLoop, atLoopStart};
  MergedPlaybackStreamCtx mergedCtx{&loop.getPlaybackOrder(), &mergedEvents};
  PlaybackCursorAdvanceState mergedAdvance{&loop.nextEventIndex, &loop.playbackOrderDirty};
  const PlaybackEmitPolicy mergedPolicy =
      isActive ? PlaybackEmitPolicy::ActiveCommitted : PlaybackEmitPolicy::LayeredSlot;
  PlaybackJamFilterCtx jamCtx{this, &loop};
  (void)advancePlaybackCursor(
      mergedAdvance, frame, mergedPolicy, makeMergedPlaybackStream(mergedCtx),
      playbackCursorAdvanceSend, this, slotIndex,
      isActive ? playbackCursorAdvanceJamFilter : nullptr, isActive ? &jamCtx : nullptr, midiChannel);

  if (isActive && loop.capture.phase == CapturePhase::Overdub && !loop.capture.store.empty()) {
    if (loop.ensureCaptureEventsSorted()) {
      reanchorCaptureIndex(loop);
    }
    PlaybackCursorAdvanceState captureAdvance{&loop.captureNextEventIndex, nullptr};
    (void)advancePlaybackCursor(
        captureAdvance, frame, PlaybackEmitPolicy::ActiveCaptureOverdub,
        makeCapturePlaybackStream(loop), playbackCursorAdvanceSend, this, slotIndex,
        playbackCursorAdvanceJamFilter, &jamCtx, midiChannel);
  }

  runtime.syncRevision(loop.playbackRevision, playbackGeneration);
}

void Track::playMidiEvents(uint32_t currentTick, bool isAudible) {
  if (isStoppedRecording()) {
    return;
  }
  Loop& loop = getActiveLoop();
  if (!isAudible || muted || !loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return;
  }
  playCommittedLoopMidi(activeLoopIndex, currentTick, PlaybackMidiTarget::ActiveSlot);
}

void Track::playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool isAudible) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  if (isStoppedRecording()) {
    return;
  }
  if (!isAudible || muted) {
    return;
  }
  Loop& loop = getLoop(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return;
  }
  playCommittedLoopMidi(slotIndex, currentTick, PlaybackMidiTarget::LayeredSlot);
}

void Track::sendMidiEvent(const MidiEvent& evt, uint8_t playbackSlotIndex) {
  if (trackState != TRACK_PLAYING && trackState != TRACK_OVERDUBBING) return;
  if (playbackSlotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  isPlayingBack = true;  // Mark playback so noteOn/noteOff ignores it
  MidiEvent evtCopy = evt;
  // Per-event channel 1-16 is remapped to the track's output channel. Channel 0 is treated as
  // unset (edit paths that default-construct MidiEvent and never set channel).
  const bool isChannelMessage = evt.type == midi::NoteOn || evt.type == midi::NoteOff ||
                                evt.type == midi::ControlChange || evt.type == midi::PitchBend ||
                                evt.type == midi::AfterTouchChannel || evt.type == midi::ProgramChange;
  if (isChannelMessage && (evt.channel == 0 || (evt.channel >= 1 && evt.channel <= 16))) {
    evtCopy.channel = midiChannel;
  }

  LoopPlaybackRuntime& runtime = playbackRuntime.slot(playbackSlotIndex);
  if (evt.isNoteOff()) {
    const uint8_t note = evtCopy.data.noteData.note;
    if (!runtime.ledger.isActive(evtCopy.channel, note)) {
      isPlayingBack = false;
      return;
    }
    runtime.ledger.noteOff(evtCopy.channel, note);
  } else if (evt.isNoteOn()) {
    runtime.ledger.noteOn(evtCopy.channel, evtCopy.data.noteData.note, evt.tick,
                          evtCopy.data.noteData.velocity);
  }

  // Hot path: logging every loop note at DEBUG blocks USB Serial for milliseconds and freezes the UI.
  // Use LOG_TRACE so deep MIDI tracing is opt-in (Logger at TRACE + CAT_MIDI on).
  if (evt.isNoteOn() || evt.isNoteOff()) {
    const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (evtCopy.data.noteData.note / 12) - 1;
    const char* noteName = noteNames[evtCopy.data.noteData.note % 12];
    logger.log(CAT_MIDI, LOG_TRACE, "Loop SEND: %s ch=%d %s%d (note %d) vel=%d tick=%lu",
               evt.isNoteOn() ? "NoteOn" : "NoteOff",
               evtCopy.channel, noteName, octave,
               evtCopy.data.noteData.note, evtCopy.data.noteData.velocity, evt.tick);
  }
  midiHandler.sendMidiEvent(evtCopy);
  isPlayingBack = false;  // Reset playback state
}

void Track::sendAllNotesOff() {
  // Control Change 123 = All Notes Off. Skip controller-only channels so we do
  // not clear DROID LEDs/buttons/faders when transport stops.
  for (uint8_t ch = 1; ch <= 16; ++ch) {
    if ((ch >= MidiConfig::LED_CHANNEL_MIN && ch <= MidiConfig::LED_CHANNEL_MAX) ||
        (ch >= MidiConfig::RECORD_EXCLUDE_MIN && ch <= MidiConfig::RECORD_EXCLUDE_MAX)) {
      continue;
    }
    midiHandler.sendControlChange(ch, 123, 0);
  }
  playbackRuntime.clearAllLedgers();
  // also clear any half-open pending notes so they don't get forced later
  pendingNotes.clear();
  logger.logTrackEvent("All Notes Off sent", clockManager.getCurrentTick());
}

uint32_t Track::getTicksPerBar() {
    return TICKS_PER_BAR;
}

void Track::setLoopLength(uint32_t ticks) {
  Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == ticks) return;
  loop.loopLengthTicks = ticks;
  StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                              getActiveLoopIndex());
  invalidateCaches();
}

void Track::setLoopLengthWithWrapping(uint32_t newLoopLength) {
  Loop& loop = getActiveLoop();
  if (newLoopLength == loop.loopLengthTicks) return;

  uint32_t oldLoopLength = loop.loopLengthTicks;
  logger.log(CAT_TRACK, LOG_INFO, "Loop length change: %lu -> %lu ticks", oldLoopLength, newLoopLength);
  loop.loopLengthTicks = newLoopLength;
  StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                              getActiveLoopIndex());
  invalidateCaches();
  logger.log(CAT_TRACK, LOG_INFO, "Loop length updated to %lu ticks (wrapping handled dynamically)", loop.loopLengthTicks);
}

void Track::setLoopStartTick(uint32_t startTick) {
  Loop& loop = getActiveLoop();
  if (startTick == loop.loopStartTick) return;

  uint32_t oldStartTick = loop.loopStartTick;
  if (startTick >= loop.loopLengthTicks && loop.loopLengthTicks > 0) {
    startTick = startTick % loop.loopLengthTicks;
  }
  loop.loopStartTick = startTick;
  logger.log(CAT_TRACK, LOG_INFO, "Loop start point changed: %lu -> %lu ticks", oldStartTick, loop.loopStartTick);
  StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                              getActiveLoopIndex());
  invalidateCaches();
}

void Track::setLoopStartAndEnd(uint32_t startTick, uint32_t endTick) {
  if (endTick <= startTick) {
    logger.log(CAT_TRACK, LOG_ERROR, "Invalid loop range: start=%lu >= end=%lu", startTick, endTick);
    return;
  }
  Loop& loop = getActiveLoop();
  uint32_t newLength = endTick - startTick;
  logger.log(CAT_TRACK, LOG_INFO, "Setting loop start=%lu, end=%lu, length=%lu", startTick, endTick, newLength);
  loop.loopStartTick = startTick;
  loop.loopLengthTicks = newLength;
  StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                              getActiveLoopIndex());
  invalidateCaches();
}

void Track::setJam(uint32_t startTick, uint32_t length) {
  noInterrupts();
  jamStartTick = startTick;
  jamLength = length;
  jamTick = 0;
  getActiveLoop().nextEventIndex = 0;
  getActiveLoop().lastTickInLoop = UINT32_MAX;
  interrupts();
  logger.log(CAT_TRACK, LOG_INFO, "Jam set: start=%lu, length=%lu", jamStartTick, jamLength);
}

void Track::clearJam() {
  noInterrupts();
  jamStartTick = UINT32_MAX;
  jamLength = 0;
  jamPlaybackActive = false;
  jamTick = 0;
  getActiveLoop().nextEventIndex = 0;
  getActiveLoop().lastTickInLoop = UINT32_MAX;
  interrupts();
  logger.log(CAT_TRACK, LOG_INFO, "Jam cleared");
}

void Track::advanceJamTick(uint32_t delta) {
  if (!jamPlaybackActive || jamLength == 0) return;
  jamTick = IntervalProjection::tickPhaseInLoop(jamTick + delta, 0, jamLength);
}

uint32_t Track::getJamTick() const {
  noInterrupts();
  uint32_t t = jamTick;
  interrupts();
  return t;
}

void Track::setJamTick(uint32_t tick) {
  noInterrupts();
  uint32_t newTick = IntervalProjection::tickPhaseInLoop(tick, 0, jamLength);
  if (newTick != jamTick) {
    jamTick = newTick;
    getActiveLoop().nextEventIndex = 0;
    getActiveLoop().lastTickInLoop = UINT32_MAX;
  }
  interrupts();
}

void Track::setJamPlayback(bool enabled) {
  noInterrupts();
  jamPlaybackActive = enabled;
  if (enabled) {
    getActiveLoop().nextEventIndex = 0;
    getActiveLoop().lastTickInLoop = UINT32_MAX;
  }
  interrupts();
}

uint32_t Track::getEffectivePlaybackTick(uint32_t currentTick) const {
  if (!jamPlaybackActive || jamLength == 0) return currentTick;
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0) return currentTick;
  uint32_t storagePos = (jamStartTick + jamTick) % loop.loopLengthTicks;
  return loop.startLoopTick + storagePos;
}

TRACK_COLD_MEM bool Track::hasCommittedPassesInSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  return loopForSlot(slotIndex).hasCommittedPasses();
}

TRACK_COLD_MEM uint32_t Track::quantizeTransportRecordLength(uint32_t rawLength) const {
  return RecordStopLength::quantizeTransportRecordLength(rawLength);
}

TRACK_COLD_MEM uint32_t Track::computeRecordStopLengthTicks(uint32_t rawLength,
                                                            uint32_t lastEventTick) const {
  return RecordStopLength::computeRecordStopLengthTicks(rawLength, lastEventTick);
}

TRACK_COLD_MEM void Track::resetLoopSlotAfterEmptyCapture(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  resetActiveLoopAfterEmptyCapture(loopForSlot(slotIndex));
}

