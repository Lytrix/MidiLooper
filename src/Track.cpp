//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Track.h"
#include <new>
#include "Logger.h"
#include "MidiHandler.h"
#include "ClockManager.h"
#include "StorageManager.h"
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
#include <cstring>
#include <limits>
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/NoteUtils.h"

namespace {

bool isSamePitchSoundingAtTick(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                               uint32_t tick) {
  for (const NoteUtils::DisplayNote& displayNote : notes) {
    if (displayNote.note != pitch) {
      continue;
    }
    if (tick >= displayNote.startTick && tick < displayNote.endTick) {
      return true;
    }
  }
  return false;
}

bool shouldRestorePublishedOverlapOnOverdubStop(const Loop& loop, uint8_t note,
                                                uint32_t pendingOnPhaseTick,
                                                uint32_t closePhaseTick) {
  if (loop.loopLengthTicks == 0 || !loop.hasPublishedEvents()) {
    return false;
  }
  SessionMidiEventVec published;
  loop.passes.materializeToEventVector(published, loop.loopLengthTicks);
  if (published.empty()) {
    return false;
  }
  const NoteUtils::DisplayNoteVec reconstructed =
      NoteUtils::reconstructDisplayNotes(published, loop.loopLengthTicks, false);
  if (isSamePitchSoundingAtTick(reconstructed, note, pendingOnPhaseTick)) {
    return true;
  }
  return isSamePitchSoundingAtTick(reconstructed, note, closePhaseTick);
}

}  // namespace

#include "Utils/IntervalProjection.h"
#include "Utils/TrackMem.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "TrackManager.h"

extern TrackManager trackManager;

#if defined(SESSION_CAPTURE)
namespace {

void logOverdubCaptureCoordinate(const Track& track, uint32_t absTick, uint32_t storageTick,
                                 uint8_t channel, uint8_t note) {
  const Loop& loop = track.getActiveLoop();
  if (loop.loopLengthTicks == 0) {
    return;
  }
  const uint32_t projPhase = IntervalProjection::tickPhaseInProjectionCycle(
      absTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks);
  const uint32_t displayPhase =
      IntervalProjection::noteRelativeTick(projPhase, loop.loopStartTick, loop.loopLengthTicks);
  SC_CAPTURE_COORD(absTick, storageTick, projPhase, displayPhase, loop.startLoopTick,
                   track.getProjectionCycleStartTick(), loop.loopStartTick, channel, note);
}

}  // namespace
#endif

MidiEventVec& Track::legacyMidiEventsFromPublished() {
  // Edit-path boundary only (EditManager::editMidiEvents when session inactive).
  // Display and playback use SessionMidiEventVec via getMidiEvents().
  Loop& loop = getActiveLoop();
  const uint32_t revision = loop.playbackRevision;
  if (publishedMidiScratchRevision_ != revision) {
    loop.ensurePassesMaterializedStore();
    const SessionMidiEventVec& published = loop.midiEvents();
    publishedMidiScratch_.assign(published.begin(), published.end());
    publishedMidiScratchRevision_ = revision;
  }
  return publishedMidiScratch_;
}

const MidiEventVec& Track::legacyMidiEventsFromPublished() const {
  return const_cast<Track*>(this)->legacyMidiEventsFromPublished();
}

MidiEventVec& Track::editAwareMidiEvents() {
  return editManager.editMidiEvents(*this);
}

const MidiEventVec& Track::editAwareMidiEvents() const {
  return editManager.editMidiEvents(*this);
}

void Track::invalidateCaches(bool refreshPlaybackPreview) {
  publishedMidiScratchRevision_ = UINT32_MAX;
  Loop& activeLoop = getActiveLoop();
  activeLoop.invalidateCaches();
  activeLoop.playbackOrderDirty = true;
  if (editManager.isNoteEditActive()) {
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
      if (&trackManager.getTrack(trackIndex) != this) {
        continue;
      }
      const uint8_t selectedSlot = trackManager.getSelectedSlotIndex(trackIndex);
      if (selectedSlot != activeLoopIndex) {
        getLoop(selectedSlot).invalidateCaches();
      }
      break;
    }
    editManager.bumpSessionPreviewRevision();
    if (refreshPlaybackPreview) {
      editManager.bumpSessionPlaybackPreviewRevision();
    }
  }
}

namespace {

struct StopPathStorageStats {
  size_t eventCount = 0;
  size_t chunkRefCount = 0;
};

StopPathStorageStats collectStopPathStorageStats(const Loop& loop, bool includeCaptureBuffer = true) {
  StopPathStorageStats stats{};
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.state == CapturePassState::Active) {
    stats.chunkRefCount += loop.passes.recordPass.chunkRefs.size();
    stats.eventCount += LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.chunkRefs);
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state != CapturePassState::Active) {
      continue;
    }
    stats.chunkRefCount += pass.chunkRefs.size();
    stats.eventCount += LoopEventStore::countEventsInChunkIds(pass.chunkRefs);
  }
  if (loop.hasPendingCapturePass()) {
    const PendingCapturePass& pending = loop.pendingCapturePass();
    stats.chunkRefCount += pending.chunkRefs.size();
    stats.eventCount += LoopEventStore::countEventsInChunkIds(pending.chunkRefs);
  }
  if (includeCaptureBuffer && loop.captureActive()) {
    stats.eventCount += loop.capture.store.size();
  }
  return stats;
}

const char* commitResultLabel(CommitResult result) {
  switch (result) {
    case CommitResult::Skipped:
      return "skipped";
    case CommitResult::Published:
      return "published";
    case CommitResult::SealFailed:
      return "seal_failed";
  }
  return "unknown";
}

void logRecordStopStage(const Loop& loop, uint32_t stopStartUs, const char* stage,
                        uint32_t stageDurationUs, uint32_t heapBefore, uint32_t heapAfter,
                        const char* outcome, const StopPathStorageStats* cachedStats = nullptr) {
  const StopPathStorageStats stats =
      cachedStats ? *cachedStats : collectStopPathStorageStats(loop);
  const uint32_t elapsedUs = micros() - stopStartUs;
  SC_REC_STOP_STAGE(stage, elapsedUs, stageDurationUs, heapBefore, heapAfter,
                    stats.eventCount, stats.chunkRefCount, outcome);
  // Record-stop publish DIAG emits from Loop::commitCapturePass (seal/publish stages live there).
}

void logOverdubStopStage(const Loop& loop, uint32_t stopStartUs, const char* stage,
                         uint32_t stageDurationUs, uint32_t heapBefore, uint32_t heapAfter,
                         const char* outcome, const StopPathStorageStats* cachedStats = nullptr) {
  const StopPathStorageStats stats =
      cachedStats ? *cachedStats : collectStopPathStorageStats(loop, false);
  const uint32_t elapsedUs = micros() - stopStartUs;
  SC_ODUB_STOP_STAGE(stage, elapsedUs, stageDurationUs, heapBefore, heapAfter, stats.eventCount,
                     stats.chunkRefCount, outcome);
  if (stage != nullptr && std::strcmp(stage, "display") == 0) {
    Diagnostics::emitArchitectureMetricsSnapshot();
  }
}

