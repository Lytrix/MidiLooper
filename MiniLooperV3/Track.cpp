// Track.cpp

#include "Globals.h"
#include "Track.h"
#include "MidiHandler.h" // So we can send out recorded events

Track::Track()
  : state(TRACK_STOPPED),
    startTick(0),
    lengthTicks(0),
    lastTickPlayed(0)
{}

void Track::startRecording(uint32_t currentTick) {
  events.clear();
  startTick = currentTick;
  state = TRACK_RECORDING;
}

void Track::stopRecording() {
  if (!events.empty()) {
    // Set loop length based on last event
    lengthTicks = events.back().tick;
  }
  state = TRACK_PLAYING;
}

void Track::startPlaying() {
  if (lengthTicks > 0) {
    lastTickPlayed = 0;
    state = TRACK_PLAYING;
  }
}

void Track::startOverdubbing() {
  if (state == TRACK_PLAYING) {
    state = TRACK_OVERDUBBING;
  }
}

void Track::stopPlaying() {
  state = TRACK_STOPPED;
}

void Track::clear() {
  events.clear();
  lengthTicks = 0;
  state = TRACK_STOPPED;
}

TrackState Track::getState() const {
  return state;
}

uint32_t Track::getLength() const {
  return lengthTicks;
}

void Track::setLength(uint32_t ticks) {
  lengthTicks = ticks;
}

void Track::recordMidiEvent(MidiEventType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if (state == TRACK_RECORDING || state == TRACK_OVERDUBBING) {
    MidiEvent evt = { currentTick - startTick, type, channel, data1, data2 };
    events.push_back(evt);
  }
}

void Track::playEvents(uint32_t currentTick, bool isAudible) {
  if (!isAudible) return;

  for (size_t i = 0; i < events.size(); i++) {
    const MidiEvent& evt = events[i];

    if (evt.tick == (currentTick % lengthTicks)) {
      // Send the MIDI event out
      sendMidiEvent(evt);
    }
  }
}

void Track::sendMidiEvent(const MidiEvent& evt) {
   if (state == TRACK_PLAYING || state == TRACK_OVERDUBBING) {
    uint32_t tickInLoop = (currentTick - startTick) % lengthTicks;

    switch (evt.type) {
      case EVENT_NOTE_ON:
        sendNoteOn(evt.channel, evt.data1, evt.data2);
        break;
      case EVENT_NOTE_OFF:
        sendNoteOff(evt.channel, evt.data1, evt.data2);
        break;
      case EVENT_CONTROL_CHANGE:
        sendControlChange(evt.channel, evt.data1, evt.data2);
        break;
      case EVENT_PITCH_BEND:
        sendPitchBend(evt.channel, (evt.data2 << 7) | evt.data1);
        break;
      case EVENT_AFTERTOUCH:
        sendAfterTouch(evt.channel, evt.data1);
        break;
      default:
        // Unknown type — you might want to log or ignore
        break;
    }
    lastTickPlayed = tickInLoop;
  }
  
}

// Main process on every currentTick ---------------------
void Track::process(uint32_t currentTick, bool audible) {
  playEvents(currentTick, audible);
}

// TODO Is this one still relevant?
// void Track::update(uint32_t currentTick) {
//   if (state == TRACK_PLAYING || state == TRACK_OVERDUBBING) {
//     uint32_t tickInLoop = (currentTick - startTick) % lengthTicks;

//     // Play events that match current tick
//     for (const auto& evt : events) {
//       if (evt.tick == tickInLoop) {
//         switch (evt.type) {
//           case EVENT_NOTE_ON:
//             sendNoteOn(evt.channel, evt.data1, evt.data2);
//             break;
//           case EVENT_NOTE_OFF:
//             sendNoteOff(evt.channel, evt.data1, evt.data2);
//             break;
//           case EVENT_CONTROL_CHANGE:
//             sendControlChange(evt.channel, evt.data1, evt.data2);
//             break;
//           case EVENT_PITCH_BEND:
//             sendPitchBend(evt.channel, (evt.data2 << 7) | evt.data1);
//             break;
//           case EVENT_AFTERTOUCH:
//             sendAfterTouch(evt.channel, evt.data1);
//             break;
//         }
//       }
//     }

//     lastTickPlayed = tickInLoop;
//   }
// }
