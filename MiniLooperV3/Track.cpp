#include "Globals.h"
#include "Track.h"
#include "MidiHandler.h"

Track track;

Track::Track()
  : state(TRACK_STOPPED),
    startLoopTick(0),
    loopLengthTicks(0),
    nextEventIndex(0) {}

void Track::startRecording(uint32_t currentTick) {
  events.clear();
  startLoopTick = currentTick;
  state = TRACK_RECORDING;
  Serial.println("Recording started.");
}


void Track::stopRecording(uint32_t currentTick) {
  loopLengthTicks = currentTick - startLoopTick;
  state = TRACK_STOPPED_RECORDING;
  nextEventIndex = 0;
  Serial.println("Recording stopped.");
}

// void Track::stopRecording() {
//   if (!events.empty()) {
//     loopLengthTicks = events.back().tick + 1;
//   }
//   state = TRACK_STOPPED_RECORDING;  // ✅ ready for overdub instead
//   Serial.println("Recording stopped, ready to overdub.");
// }

void Track::startPlaying() {
  if (loopLengthTicks > 0) {
    state = TRACK_PLAYING;
  }
}

void Track::startOverdubbing() {
  state = TRACK_OVERDUBBING;
  
  // 🛠 Align to current loop phase curcial when switching from stopOverdubbing
  startLoopTick = currentTick - ((currentTick - startLoopTick) % loopLengthTicks);

  Serial.println("Overdubbing started.");
  //}
}

void Track::stopOverdubbing(uint32_t currentTick) {
  state = TRACK_PLAYING;
  startLoopTick = currentTick;     // 🔁 Resync loop
  nextEventIndex = 0;           // ⏮️ Reset event scanning
  Serial.println("Overdubbing stopped, back to playback.");
}

