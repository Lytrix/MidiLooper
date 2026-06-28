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
#include <limits>
#include "Utils/DebugSessionCapture.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/NoteUtils.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "TrackManager.h"

extern TrackManager trackManager;

MidiEventVec& Track::editAwareMidiEvents() {
  return editManager.editMidiEvents(*this);
}

const MidiEventVec& Track::editAwareMidiEvents() const {
  return editManager.editMidiEvents(*this);
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
                        const char* outcome) {
  const StopPathStorageStats stats = collectStopPathStorageStats(loop);
  const uint32_t elapsedUs = micros() - stopStartUs;
  SC_REC_STOP_STAGE(stage, elapsedUs, stageDurationUs, heapBefore, heapAfter,
                    stats.eventCount, stats.chunkRefCount, outcome);
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

uint32_t playbackSortPhase(const MidiEvent& evt, uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0 || evt.tick < loopLengthTicks) {
    return evt.tick;
  }
  // Beyond active loop length: keep in storage but play after in-loop events each pass.
  return loopLengthTicks + evt.tick;
}

/// After a mid-pass playback-order rebuild, point nextEventIndex at the current playhead.
void reanchorPlaybackIndex(Loop& loop, const MidiEventVec& mergedEvents, const PlaybackOrderVec& order) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.nextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < order.size()) {
    const MidiEvent& e = mergedEvents[order[idx]];
    if (e.tick >= loop.loopLengthTicks || e.tick > loop.lastTickInLoop) break;
    ++idx;
  }
  loop.nextEventIndex = static_cast<uint16_t>(idx);
}

void ensurePlaybackWindowBuilt(Loop& loop, LoopPlaybackRuntime& runtime) {
  if (runtime.primaryWindow.builtFromRevision == loop.playbackRevision) {
    return;
  }
  loop.mergeActiveCapturePasses(runtime.primaryWindow.mergedEvents);
  runtime.primaryWindow.builtFromRevision = loop.playbackRevision;
  runtime.primaryWindow.effectiveWindowBars = Config::PLAYBACK_WINDOW_MAX_BARS;
  loop.playbackOrderDirty = true;
}

