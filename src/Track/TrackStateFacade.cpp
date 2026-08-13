//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "Globals.h"
#include "Logger.h"
#include "TrackStateMachine.h"

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

  logger.logStateTransition("Track", TrackStateMachine::toString(oldState),
                            TrackStateMachine::toString(newState));
  return true;
}

void Track::forceSetState(TrackState newState) { trackState = newState; }

void Track::toggleMuteTrack() {
  muted = !muted;
  if (muted) {
    silencePlaybackPort();
  }
}

bool Track::isMuted() const {
  return muted;
}

bool Track::isEmpty() const {
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