void emitOverdubStopDisplaySnapshot(Track& track, uint8_t displaySlot, uint32_t currentTick) {
  const Loop& loop = track.getLoop(displaySlot);
  if (LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(
          MemoryMonitor::getInternalHeapFreeBytes())) {
    displayManager.emitDisplayCaptureSnapshot(track, displaySlot, currentTick);
    return;
  }
  SC_DISP(displaySlot, TrackStateMachine::toString(track.getState()), loop.loopLengthTicks, 0, 0, 0,
          0, loop.hasPublishedEvents() ? 1 : 0);
}

void logMemoryAfterOverdubStop(uint32_t overdubNoteOns, const Loop& loop) {
  const StopPathStorageStats stats = collectStopPathStorageStats(loop, false);
  MemoryMonitor::logStatusAtAddedNotes(overdubNoteOns, stats.eventCount, nullptr,
                                       stats.chunkRefCount, stats.chunkRefCount > 0);
}

/// When record-stop snaps length shorter than raw capture, rewind the global tick so playhead
/// lands in bar 1 at the same beat position as in the truncated bar (keeps all tracks in sync).
uint32_t computeTruncationRewindTicks(uint32_t rawLength, uint32_t finalLength) {
  if (finalLength == 0 || rawLength <= finalLength) {
    return 0;
  }
  const uint32_t positionInBar = rawLength % Config::TICKS_PER_BAR;
  return rawLength - positionInBar;
}

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

void ensurePlaybackWindowBuilt(Track& track, Loop& loop, LoopPlaybackRuntime& runtime) {
  // Projection boundary (linear-loop-tick-storage): mergedEvents are read-only input to
  // playback order + MIDI send. NOTE_EDIT uses session store (Tier 2) — full replace, no
  // materialized underlay. Outside NOTE_EDIT and live capture, chunk-ref merge only (DEC-016).
  const bool noteEditPreview = editManager.isNoteEditActive() &&
                               &track == &trackManager.getSelectedTrack();
  const uint32_t windowRevision = noteEditPreview ? editManager.sessionPlaybackPreviewRevision()
                                                    : loop.playbackRevision;
  if (runtime.primaryWindow.builtFromRevision == windowRevision) {
    DIAG_COUNTER_INC(PlaybackDeferredReuse);
    return;
  }
  const uint32_t playbackBuildStartUs = micros();
  DIAG_COUNTER_INC(PlaybackWindowRebuild);
  if (noteEditPreview) {
    const MidiEventVec& preview = editManager.sessionMidiEvents();
    runtime.primaryWindow.mergedEvents.assign(preview.begin(), preview.end());
  } else if (!loop.captureActive()) {
    loop.gatherPublishedFlatForDerivedView(runtime.primaryWindow.mergedEvents);
  } else {
    loop.gatherPublishedFlatWithCapture(runtime.primaryWindow.mergedEvents);
  }
  runtime.primaryWindow.builtFromRevision = windowRevision;
  loop.playbackOrderDirty = true;
  DIAG_TIMING_RECORD(PlaybackBuild, micros() - playbackBuildStartUs);
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

TRACK_COLD_MEM void resetActiveLoopAfterEmptyCapture(Loop& loop) {
  loop.discardCapture();
  loop.resetPassTimeline();
  loop.loopLengthTicks = 0;
  loop.loopStartTick = 0;
  loop.startLoopTick = 0;
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = 0;
  loop.invalidatePlaybackCaches();
}

uint8_t resolveTrackIndexForPersistence(const Track& track) {
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    if (&trackManager.getTrack(i) == &track) {
      return i;
    }
  }
  return trackManager.getSelectedTrackIndex();
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

TRACK_COLD_MEM bool Track::hasAnySlotData() const {
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (hasDataInSlot(s)) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM void Track::reconcileTransportStateAfterSlotMutation() {
  if (isRecording() || isOverdubbing() || isStoppedRecording()) {
    return;
  }
  if (trackState == TRACK_ARMED && getActiveLoop().hasPublishedEvents()) {
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
    }
  } else if (trackState == TRACK_ARMED || trackState == TRACK_STOPPED ||
             trackState == TRACK_PLAYING || trackState == TRACK_OVERDUBBING) {
    setState(TRACK_EMPTY);
  } else if (trackState != TRACK_EMPTY) {
    setState(TRACK_EMPTY);
  }
}

// -------------------------
// Getters
// -------------------------


uint8_t Track::getMidiChannel() const {
  return midiChannel;
}

void Track::setMidiChannel(uint8_t ch) {
  midiChannel = (ch >= 1 && ch <= 16) ? ch : 1;
}

uint8_t Track::getActiveLoopIndex() const {
  return activeLoopIndex;
}

void Track::setActiveLoopIndex(uint8_t index) {
  if (index < Config::MAX_LOOPS_PER_TRACK) {
    activeLoopIndex = index;
  }
}

SlotOpState Track::getSlotOpState(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return SlotOpState::SLOT_OP_IDLE;
  if (slotIndex != activeLoopIndex) return SlotOpState::SLOT_OP_IDLE;
  if (isRecording()) return SlotOpState::SLOT_OP_RECORDING;
  if (isOverdubbing()) return SlotOpState::SLOT_OP_OVERDUBBING;
  return SlotOpState::SLOT_OP_IDLE;
}

uint8_t Track::getRecordingFocusSlot() const {
  if (isRecording() || isOverdubbing()) return activeLoopIndex;
  return Config::INVALID_LOOP_SLOT;
}

// -------------------------
// State management
// -------------------------

TrackState Track::getState() const {
  return trackState;
}

bool Track::isValidStateTransition(TrackState newState) const {
  return TrackStateMachine::isValidTransition(trackState, newState);
}

bool Track::setState(TrackState newState) {
  if (!TrackStateMachine::isValidTransition(trackState, newState)) {
    logger.log(CAT_STATE, LOG_WARNING, "Invalid state transition from %s to %s",
               TrackStateMachine::toString(trackState),
               TrackStateMachine::toString(newState));
    return false;
  }
  return transitionState(newState);
}

const char* Track::getStateName(TrackState state) {
  return TrackStateMachine::toString(state);
}

bool Track::transitionState(TrackState newState) {
  if (!TrackStateMachine::isValidTransition(trackState, newState)) {
    return false;
  }

  TrackState oldState = trackState;
  trackState = newState;

  if (oldState == TRACK_ARMED && newState != TRACK_RECORDING) {
    armedPreRollNotes.clear();
  }

  logger.logStateTransition("Track", TrackStateMachine::toString(oldState), TrackStateMachine::toString(newState));
  return true;
}

// Required for loading state from SD card else the state machine will corrupt the state
void Track::forceSetState(TrackState newState) { trackState = newState; }

// -------------------------
// Recording control
// -------------------------

void Track::startRecording(uint32_t currentTick) {
  Loop& loop = getActiveLoop();
  recordCaptureBaselineGeometry_ = {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick};
  hasRecordCaptureBaselineGeometry_ = true;
  if (!loop.hasPublishedEvents()) {
    loop.loopLengthTicks = 0;
    loop.loopStartTick = 0;
  }
  auto preRoll = std::move(armedPreRollNotes);
  if (!setState(TRACK_RECORDING)) {
    armedPreRollNotes = std::move(preRoll);
    return;
  }
  recordAddedNoteOnCount = 0;
  loop.resetPassTimeline();
  loop.beginCapture(CapturePhase::Record);
  pendingNotes.clear();
  for (const auto& entry : preRoll) {
    const PendingNote& pn = entry.second;
    pendingNotes[entry.first] =
        PendingNote{pn.note, pn.channel, currentTick, pn.velocity};
    recordMidiEvents(midi::NoteOn, pn.channel, pn.note, pn.velocity, currentTick);
  }
  loop.nextEventIndex = 0;    // so playback will start from the top
  loop.lastTickInLoop = 0;
  // Fresh take: a stale loop-start window (prior loop-start edit or SD restore) must not
  // shift the new notes on the piano roll — playback and grid use raw storage ticks.
  loop.loopStartTick = 0;

  // Stamp the new start tick quantized to a beat.
  loop.startLoopTick = currentTick;
  projectionCycleStartTick = static_cast<int32_t>(currentTick);
  
  invalidateCaches();
  SC_REC_START(activeLoopIndex, currentTick);
  logger.logTrackEvent("Recording started", currentTick, "startLoopTick=%lu loopStart=0",
                       static_cast<unsigned long>(loop.startLoopTick));
}

// -------------------------
// Helpers for stopRecording 
// -------------------------

const uint32_t Track::TICKS_PER_BAR = Config::TICKS_PER_BAR;

uint32_t Track::quantizeStart(uint32_t original) const {
    return (original / TICKS_PER_BAR) * TICKS_PER_BAR;
}

void Track::shiftMidiEvents(int32_t offset) {
    Loop& loop = getActiveLoop();
    loop.shiftActiveCapturePassTicks(offset);
    invalidateCaches();
}

uint32_t Track::findLastEventTick() const {
    const Loop& loop = getActiveLoop();
    MidiEventVec flat;
    loop.mergeActiveCapturePasses(flat);
    uint32_t last = 0;
    for (const auto &evt : flat) {
        last = std::max(last, evt.tick);
    }
    return last;
}

uint32_t Track::computeLoopLengthTicks(uint32_t lastTick) const {
    uint32_t fullBars = lastTick / TICKS_PER_BAR;
    uint32_t rem      = lastTick % TICKS_PER_BAR;
    uint32_t grace    = TICKS_PER_BAR / 6;  // More generous grace window

    if (rem <= grace) {
        return (fullBars > 0 ? fullBars : 1) * TICKS_PER_BAR;
    }

    // Special case: very short pass (accidental press?)
    if (lastTick < TICKS_PER_BAR / 2) {
        return TICKS_PER_BAR;
    }

    return (fullBars + 1) * TICKS_PER_BAR;
}

void Track::resetPlaybackState(uint32_t currentTick) {
  Loop& loop = getActiveLoop();
  loop.nextEventIndex = 0;
  invalidatePlaybackWindow(true);
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
  invalidatePlaybackWindow(true);
}

void Track::invalidatePlaybackWindow(bool preserveLedger) {
  bumpPlaybackGeneration();
  playbackRuntime.resetAll(preserveLedger);
}

uint32_t Track::capturePhaseTick(uint32_t absTick) const {
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || loop.startLoopTick == UINT32_MAX) {
    if (absTick >= loop.startLoopTick) {
      return absTick - loop.startLoopTick;
    }
    return 0;
  }
  if (isOverdubbing() || (isRecording() && isPlaying())) {
    return IntervalProjection::tickPhaseInProjectionCycle(
        absTick, projectionCycleStartTick, loop.loopLengthTicks);
  }
  if (isRecording() && !isPlaying()) {
    return absTick >= loop.startLoopTick ? absTick - loop.startLoopTick : 0;
  }
  return tickPhaseInLoop(absTick, loop.startLoopTick, loop.loopLengthTicks);
}