void rebuildPlaybackOrder(Loop& loop, const MidiEventVec& mergedEvents) {
  PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();
  playbackOrder.resize(mergedEvents.size());
  for (size_t i = 0; i < mergedEvents.size(); i++) {
    playbackOrder[i] = i;
  }
  uint32_t ll = loop.loopLengthTicks;
  std::sort(playbackOrder.begin(), playbackOrder.end(),
    [&](size_t a, size_t b) {
      return playbackSortPhase(mergedEvents[a], ll) < playbackSortPhase(mergedEvents[b], ll);
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
  if (isEmpty()) {
    // Preroll target is truly empty.
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
    return;
  }
  loop.lastTickInLoop = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
}

void Track::resetPlaybackStateForSlot(uint8_t slotIndex, uint32_t currentTick) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  Loop& loop = getLoop(slotIndex);
  if (loop.loopLengthTicks == 0) return;
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  invalidatePlaybackWindow(true);
}

void Track::invalidatePlaybackWindow(bool preserveLedger) {
  bumpPlaybackGeneration();
  playbackRuntime.resetAll(preserveLedger);
}

void Track::finalizePendingNotes(uint32_t offAbsTick) {
    // Temporarily pretend we're still recording so noteOff() will queue things
    TrackState prev = trackState;
    trackState = TRACK_RECORDING;

    // 1) Copy out the pending keys
    std::vector<std::pair<uint8_t,uint8_t>> toClose;
    toClose.reserve(pendingNotes.size());
    for (auto const &kv : pendingNotes) {
        toClose.push_back(kv.first);
    }

    // 2) Emit a noteOff() for each key (this will record the NoteEvent
    //    but no longer erase inside the map)
    for (auto const &key : toClose) {
        uint8_t note    = key.first;
        uint8_t channel = key.second;
        noteOff(channel, note, 0, offAbsTick);
    }

    // 3) Now safely clear all remaining pending notes
    pendingNotes.clear();

    // Restore the real state
    trackState = prev;
    
    // OPTIMIZATION: Invalidate caches when MIDI events change
    invalidateCaches();
}

void Track::validateAndCleanupMidiEvents(uint32_t openTailCloseTick) {
    Loop& loop = getActiveLoop();
    MidiEventVec materializedEvents;
    loop.mergeActiveCapturePasses(materializedEvents);
    const size_t publishedBefore = materializedEvents.size();
    if (materializedEvents.empty()) return;
    
    // Map to track active notes: key = (note, channel), value = note-on event index
    std::unordered_map<std::pair<uint8_t, uint8_t>, size_t, PairHash> activeNotes;
    std::vector<bool> eventsToKeep(materializedEvents.size(), true);
    std::vector<MidiEvent> syntheticNoteOffs;
    int orphanedCount = 0;
    
    // Sort events by tick to ensure proper order
    std::sort(materializedEvents.begin(), materializedEvents.end(),
              [](const MidiEvent& a, const MidiEvent& b) {
                  if (a.tick != b.tick) return a.tick < b.tick;
                  // On equal tick, process note-offs before note-ons to avoid false overlap.
                  const int aOrder = a.isNoteOff() ? 0 : (a.isNoteOn() ? 1 : 2);
                  const int bOrder = b.isNoteOff() ? 0 : (b.isNoteOn() ? 1 : 2);
                  return aOrder < bOrder;
              });
    
    // First pass: match note-on/note-off pairs
    for (size_t i = 0; i < materializedEvents.size(); i++) {
        const MidiEvent& evt = materializedEvents[i];
        
        if (evt.isNoteOn()) {
            std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
            
            // Check if there's already an active note (orphaned note-on)
            if (activeNotes.find(key) != activeNotes.end()) {
                size_t prevIndex = activeNotes[key];
                eventsToKeep[prevIndex] = false;
                orphanedCount++;
                logger.log(CAT_MIDI, LOG_WARNING,
                          "Removed orphaned note-on: note %d, channel %d, tick %lu",
                          evt.data.noteData.note, evt.channel, materializedEvents[prevIndex].tick);
            }
            
            // Track this note-on
            activeNotes[key] = i;
            
        } else if (evt.isNoteOff()) {
            std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
            auto it = activeNotes.find(key);
            
            if (it != activeNotes.end()) {
                // Found matching note-on, remove from active notes
                activeNotes.erase(it);
            } else {
                bool wrappedTailAhead = false;
                if (loop.loopLengthTicks > 0) {
                    const uint32_t noteOffTick = evt.tick;
                    for (size_t j = i + 1; j < materializedEvents.size(); ++j) {
                        if (!eventsToKeep[j]) {
                            continue;
                        }
                        const MidiEvent& later = materializedEvents[j];
                        if (!later.isNoteOn() || later.channel != evt.channel ||
                            later.data.noteData.note != evt.data.noteData.note) {
                            continue;
                        }
                        if (!NoteUtils::isHeadTailWrappedPair(later.tick, noteOffTick,
                                                             loop.loopLengthTicks)) {
                            continue;
                        }
                        if (!NoteUtils::wrapPairIsUnblocked(materializedEvents, noteOffTick,
                                                            later.tick, evt.data.noteData.note,
                                                            evt.channel)) {
                            continue;
                        }
                        wrappedTailAhead = true;
                        break;
                    }
                }
                if (!wrappedTailAhead) {
                    eventsToKeep[i] = false;
                    orphanedCount++;
                    logger.log(CAT_MIDI, LOG_WARNING,
                              "Removed orphaned note-off: note %d, channel %d, tick %lu",
                              evt.data.noteData.note, evt.channel, evt.tick);
                }
            }
        }
    }
    
    // Check for remaining active notes (note-on without note-off)
    for (const auto& pair : activeNotes) {
        size_t index = pair.second;
        const MidiEvent& noteOn = materializedEvents[index];
        if (loop.loopLengthTicks > 0) {
            uint32_t closeTick = loop.loopLengthTicks - 1;
            if (openTailCloseTick != UINT32_MAX) {
                closeTick = std::min(openTailCloseTick, loop.loopLengthTicks - 1);
                if (closeTick < noteOn.tick) {
                    closeTick = loop.loopLengthTicks - 1;
                }
            }

            syntheticNoteOffs.push_back(
                MidiEvent::NoteOff(closeTick, noteOn.channel, noteOn.data.noteData.note, 0));
            if (openTailCloseTick != UINT32_MAX) {
                logger.log(CAT_MIDI, LOG_INFO,
                          "Inserted synthetic note-off at stop playhead: note %d, channel %d, tick %lu",
                          noteOn.data.noteData.note, noteOn.channel, closeTick);
            } else {
                logger.log(CAT_MIDI, LOG_INFO,
                          "Inserted synthetic note-off for open tail note: note %d, channel %d, tick %lu",
                          noteOn.data.noteData.note, noteOn.channel, closeTick);
            }
        } else {
            // Loop length is not finalized yet (first record-stop path). Keep this tail
            // note now; a later validation pass with known loop length will close it.
            logger.log(CAT_MIDI, LOG_INFO,
                      "Deferred open tail note cleanup (loop length unknown): note %d, channel %d, tick %lu",
                      noteOn.data.noteData.note, noteOn.channel, noteOn.tick);
        }
    }
    
    // Second pass: handle loop wrapping for remaining unmatched notes
    if (loop.loopLengthTicks > 0) {
        activeNotes.clear();
        
        // Look for note-on near end that might have note-off near beginning
        for (size_t i = 0; i < materializedEvents.size(); i++) {
            if (!eventsToKeep[i]) continue;
            
            const MidiEvent& evt = materializedEvents[i];
            
            if (evt.isNoteOn()) {
                std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
                activeNotes[key] = i;
                
            } else if (evt.isNoteOff()) {
                std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
                auto it = activeNotes.find(key);
                
                if (it != activeNotes.end()) {
                    // Check if this could be a wrapped note
                    size_t noteOnIndex = it->second;
                    uint32_t noteOnTick = materializedEvents[noteOnIndex].tick;
                    uint32_t noteOffTick = evt.tick;
                    
                    // If note-off is much earlier than note-on, it might be wrapped
                    if (noteOffTick < noteOnTick && (noteOnTick - noteOffTick) > (loop.loopLengthTicks / 2)) {
                        // This looks like a wrapped note - keep both events
                        activeNotes.erase(it);
                        logger.log(CAT_MIDI, LOG_INFO, 
                                  "Found wrapped note: note %d, channel %d, on-tick %lu, off-tick %lu",
                                  evt.data.noteData.note, evt.channel, noteOnTick, noteOffTick);
                    }
                }
            }
        }
    }
    
    // Remove orphaned events and append synthetic open-tail note-offs.
    if (orphanedCount > 0 || !syntheticNoteOffs.empty()) {
        MidiEventVec cleanedEvents;
        cleanedEvents.reserve(materializedEvents.size() - orphanedCount + syntheticNoteOffs.size());
        
        for (size_t i = 0; i < materializedEvents.size(); i++) {
            if (eventsToKeep[i]) {
                cleanedEvents.push_back(materializedEvents[i]);
            }
        }
        for (const auto& evt : syntheticNoteOffs) {
            cleanedEvents.push_back(evt);
        }
        std::sort(cleanedEvents.begin(), cleanedEvents.end(),
                  [](const MidiEvent& a, const MidiEvent& b) {
                      if (a.tick != b.tick) return a.tick < b.tick;
                      const int aOrder = a.isNoteOff() ? 0 : (a.isNoteOn() ? 1 : 2);
                      const int bOrder = b.isNoteOff() ? 0 : (b.isNoteOn() ? 1 : 2);
                      return aOrder < bOrder;
                  });

        const bool cleanedEmpty = cleanedEvents.empty();
        if (cleanedEmpty && publishedBefore > 0 && loop.hasPublishedEvents() &&
            loop.loopLengthTicks > 0) {
            invalidateCaches();
            logger.log(CAT_MIDI, LOG_WARNING,
                      "MIDI validation aborted flush: cleaned materialized view empty but published passes remain");
            return;
        }
        LoopEventStore cleanedStore;
        cleanedStore.loadFromFlat(cleanedEvents);
        loop.commitStopFinalizeFromStore(cleanedStore);
        invalidateCaches();
        
        logger.log(CAT_MIDI, LOG_INFO, 
                  "MIDI validation complete: removed %d orphaned events, inserted %d synthetic note-offs, %d events remaining",
                  orphanedCount, (int)syntheticNoteOffs.size(), (int)cleanedEvents.size());
    } else {
        logger.log(CAT_MIDI, LOG_INFO, 
                  "MIDI validation complete: no orphaned events found, %d events total",
                  (int)materializedEvents.size());
    }
}

void Track::finalizeLoopAtStop(uint32_t openTailCloseTick, bool scheduleDeferredFullValidate) {
  Loop& loop = getActiveLoop();
  if (!loop.hasPublishedEvents() || loop.loopLengthTicks == 0) {
    deferredFullMidiValidate = false;
    deferredValidateQueuedAtMs = 0;
    return;
  }

  MidiEventVec flat;
  loop.mergeActiveCapturePasses(flat);
  if (flat.empty()) {
    deferredFullMidiValidate = false;
    deferredValidateQueuedAtMs = 0;
    return;
  }

  LoopEventStore merged;
  merged.loadFromFlat(flat);
  bool mutated = false;

  if (!pendingNotes.empty()) {
    uint32_t closeTick = loop.loopLengthTicks - 1;
    if (openTailCloseTick != UINT32_MAX) {
      closeTick = std::min(openTailCloseTick, loop.loopLengthTicks - 1);
    }
    for (const auto& kv : pendingNotes) {
      merged.append(MidiEvent::NoteOff(closeTick, kv.first.second, kv.first.first, 0));
    }
    pendingNotes.clear();
    mutated = true;
  }

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindowOnStore(merged, loop.loopLengthTicks,
                                                  openTailCloseTick, Config::TICKS_PER_BAR);
  if (mutated || result.syntheticOffsInserted > 0) {
    loop.commitStopFinalizeFromStore(merged);
    loop.invalidatePlaybackCaches();
  }
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
        StorageManager::requestDeferredSaveState(looperState.getLooperState());
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
      } else if (!editManager.isNoteEditActive()) {
        TrackUndo::pushOverdubPassAdded(*this, getActiveLoopIndex(), undoPassId);
      }
      if (recordStop || overdubStop) {
        StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                                    getActiveLoopIndex());
        StorageManager::requestDeferredSaveState(looperState.getLooperState());
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

  MidiEventVec flat;
  loop.mergeActiveCapturePasses(flat);
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn()) {
      SC_STORED_NOTE_EVENT('N', evt.tick, evt.channel, evt.data.noteData.note);
    } else if (evt.isNoteOff()) {
      SC_STORED_NOTE_EVENT('F', evt.tick, evt.channel, evt.data.noteData.note);
    }
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
}

