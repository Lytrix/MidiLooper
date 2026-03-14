//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Track.h"
#include "Logger.h"
#include "MidiHandler.h"
#include "ClockManager.h"
#include "StorageManager.h"
#include "stdint.h"
#include <unordered_map>
#include <utility>
#include <algorithm>
#include "Globals.h"
#include "TrackStateMachine.h"
#include "TrackUndo.h"
#include "LooperState.h"

// -------------------------
// Track class implementation
// -------------------------
Track::Track() :
  muted(false),
  midiChannel(1),
  trackState(TRACK_EMPTY),
  startLoopTick(0),
  loopLengthTicks(0),
  loopStartTick(0),
  jamStartTick(UINT32_MAX),
  jamLength(0),
  jamTick(0),
  jamPlaybackActive(false),
  lastTickInLoop(0),
  nextEventIndex(0),
  isPlayingBack(false),
  eventIndexValid(false) {
}

// -------------------------
// Getters
// -------------------------

uint32_t Track::getStartLoopTick() const {
  return startLoopTick;
}

uint8_t Track::getMidiChannel() const {
  return midiChannel;
}

void Track::setMidiChannel(uint8_t ch) {
  midiChannel = (ch >= 1 && ch <= 16) ? ch : 1;
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

  logger.logStateTransition("Track", TrackStateMachine::toString(oldState), TrackStateMachine::toString(newState));
  return true;
}

// Required for loading state from SD card else the state machine will corrupt the state
void Track::forceSetState(TrackState newState) { trackState = newState; }

// -------------------------
// Recording control
// -------------------------

void Track::startRecording(uint32_t currentTick) {
  if (isEmpty()) {
    TrackUndo::pushUndoSnapshot(*this);
  }
  if (!setState(TRACK_RECORDING)) {
    return;
  }
  // Clear out any old data
  midiEvents.clear();
  pendingNotes.clear();       // any hanging NoteOns
  nextEventIndex = 0;         // so playback will start from the top
  lastTickInLoop = 0;

  // Stamp the new start tick quantized to a beat.
  startLoopTick = currentTick;
  
  // OPTIMIZATION: Invalidate caches when MIDI events change
  invalidateCaches();
  
  logger.logTrackEvent("Recording started", currentTick);
}

// -------------------------
// Helpers for stopRecording 
// -------------------------

const uint32_t Track::TICKS_PER_BAR = Config::TICKS_PER_BAR;

uint32_t Track::quantizeStart(uint32_t original) const {
    return (original / TICKS_PER_BAR) * TICKS_PER_BAR;
}

void Track::shiftMidiEvents(int32_t offset) {
    for (auto &evt : midiEvents) {
        evt.tick += offset;
    }
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](auto &a, auto &b){ return a.tick < b.tick; });
    
    // OPTIMIZATION: Invalidate caches when MIDI events change
    invalidateCaches();
}

