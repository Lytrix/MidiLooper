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
#include "Utils/SessionCapture.h"

namespace {

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

/// After a mid-pass playback-order rebuild (e.g. overdub appended an event and dirtied the order),
/// point nextEventIndex at the current playhead so events already played this pass are not re-sent.
/// Without this, resetting to loop start replays every event from 0..playhead on each edit, which
/// stalls the UI proportionally to playhead position and audibly retriggers notes until the wrap.
void reanchorPlaybackIndex(Loop& loop) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.nextEventIndex = 0;
    return;
  }
  const PlaybackOrderVec& order = loop.getPlaybackOrder();
  size_t idx = 0;
  while (idx < order.size()) {
    const MidiEvent& e = loop.midiEvents[order[idx]];
    if (e.tick >= loop.loopLengthTicks || e.tick > loop.lastTickInLoop) break;
    ++idx;
  }
  loop.nextEventIndex = static_cast<uint16_t>(idx);
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
  loops(nullptr) {
}

Track::~Track() {
  delete[] loops;
  loops = nullptr;
}

void Track::ensureLoopsAllocated() {
  if (loops) return;
  loops = new (std::nothrow) Loop[Config::MAX_LOOPS_PER_TRACK];
  if (!loops) {
    while (1) { delay(1); }  // Out of heap - should not happen
  }
}

Loop& Track::getLoop(uint8_t index) {
  const_cast<Track*>(this)->ensureLoopsAllocated();
  return loops[index < Config::MAX_LOOPS_PER_TRACK ? index : 0];
}

const Loop& Track::getLoop(uint8_t index) const {
  const_cast<Track*>(this)->ensureLoopsAllocated();
  return loops[index < Config::MAX_LOOPS_PER_TRACK ? index : 0];
}

bool Track::hasDataInSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  const_cast<Track*>(this)->ensureLoopsAllocated();
  return loops[slotIndex].hasData();
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
    TrackUndo::pushUndoSnapshot(*this);
  }
  auto preRoll = std::move(armedPreRollNotes);
  if (!setState(TRACK_RECORDING)) {
    armedPreRollNotes = std::move(preRoll);
    return;
  }
  // Clear out any old data in active slot
  loop.midiEvents.clear();
  pendingNotes.clear();       // any hanging NoteOns
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
    for (auto &evt : loop.midiEvents) {
        evt.tick += offset;
    }
    std::sort(loop.midiEvents.begin(), loop.midiEvents.end(),
              [](auto &a, auto &b){ return a.tick < b.tick; });
    invalidateCaches();
}