void Track::processDeferredIdleMaintenance(uint32_t nowMs) {
  // REVT capture is chunked and reads committed passes only; allow while PLAYING so
  // record-stop -> PLAYING does not strand deferred REVT until transport stops.
  if (!isRecording() && !isOverdubbing()) {
    processDeferredRecordRevts(64);
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
      SC_REC_STORED_NOTE_ON(evt.tick, evt.channel, evt.data.noteData.note);
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
      SC_REC_STORED_NOTE_ON(evt.tick, evt.channel, evt.data.noteData.note);
      ++queued;
    }
  }

  if (deferredRecordRevtCursor >= deferredRecordRevtEvents.size()) {
    logger.log(CAT_TRACK, LOG_DEBUG, "Queued REVT note-ons (deferred): %d",
               static_cast<int>(queued));
    resetDeferredRecordRevts();
  }
}

void Track::closeOpenNotesAtLoopWrap() {
  if (!isOverdubbing()) {
    return;
  }

  Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || loop.capture.phase != CapturePhase::Overdub) {
    return;
  }
  if (pendingNotes.empty()) {
    return;
  }

  const uint32_t closeTick = loop.loopLengthTicks - 1;
  MidiEventVec liveView;
  loop.mergeMaterializedPassesWithCapture(liveView);

  std::vector<MidiEvent> syntheticNoteOffs;
  syntheticNoteOffs.reserve(pendingNotes.size());

  for (const auto& kv : pendingNotes) {
    const uint8_t note = kv.first.first;
    const uint8_t channel = kv.first.second;
    const PendingNote& pending = kv.second;

    bool alreadyClosedAtLoopEnd = false;
    for (const auto& evt : liveView) {
      if (!evt.isNoteOff()) {
        continue;
      }
      if (evt.channel != channel || evt.data.noteData.note != note) {
        continue;
      }
      if (evt.tick == closeTick) {
        alreadyClosedAtLoopEnd = true;
        break;
      }
    }
    if (alreadyClosedAtLoopEnd) {
      continue;
    }

    syntheticNoteOffs.push_back(MidiEvent::NoteOff(closeTick, channel, note, 0));
    const uint32_t onTick = tickPhaseInLoop(pending.startNoteTick, loop.startLoopTick,
                                            loop.loopLengthTicks);
    logger.log(CAT_MIDI, LOG_INFO,
               "Loop-wrap synthetic note-off: note %d, channel %d, on %lu -> off %lu",
               note, channel, onTick, closeTick);
  }

  if (syntheticNoteOffs.empty()) {
    return;
  }

  for (const MidiEvent& off : syntheticNoteOffs) {
    loop.appendCaptureEvent(off);
  }

  ++loop.captureDisplayRevision;
}