uint32_t Track::findLastEventTick() const {
    uint32_t last = 0;
    for (auto &evt : midiEvents) {
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
    nextEventIndex = 0;
    lastTickInLoop = (currentTick - startLoopTick) % loopLengthTicks;
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

void Track::validateAndCleanupMidiEvents() {
    if (midiEvents.empty()) return;
    
    // Map to track active notes: key = (note, channel), value = note-on event index
    std::unordered_map<std::pair<uint8_t, uint8_t>, size_t, PairHash> activeNotes;
    std::vector<bool> eventsToKeep(midiEvents.size(), true);
    int orphanedCount = 0;
    
    // Sort events by tick to ensure proper order
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](const MidiEvent& a, const MidiEvent& b) {
                  return a.tick < b.tick;
              });
    
    // First pass: match note-on/note-off pairs
    for (size_t i = 0; i < midiEvents.size(); i++) {
        const MidiEvent& evt = midiEvents[i];
        
        if (evt.isNoteOn()) {
            std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
            
            // Check if there's already an active note (orphaned note-on)
            if (activeNotes.find(key) != activeNotes.end()) {
                // Mark the previous orphaned note-on for removal
                size_t prevIndex = activeNotes[key];
                eventsToKeep[prevIndex] = false;
                orphanedCount++;
                logger.log(CAT_MIDI, LOG_WARNING, 
                          "Removed orphaned note-on: note %d, channel %d, tick %lu",
                          evt.data.noteData.note, evt.channel, midiEvents[prevIndex].tick);
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
        size_t index = pair.second;
        eventsToKeep[index] = false;
        orphanedCount++;
        logger.log(CAT_MIDI, LOG_WARNING, 
                  "Removed orphaned note-on: note %d, channel %d, tick %lu",
                  midiEvents[index].data.noteData.note, midiEvents[index].channel, midiEvents[index].tick);
    }
    
    // Second pass: handle loop wrapping for remaining unmatched notes
    if (loopLengthTicks > 0) {
        activeNotes.clear();
        
        // Look for note-on near end that might have note-off near beginning
        for (size_t i = 0; i < midiEvents.size(); i++) {
            if (!eventsToKeep[i]) continue;
            
            const MidiEvent& evt = midiEvents[i];
            
            if (evt.isNoteOn()) {
                std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
                activeNotes[key] = i;
                
            } else if (evt.isNoteOff()) {
                std::pair<uint8_t, uint8_t> key = {evt.data.noteData.note, evt.channel};
                auto it = activeNotes.find(key);
                
                if (it != activeNotes.end()) {
                    // Check if this could be a wrapped note
                    size_t noteOnIndex = it->second;
                    uint32_t noteOnTick = midiEvents[noteOnIndex].tick;
                    uint32_t noteOffTick = evt.tick;
                    
                    // If note-off is much earlier than note-on, it might be wrapped
                    if (noteOffTick < noteOnTick && (noteOnTick - noteOffTick) > (loopLengthTicks / 2)) {
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
    
    // Remove orphaned events
    if (orphanedCount > 0) {
        std::vector<MidiEvent> cleanedEvents;
        cleanedEvents.reserve(midiEvents.size() - orphanedCount);
        
        for (size_t i = 0; i < midiEvents.size(); i++) {
            if (eventsToKeep[i]) {
                cleanedEvents.push_back(midiEvents[i]);
            }
        }
        
        midiEvents = std::move(cleanedEvents);
        invalidateCaches();
        
        logger.log(CAT_MIDI, LOG_INFO, 
                  "MIDI validation complete: removed %d orphaned events, %d events remaining",
                  orphanedCount, (int)midiEvents.size());
    } else {
        logger.log(CAT_MIDI, LOG_INFO, 
                  "MIDI validation complete: no orphaned events found, %d events total",
                  (int)midiEvents.size());
    }
}

// -------------------------
// Stop recording
// -------------------------

void Track::stopRecording(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  // Close any held notes
  finalizePendingNotes(currentTick);

  // Validate and cleanup orphaned MIDI events
  validateAndCleanupMidiEvents();

  // Use the actual time between start and stop as the loop length
  uint32_t rawLength = currentTick - startLoopTick;
  uint32_t rem       = rawLength % TICKS_PER_BAR;
  uint32_t grace     = TICKS_PER_BAR / 2;  // Allow 1/8 bar grace window

  if (rem <= grace) {
      loopLengthTicks = (rawLength / TICKS_PER_BAR) * TICKS_PER_BAR;
  } else {
      loopLengthTicks = ((rawLength / TICKS_PER_BAR) + 1) * TICKS_PER_BAR;
  }

  // Reset playback state for next pass
  nextEventIndex = 0;
  lastTickInLoop = 0;
  startLoopTick = 0;
  
  // OPTIMIZATION: Invalidate caches when MIDI events or loop length change
  invalidateCaches();
  
  logger.logTrackEvent("Recording stopped", currentTick, "start=%lu length=%lu", startLoopTick, loopLengthTicks);
  logger.debug("Final ticks: currentTick=%lu startLoopTick=%lu rawLength=%lu", currentTick, startLoopTick, rawLength);

  startOverdubbing(currentTick);
}

void Track::stopRecordingToStopped(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) return;

  finalizePendingNotes(currentTick);
  validateAndCleanupMidiEvents();

  uint32_t rawLength = currentTick - startLoopTick;
  uint32_t rem       = rawLength % TICKS_PER_BAR;
  uint32_t grace     = TICKS_PER_BAR / 2;

  if (rem <= grace) {
      loopLengthTicks = (rawLength / TICKS_PER_BAR) * TICKS_PER_BAR;
  } else {
      loopLengthTicks = ((rawLength / TICKS_PER_BAR) + 1) * TICKS_PER_BAR;
  }

  nextEventIndex = 0;
  lastTickInLoop = 0;
  startLoopTick = 0;
  invalidateCaches();

  logger.logTrackEvent("Recording stopped (to STOPPED)", currentTick, "length=%lu", loopLengthTicks);

  TrackUndo::pushUndoSnapshot(*this);
  setState(TRACK_STOPPED);
}

// -------------------------
// Start playing
// -------------------------

void Track::startPlaying(uint32_t currentTick) {
  if (loopLengthTicks > 0) {
    if (!setState(TRACK_PLAYING)) {
      return;
    }
   // startLoopTick = 0;
    // This is the old way to start playing. playing from the absolute tick when it was origionally recorded
    //startLoopTick = currentTick - startLoopTick;
    startLoopTick = 0;
    // for support to pickup in the middle or quintized start live looping to master clock:
    //startLoopTick = currentTick - ((currentTick - startLoopTick) % loopLengthTicks);
    logger.logTrackEvent("Playback started", currentTick);
  }
}

// -------------------------
// Start overdubbing
// -------------------------

void Track::startOverdubbing(uint32_t currentTick) {
  if (!setState(TRACK_OVERDUBBING)) return;
  
  // Create undo snapshot before starting overdub
  TrackUndo::pushUndoSnapshot(*this);
  logger.info("Overdub snapshot created: events=%d, snapshots=%d", midiEvents.size(), midiHistory.size());
  
  logger.logTrackEvent("Overdubbing started", currentTick);
}


void Track::stopOverdubbing() {
  setState(TRACK_PLAYING);
  
  // Validate and cleanup orphaned MIDI events after overdubbing
  validateAndCleanupMidiEvents();
  
  logger.logTrackEvent("Overdubbing stopped", clockManager.getCurrentTick());
  logger.info("Overdub stopped: events=%d, snapshots=%d", midiEvents.size(), midiHistory.size());
  logger.dumpMidiEvents(midiEvents, -1);

  // Reset playback state
  startLoopTick = 0;
  resetPlaybackState(0);
  StorageManager::saveState(looperState.getLooperState()); // Save after overdubbing
}

void Track::stopOverdubbingToStopped() {
  if (isEmpty()) return;
  sendAllNotesOff();
  validateAndCleanupMidiEvents();
  setState(TRACK_STOPPED);
  startLoopTick = 0;
  resetPlaybackState(0);
  logger.logTrackEvent("Overdubbing stopped (to STOPPED)", clockManager.getCurrentTick());
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

bool Track::hasData() const {
  return !midiEvents.empty();
}

size_t Track::getMidiEventCount() const {
  return midiEvents.size();
}

// -------------------------
// Track Clear
// -------------------------

// Clears the current track data and returns to the EMPTY state
void Track::clear() {
    if (trackState == TRACK_EMPTY) {
        logger.debug("Track already empty; ignoring clear");
        return;
    }

    // Remove all recorded events
    midiEvents.clear();

    // Reset timing
    startLoopTick = 0;
    loopLengthTicks = 0;
    loopStartTick = 0;  // Reset loop start point

    // Clear undo history (pooled vectors will be automatically cleaned up)
    midiHistory.clear();
    midiRedoHistory.clear();
    clearMidiHistory.clear();
    clearMidiRedoHistory.clear();
    clearStateHistory.clear();
    clearStateRedoHistory.clear();
    clearLengthHistory.clear();
    clearLengthRedoHistory.clear();
    clearStartHistory.clear();
    clearStartRedoHistory.clear();
    loopStartHistory.clear();
    loopStartRedoHistory.clear();

    // Go back to "never recorded"
    setState(TRACK_EMPTY);

    // OPTIMIZATION: Invalidate caches when MIDI events change
    invalidateCaches();

    // Log the clear action
    logger.logTrackEvent("Track cleared", clockManager.getCurrentTick());
}

void Track::recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if ((isRecording() && !isPlaying()) || isOverdubbing()) {
    uint32_t tickRelative;

    // First pass: build the loop
    if (isRecording() && !isPlaying()) {
      tickRelative = currentTick - startLoopTick;

    // Overdub passes: wrap every hit back into the loop
    } else if (isOverdubbing()) {
      // defensive: loopLengthTicks was set when you stopped recording
      if (loopLengthTicks == 0) return;
      tickRelative = (currentTick - startLoopTick) % loopLengthTicks;
      
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

    // Find insertion point (events are sorted by tick)
    auto it = std::lower_bound(midiEvents.begin(), midiEvents.end(), tickRelative,
        [](const MidiEvent& e, uint32_t t) { return e.tick < t; });

    // Duplicate check: scan events within ±DUPLICATE_TICK_TOLERANCE (handles jitter on loop restart)
    // Musically, notes within 1/64th are typically unintended duplicates
    const uint32_t lo = (tickRelative > Config::DUPLICATE_TICK_TOLERANCE)
        ? (tickRelative - Config::DUPLICATE_TICK_TOLERANCE) : 0;
    const uint32_t hi = tickRelative + Config::DUPLICATE_TICK_TOLERANCE;
    auto scan = std::lower_bound(midiEvents.begin(), midiEvents.end(), lo,
        [](const MidiEvent& e, uint32_t t) { return e.tick < t; });
    while (scan != midiEvents.end() && scan->tick <= hi) {
        const auto& e = *scan;
        if (e.type == type && e.channel == channel &&
            e.data.noteData.note == data1 && e.data.noteData.velocity == data2) {
            return;  // Skip duplicate event
        }
        ++scan;
    }

    // Insert in sorted position (no full sort needed)
    midiEvents.insert(it, newEvt);

    // Log the event
    logger.logMidiEvent(newEvt);

    // OPTIMIZATION: Invalidate caches when MIDI events change
    invalidateCaches();
  }
}

void Track::rebuildPlaybackOrder() {
  playbackOrder.resize(midiEvents.size());
  for (size_t i = 0; i < midiEvents.size(); i++) {
    playbackOrder[i] = i;
  }
  uint32_t ll = loopLengthTicks;
  std::sort(playbackOrder.begin(), playbackOrder.end(),
    [&](size_t a, size_t b) {
      return (midiEvents[a].tick % ll) < (midiEvents[b].tick % ll);
    });
  playbackOrderDirty = false;
}

void Track::playMidiEvents(uint32_t currentTick, bool isAudible) {
  if (!isAudible || muted || midiEvents.empty() || loopLengthTicks == 0)
    return;

  if (playbackOrderDirty) {
    rebuildPlaybackOrder();
  }

  uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

  if (tickInLoop < lastTickInLoop) {
    nextEventIndex = 0;
    logger.debug("Loop wrapped, resetting index");
  }

  uint32_t prevTickInLoop = lastTickInLoop;
  lastTickInLoop = tickInLoop;

  while (nextEventIndex < playbackOrder.size()) {
    const MidiEvent &evt = midiEvents[playbackOrder[nextEventIndex]];
    uint32_t evTick = evt.tick % loopLengthTicks;

    if ( prevTickInLoop < evTick && evTick <= tickInLoop ) {
      sendMidiEvent(evt);
      nextEventIndex++;
    }
    else if (evTick > tickInLoop) {
      break;
    }
    else {
      nextEventIndex++;
    }
  }
}

void Track::sendMidiEvent(const MidiEvent& evt) {
  if (trackState != TRACK_PLAYING && trackState != TRACK_OVERDUBBING) return;
  isPlayingBack = true;  // Mark playback so noteOn/noteOff ignores it
  MidiEvent evtCopy = evt;
  if (evt.channel >= 1 && evt.channel <= 16) {
    evtCopy.channel = midiChannel;
  }
  // Log loop playback notes for debugging
  if (evt.isNoteOn() || evt.isNoteOff()) {
    const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (evtCopy.data.noteData.note / 12) - 1;
    const char* noteName = noteNames[evtCopy.data.noteData.note % 12];
    logger.log(CAT_MIDI, LOG_DEBUG, "Loop SEND: %s ch=%d %s%d (note %d) vel=%d tick=%lu",
               evt.isNoteOn() ? "NoteOn" : "NoteOff",
               evtCopy.channel, noteName, octave,
               evtCopy.data.noteData.note, evtCopy.data.noteData.velocity, evt.tick);
  }
  midiHandler.sendMidiEvent(evtCopy);
  isPlayingBack = false;  // Reset playback state
}

void Track::sendAllNotesOff() {
  // Control Change 123 = All Notes Off (MIDI channels are 1-16)
  for (uint8_t ch = 1; ch <= 16; ++ch) {
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

uint32_t Track::getLoopLength() const {
  return loopLengthTicks;
}

void Track::setLoopLength(uint32_t ticks) {
  loopLengthTicks = ticks;
}

// Simple loop length change - wrapping handled dynamically in logic layer
void Track::setLoopLengthWithWrapping(uint32_t newLoopLength) {
  if (newLoopLength == loopLengthTicks) {
    return; // No change needed
  }
  
  uint32_t oldLoopLength = loopLengthTicks;
  
  logger.log(CAT_TRACK, LOG_INFO, "Loop length change: %lu -> %lu ticks", oldLoopLength, newLoopLength);
  
  // Simply update the loop length - original MIDI events remain unchanged
  loopLengthTicks = newLoopLength;
  
  // Invalidate caches when loop length changes so display/playback recalculates
  invalidateCaches();
  
  logger.log(CAT_TRACK, LOG_INFO, "Loop length updated to %lu ticks (wrapping handled dynamically)", loopLengthTicks);
}

// -------------------------
// Loop start point control
// -------------------------

uint32_t Track::getLoopStartTick() const {
  return loopStartTick;
}

void Track::setLoopStartTick(uint32_t startTick) {
  if (startTick == loopStartTick) {
    return; // No change needed
  }
  
  uint32_t oldStartTick = loopStartTick;
  
  // Constrain to valid range within the loop
  if (startTick >= loopLengthTicks && loopLengthTicks > 0) {
    startTick = startTick % loopLengthTicks;
  }
  
  loopStartTick = startTick;
  
  logger.log(CAT_TRACK, LOG_INFO, "Loop start point changed: %lu -> %lu ticks", oldStartTick, loopStartTick);
  
  // Invalidate caches when loop start changes so display recalculates
  invalidateCaches();
}

void Track::setLoopStartAndEnd(uint32_t startTick, uint32_t endTick) {
  if (endTick <= startTick) {
    logger.log(CAT_TRACK, LOG_ERROR, "Invalid loop range: start=%lu >= end=%lu", startTick, endTick);
    return;
  }
  
  uint32_t newLength = endTick - startTick;
  
  logger.log(CAT_TRACK, LOG_INFO, "Setting loop start=%lu, end=%lu, length=%lu", startTick, endTick, newLength);
  
  loopStartTick = startTick;
  loopLengthTicks = newLength;
  
  // Invalidate caches when loop boundaries change
  invalidateCaches();
}

uint32_t Track::getLoopEndTick() const {
  return loopStartTick + loopLengthTicks;
}

void Track::setJam(uint32_t startTick, uint32_t length) {
  noInterrupts();
  jamStartTick = startTick;
  jamLength = length;
  jamTick = 0;
  nextEventIndex = 0;
  lastTickInLoop = UINT32_MAX;
  interrupts();
  logger.log(CAT_TRACK, LOG_INFO, "Jam set: start=%lu, length=%lu", jamStartTick, jamLength);
}

void Track::clearJam() {
  noInterrupts();
  jamStartTick = UINT32_MAX;
  jamLength = 0;
  jamPlaybackActive = false;
  jamTick = 0;
  nextEventIndex = 0;
  lastTickInLoop = UINT32_MAX;
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
  jamTick = (jamLength > 0) ? (tick % jamLength) : 0;
  nextEventIndex = 0;
  lastTickInLoop = UINT32_MAX;
  interrupts();
}

void Track::setJamPlayback(bool enabled) {
  noInterrupts();
  jamPlaybackActive = enabled;
  if (enabled) {
    nextEventIndex = 0;
    lastTickInLoop = UINT32_MAX;
  }
  interrupts();
}

uint32_t Track::getEffectivePlaybackTick(uint32_t currentTick) const {
  if (!jamPlaybackActive || jamLength == 0) return currentTick;
  uint32_t storagePos = (jamStartTick + jamTick) % loopLengthTicks;
  return startLoopTick + storagePos;
}

// Display functions
void Track::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered MIDI events

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