void Track::stopPlaying() {
  state = TRACK_STOPPED;
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

void Track::clear() {
  events.clear();      // Remove all recorded MIDI events
  noteEvents.clear();  // Remove any note-specific events if you have a separate list
  startLoopTick = 0;       // Reset the recording start tick
  loopLengthTicks = 0;     // Reset the track length
  // nextEventIndex = 0;      // Reset playback position
  state = TRACK_STOPPED;  // Set state to stopped (or idle, depending on your design)
  Serial.println("Track erased.");
}

void Track::recordMidiEvent(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if ((Track::isRecording() && !Track::isPlaying()) || Track::isOverdubbing()) {

    // for recording and overdub recalculate where the ticks relative to loopLengthTicks
    uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks; 
    
    // Prevent recording duplicate events at the same tick with same parameters
    for (auto& e : events) {
      if (e.tick == tickInLoop && 
          e.type == type && 
          e.channel == channel && 
          e.data1 == data1 && 
          e.data2 == data2) {
        return;  // Duplicate event, skip it
      }
    }
    Serial.printf("Recording event at tick %lu (currentTick=%lu, startLoopTick=%lu, loopLengthTicks=%lu)\n", tickInLoop, currentTick, startLoopTick, loopLengthTicks);
    
    MidiEvent evt = { (currentTick - startLoopTick), type, channel, data1, data2 };
    events.push_back(evt);
    
  }
}



void Track::playEvents(uint32_t currentTick, bool isAudible) {
  if (!isAudible) return;  // global setting if playing is stopped
  if (muted) return;       // local mute flag stored for each track

  if (events.empty() || loopLengthTicks == 0) return;

  // 🔁 Get tick relative to the start of the loop
  uint32_t tickInLoop = (currentTick - startLoopTick) % loopLengthTicks;

  // 🎬 Transition from RECORDING or OVERDUBBING to OVERDUBBING after 1 full loop
  if ((isRecording() || isOverdubbing()) && (currentTick - startLoopTick >= loopLengthTicks)) {
    // tickInLoop == 0 && (
    state = TRACK_OVERDUBBING;
    startLoopTick = currentTick;
    nextEventIndex = 0;
  }

  // ▶️ Play all events scheduled up to the current tick
  while (nextEventIndex < events.size() && events[nextEventIndex].tick <= tickInLoop) {
    const MidiEvent& evt = events[nextEventIndex];
    sendMidiEvent(evt);
    nextEventIndex++;
  }

  // 🔄 At loop start, reset playback index to start scanning from beginning
  if (tickInLoop == 0) {
    nextEventIndex = 0;
  }
}

void Track::sendMidiEvent(const MidiEvent& evt) {
  if (state != TRACK_PLAYING && state != TRACK_OVERDUBBING) return;

  isPlayingBack = true;  // Mark playback so noteOn/noteOff ignores it


  Serial.print("[Playback] ");
  Serial.print(evt.tick);
  Serial.print(": ");
  Serial.print(evt.type == midi::NoteOn ? "NoteOn" : evt.type == midi::NoteOff ? "NoteOff"
                                                                               : "Other");
  Serial.print(" note=");
  Serial.print(evt.data1);
  Serial.print(" vel=");
  Serial.println(evt.data2);

  switch (evt.type) {
    case midi::NoteOn:
      Serial.printf("Play NoteOn ch=%d note=%d vel=%d\n", evt.channel, evt.data1, evt.data2);
      midiHandler.sendNoteOn(evt.channel, evt.data1, evt.data2);
      break;
    case midi::NoteOff:
      Serial.printf("Play NoteOff ch=%d note=%d vel=%d\n", evt.channel, evt.data1, evt.data2);
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
      Serial.printf("[Playback] %d: Unknown MIDI type=%d data1=%d data2=%d\n", evt.tick, evt.type, evt.data1, evt.data2);
      break;
  }

  isPlayingBack = false;  // Unset after done
}

// Shortcuts to check the "state" variable in each track function
bool Track::isStopped() const {
  return state == TRACK_STOPPED;
}

bool Track::isArmed() const {
  return state == TRACK_ARMED;
}

bool Track::isRecording() const {
  return state == TRACK_RECORDING;
}
bool Track::isStoppedRecording() const {
  return state == TRACK_STOPPED_RECORDING;
}

bool Track::isOverdubbing() const {
  return state == TRACK_OVERDUBBING;
}

bool Track::isStoppedOverdubbing() const {
  return state == TRACK_STOPPED_OVERDUBBING;
}

bool Track::isPlaying() const {
  return state == TRACK_PLAYING;
}

uint32_t Track::getLength() const {
  return loopLengthTicks;
}
void Track::setLength(uint32_t ticks) {
  loopLengthTicks = ticks;
}

TrackState Track::getState() const {
  return state;
}

// -------------------------------------------------------------
// Display functions
// -------------------------------------------------------------

const std::vector<MidiEvent>& Track::getEvents() const {
  return events;
}

const std::vector<NoteEvent>& Track::getNoteEvents() const {
  return noteEvents;
}

void Track::noteOn(uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered events

  if (state == TRACK_RECORDING || state == TRACK_OVERDUBBING) {
    pendingNotes[note] = { tick, velocity };  // remember when this note started
    // Record a MIDI event correctly
    recordMidiEvent(midi::NoteOn, 1, note, velocity, tick);

    Serial.print("[Record] NoteOn: ");
    Serial.print(note);
    Serial.print(" @ ");
    Serial.println(tick);
  }
}

void Track::noteOff(uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered events
  if (state == TRACK_RECORDING || state == TRACK_OVERDUBBING) {
      auto it = pendingNotes.find(note);
      if (it != pendingNotes.end()) {

        // Record a MIDI event correctly
        recordMidiEvent(midi::NoteOff, 1, note, 0, tick);  // velocity 0 fin

        // Create a finalized NoteEvent
        NoteEvent noteEvent;
        noteEvent.note = note;
        noteEvent.velocity = it->second.velocity;
        noteEvent.startLoopTick = it->second.startLoopTick;
        noteEvent.endTick = tick;

        noteEvents.push_back(noteEvent);
        pendingNotes.erase(it);

        Serial.print("[Record] NoteOff: ");
        Serial.print(note);
        Serial.print(" @ ");
        Serial.println(tick);
    }
  } 
  // else {
  //   Serial.printf("WARNING: NoteOff for note %d with no matching NoteOn!\n", note);
  // }
}
// -------------------------------------------------------------