// -------------------------
// Stop recording
// -------------------------

void Track::stopRecording(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  [[maybe_unused]] const bool captureAlignFlag = alignLoopOriginOnNextStop;
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
  uint32_t rem       = rawLength % TICKS_PER_BAR;
  uint32_t grace     = TICKS_PER_BAR / 2;

  if (rawLength == 0) {
      loop.loopLengthTicks = TICKS_PER_BAR;
  } else if (rem <= grace) {
      loop.loopLengthTicks = (rawLength / TICKS_PER_BAR) * TICKS_PER_BAR;
      if (loop.loopLengthTicks == 0) {
        loop.loopLengthTicks = TICKS_PER_BAR;
      }
  } else {
      loop.loopLengthTicks = ((rawLength / TICKS_PER_BAR) + 1) * TICKS_PER_BAR;
  }

  if (loop.loopLengthTicks > 0) {
    if (!pendingNotes.empty()) {
      const uint32_t closeRel = loop.loopLengthTicks - 1;
      finalizePendingNotes(loop.startLoopTick + closeRel);
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
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs, finalizeHeapBefore,
                     finalizeHeapAfter, commitResultLabel(sideEffectResult));

  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0, finalizeHeapAfter,
                     finalizeHeapAfter,
                     sideEffectResult == CommitResult::Published ? "deferred" : "skipped");

  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0, finalizeHeapAfter, finalizeHeapAfter,
                     sideEffectResult == CommitResult::Published ? "deferred" : "skipped");

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
    clockManager.setCurrentTick(playbackTick);
    logger.log(CAT_TRACK, LOG_INFO,
               "Record stop truncation rewind: raw=%lu final=%lu rewind=%lu playbackTick=%lu positionInBar=%lu",
               rawLength, finalLength, rewindTicks, playbackTick, rawLength % Config::TICKS_PER_BAR);
  }

  loop.startLoopTick = recordStartTick;
  loop.lastTickInLoop = (finalLength > 0)
                            ? tickPhaseInLoop(playbackTick, recordStartTick, finalLength)
                            : 0;

  invalidatePlaybackCaches();
  SC_REC_STOP("stop", activeLoopIndex, playbackTick, recordStartTick, rawLength, finalLength, captureAlignFlag);
  logger.logTrackEvent("Recording stopped", playbackTick, "recStart=%lu length=%lu",
                       static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(finalLength));
  logger.debug("Final ticks: playbackTick=%lu recStart=%lu rawLength=%lu length=%lu", playbackTick,
               static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(rawLength),
               static_cast<unsigned long>(finalLength));

  // Empty record-stop (commit skipped) clears loopLengthTicks; leave a valid state.
  if (loop.loopLengthTicks == 0 || loop.activeCapturePassCount() == 0) {
    logRecordStopStage(loop, stopPathStartUs, "state_advance", 0, stopHeap, stopHeap,
                       "skipped_empty");
    logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stopHeap, stopHeap,
                       sideEffectResult == CommitResult::Published ? "requested" : "skipped");
    setState(TRACK_EMPTY);
    return;
  }

  // Return to playback after record-stop. Overdub starts on the next explicit
  // record press from PLAYING (record -> play -> overdub -> play flow).
  const uint32_t stateAdvanceHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t stateAdvanceStartUs = micros();
  startPlaying(playbackTick, true);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  const uint32_t stateAdvanceHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     stateAdvanceHeapBefore, stateAdvanceHeapAfter,
                     trackState == TRACK_PLAYING ? "ok" : "failed");
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stateAdvanceHeapAfter,
                     stateAdvanceHeapAfter,
                     sideEffectResult == CommitResult::Published ? "requested" : "skipped");
  if (sideEffectResult == CommitResult::Published) {
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                                getActiveLoopIndex());
    StorageManager::requestDeferredSaveState(looperState.getLooperState(), stateAdvanceHeapAfter);
  }
}

