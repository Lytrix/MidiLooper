#include "Globals.h"
#include "Track.h"
#include "ClockManager.h"
#include "MidiHandler.h"

// Track class implementation

Track::Track()
  : isPlayingBack(false),
    muted(false),
    trackState(TRACK_STOPPED),
    startLoopTick(0),
    loopLengthTicks(0),
    lastTickInLoop(0),
    pendingNotes(),
    midiEvents(),
    noteEvents()
 {}

const std::vector<MidiEvent>& Track::getEvents() const {
  return midiEvents;
}

const std::vector<NoteEvent>& Track::getNoteEvents() const {
  return noteEvents;
}

uint32_t Track::getStartLoopTick() const {
  return startLoopTick;
}

TrackState Track::getState() const {
  return trackState;
}

bool Track::isValidStateTransition(TrackState newState) const {
  return TrackStateMachine::isValidTransition(trackState, newState);
}

bool Track::setState(TrackState newState) {
  if (!isValidStateTransition(newState)) {
    if (DEBUG) {
      Serial.printf("Invalid state transition from %d to %d\n", trackState, newState);
    }
    return false;
  }
  return transitionState(newState);
}

bool Track::transitionState(TrackState newState) {
  if (!isValidStateTransition(newState)) {
    return false;
  }

  TrackState oldState = trackState;
  trackState = newState;

  if (DEBUG) {
    Serial.printf("Track state transition: %d -> %d\n", oldState, newState);
  }

  return true;
}

void Track::startRecording(uint32_t currentTick) {
  if (!setState(TRACK_RECORDING)) {
    return;
  }
  midiEvents.clear();
  startLoopTick = currentTick;
  Serial.println("Recording started.");
}

void Track::stopRecording(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_RECORDING)) {
    return;
  }
  loopLengthTicks = currentTick - startLoopTick;
  Serial.println("Recording stopped.");
}

void Track::startPlaying(uint32_t currentTick) {
  if (loopLengthTicks > 0) {
    if (!setState(TRACK_PLAYING)) {
      return;
    }
    startLoopTick = currentTick - ((currentTick - startLoopTick) % loopLengthTicks);
    Serial.println("Start Playing.");
  }
}

void Track::startOverdubbing(uint32_t currentTick) {
  if (!setState(TRACK_OVERDUBBING)) {
    return;
  }
  Serial.println("Overdubbing started.");
}

void Track::stopOverdubbing(uint32_t currentTick) {
  if (!setState(TRACK_STOPPED_OVERDUBBING)) {
    return;
  }
  Serial.println("Overdubbing stopped, back to playback.");
}

void Track::stopPlaying() {
  setState(TRACK_STOPPED);
}

void Track::togglePlayStop() {
  isPlaying() ? stopPlaying() : startPlaying(clockManager.getCurrentTick());
}

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

size_t Track::getNoteEventCount() const {
  return noteEvents.size();
}

void Track::clear() {
  midiEvents.clear();
  noteEvents.clear();
  startLoopTick = 0;
  loopLengthTicks = 0;
  trackState = TRACK_STOPPED;
  Serial.println("Track erased.");
}

void Track::recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if ((isRecording() && !isPlaying()) || isOverdubbing()) {
    uint32_t tickRelative = currentTick - startLoopTick;

    // Prevent duplicate midiEvents at the same tick with same parameters
    for (const auto& e : midiEvents) {
      if (e.tick == tickRelative && e.type == type && e.channel == channel && e.data1 == data1 && e.data2 == data2) {
        return;  // Skip duplicate event
      }
    }

    if (DEBUG) {
      Serial.printf("[RECORD] midiEvent @ tick %lu (current=%lu, start=%lu, loop=%lu)\n", 
                    tickRelative, currentTick, startLoopTick, loopLengthTicks);
    }

    midiEvents.push_back(MidiEvent{ tickRelative, type, channel, data1, data2 });
  }
}

void Track::playMidiEvents(uint32_t currentTick, bool isAudible) {
  if (!isAudible || muted || midiEvents.empty() || loopLengthTicks == 0) return;

  uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;
          
  // Only allow state transition if we're in a valid state
  if ((isRecording() || isOverdubbing()) && (currentTick - startLoopTick >= loopLengthTicks)) {
    if (isRecording()) {
      setState(TRACK_STOPPED_RECORDING);
    } else if (isOverdubbing()) {
      setState(TRACK_STOPPED_OVERDUBBING);
    }
  }

  // Reset playback at start of each loop
  // if ((currentTick - startLoopTick) % loopLengthTicks == 0) {
  //   nextEventIndex = 0;
  // }

    if ((lastTickInLoop > tickInLoop)) {
      nextEventIndex = 0;
    }
    lastTickInLoop = tickInLoop;


  // Match absolute midiEvent tick with relative loop tick
  while (nextEventIndex < midiEvents.size()) {
    const MidiEvent& midiEvent = midiEvents[nextEventIndex];
    //uint32_t eventTickInLoop = (midiEvent.tick - startLoopTick + loopLengthTicks) % loopLengthTicks;
    uint32_t eventTickInLoop = midiEvent.tick % loopLengthTicks;
    
    if (eventTickInLoop == tickInLoop) {
      sendMidiEvent(midiEvent);
      nextEventIndex++;
    } else if (eventTickInLoop > tickInLoop) {
      break;  // Stop if midiEvents are for later in the loop
    } else {
      // Catch up missed or out-of-sync midiEvents
      nextEventIndex++;
    }
  }
}