bool Track::appendCaptureNoteOffAtPhase(uint8_t channel, uint8_t note, uint32_t phaseTick) {
  Loop& loop = getActiveLoop();
  if (!loop.captureActive()) {
    return false;
  }
  uint32_t tickRelative = phaseTick;
  if (loop.loopLengthTicks > 0 && tickRelative >= loop.loopLengthTicks) {
    tickRelative = loop.loopLengthTicks - 1;
  }
  const MidiEvent newEvt = MidiEvent::NoteOff(tickRelative, channel, note, 0);
  if (!loop.appendCaptureEvent(newEvt)) {
    return false;
  }
  ++loop.captureDisplayRevision;
  return true;
}

void Track::finalizePendingNotes(uint32_t offAbsTick) {
  const uint32_t phaseTick = capturePhaseTick(offAbsTick);
  Loop& loop = getActiveLoop();

  std::vector<std::pair<uint8_t, uint8_t>> toClose;
  toClose.reserve(pendingNotes.size());
  for (const auto& kv : pendingNotes) {
    toClose.push_back(kv.first);
  }

  size_t pendingFinalized = 0;
  size_t captureNoteOffsAppended = 0;
  size_t overlapCaptureRestored = 0;
  for (const auto& key : toClose) {
    const uint8_t note = key.first;
    const uint8_t channel = key.second;
    ++pendingFinalized;

    if (isOverdubbing()) {
      const auto pendingIt = pendingNotes.find(key);
      const uint32_t pendingOnAbsTick =
          pendingIt != pendingNotes.end() ? pendingIt->second.startNoteTick : offAbsTick;
      const uint32_t pendingOnPhaseTick = capturePhaseTick(pendingOnAbsTick);
      // If a performer NoteOff already landed in capture but pending did not clear (dedup / warning),
      // do not synthesize a stop-time off that truncates the earlier note.
      if (loop.captureHasNoteOffAfter(channel, note, pendingOnPhaseTick)) {
        pendingNotes.erase(key);
        continue;
      }
      if (shouldRestorePublishedOverlapOnOverdubStop(loop, note, pendingOnPhaseTick, phaseTick)) {
        if (loop.removeOpenCaptureNoteOn(channel, note)) {
          ++overlapCaptureRestored;
        }
        pendingNotes.erase(key);
        continue;
      }
    }

    if (appendCaptureNoteOffAtPhase(channel, note, phaseTick)) {
#if defined(SESSION_CAPTURE)
      uint32_t storageTick = phaseTick;
      if (loop.loopLengthTicks > 0 && storageTick >= loop.loopLengthTicks) {
        storageTick = loop.loopLengthTicks - 1;
      }
      logOverdubCaptureCoordinate(*this, offAbsTick, storageTick, channel, note);
#endif
      pendingNotes.erase(key);
      ++captureNoteOffsAppended;
    } else {
      pendingNotes.erase(key);
    }
  }

#if defined(SESSION_CAPTURE)
  logger.info("Stop finalize pending: finalized=%u capture_offs=%u overlap_restore=%u phase=%u",
              static_cast<unsigned>(pendingFinalized),
              static_cast<unsigned>(captureNoteOffsAppended),
              static_cast<unsigned>(overlapCaptureRestored),
              phaseTick);
#endif

  invalidateCaches();
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

void Track::finalizeLoopAtStop(uint32_t openTailCloseTick, bool scheduleDeferredFullValidate) {
  (void)openTailCloseTick;
  deferredFullMidiValidate = scheduleDeferredFullValidate;
  deferredValidateQueuedAtMs = scheduleDeferredFullValidate ? millis() : 0;
}

CommitResult Track::finalizeCommitSideEffects(CommitResult result, CommitReason reason,
                                              uint32_t closeTick) {
  Loop& loop = getActiveLoop();
  const bool recordStop = reason == CommitReason::RecordStop ||
                          reason == CommitReason::RecordStopToStopped;
  const bool overdubStop = reason == CommitReason::OverdubStop ||
                           reason == CommitReason::OverdubStopToStopped;
  const bool deferFullValidate = recordStop;
  auto scheduleDeferredValidateOnly = [&]() {
    const bool hasPublished = loop.hasPublishedEvents() && loop.loopLengthTicks > 0;
    deferredFullMidiValidate = deferFullValidate && hasPublished;
    deferredValidateQueuedAtMs = deferredFullMidiValidate ? millis() : 0;
  };

  switch (result) {
    case CommitResult::Skipped: {
      loop.discardCapture();
      if (recordStop && loop.activeCapturePassCount() == 0) {
        loop.resetPassTimeline();
        loop.loopLengthTicks = 0;
        loop.loopStartTick = 0;
        loop.startLoopTick = 0;
        loop.nextEventIndex = 0;
        loop.lastTickInLoop = 0;
        loop.invalidatePlaybackCaches();
      }
      if (overdubStop) {
        finalizeLoopAtStop(closeTick, false);
        StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                                    getActiveLoopIndex());
        StorageManager::requestDeferredSaveState(looperState.getLooperState(),
                                                 MemoryMonitor::getInternalHeapFreeBytes(), true);
      } else {
        scheduleDeferredValidateOnly();
      }
      break;
    }
    case CommitResult::Published: {
      const PassId undoPassId = loop.lastPublishedPassId();
      if (overdubStop) {
        finalizeLoopAtStop(closeTick, false);
      } else {
        // Record stop already finalizes wrap-window at seal; defer full validate only.
        scheduleDeferredValidateOnly();
      }
      loop.markDisplayCachesStale();
      const bool isRecordPass =
          loop.passes.hasRecordPass() && loop.passes.recordPass.id == undoPassId;
      if (isRecordPass) {
        TrackUndo::pushRecordPassAdded(*this, getActiveLoopIndex(), undoPassId);
        StorageManager::admitLoopUndoHistory(resolveTrackIndexForPersistence(*this),
                                             getActiveLoopIndex());
      } else if (!editManager.isNoteEditActive()) {
        TrackUndo::pushOverdubPassAdded(*this, getActiveLoopIndex(), undoPassId);
        StorageManager::admitLoopUndoHistory(resolveTrackIndexForPersistence(*this),
                                             getActiveLoopIndex());
      }
      if (overdubStop) {
        StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                                    getActiveLoopIndex());
        StorageManager::requestDeferredSaveState(looperState.getLooperState(),
                                                 MemoryMonitor::getInternalHeapFreeBytes(), true);
      }
      break;
    }
    case CommitResult::SealFailed: {
      loop.discardPendingCapturePass();
      trackManager.reclaimUnreferencedDisabledPasses();
      const CommitResult retry = loop.commitCapturePass(reason, closeTick);
      if (retry != CommitResult::SealFailed) {
        result = finalizeCommitSideEffects(retry, reason, closeTick);
        return result;
      }
      break;
    }
  }

  if (result == CommitResult::SealFailed) {
    deferredFullMidiValidate = false;
    deferredValidateQueuedAtMs = 0;
  }

  if (result == CommitResult::Published) {
    invalidateCaches();
    if (recordStop) {
      queueDeferredRecordRevts();
    }
    if (overdubStop) {
      emitStoredMidiVerification();
    }
  }
  return result;
}