void Track::stopRecordingToStopped(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  alignLoopOriginOnNextStop = false;
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
  uint32_t rem       = rawLength % TICKS_PER_BAR;
  uint32_t grace     = TICKS_PER_BAR / 2;

  if (rawLength == 0) {
      loop.loopLengthTicks = TICKS_PER_BAR;
  } else if (rem <= grace) {
      loop.loopLengthTicks = (rawLength / TICKS_PER_BAR) * TICKS_PER_BAR;
      if (loop.loopLengthTicks == 0) {
        loop.loopLengthTicks = TICKS_PER_BAR;
      }
  } else {
      loop.loopLengthTicks = ((rawLength / TICKS_PER_BAR) + 1) * TICKS_PER_BAR;
  }

  if (loop.loopLengthTicks > 0) {
    if (!pendingNotes.empty()) {
      const uint32_t closeRel = loop.loopLengthTicks - 1;
      finalizePendingNotes(loop.startLoopTick + closeRel);
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
  logRecordStopStage(loop, stopPathStartUs, "finalize", finalizeDurationUs, finalizeHeapBefore,
                     finalizeHeapAfter, commitResultLabel(sideEffectResult));

  logRecordStopStage(loop, stopPathStartUs, "visual_cache_request", 0, finalizeHeapAfter,
                     finalizeHeapAfter,
                     sideEffectResult == CommitResult::Published ? "deferred" : "skipped");

  logRecordStopStage(loop, stopPathStartUs, "revt_queue", 0, finalizeHeapAfter, finalizeHeapAfter,
                     sideEffectResult == CommitResult::Published ? "deferred" : "skipped");

  [[maybe_unused]] const uint32_t recordStartTickStopped = loop.startLoopTick;
  loop.nextEventIndex = 0;
  uint32_t playbackTick = currentTick;
  const uint32_t rewindTicks = computeTruncationRewindTicks(rawLength, loop.loopLengthTicks);
  if (rewindTicks > 0) {
    playbackTick = currentTick - rewindTicks;
    clockManager.setCurrentTick(playbackTick);
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
  const uint32_t stateAdvanceStartUs = micros();
  setState(TRACK_STOPPED);
  const uint32_t stateAdvanceDurationUs = micros() - stateAdvanceStartUs;
  const uint32_t stateAdvanceHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  logRecordStopStage(loop, stopPathStartUs, "state_advance", stateAdvanceDurationUs,
                     stateAdvanceHeapBefore, stateAdvanceHeapAfter,
                     trackState == TRACK_STOPPED ? "ok" : "failed");
  logRecordStopStage(loop, stopPathStartUs, "save_request", 0, stateAdvanceHeapAfter,
                     stateAdvanceHeapAfter,
                     sideEffectResult == CommitResult::Published ? "requested" : "skipped");
  if (sideEffectResult == CommitResult::Published) {
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(*this),
                                                getActiveLoopIndex());
    StorageManager::requestDeferredSaveState(looperState.getLooperState(), stateAdvanceHeapAfter);
  }
}

// -------------------------
// Start playing
// -------------------------

void Track::startPlaying(uint32_t currentTick, bool preserveLoopPhaseOrigin) {
  Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks > 0) {
    if (trackState == TRACK_EMPTY) {
      forceSetState(TRACK_STOPPED);
    }
    if (!setState(TRACK_PLAYING)) return;
    if (!preserveLoopPhaseOrigin) {
      loop.startLoopTick = 0;
      loop.nextEventIndex = 0;
      loop.lastTickInLoop = UINT32_MAX;
    }
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
  const Loop& active = loopRef;
  if (trackState == TRACK_EMPTY && active.loopLengthTicks > 0) {
    forceSetState(TRACK_STOPPED);
  }
  if (!setState(TRACK_OVERDUBBING)) return;
  recordAddedNoteOnCount = 0;
  Loop& loop = getActiveLoop();
  loop.ensureVisualCacheBuilt();
  loop.beginCapture(CapturePhase::Overdub);
  TrackUndo::beginOverdubSession(*this);
  logger.info("Overdub session opened: events=%d, undo_entries=%d",
              static_cast<int>(loop.displayEventCountHint()),
              static_cast<int>(TrackUndo::getUndoCount(*this)));
  HotPathTelemetry::recordOverdubStart(micros() - telemetryStartUs,
                                       static_cast<uint32_t>(loop.displayEventCountHint()),
                                       static_cast<uint32_t>(TrackUndo::getUndoCount(*this)));
  logger.logTrackEvent("Overdubbing started", currentTick);
}


void Track::stopOverdubbing() {
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  }
  if (editManager.isNoteEditActive()) {
    closeOpenNotesAtLoopWrap();
    editManager.foldLiveCaptureIntoNoteEditSession(*this, closeTick);
    pendingNotes.clear();
    setState(TRACK_PLAYING);
    logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
    logger.logTrackEvent("Overdubbing stopped", currentTick);
    logger.info("Overdub stopped (in-edit fold): events=%d, undo_entries=%d",
                static_cast<int>(loop.displayEventCountHint()), TrackUndo::getUndoCount(*this));
    resetPlaybackState(currentTick);
    displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
    HotPathTelemetry::requestDeferredSummary("overdub_stop");
    return;
  }
  const CommitResult commitResult =
      loop.commitCapturePass(CommitReason::OverdubStop, currentTick);
  setState(TRACK_PLAYING);
  finalizeCommitSideEffects(commitResult, CommitReason::OverdubStop, closeTick);
  logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
  logger.logTrackEvent("Overdubbing stopped", currentTick);
  logger.info("Overdub stopped: events=%d, undo_entries=%d", static_cast<int>(loop.displayEventCountHint()),
              TrackUndo::getUndoCount(*this));

  // Preserve phase origin from record-stop rewind so playback cursor and event phase stay aligned.
  resetPlaybackState(currentTick);
  displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
  HotPathTelemetry::requestDeferredSummary("overdub_stop");
}

void Track::stopOverdubbingToStopped() {
  if (isEmpty()) return;
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  }
  sendAllNotesOff();
  if (editManager.isNoteEditActive()) {
    closeOpenNotesAtLoopWrap();
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

void Track::clear() {
    if (trackState == TRACK_EMPTY) {
        logger.debug("Track already empty; ignoring clear");
        return;
    }

    Loop& loop = getActiveLoop();
    const uint8_t clearedSlot = activeLoopIndex;
    loop.resetPassTimeline();
    loop.discardCapture();
    loop.startLoopTick = 0;
    loop.loopLengthTicks = 0;
    loop.loopStartTick = 0;

    const size_t prunedUndo = TrackUndo::clearUndoHistoryForSlot(*this, clearedSlot);

    setState(TRACK_EMPTY);
    alignLoopOriginOnNextStop = false;
    invalidateCaches();
    logger.log(CAT_TRACK, LOG_INFO, "Clear pruned undo entries=%u", static_cast<unsigned>(prunedUndo));
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
      tickRelative = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
      
    } else {
      return;
    }

    // Build the new event first (needed for duplicate check)
    MidiEvent newEvt;
    bool eventAdded = false;
    switch (type) {
        case midi::NoteOn:
            newEvt = MidiEvent::NoteOn(tickRelative, channel, data1, data2);
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

    if (type == midi::NoteOff) {
      const LoopEventStore& capture = loop.capture.store;
      for (size_t i = capture.size(); i > 0; --i) {
        const MidiEvent& prior = capture.at(i - 1);
        if (!prior.isNoteOn() || prior.channel != channel ||
            prior.data.noteData.note != data1) {
          continue;
        }
        const bool wrappedHeadOff =
            isOverdubbing() && loop.loopLengthTicks > 0 && tickRelative < prior.tick &&
            pendingNotes.find({data1, channel}) != pendingNotes.end();
        if (tickRelative <= prior.tick && !wrappedHeadOff) {
          tickRelative = prior.tick + 1;
          newEvt.tick = tickRelative;
        }
        break;
      }

      if (isOverdubbing() && loop.loopLengthTicks > 0) {
        const uint32_t wrapWindow = std::min(Config::TICKS_PER_BAR, loop.loopLengthTicks);
        if (tickRelative < wrapWindow) {
          loop.removeCaptureNoteOffAt(channel, data1, loop.loopLengthTicks - 1);
        }
      }
    }

    if (loop.loopLengthTicks > 0 && tickRelative >= loop.loopLengthTicks) {
      tickRelative = loop.loopLengthTicks - 1;
      newEvt.tick = tickRelative;
    }

    if (!loop.appendCaptureEvent(newEvt)) {
      return;
    }

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
  ensurePlaybackWindowBuilt(loop, runtime);
  ::rebuildPlaybackOrder(loop, runtime.primaryWindow.mergedEvents);
}

void Track::playMidiEvents(uint32_t currentTick, bool isAudible) {
  Loop& loop = getActiveLoop();
  if (!isAudible || muted || !loop.hasPublishedEvents() || loop.loopLengthTicks == 0)
    return;
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(activeLoopIndex);
  if (runtime.cursor.isStale(loop.playbackRevision, playbackGeneration)) {
    runtime.reset(true);
    loop.nextEventIndex = 0;
    loop.captureNextEventIndex = 0;
    runtime.cursor.syncRevision(loop.playbackRevision, playbackGeneration);
  }

  ensurePlaybackWindowBuilt(loop, runtime);
  const MidiEventVec& mergedEvents = runtime.primaryWindow.mergedEvents;
  if (mergedEvents.empty()) {
    return;
  }

  if (loop.playbackOrderDirty) {
    ::rebuildPlaybackOrder(loop, mergedEvents);
    reanchorPlaybackIndex(loop, mergedEvents, loop.getPlaybackOrder());
  }

  uint32_t tickInLoop = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);

  if (tickInLoop < loop.lastTickInLoop && loop.lastTickInLoop != UINT32_MAX) {
    loop.nextEventIndex = 0;
    loop.captureNextEventIndex = 0;
    runtime.cursor.mergeCursorIndex = 0;
    if (isOverdubbing()) {
      closeOpenNotesAtLoopWrap();
    }
    logger.trace("Loop wrapped, resetting index");
  }

  uint32_t prevTickInLoop = loop.lastTickInLoop;
  loop.lastTickInLoop = tickInLoop;
  runtime.cursor.lastTickInLoop = tickInLoop;

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
    if (evt.tick >= loop.loopLengthTicks) {
      loop.nextEventIndex++;
      continue;
    }
    uint32_t evTick = evt.tick;

    bool crossed = atLoopStart ? (evTick <= tickInLoop) : (prevTickInLoop < evTick && evTick <= tickInLoop);
    if (crossed && eventInJamRegion(evTick)) {
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
      if (evt.tick >= loop.loopLengthTicks) {
        loop.captureNextEventIndex++;
        continue;
      }
      const uint32_t evTick = evt.tick;
      const bool crossed =
          atLoopStart ? (evTick <= tickInLoop) : (prevTickInLoop < evTick && evTick <= tickInLoop);
      if (crossed && eventInJamRegion(evTick)) {
        sendMidiEvent(evt);
        loop.captureNextEventIndex++;
      } else if (evTick > tickInLoop) {
        break;
      } else {
        loop.captureNextEventIndex++;
      }
    }
  }
  runtime.cursor.mergeCursorIndex = loop.nextEventIndex;
  runtime.cursor.syncRevision(loop.playbackRevision, playbackGeneration);
}

