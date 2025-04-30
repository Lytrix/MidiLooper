#include "Globals.h"
#include "Track.h"
#include "MidiHandler.h"

// Track track; Don't initiate it, else you will create a duplicate which lease to empty arrays

Track::Track()
  : trackState(TRACK_STOPPED),
    startLoopTick(0),
    loopLengthTicks(0),
    nextEventIndex(0) {}

const std::vector<MidiEvent>& Track::getEvents() const {
  return events;
}

const std::vector<NoteEvent>& Track::getNoteEvents() const {
  return noteEvents;
}

uint32_t Track::getStartLoopTick() const {
  return startLoopTick;
}

void Track::startRecording(uint32_t currentTick) {
  events.clear();
  startLoopTick = currentTick;
  trackState = TRACK_RECORDING;
  Serial.println("Recording started.");
}


void Track::stopRecording(uint32_t currentTick) {
  loopLengthTicks = currentTick - startLoopTick;
  trackState = TRACK_STOPPED_RECORDING;
  nextEventIndex = 0;
  Serial.println("Recording stopped.");
}

// void Track::stopRecording() {
//   if (!events.empty()) {
//     loopLengthTicks = events.back().tick + 1;
//   }
//   trackState = TRACK_STOPPED_RECORDING;  // ✅ ready for overdub instead
//   Serial.println("Recording stopped, ready to overdub.");
// }

void Track::startPlaying() {
  if (loopLengthTicks > 0) {
    trackState = TRACK_PLAYING;
  }
}

void Track::startOverdubbing(uint32_t currentTick) {
  trackState = TRACK_OVERDUBBING;
  
  // 🛠 Align to current loop phase curcial when switching from stopOverdubbing
  startLoopTick = currentTick - ((currentTick - startLoopTick) % loopLengthTicks);

  Serial.println("Overdubbing started.");
  //}
}

void Track::stopOverdubbing(uint32_t currentTick) {
  trackState = TRACK_PLAYING;
  startLoopTick = currentTick;     // 🔁 Resync loop
  nextEventIndex = 0;           // ⏮️ Reset midiEvent scanning
  Serial.println("Overdubbing stopped, back to playback.");
}

void Track::stopPlaying() {
  trackState = TRACK_STOPPED;
}

void Track::togglePlayStop() {
  if (isPlaying()) {
    stopPlaying();
  } else {
    startPlaying();
  }
}

void Track::toggleMuteTrack() {
  muted = !muted;
}

bool Track::isMuted() const {
  return muted;
}

bool Track::hasData() const {
  return !events.empty();
}

size_t Track::getEventCount() const {
  return events.size();
}

size_t Track::getNoteEventCount() const {
  return noteEvents.size();
}

void Track::clear() {
  events.clear();      // Remove all recorded MIDI events
  noteEvents.clear();  // Remove any note-specific events if you have a separate list
  startLoopTick = 0;       // Reset the recording start tick
  loopLengthTicks = 0;     // Reset the track length
  // nextEventIndex = 0;      // Reset playback position
  trackState = TRACK_STOPPED;  // Set trackState to stopped (or idle, depending on your design)
  Serial.println("Track erased.");
}

void Track::recordMidiEvent(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if ((isRecording() && !isPlaying()) || isOverdubbing()) {
    uint32_t tickRelative = currentTick - startLoopTick;

    // Prevent recording duplicate events at the same tick with same parameters
    for (auto& e : events) {
      if (e.tick == tickRelative && 
          e.type == type && 
          e.channel == channel && 
          e.data1 == data1 && 
          e.data2 == data2) {
        return;  // Duplicate midiEvent, skip it
      }
    }

    if (DEBUG) {
      Serial.printf("[RECORD] midiEvent @ tick %lu (current=%lu, start=%lu, loop=%lu)\n", 
                    tickRelative, currentTick, startLoopTick, loopLengthTicks);
    }

    MidiEvent evt = { tickRelative, type, channel, data1, data2 };
    events.push_back(evt);
  }
}