void Track::sendMidiEvent(const MidiEvent& evt) {
  if (trackState != TRACK_PLAYING && trackState != TRACK_OVERDUBBING) return;

  isPlayingBack = true;  // Mark playback so noteOn/noteOff ignores it

  if (DEBUG_MIDI) {
    Serial.print("[Playback] ");
    Serial.print(evt.tick);
    Serial.print(": ");
    Serial.print(evt.type == midi::NoteOn ? "NoteOn" : evt.type == midi::NoteOff ? "NoteOff" : "Other");
    Serial.print(" note=");
    Serial.print(evt.data1);
    Serial.print(" vel=");
    Serial.println(evt.data2);
  }

  // Send appropriate MIDI event
  switch (evt.type) {
    case midi::NoteOn:
      midiHandler.sendNoteOn(evt.channel, evt.data1, evt.data2);
      break;
    case midi::NoteOff:
      midiHandler.sendNoteOff(evt.channel, evt.data1, evt.data2);
      break;
    case midi::ControlChange:
      midiHandler.sendControlChange(evt.channel, evt.data1, evt.data2);
      break;
    case midi::PitchBend:
      midiHandler.sendPitchBend(evt.channel, (evt.data2 << 7) | evt.data1);
      break;
    case midi::AfterTouchChannel:
      midiHandler.sendAfterTouch(evt.channel, evt.data1);
      break;
    default:
      if (DEBUG_MIDI) {
        Serial.printf("[Playback] %d: Unknown MIDI type=%d data1=%d data2=%d\n", evt.tick, evt.type, evt.data1, evt.data2);
      }
      break;
  }

  isPlayingBack = false;  // Reset playback state
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

bool Track::isStoppedOverdubbing() const {
  return trackState == TRACK_STOPPED_OVERDUBBING;
}

bool Track::isPlaying() const {
  return trackState == TRACK_PLAYING;
}

uint32_t Track::getLength() const {
  return loopLengthTicks;
}

void Track::setLength(uint32_t ticks) {
  loopLengthTicks = ticks;
}

// Display functions
void Track::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered midiEvents

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    pendingNotes[{note, channel}] = PendingNote{tick, velocity};
    recordMidiEvents(midi::NoteOn, channel, note, velocity, tick);
    if (DEBUG_NOTES) {
      Serial.print("[Record] NoteOn: ");
      Serial.print(note);
      Serial.print(" @ ");
      Serial.println(tick);
    }
  }
}

void Track::noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered midiEvents

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    auto it = pendingNotes.find({note, channel});
    if (it != pendingNotes.end()) {
      recordMidiEvents(midi::NoteOff, channel, note, 0, tick);  // velocity 0 to mark end

      NoteEvent noteEvent;
      noteEvent.note = note;
      noteEvent.velocity = it->second.velocity;
      noteEvent.startNoteTick = it->second.startNoteTick;
      noteEvent.endNoteTick = tick;

      noteEvents.push_back(noteEvent);
      pendingNotes.erase(it);

      if (DEBUG_MIDI) {
        Serial.print("[Record] NoteOff: ");
        Serial.print(note);
        Serial.print(" @ ");
        Serial.println(tick);
      }
      if (DEBUG_NOTES) {
        Serial.print("Start: ");
        Serial.print(noteEvent.startNoteTick);
        Serial.print(", End: ");
        Serial.println(noteEvent.endNoteTick);
      }
    } else {
      // Log unmatched NoteOffs
      Serial.printf("[Warning] NoteOff for note %d on ch %d with no matching NoteOn\n", note, channel);
    }
  }
}

void Track::printNoteEvents() const {
  Serial.println("---- NoteEvents ----");
  for (const auto& note : noteEvents) {
    Serial.print("Note: ");
    Serial.print(note.note);
    Serial.print("  Velocity: ");
    Serial.print(note.velocity);
    Serial.print("  StartTick: ");
    Serial.print(note.startNoteTick);
    Serial.print("  EndTick: ");
    Serial.println(note.endNoteTick);
  }
  Serial.println("---------------------");
}