void Track::emitStoredMidiVerification() const {
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || !loop.hasPublishedEvents()) {
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
    if (loop.hasPublishedEvents() && loop.visualCacheDirty) {
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
    if (loop.hasPublishedEvents()) {
      const bool bootHydrateActive = StorageManager::hasPendingLoopSlotRestore() ||
                                     StorageManager::hasPendingUndoSnapshotHydrate();
      const bool deferHeavyDerivedView =
          bootHydrateActive || StorageManager::hasDeferredSaveWork();
      if (!loop.isPassesMaterializedStoreFresh() && !deferHeavyDerivedView) {
        loop.ensurePassesMaterializedStore();
      }
      if (loop.visualCacheDirty) {
        if (deferHeavyDerivedView) {
          uint8_t barsPerSlice = StorageManager::hasDeferredSaveWork() ? 2 : 4;
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
  Loop& loop = loopForSlot(slotIndex);
  (void)playbackRuntime.slot(slotIndex);
  (void)loop.getPlaybackOrder();
}

void Track::releasePlaybackWindowMemory() {
  playbackRuntime.resetAll(false);
}

TRACK_COLD_MEM bool Track::tryReleasePlaybackWindowMemory() {
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
    if (!trackManager.isSlotEnabled(trackIndex, slot) && !loop.hasPublishedEvents()) {
      continue;
    }

    LoopPlaybackRuntime& runtime = playbackRuntime.slot(slot);
    if (runtime.primaryWindow.empty()) {
      continue;
    }

    const bool noteEditPreview =
        noteEditActive && selected && slot == getActiveLoopIndex();
    if (noteEditPreview) {
      continue;
    }

    const uint32_t windowRevision = loop.playbackRevision;
    const bool windowStale = runtime.primaryWindow.builtFromRevision != windowRevision;
    if (transportActive && !windowStale) {
      continue;
    }

    runtime.primaryWindow.clear();
    reclaimed = true;
  }
  return reclaimed;
}

TRACK_COLD_MEM bool Track::tryClearPublishedMidiScratch() {
  if (editManager.isNoteEditActive() && trackManager.isSelectedTrack(*this)) {
    return false;
  }
  if (publishedMidiScratch_.empty()) {
    return false;
  }
  publishedMidiScratch_.clear();
  publishedMidiScratchRevision_ = UINT32_MAX;
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
  if (!loop.hasPublishedEvents()) {
    resetDeferredRecordRevts();
    return;
  }

  resetDeferredRecordRevts();
  deferredRecordRevtsPending = true;

  const bool hasActiveRecordPass =
      loop.passes.hasRecordPass() &&
      loop.passes.recordPass.state == CapturePassState::Active &&
      !loop.passes.recordPass.chunkRefs.empty();
  bool hasActiveOverdubPass = false;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.chunkRefs.empty()) {
      hasActiveOverdubPass = true;
      break;
    }
  }

  // Fast path: record-stop baseline has one active record pass and no active overdub passes.
  if (hasActiveRecordPass && !hasActiveOverdubPass) {
    deferredRecordRevtChunkScan = true;
    deferredRecordRevtChunkRefs = loop.passes.recordPass.chunkRefs;
  }
}

void Track::processDeferredRecordRevts(size_t maxEventsPerSlice) {
  if (!deferredRecordRevtsPending) {
    return;
  }

  const Loop& loop = getActiveLoop();
  if (!loop.hasPublishedEvents()) {
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
// Stop recording
// -------------------------

void Track::stopRecording(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  [[maybe_unused]] const bool captureAlignFlag = alignLoopOriginOnNextStop;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  Loop& loop = getActiveLoop();
  const uint32_t stopPathStartUs = micros();
  const uint32_t stopHeap = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0, stopHeap, stopHeap, "entered");

  uint32_t rawLength = 0;
  if (currentTick >= loop.startLoopTick) {
    rawLength = currentTick - loop.startLoopTick;
  } else {
    logger.warning("stopRecording guard: currentTick(%lu) < startLoopTick(%lu), clamping length", currentTick, loop.startLoopTick);
  }
  const uint32_t lastEventTick = findLastEventTick();
  loop.loopLengthTicks = computeRecordStopLengthTicks(rawLength, lastEventTick);

  if (loop.loopLengthTicks > 0) {
    if (!pendingNotes.empty()) {
      finalizePendingNotes(currentTick);
    }
    // Record-stop truncation: events captured past final loop length must not
    // survive into committed playback state.
    loop.capture.store.dropEventsAtOrBeyondTick(loop.loopLengthTicks);
  }

  const CommitResult commitResult =
      loop.commitCapturePass(CommitReason::RecordStop, currentTick);
  pendingNotes.clear();

  // Validate AFTER loopLengthTicks is known so wrap-matching and open-tail closing
  // (the second pass and synthetic note-offs) are active for this record-stop.
  // Record-stop must close open tails at loop end, not at the stop playhead tick.
  const uint32_t closeTick = UINT32_MAX;
  const uint32_t finalizeHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t finalizeStartUs = micros();
  const CommitResult sideEffectResult =
      finalizeCommitSideEffects(commitResult, CommitReason::RecordStop, closeTick);
  const uint32_t finalizeDurationUs = micros() - finalizeStartUs;
  const uint32_t finalizeHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs, finalizeHeapBefore,
                     finalizeHeapAfter, commitResultLabel(sideEffectResult), &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0, finalizeHeapAfter,
                     finalizeHeapAfter,
                     sideEffectResult == CommitResult::Published ? "deferred" : "skipped",
                     &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0, finalizeHeapAfter, finalizeHeapAfter,
                     sideEffectResult == CommitResult::Published ? "deferred" : "skipped",
                     &stopPathStats);

  if (alignLoopOriginOnNextStop) {
    alignLoopOriginOnNextStop = false;
    uint32_t absRecStart = loop.startLoopTick;
    uint32_t remBar = absRecStart % TICKS_PER_BAR;
    uint32_t graceBar = TICKS_PER_BAR / 2;
    uint32_t snapBar;
    if (remBar <= graceBar) {
      snapBar = absRecStart - remBar;
    } else {
      snapBar = absRecStart - remBar + TICKS_PER_BAR;
    }
    int64_t delta = (int64_t)snapBar - (int64_t)absRecStart;
    if (delta != 0 && loop.hasPublishedEvents()) {
      loop.shiftActiveCapturePassTicks(delta);
      loop.invalidatePlaybackCaches();
    }
  }

  loop.nextEventIndex = 0;
  uint32_t recordStartTick = loop.startLoopTick;
  uint32_t finalLength = loop.loopLengthTicks;

  uint32_t playbackTick = currentTick;
  const uint32_t rewindTicks = computeTruncationRewindTicks(rawLength, finalLength);
  if (rewindTicks > 0) {
    playbackTick = currentTick - rewindTicks;
    clockManager.assignCurrentTickSilently(playbackTick);
    logger.log(CAT_TRACK, LOG_INFO,
               "Record stop truncation rewind: raw=%lu final=%lu rewind=%lu playbackTick=%lu positionInBar=%lu",
               rawLength, finalLength, rewindTicks, playbackTick, rawLength % Config::TICKS_PER_BAR);
  }

  loop.startLoopTick = recordStartTick;
  loop.lastTickInLoop = (finalLength > 0)
                            ? tickPhaseInLoop(playbackTick, recordStartTick, finalLength)
                            : 0;
  const uint32_t storagePhaseTickAtStop = loop.lastTickInLoop;
  if (finalLength > 0) {
    projectionCycleStartTick =
        static_cast<int32_t>(playbackTick) - static_cast<int32_t>(loop.lastTickInLoop);
  }

  invalidatePlaybackCaches();
  SC_REC_STOP("stop", activeLoopIndex, playbackTick, recordStartTick, rawLength, finalLength, captureAlignFlag);
  logger.logTrackEvent("Recording stopped", playbackTick, "recStart=%lu length=%lu",
                       static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(finalLength));
  logger.debug("Final ticks: playbackTick=%lu recStart=%lu rawLength=%lu length=%lu", playbackTick,
               static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(rawLength),
               static_cast<unsigned long>(finalLength));

  // Empty record-stop: reset capture slot geometry so hasDataInSlot stays false.
  if (loop.loopLengthTicks == 0 || loop.activeCapturePassCount() == 0) {
    resetActiveLoopAfterEmptyCapture(loop);
    logRecordStopStage(loop, stopPathStartUs, "state_advance", 0, stopHeap, stopHeap,
                       "skipped_empty", &stopPathStats);
    logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stopHeap, stopHeap,
                       sideEffectResult == CommitResult::Published ? "requested" : "skipped",
                       &stopPathStats);
    setState(hasAnySlotData() ? TRACK_STOPPED : TRACK_EMPTY);
    return;
  }

  // Return to playback after record-stop. Overdub starts on the next explicit
  // record press from PLAYING (record -> play -> overdub -> play flow).
  playbackRuntime.slot(activeLoopIndex).primaryWindow.clear();
  const uint32_t stateAdvanceHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "pre_state_advance", 0, stateAdvanceHeapBefore,
                     stateAdvanceHeapBefore, "enter", &stopPathStats);
  const uint32_t stateAdvanceStartUs = micros();
  startPlaying(playbackTick, true);
  displayManager.refreshViewportAfterRecordStop(*this, activeLoopIndex, storagePhaseTickAtStop);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  const uint32_t stateAdvanceHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     stateAdvanceHeapBefore, stateAdvanceHeapAfter,
                     trackState == TRACK_PLAYING ? "ok" : "failed", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stateAdvanceHeapAfter,
                     stateAdvanceHeapAfter,
                     sideEffectResult == CommitResult::Published ? "requested" : "skipped",
                     &stopPathStats);
  if (sideEffectResult == CommitResult::Published) {
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                                recordedSlotIndex);
    StorageManager::requestDeferredSaveState(looperState.getLooperState(), stateAdvanceHeapAfter,
                                             true);
  }
}