void Track::playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool isAudible) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  if (!isAudible || muted) return;

  Loop& loop = getLoop(slotIndex);
  if (!loop.hasPublishedEvents() || loop.loopLengthTicks == 0) return;
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(slotIndex);
  if (runtime.cursor.isStale(loop.playbackRevision, playbackGeneration)) {
    runtime.reset(true);
    loop.nextEventIndex = 0;
    runtime.cursor.syncRevision(loop.playbackRevision, playbackGeneration);
  }

  ensurePlaybackWindowBuilt(loop, runtime);
  const MidiEventVec& mergedEvents = runtime.primaryWindow.mergedEvents;
  if (mergedEvents.empty()) {
    return;
  }

  if (loop.playbackOrderDirty) {
    ::rebuildPlaybackOrder(loop, mergedEvents);
    reanchorPlaybackIndex(loop, mergedEvents, loop.getPlaybackOrder());
  }

  uint32_t tickInLoop = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  if (tickInLoop < loop.lastTickInLoop) {
    loop.nextEventIndex = 0;
    runtime.cursor.mergeCursorIndex = 0;
  }

  uint32_t prevTickInLoop = loop.lastTickInLoop;
  loop.lastTickInLoop = tickInLoop;
  runtime.cursor.lastTickInLoop = tickInLoop;
  bool atLoopStart = (prevTickInLoop == UINT32_MAX) || (tickInLoop <= prevTickInLoop);
  const PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();

  while (loop.nextEventIndex < playbackOrder.size()) {
    const MidiEvent &evt = mergedEvents[playbackOrder[loop.nextEventIndex]];
    if (evt.tick >= loop.loopLengthTicks) {
      loop.nextEventIndex++;
      continue;
    }
    uint32_t evTick = evt.tick;
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
  runtime.cursor.mergeCursorIndex = loop.nextEventIndex;
  runtime.cursor.syncRevision(loop.playbackRevision, playbackGeneration);
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
  jamTick = (jamTick + delta) % jamLength;
}

uint32_t Track::getJamTick() const {
  noInterrupts();
  uint32_t t = jamTick;
  interrupts();
  return t;
}

void Track::setJamTick(uint32_t tick) {
  noInterrupts();
  uint32_t newTick = (jamLength > 0) ? (tick % jamLength) : 0;
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
      recordMidiEvents(midi::NoteOff, channel, note, 0, tick);  // Use velocity 0 to mark end
      pendingNotes.erase(it);
    } else {
      logger.log(CAT_MIDI, LOG_WARNING,
                 "NoteOff for note %d on ch %d with no matching NoteOn",
                 note, channel);
    }
  }
}


