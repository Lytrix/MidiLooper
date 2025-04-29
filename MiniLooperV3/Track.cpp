#include "Globals.h"
#include "Track.h"
#include "MidiHandler.h"

Track track;


Track::Track()
  : state(TRACK_STOPPED),
    startTick(0),
    lengthTicks(0),
    playbackIndex(0) {}

void Track::startRecording(uint32_t currentTick) {
  events.clear();
  startTick = currentTick;
  state = TRACK_RECORDING;
  Serial.println("Recording started.");
}


void Track::stopRecording(uint32_t currentTick) {
  lengthTicks = currentTick - startTick;
  state = TRACK_STOPPED_RECORDING;
  playbackIndex = 0;
  Serial.println("Recording stopped.");
}

// void Track::stopRecording() {
//   if (!events.empty()) {
//     lengthTicks = events.back().tick + 1;
//   }
//   state = TRACK_STOPPED_RECORDING;  // ✅ ready for overdub instead
//   Serial.println("Recording stopped, ready to overdub.");
// }

void Track::startPlaying() {
  if (lengthTicks > 0) {
    state = TRACK_PLAYING;
  }
}

void Track::startOverdubbing() {
  state = TRACK_OVERDUBBING;
  Serial.println("Overdubbing started.");
  //}
}

void Track::stopOverdubbing() {
  //if (state == TRACK_OVERDUBBING) {
  state = TRACK_PLAYING;
  Serial.println("Overdubbing stopped, back to playback.");
  //}
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
  events.clear();         // Remove all recorded MIDI events
  noteEvents.clear();     // Remove any note-specific events if you have a separate list
  startTick = 0;          // Reset the recording start tick
  lengthTicks = 0;        // Reset the track length
  // playbackIndex = 0;      // Reset playback position
  state = TRACK_STOPPED;  // Set state to stopped (or idle, depending on your design)
  Serial.println("Track erased.");
}

void Track::recordMidiEvent(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if ((Track::isRecording() && !Track::isPlaying()) || Track::isOverdubbing()) {
    // Prevent recording duplicate events at the same tick with same parameters
    for (auto& e : events) {
      if (e.tick == (currentTick - startTick) &&
          e.type == type &&
          e.channel == channel &&
          e.data1 == data1 &&
          e.data2 == data2) {
        return;  // Duplicate event, skip it
      }
    }

    MidiEvent evt = { (currentTick - startTick), type, channel, data1, data2 };
    events.push_back(evt);
  }
}



void Track::playEvents(uint32_t currentTick, bool isAudible) {
  if (!isAudible) return;  // global setting if playing is stopped
  if (muted) return;       // local mute flag stored for each track

  if (events.empty() || lengthTicks == 0) return;

  uint32_t tickInLoop = (currentTick - startTick) % lengthTicks;

  // ✅ Reset to PLAYING when loop completes during recording
  if ((isRecording() || isOverdubbing()) && (currentTick - startTick >= lengthTicks)) {
      // tickInLoop == 0 && (
      state = TRACK_OVERDUBBING;
      startTick = currentTick;
      playbackIndex = 0;
  }

  // if (tickInLoop == 0 && currentTick - startTick >= lengthTicks) {
  //   playbackIndex = 0;
  //   startTick = currentTick;  // Reset loop time base
  // }

  // Play all events that match current tick
  while (playbackIndex < events.size() && events[playbackIndex].tick <= tickInLoop) {
    const MidiEvent& evt = events[playbackIndex];
    sendMidiEvent(evt);
    playbackIndex++;
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
  return lengthTicks;
}
void Track::setLength(uint32_t ticks) {
  lengthTicks = ticks;
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
    Serial.print("[Record] NoteOn: ");
    Serial.print(note);
    Serial.print(" @ ");
    Serial.println(tick);

    pendingNotes[note] = { tick, velocity };  // remember when this note started

    // Record a MIDI event correctly
    recordMidiEvent(midi::NoteOn, 1, note, velocity, tick);
  }
}

void Track::noteOff(uint8_t note, uint8_t velocity, uint32_t tick) {
  if (isPlayingBack) return;  // Ignore playback-triggered events
  auto it = pendingNotes.find(note);
  if (it != pendingNotes.end()) {
    if (state == TRACK_RECORDING || state == TRACK_OVERDUBBING) {
      Serial.print("[Record] NoteOff: ");
      Serial.print(note);
      Serial.print(" @ ");
      Serial.println(tick);

      // Record a MIDI event correctly
      recordMidiEvent(midi::NoteOff, 1, note, 0, tick);  // velocity 0 fin
    }
    // Create a finalized NoteEvent
    NoteEvent noteEvent;
    noteEvent.note = note;
    noteEvent.velocity = it->second.velocity;
    noteEvent.startTick = it->second.startTick;
    noteEvent.endTick = tick;

    noteEvents.push_back(noteEvent);
    pendingNotes.erase(it);
  } else {
    Serial.printf("WARNING: NoteOff for note %d with no matching NoteOn!\n", note);
  }
}
// -------------------------------------------------------------