TRACK_COLD_MEM void Track::stopRecordingToStopped(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  alignLoopOriginOnNextStop = false;
  const uint8_t recordedSlotIndex = activeLoopIndex;
  Loop& loop = getActiveLoop();
  const uint32_t stopPathStartUs = micros();
  const uint32_t stopHeap = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "record_stop", 0, stopHeap, stopHeap, "entered");

  uint32_t rawLength = 0;
  if (currentTick >= loop.startLoopTick) {
    rawLength = currentTick - loop.startLoopTick;
  } else {
    logger.warning("stopRecordingToStopped guard: currentTick(%lu) < startLoopTick(%lu), clamping length", currentTick, loop.startLoopTick);
  }
  const uint32_t lastEventTick = findLastEventTick();
  loop.loopLengthTicks = computeRecordStopLengthTicks(rawLength, lastEventTick);

  if (loop.loopLengthTicks > 0) {
    if (!pendingNotes.empty()) {
      finalizePendingNotes(currentTick);
    }
    // Record-stop truncation: drop overflow capture events before seal/publish.
    loop.capture.store.dropEventsAtOrBeyondTick(loop.loopLengthTicks);
  }

  const CommitResult commitResult =
      loop.commitCapturePass(CommitReason::RecordStopToStopped, currentTick);
  pendingNotes.clear();

  // Validate AFTER loopLengthTicks is known (see stopRecording for rationale).
  const uint32_t closeTick = UINT32_MAX;
  const uint32_t finalizeHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t finalizeStartUs = micros();
  const CommitResult sideEffectResult =
      finalizeCommitSideEffects(commitResult, CommitReason::RecordStopToStopped, closeTick);
  const uint32_t finalizeDurationUs = micros() - finalizeStartUs;
  const uint32_t finalizeHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  const StopPathStorageStats stopPathStats = collectStopPathStorageStats(loop, false);
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs, finalizeHeapBefore,
                     finalizeHeapAfter, commitResultLabel(sideEffectResult), &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0, finalizeHeapAfter,
                     finalizeHeapAfter,
                     sideEffectResult == CommitResult::Published ? "deferred" : "skipped",
                     &stopPathStats);

  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0, finalizeHeapAfter, finalizeHeapAfter,
                     sideEffectResult == CommitResult::Published ? "deferred" : "skipped",
                     &stopPathStats);

  [[maybe_unused]] const uint32_t recordStartTickStopped = loop.startLoopTick;
  loop.nextEventIndex = 0;
  uint32_t playbackTick = currentTick;
  const uint32_t rewindTicks = computeTruncationRewindTicks(rawLength, loop.loopLengthTicks);
  if (rewindTicks > 0) {
    playbackTick = currentTick - rewindTicks;
    clockManager.assignCurrentTickSilently(playbackTick);
  }
  loop.startLoopTick = recordStartTickStopped;
  loop.lastTickInLoop = (loop.loopLengthTicks > 0)
                            ? tickPhaseInLoop(playbackTick, recordStartTickStopped, loop.loopLengthTicks)
                            : 0;
  invalidatePlaybackCaches();

  SC_REC_STOP("stopToStopped", activeLoopIndex, playbackTick, recordStartTickStopped,
              rawLength, loop.loopLengthTicks, false);
  logger.logTrackEvent("Recording stopped (to STOPPED)", playbackTick, "length=%lu",
                       static_cast<unsigned long>(loop.loopLengthTicks));

  const uint32_t stateAdvanceHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  if (!loop.hasData()) {
    resetActiveLoopAfterEmptyCapture(loop);
    logRecordStopStage(loop, stopPathStartUs, "state_advance", 0, stateAdvanceHeapBefore,
                       stateAdvanceHeapBefore, "skipped_empty", &stopPathStats);
    logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stateAdvanceHeapBefore,
                       stateAdvanceHeapBefore, "skipped", &stopPathStats);
    setState(hasAnySlotData() ? TRACK_STOPPED : TRACK_EMPTY);
    return;
  }

  const uint32_t stateAdvanceStartUs = micros();
  setState(TRACK_STOPPED);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  const uint32_t stateAdvanceHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     stateAdvanceHeapBefore, stateAdvanceHeapAfter,
                     trackState == TRACK_STOPPED ? "ok" : "failed", &stopPathStats);
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stateAdvanceHeapAfter,
                     stateAdvanceHeapAfter,
                     sideEffectResult == CommitResult::Published ? "requested" : "skipped",
                     &stopPathStats);
  if (sideEffectResult == CommitResult::Published) {
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                                recordedSlotIndex);
    StorageManager::requestDeferredSaveState(looperState.getLooperState(), stateAdvanceHeapAfter,
                                             true);
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
  if (editManager.isNoteEditActive()) {
    finalizePendingNotes(currentTick);
    editManager.foldLiveCaptureIntoNoteEditSession(*this, closeTick);
    pendingNotes.clear();
    const uint32_t stateHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
    const uint32_t stateStartUs = micros();
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
    resetPlaybackState(currentTick);
    emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
    logOverdubStopStage(loop, stopStartUs, "display", 0, MemoryMonitor::getInternalHeapFreeBytes(),
                        MemoryMonitor::getInternalHeapFreeBytes(), "ok");
    HotPathTelemetry::requestDeferredSummary("overdub_stop");
    return;
  }
  finalizePendingNotes(currentTick);
  const uint32_t sealHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t sealStartUs = micros();
  const CommitResult commitResult =
      loop.commitCapturePass(CommitReason::OverdubStop, currentTick);
  logOverdubStopStage(loop, stopStartUs, "seal", micros() - sealStartUs, sealHeapBefore,
                      MemoryMonitor::getInternalHeapFreeBytes(), commitResultLabel(commitResult));
  const uint32_t finalizeHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t finalizeStartUs = micros();
  const CommitResult sideEffectResult =
      finalizeCommitSideEffects(commitResult, CommitReason::OverdubStop, closeTick);
  logOverdubStopStage(loop, stopStartUs, "finalize", micros() - finalizeStartUs,
                      finalizeHeapBefore, MemoryMonitor::getInternalHeapFreeBytes(),
                      commitResultLabel(sideEffectResult));
  const uint32_t stateHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t stateStartUs = micros();
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

  // Preserve phase origin from record-stop rewind so playback cursor and event phase stay aligned.
  resetPlaybackState(currentTick);
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
  if (editManager.isNoteEditActive()) {
    finalizePendingNotes(currentTick);
    editManager.foldLiveCaptureIntoNoteEditSession(*this, closeTick);
    pendingNotes.clear();
    logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
    setState(TRACK_STOPPED);
    resetPlaybackState(currentTick);
    displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
    logger.logTrackEvent("Overdubbing stopped (to STOPPED)", currentTick);
    HotPathTelemetry::requestDeferredSummary("overdub_stop_to_stopped");
    return;
  }
  finalizePendingNotes(currentTick);
  const CommitResult commitResult =
      loop.commitCapturePass(CommitReason::OverdubStopToStopped, currentTick);
  finalizeCommitSideEffects(commitResult, CommitReason::OverdubStopToStopped, closeTick);
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
// Toggle mute
// -------------------------