void Track::playEvents(uint32_t currentTick, bool isAudible) {
  if (!isAudible || muted || events.empty() || loopLengthTicks == 0) return;

  uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

  // Transition to overdub after full loop if recording/overdubbing
  if ((isRecording() || isOverdubbing()) && (currentTick - startLoopTick >= loopLengthTicks)) {
    trackState = TRACK_OVERDUBBING;
    startLoopTick = currentTick;
    nextEventIndex = 0;
  }

  // Reset playback at start of each loop
  if ((currentTick - startLoopTick) % loopLengthTicks == 0) {
    nextEventIndex = 0;
  }

  // Now correctly match absolute midiEvent tick with relative loop tick
  while (nextEventIndex < events.size()) {
    const MidiEvent& midiEvent = events[nextEventIndex];

    // Convert absolute midiEvent tick to loop-relative position
    uint32_t eventTickInLoop = (midiEvent.tick - startLoopTick + loopLengthTicks) % loopLengthTicks;

    if (eventTickInLoop == tickInLoop) {
      sendMidiEvent(midiEvent);
      if (DEBUG) Serial.printf("[PLAY] Note %d @ tick %lu (current %lu, loop start %lu)\n", midiEvent.data1, eventTickInLoop, currentTick, startLoopTick);
      nextEventIndex++;
    } else if (eventTickInLoop > tickInLoop) {
      break;  // Stop if events are for later in the loop
    } else {
      // Catch up missed or out-of-sync events
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
    Serial.print(evt.type == midi::NoteOn ? "NoteOn" : evt.type == midi::NoteOff ? "NoteOff"
                                                                                : "Other");
    Serial.print(" note=");
    Serial.print(evt.data1);
    Serial.print(" vel=");
    Serial.println(evt.data2);
  }
  switch (evt.type) {
    case midi::NoteOn:
       if (DEBUG_MIDI) Serial.printf("Play NoteOn ch=%d note=%d vel=%d\n", evt.channel, evt.data1, evt.data2);
      midiHandler.sendNoteOn(evt.channel, evt.data1, evt.data2);
      break;
    case midi::NoteOff:
      if (DEBUG_MIDI) Serial.printf("Play NoteOff ch=%d note=%d vel=%d\n", evt.channel, evt.data1, evt.data2);
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
      if (DEBUG_MIDI) Serial.printf("[Playback] %d: Unknown MIDI type=%d data1=%d data2=%d\n", evt.tick, evt.type, evt.data1, evt.data2);
      break;
  }

  isPlayingBack = false;  // Unset after done
}

// Shortcuts to check the "trackState" variable in each track function
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

TrackState Track::getState() const {
  return trackState;
}

// -------------------------------------------------------------
// Display functions
// -------------------------------------------------------------


void Track::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered events

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    //pendingNotes[{note, 1}] = { tick, velocity };  // remember when this note started
    auto key = std::make_pair(note, channel);
    pendingNotes[key] = PendingNote{ .startNoteTick = tick,  .velocity = velocity };
    //pendingNotes[{note, channel}] = { tick, velocity };  // remember when this note started
    // Record a MIDI midiEvent correctly
    recordMidiEvent(midi::NoteOn, channel, note, velocity, tick);
    if (DEBUG_NOTES){
      Serial.print("[Record] NoteOn: ");
      Serial.print(note);
      Serial.print(" @ ");
      Serial.println(tick);
    }
  }
}

void Track::noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered events

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    auto key = std::make_pair(note, channel);
    auto it = pendingNotes.find(key);

    if (it != pendingNotes.end()) {
      // Record a MIDI midiEvent correctly
      recordMidiEvent(midi::NoteOff, channel, note, 0, tick);  // velocity 0 to mark end

      // Create a finalized NoteEvent
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
      if (DEBUG) {
        Serial.print("Pushed note: ");
        Serial.println(noteEvent.note);
        Serial.print("Total notes: ");
        Serial.println(noteEvents.size());
      }
    } else {
      // 🔁 This block was unreachable before — now it logs unmatched NoteOffs
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

// -------------------------------------------------------------