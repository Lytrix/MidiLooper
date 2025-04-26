#include "Globals.h"
#include "MidiHandler.h"
#include "ClockManager.h"
#include <MIDI.h>

MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDIserial); // Use Serial1 for 5-pin DIN MIDI

bool outputUSB = true;
bool outputSerial = true;

void setupMidi() {
  MIDIserial.begin(MIDI_CHANNEL_OMNI); // listen to all channels
}

// ---------------------------------------------------------------------------
// Midi Input handlers
// ---------------------------------------------------------------------------

// Then your usual handler functions:
void handleNoteOn(byte channel, byte note, byte velocity) { /* ... */ }
void handleNoteOff(byte channel, byte note, byte velocity) { /* ... */ }
void handleControlChange(byte channel, byte control, byte value) { /* ... */ }
void handlePitchBend(byte channel, int pitchValue) { /* ... */ }
void handleAfterTouch(byte channel, byte pressure) { /* ... */ }
void handleMidiStart() { currentTick = 0; }
void handleMidiStop() { /* maybe pause */ }
void handleMidiContinue() { /* maybe resume */ }


// Unified message handler
void handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source) {
  switch (type) {
    case midi::NoteOn:
      if (data2 > 0) {
        handleNoteOn(channel, data1, data2);
      } else {
        handleNoteOff(channel, data1, data2); // velocity 0 = note off
      }
      break;

    case midi::NoteOff:
      handleNoteOff(channel, data1, data2);
      break;

    case midi::ControlChange:
      handleControlChange(channel, data1, data2);
      break;

    case midi::PitchBend:
      handlePitchBend(channel, (data2 << 7) | data1); // Combine for 14-bit value
      break;

    case midi::AfterTouchChannel:
      handleAfterTouch(channel, data1);
      break;

    case midi::Clock:
      onMidiClockPulse();
      break;

    case midi::Start:
      handleMidiStart();
      break;

    case midi::Stop:
      handleMidiStop();
      break;

    case midi::Continue:
      handleMidiContinue();
      break;

    default:
      // Ignore other types for now
      break;
  }
}

void handleMidiInput() {
  // ---- Read USB MIDI ----
  while (usbMIDI.read()) {
    byte type = usbMIDI.getType();
    byte channel = usbMIDI.getChannel();
    byte data1 = usbMIDI.getData1();
    byte data2 = usbMIDI.getData2();
    InputSource source = SOURCE_USB;

    handleMidiMessage(type, channel, data1, data2, source);
  }

  // ---- Read Serial MIDI (5-pin DIN) ----
  while (MIDIserial.read()) {
    byte type = MIDIserial.getType();
    byte channel = MIDIserial.getChannel();
    byte data1 = MIDIserial.getData1();
    byte data2 = MIDIserial.getData2();
    InputSource source = SOURCE_SERIAL;

    handleMidiMessage(type, channel, data1, data2, source);
  }
}

// ---------------------------------------------------------------------------
// Midi Output handlers
// ---------------------------------------------------------------------------


void sendNoteOn(byte channel, byte note, byte velocity) {
  usbMIDI.sendNoteOn(note, velocity, channel);
  MIDIserial.sendNoteOn(note, velocity, channel);
}

void sendNoteOff(byte channel, byte note, byte velocity) {
  usbMIDI.sendNoteOff(note, velocity, channel);
  MIDIserial.sendNoteOff(note, velocity, channel);
}

void sendControlChange(byte channel, byte control, byte value) {
  usbMIDI.sendControlChange(control, value, channel);
  MIDIserial.sendControlChange(control, value, channel);
}

void sendPitchBend(byte channel, int value) {
  usbMIDI.sendPitchBend(value, channel);
  MIDIserial.sendPitchBend(value, channel);
}

void sendAfterTouch(byte channel, byte pressure) {
  usbMIDI.sendAfterTouch(pressure, channel);
  MIDIserial.sendAfterTouch(pressure, channel);
}

// Clock + transport and selectable which channel sends
void sendClock() {
  if (outputUSB) {
    usbMIDI.sendRealTime(usbMIDI.Clock);
  }
  if (outputSerial) {
    MIDIserial.sendRealTime(midi::Clock);
  }
}

void sendStart() {
  usbMIDI.sendRealTime(usbMIDI.Start);
  MIDIserial.sendRealTime(midi::Start);
}

void sendStop() {
  usbMIDI.sendRealTime(usbMIDI.Stop);
  MIDIserial.sendRealTime(midi::Stop);
}

void sendContinueMIDI() {
  usbMIDI.sendRealTime(usbMIDI.Continue);
  MIDIserial.sendRealTime(midi::Continue);
}