void Track::toggleMuteTrack() {
  muted = !muted;
}

bool Track::isMuted() const {
  return muted;
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

void Track::recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if ((isRecording() && !isPlaying()) || isOverdubbing()) {
    Loop& loop = getActiveLoop();
    uint32_t tickRelative;

    if (isRecording() && !isPlaying()) {
      tickRelative = currentTick - loop.startLoopTick;

    } else if (isOverdubbing()) {
      if (loop.loopLengthTicks == 0) return;
      tickRelative = capturePhaseTick(currentTick);

    } else {
      return;
    }

    // Build the new event first (needed for duplicate check)
    MidiEvent newEvt;
    bool eventAdded = false;
    switch (type) {
        case midi::NoteOn:
            newEvt = MidiEvent::NoteOn(tickRelative, channel, data1, data2);
            newEvt.noteId = loop.allocateNoteId();
            eventAdded = true;
            break;
        case midi::NoteOff:
            newEvt = MidiEvent::NoteOff(tickRelative, channel, data1, data2);
            eventAdded = true;
            break;
        case midi::ControlChange:
            newEvt = MidiEvent::ControlChange(tickRelative, channel, data1, data2);
            eventAdded = true;
            break;
        case midi::ProgramChange:
            newEvt = MidiEvent::ProgramChange(tickRelative, channel, data1);
            eventAdded = true;
            break;
        case midi::AfterTouchChannel:
            newEvt = MidiEvent::ChannelAftertouch(tickRelative, channel, data1);
            eventAdded = true;
            break;
        case midi::PitchBend:
            newEvt = MidiEvent::PitchBend(tickRelative, channel, (int16_t)((data2 << 7) | data1));
            eventAdded = true;
            break;
        default:
            return;
    }

    if (!eventAdded) return;

    if (type == midi::NoteOff && isOverdubbing() && loop.loopLengthTicks > 0) {
      const LoopEventStore& capture = loop.capture.store;
      for (size_t i = capture.size(); i > 0; --i) {
        const MidiEvent& prior = capture.at(i - 1);
        if (!prior.isNoteOn() || prior.channel != channel ||
            prior.data.noteData.note != data1) {
          continue;
        }
        const bool wrappedHeadOff =
            tickRelative < prior.tick &&
            pendingNotes.find({data1, channel}) != pendingNotes.end();
        if (tickRelative <= prior.tick && !wrappedHeadOff) {
          tickRelative = prior.tick + 1;
          newEvt.tick = tickRelative;
        }
        break;
      }
    }

    if (loop.loopLengthTicks > 0 && tickRelative >= loop.loopLengthTicks) {
      tickRelative = loop.loopLengthTicks - 1;
      newEvt.tick = tickRelative;
    }

    if (!loop.appendCaptureEvent(newEvt)) {
      logger.log(CAT_TRACK, LOG_WARNING,
                 "Capture append failed (chunk pool or memory pressure) ch=%u note=%u",
                 static_cast<unsigned>(channel), static_cast<unsigned>(data1));
      MemoryMonitor::notifyCaptureAppendFailed(millis());
      return;
    }

#if defined(SESSION_CAPTURE)
    if (isOverdubbing() && (type == midi::NoteOn || type == midi::NoteOff)) {
      logOverdubCaptureCoordinate(*this, currentTick, newEvt.tick, channel, data1);
    }
#endif

    if ((isRecording() && !isPlaying()) || isOverdubbing()) {
      ++loop.captureDisplayRevision;
    }

    if (type == midi::NoteOn) {
      ++recordAddedNoteOnCount;
    }
  }
}