uint32_t Track::findLastEventTick() const {
    const Loop& loop = getActiveLoop();
    uint32_t last = 0;
    for (auto &evt : loop.midiEvents) {
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
    if (loop.midiEvents.empty()) return;
    
    // Map to track active notes: key = (note, channel), value = note-on event index
    std::unordered_map<std::pair<uint8_t, uint8_t>, size_t, PairHash> activeNotes;
    std::vector<bool> eventsToKeep(loop.midiEvents.size(), true);
    std::vector<MidiEvent> syntheticNoteOffs;
    int orphanedCount = 0;
    
    // Sort events by tick to ensure proper order
    std::sort(loop.midiEvents.begin(), loop.midiEvents.end(),
              [](const MidiEvent& a, const MidiEvent& b) {
                  if (a.tick != b.tick) return a.tick < b.tick;
                  // On equal tick, process note-offs before note-ons to avoid false overlap.
                  const int aOrder = a.isNoteOff() ? 0 : (a.isNoteOn() ? 1 : 2);
                  const int bOrder = b.isNoteOff() ? 0 : (b.isNoteOn() ? 1 : 2);
                  return aOrder < bOrder;
              });
    
    // First pass: match note-on/note-off pairs
    for (size_t i = 0; i < loop.midiEvents.size(); i++) {
        const MidiEvent& evt = loop.midiEvents[i];
        
        if (evt.isNoteOn()) {
            std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
            
            // Check if there's already an active note (orphaned note-on)
            if (activeNotes.find(key) != activeNotes.end()) {
                size_t prevIndex = activeNotes[key];
                eventsToKeep[prevIndex] = false;
                orphanedCount++;
                logger.log(CAT_MIDI, LOG_WARNING,
                          "Removed orphaned note-on: note %d, channel %d, tick %lu",
                          evt.data.noteData.note, evt.channel, loop.midiEvents[prevIndex].tick);
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
                // Orphaned note-off, mark for removal
                eventsToKeep[i] = false;
                orphanedCount++;
                logger.log(CAT_MIDI, LOG_WARNING, 
                          "Removed orphaned note-off: note %d, channel %d, tick %lu",
                          evt.data.noteData.note, evt.channel, evt.tick);
            }
        }
    }
    
    // Check for remaining active notes (note-on without note-off)
    for (const auto& pair : activeNotes) {
        const auto key = pair.first;
        size_t index = pair.second;
        const MidiEvent& noteOn = loop.midiEvents[index];
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
        for (size_t i = 0; i < loop.midiEvents.size(); i++) {
            if (!eventsToKeep[i]) continue;
            
            const MidiEvent& evt = loop.midiEvents[i];
            
            if (evt.isNoteOn()) {
                std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
                activeNotes[key] = i;
                
            } else if (evt.isNoteOff()) {
                std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
                auto it = activeNotes.find(key);
                
                if (it != activeNotes.end()) {
                    // Check if this could be a wrapped note
                    size_t noteOnIndex = it->second;
                    uint32_t noteOnTick = loop.midiEvents[noteOnIndex].tick;
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
        cleanedEvents.reserve(loop.midiEvents.size() - orphanedCount + syntheticNoteOffs.size());
        
        for (size_t i = 0; i < loop.midiEvents.size(); i++) {
            if (eventsToKeep[i]) {
                cleanedEvents.push_back(loop.midiEvents[i]);
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
        
        loop.midiEvents = std::move(cleanedEvents);
        invalidateCaches();
        
        logger.log(CAT_MIDI, LOG_INFO, 
                  "MIDI validation complete: removed %d orphaned events, inserted %d synthetic note-offs, %d events remaining",
                  orphanedCount, (int)syntheticNoteOffs.size(), (int)loop.midiEvents.size());
    } else {
        logger.log(CAT_MIDI, LOG_INFO, 
                  "MIDI validation complete: no orphaned events found, %d events total",
                  (int)loop.midiEvents.size());
    }
}

// -------------------------
// Stop recording
// -------------------------

void Track::stopRecording(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  [[maybe_unused]] const bool captureAlignFlag = alignLoopOriginOnNextStop;
  Loop& loop = getActiveLoop();
  finalizePendingNotes(currentTick);
  validateAndCleanupMidiEvents();

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
    if (delta != 0 && !loop.midiEvents.empty()) {
      int64_t minT = std::numeric_limits<int64_t>::max();
      for (const auto& evt : loop.midiEvents) {
        int64_t t = (int64_t)evt.tick + delta;
        if (t < minT) minT = t;
      }
      int64_t bump = (minT < 0) ? -minT : 0;
      for (auto& evt : loop.midiEvents) {
        int64_t t = (int64_t)evt.tick + delta + bump;
        evt.tick = (uint32_t)t;
      }
      invalidateCaches();
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

  invalidateCaches();
  SC_REC_STOP("stop", activeLoopIndex, playbackTick, recordStartTick, rawLength, finalLength, captureAlignFlag);
  logger.logTrackEvent("Recording stopped", playbackTick, "recStart=%lu length=%lu",
                       static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(finalLength));
  logger.debug("Final ticks: playbackTick=%lu recStart=%lu rawLength=%lu length=%lu", playbackTick,
               static_cast<unsigned long>(recordStartTick), static_cast<unsigned long>(rawLength),
               static_cast<unsigned long>(finalLength));

  // Return to playback after record-stop. Overdub starts on the next explicit
  // record press from PLAYING (record -> play -> overdub -> play flow).
  startPlaying(playbackTick, true);
}

void Track::stopRecordingToStopped(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  alignLoopOriginOnNextStop = false;
  Loop& loop = getActiveLoop();
  finalizePendingNotes(currentTick);
  validateAndCleanupMidiEvents();

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
  invalidateCaches();

  SC_REC_STOP("stopToStopped", activeLoopIndex, playbackTick, recordStartTickStopped,
              rawLength, loop.loopLengthTicks, false);
  logger.logTrackEvent("Recording stopped (to STOPPED)", playbackTick, "length=%lu",
                       static_cast<unsigned long>(loop.loopLengthTicks));

  TrackUndo::pushUndoSnapshot(*this);
  setState(TRACK_STOPPED);
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
  const Loop& active = getActiveLoop();
  if (trackState == TRACK_EMPTY && active.loopLengthTicks > 0) {
    forceSetState(TRACK_STOPPED);
  }
  if (!setState(TRACK_OVERDUBBING)) return;
  TrackUndo::pushUndoSnapshot(*this);
  logger.info("Overdub snapshot created: events=%d, snapshots=%d", getActiveLoop().midiEvents.size(), getActiveLoop().midiHistorySize());
  logger.logTrackEvent("Overdubbing started", currentTick);
}


void Track::stopOverdubbing() {
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  }
  setState(TRACK_PLAYING);
  validateAndCleanupMidiEvents(closeTick);
  logger.logTrackEvent("Overdubbing stopped", currentTick);
  logger.info("Overdub stopped: events=%d, snapshots=%d", getActiveLoop().midiEvents.size(), getActiveLoop().midiHistorySize());
  logger.dumpMidiEvents(getActiveLoop().midiEvents, -1);

  // Preserve phase origin from record-stop rewind so playback cursor and event phase stay aligned.
  resetPlaybackState(currentTick);
  StorageManager::saveState(looperState.getLooperState());
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
  validateAndCleanupMidiEvents(closeTick);
  setState(TRACK_STOPPED);
  resetPlaybackState(currentTick);
  logger.logTrackEvent("Overdubbing stopped (to STOPPED)", currentTick);
  StorageManager::saveState(looperState.getLooperState());
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
    loop.midiEvents.clear();
    loop.startLoopTick = 0;
    loop.loopLengthTicks = 0;
    loop.loopStartTick = 0;

    loop.clearOverdubAndLoopEditUndoStacks();

    setState(TRACK_EMPTY);
    alignLoopOriginOnNextStop = false;
    invalidateCaches();
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

    auto it = std::lower_bound(loop.midiEvents.begin(), loop.midiEvents.end(), tickRelative,
        [](const MidiEvent& e, uint32_t t) { return e.tick < t; });

    const uint32_t lo = (tickRelative > Config::DUPLICATE_TICK_TOLERANCE)
        ? (tickRelative - Config::DUPLICATE_TICK_TOLERANCE) : 0;
    const uint32_t hi = tickRelative + Config::DUPLICATE_TICK_TOLERANCE;
    auto scan = std::lower_bound(loop.midiEvents.begin(), loop.midiEvents.end(), lo,
        [](const MidiEvent& e, uint32_t t) { return e.tick < t; });
    while (scan != loop.midiEvents.end() && scan->tick <= hi) {
        const auto& e = *scan;
        if (e.type == type && e.channel == channel &&
            e.data.noteData.note == data1 && e.data.noteData.velocity == data2) {
            return;  // Skip duplicate event
        }
        ++scan;
    }

    loop.midiEvents.insert(it, newEvt);

    // Log the event
    logger.logMidiEvent(newEvt);

    // OPTIMIZATION: Invalidate caches when MIDI events change
    invalidateCaches();
  }
}

void Track::rebuildPlaybackOrder() {
  Loop& loop = getActiveLoop();
  PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();
  playbackOrder.resize(loop.midiEvents.size());
  for (size_t i = 0; i < loop.midiEvents.size(); i++) {
    playbackOrder[i] = i;
  }
  uint32_t ll = loop.loopLengthTicks;
  std::sort(playbackOrder.begin(), playbackOrder.end(),
    [&](size_t a, size_t b) {
      return playbackSortPhase(loop.midiEvents[a], ll) < playbackSortPhase(loop.midiEvents[b], ll);
    });
  loop.playbackOrderDirty = false;
}

void Track::playMidiEvents(uint32_t currentTick, bool isAudible) {
  Loop& loop = getActiveLoop();
  if (!isAudible || muted || loop.midiEvents.empty() || loop.loopLengthTicks == 0)
    return;

  if (loop.playbackOrderDirty) {
    // Rebuild the sort order (event indices changed), but keep the current pass position: re-anchor
    // nextEventIndex to the playhead instead of resetting to loop start. Resetting made atLoopStart
    // re-send every event from 0..playhead on each overdub edit, growing the UI stall with playhead
    // position and clearing only at the next wrap.
    rebuildPlaybackOrder();
    reanchorPlaybackIndex(loop);
  }

  uint32_t tickInLoop = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);

  if (tickInLoop < loop.lastTickInLoop) {
    loop.nextEventIndex = 0;
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
    const MidiEvent &evt = loop.midiEvents[playbackOrder[loop.nextEventIndex]];
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
}

void Track::playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool isAudible) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  if (!isAudible || muted) return;

  Loop& loop = getLoop(slotIndex);
  if (loop.midiEvents.empty() || loop.loopLengthTicks == 0) return;

  if (loop.playbackOrderDirty) {
    PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();
    playbackOrder.resize(loop.midiEvents.size());
    for (size_t i = 0; i < loop.midiEvents.size(); i++) {
      playbackOrder[i] = i;
    }
    uint32_t ll = loop.loopLengthTicks;
    std::sort(playbackOrder.begin(), playbackOrder.end(),
      [&](size_t a, size_t b) {
        return playbackSortPhase(loop.midiEvents[a], ll) < playbackSortPhase(loop.midiEvents[b], ll);
      });
    loop.playbackOrderDirty = false;
    // Keep pass position after reorder so already-played events are not re-sent (see playMidiEvents).
    reanchorPlaybackIndex(loop);
  }

  uint32_t tickInLoop = tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  if (tickInLoop < loop.lastTickInLoop) {
    loop.nextEventIndex = 0;
  }

  uint32_t prevTickInLoop = loop.lastTickInLoop;
  loop.lastTickInLoop = tickInLoop;
  bool atLoopStart = (prevTickInLoop == UINT32_MAX) || (tickInLoop <= prevTickInLoop);
  const PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();

  while (loop.nextEventIndex < playbackOrder.size()) {
    const MidiEvent &evt = loop.midiEvents[playbackOrder[loop.nextEventIndex]];
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
  invalidateCaches();
}

void Track::setLoopLengthWithWrapping(uint32_t newLoopLength) {
  Loop& loop = getActiveLoop();
  if (newLoopLength == loop.loopLengthTicks) return;

  uint32_t oldLoopLength = loop.loopLengthTicks;
  logger.log(CAT_TRACK, LOG_INFO, "Loop length change: %lu -> %lu ticks", oldLoopLength, newLoopLength);
  loop.loopLengthTicks = newLoopLength;
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