void Track::rebuildPlaybackOrder() {
  Loop& loop = getActiveLoop();
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(activeLoopIndex);
  ensurePlaybackWindowBuilt(*this, loop, runtime);
  const uint32_t currentTick = clockManager.getCurrentTick();
  const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  ::rebuildPlaybackOrder(loop, runtime.primaryWindow.mergedEvents, playbackContext);
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

void Track::playMidiEvents(uint32_t currentTick, bool isAudible) {
  if (isStoppedRecording()) {
    return;
  }
  Loop& loop = getActiveLoop();
  if (!isAudible || muted || !loop.hasPublishedEvents() || loop.loopLengthTicks == 0)
    return;
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(activeLoopIndex);
  if (runtime.isStale(loop.playbackRevision, playbackGeneration)) {
    runtime.reset(true);
    loop.nextEventIndex = 0;
    loop.captureNextEventIndex = 0;
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
  }

  ensurePlaybackWindowBuilt(*this, loop, runtime);
  const SessionMidiEventVec& mergedEvents = runtime.primaryWindow.mergedEvents;
  if (mergedEvents.empty()) {
    return;
  }

  const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);

  if (loop.playbackOrderDirty) {
    ::rebuildPlaybackOrder(loop, mergedEvents, playbackContext);
    reanchorPlaybackIndex(loop, mergedEvents, loop.getPlaybackOrder(), playbackContext);
  }

  uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);

  if (IntervalProjection::didDisplayPlayheadWrapBackward(tickInLoop, loop.lastTickInLoop,
                                                       loop.loopStartTick,
                                                       loop.loopLengthTicks)) {
    projectionCycleStartTick = IntervalProjection::advanceProjectionCycleStartTickOnWrap(
        projectionCycleStartTick, loop.loopLengthTicks);
    loop.nextEventIndex = 0;
    loop.captureNextEventIndex = 0;
    logger.trace("Loop wrapped, resetting index");
  }

  uint32_t prevTickInLoop = loop.lastTickInLoop;
  loop.lastTickInLoop = tickInLoop;

  bool atLoopStart = (prevTickInLoop == UINT32_MAX) || (tickInLoop <= prevTickInLoop);

  auto eventInJamRegion = [this, &loop](uint32_t evTick) -> bool {
    if (!jamPlaybackActive || jamLength == 0) return true;
    if (jamStartTick == UINT32_MAX) return true;
    uint32_t jamEnd = jamStartTick + jamLength;
    if (jamEnd <= loop.loopLengthTicks) {
      return evTick >= jamStartTick && evTick < jamEnd;
    }
    return (evTick >= jamStartTick) || (evTick < jamEnd - loop.loopLengthTicks);
  };

  uint32_t lastSentEvTick = UINT32_MAX;
  uint8_t lastSentChannel = 0;
  uint8_t lastSentNote = 0;
  uint8_t lastSentType = 0xFF;

  const PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();
  while (loop.nextEventIndex < playbackOrder.size()) {
    const MidiEvent &evt = mergedEvents[playbackOrder[loop.nextEventIndex]];
    const uint32_t evTick =
        IntervalProjection::playbackEventPhase(evt.tick, playbackContext.loopLength);
    const uint32_t evStorageTick = evt.tick;

    bool crossed = atLoopStart ? (evTick <= tickInLoop) : (prevTickInLoop < evTick && evTick <= tickInLoop);
    if (crossed && eventInJamRegion(evStorageTick)) {
      uint8_t effectiveCh = (evt.channel >= 1 && evt.channel <= 16) ? midiChannel : evt.channel;
      uint8_t note = evt.isNoteOn() || evt.isNoteOff() ? evt.data.noteData.note : 0;
      bool isDuplicate = (evt.isNoteOn() || evt.isNoteOff()) &&
                         (evTick == lastSentEvTick && effectiveCh == lastSentChannel &&
                          note == lastSentNote && evt.type == lastSentType);
      if (!isDuplicate) {
        sendMidiEvent(evt);
        if (evt.isNoteOn() || evt.isNoteOff()) {
          lastSentEvTick = evTick;
          lastSentChannel = effectiveCh;
          lastSentNote = note;
          lastSentType = evt.type;
        }
      }
      loop.nextEventIndex++;
    }
    else if (evTick > tickInLoop) {
      break;
    }
    else {
      loop.nextEventIndex++;
    }
  }

  if (loop.capture.phase == CapturePhase::Overdub && !loop.capture.store.empty()) {
    if (loop.ensureCaptureEventsSorted()) {
      reanchorCaptureIndex(loop);
    }
    while (loop.captureNextEventIndex < loop.capture.store.size()) {
      const MidiEvent& evt = loop.capture.store.at(loop.captureNextEventIndex);
      const uint32_t evTick =
          IntervalProjection::projectPlaybackEventPhase(evt.tick, playbackContext);
      const uint32_t evStorageTick = evt.tick;
      const bool crossed =
          atLoopStart ? (evTick <= tickInLoop) : (prevTickInLoop < evTick && evTick <= tickInLoop);
      if (crossed && eventInJamRegion(evStorageTick)) {
        sendMidiEvent(evt);
        loop.captureNextEventIndex++;
      } else if (evTick > tickInLoop) {
        break;
      } else {
        loop.captureNextEventIndex++;
      }
    }
  }
  runtime.syncRevision(loop.playbackRevision, playbackGeneration);
}

void Track::playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool isAudible) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  if (isStoppedRecording()) return;
  if (!isAudible || muted) return;

  Loop& loop = getLoop(slotIndex);
  if (!loop.hasPublishedEvents() || loop.loopLengthTicks == 0) return;
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(slotIndex);
  if (runtime.isStale(loop.playbackRevision, playbackGeneration)) {
    runtime.reset(true);
    loop.nextEventIndex = 0;
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
  }

  ensurePlaybackWindowBuilt(*this, loop, runtime);
  const SessionMidiEventVec& mergedEvents = runtime.primaryWindow.mergedEvents;
  if (mergedEvents.empty()) {
    return;
  }

  const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);

  if (loop.playbackOrderDirty) {
    ::rebuildPlaybackOrder(loop, mergedEvents, playbackContext);
    reanchorPlaybackIndex(loop, mergedEvents, loop.getPlaybackOrder(), playbackContext);
  }

  uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);
  if (IntervalProjection::didDisplayPlayheadWrapBackward(tickInLoop, loop.lastTickInLoop,
                                                       loop.loopStartTick,
                                                       loop.loopLengthTicks)) {
    loop.nextEventIndex = 0;
  }

  uint32_t prevTickInLoop = loop.lastTickInLoop;
  loop.lastTickInLoop = tickInLoop;
  bool atLoopStart = (prevTickInLoop == UINT32_MAX) || (tickInLoop <= prevTickInLoop);
  const PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();

  while (loop.nextEventIndex < playbackOrder.size()) {
    const MidiEvent &evt = mergedEvents[playbackOrder[loop.nextEventIndex]];
    const uint32_t evTick =
        IntervalProjection::playbackEventPhase(evt.tick, playbackContext.loopLength);
    bool crossed = atLoopStart ? (evTick <= tickInLoop) : (prevTickInLoop < evTick && evTick <= tickInLoop);
    if (crossed) {
      sendMidiEvent(evt);
      loop.nextEventIndex++;
    } else if (evTick > tickInLoop) {
      break;
    } else {
      loop.nextEventIndex++;
    }
  }
  runtime.syncRevision(loop.playbackRevision, playbackGeneration);
}

void Track::sendMidiEvent(const MidiEvent& evt) {
  if (trackState != TRACK_PLAYING && trackState != TRACK_OVERDUBBING) return;
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
  // also clear any half-open pending notes so they don't get forced later
  pendingNotes.clear();
  logger.logTrackEvent("All Notes Off sent", clockManager.getCurrentTick());
}

uint32_t Track::getTicksPerBar() {
    return TICKS_PER_BAR;
}

bool Track::isEmpty() const{
  return trackState == TRACK_EMPTY;
}

bool Track::isStopped() const {
  return trackState == TRACK_STOPPED;
}

bool Track::isArmed() const {
  return trackState == TRACK_ARMED;
}

bool Track::isRecording() const {
  return trackState == TRACK_RECORDING;
}

bool Track::isStoppedRecording() const {
  return trackState == TRACK_STOPPED_RECORDING;
}

bool Track::isOverdubbing() const {
  return trackState == TRACK_OVERDUBBING;
}

bool Track::isPlaying() const {
  return trackState == TRACK_PLAYING;
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

// Display functions
void Track::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered MIDI events

  if (trackState == TRACK_ARMED) {
    armedPreRollNotes[{note, channel}] =
        PendingNote{note, channel, tick, velocity};
    return;
  }

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    // Store pending note for later duration fix
    pendingNotes[{note, channel}] = PendingNote{
      note,
      channel,
      tick,
      velocity
    };

    recordMidiEvents(midi::NoteOn, channel, note, velocity, tick);
  }
}

void Track::noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;

  if (trackState == TRACK_ARMED) {
    armedPreRollNotes.erase({note, channel});
    return;
  }

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    auto key = std::make_pair(note, channel);
    auto it = pendingNotes.find(key);
    if (it != pendingNotes.end()) {
      recordMidiEvents(midi::NoteOff, channel, note, 0, tick);
      pendingNotes.erase(it);
    } else {
      logger.log(CAT_MIDI, LOG_WARNING,
                 "NoteOff for note %d on ch %d with no matching NoteOn",
                 note, channel);
    }
    return;
  }

  // After stop/finalize: pending already closed; ignore late physical release.
}

TRACK_COLD_MEM bool Track::hasPublishedEventsInSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  return loopForSlot(slotIndex).hasPublishedEvents();
}

TRACK_COLD_MEM uint32_t Track::quantizeTransportRecordLength(uint32_t rawLength) const {
  if (rawLength == 0) {
    return TICKS_PER_BAR;
  }
  const uint32_t rem = rawLength % TICKS_PER_BAR;
  const uint32_t grace = TICKS_PER_BAR / 2;
  if (rem <= grace) {
    const uint32_t quantized = (rawLength / TICKS_PER_BAR) * TICKS_PER_BAR;
    return quantized == 0 ? TICKS_PER_BAR : quantized;
  }
  return ((rawLength / TICKS_PER_BAR) + 1) * TICKS_PER_BAR;
}

TRACK_COLD_MEM uint32_t Track::computeRecordStopLengthTicks(uint32_t rawLength,
                                                            uint32_t lastEventTick) const {
  const uint32_t transportLength = quantizeTransportRecordLength(rawLength);
  if (lastEventTick == 0) {
    return transportLength;
  }
  const uint32_t contentLength = computeLoopLengthTicks(lastEventTick);
  return std::min(transportLength, contentLength);
}

TRACK_COLD_MEM void Track::resetLoopSlotAfterEmptyCapture(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  resetActiveLoopAfterEmptyCapture(loopForSlot(slotIndex));
}

