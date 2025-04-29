#include "Globals.h"
#include "MidiHandler.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include <MIDI.h>


MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDIserial); // Teensy Serial8 for 5-pin DIN MIDI

MidiHandler midiHandler;  // Global instance

MidiHandler::MidiHandler()
  : outputUSB(true), outputSerial(true) {}

void MidiHandler::setup() {
  MIDIserial.begin(MIDI_CHANNEL_OMNI); // Listen to all channels
}


void MidiHandler::handleInput() {
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
// Midi Input handlers
// ---------------------------------------------------------------------------


void MidiHandler::handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source) {
  uint32_t tickNow = clockManager.getCurrentTick();
  switch (type) {
    case midi::NoteOn:
      if (data2 > 0) {
        handleNoteOn(channel, data1, data2, tickNow);
      } else {
        handleNoteOff(channel, data1, data2, tickNow); // velocity 0 = NoteOff
      }
      break;

    case midi::NoteOff:
      handleNoteOff(channel, data1, data2, tickNow);
      break;

    case midi::ControlChange:
      handleControlChange(channel, data1, data2, tickNow);
      break;

    case midi::PitchBend:
      handlePitchBend(channel, (data2 << 7) | data1); // Combine for 14-bit value
      break;

    case midi::AfterTouchChannel:
      handleAfterTouch(channel, data1);
      break;

    case midi::Clock:
      clockManager.onMidiClockPulse();
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
      // Ignore other messages for now
      break;
  }
}




// You would implement your actual MIDI input reaction here:
void MidiHandler::handleNoteOn(byte channel, byte note, byte velocity, uint32_t tickNow) { 
  //trackManager.getTrack(selectedTrack).recordMidiEvent(midi::NoteOn, channel, note, velocity, tickNow);
  trackManager.getTrack(selectedTrack).noteOn(note, velocity, tickNow);
}

void MidiHandler::handleNoteOff(byte channel, byte note, byte velocity, uint32_t tickNow) { 
  //trackManager.getTrack(selectedTrack).recordMidiEvent(midi::NoteOff, channel, note, velocity, tickNow);
  trackManager.getTrack(selectedTrack).noteOff(note, velocity, tickNow);
}

void MidiHandler::handleControlChange(byte channel, byte control, byte value, uint32_t tickNow) {
  trackManager.getTrack(selectedTrack).recordMidiEvent(midi::ControlChange, channel, control, value, tickNow);
}
void MidiHandler::handlePitchBend(byte channel, int pitchValue) { /* ... */ }
void MidiHandler::handleAfterTouch(byte channel, byte pressure) { /* ... */ }

void MidiHandler::handleMidiStart() { 
  clockManager.onMidiStart();
}

void MidiHandler::handleMidiStop() { 
  clockManager.onMidiStop();
}

void MidiHandler::handleMidiContinue() {
  clockManager.externalClockPresent = true;
  clockManager.lastMidiClockTime = micros();
}

// ---------------------------------------------------------------------------
// Midi Output handlers
// ---------------------------------------------------------------------------

void MidiHandler::sendNoteOn(byte channel, byte note, byte velocity) {
  if (outputUSB) usbMIDI.sendNoteOn(note, velocity, channel);
  if (outputSerial) MIDIserial.sendNoteOn(note, velocity, channel);
}

void MidiHandler::sendNoteOff(byte channel, byte note, byte velocity) {
  if (outputUSB) usbMIDI.sendNoteOff(note, velocity, channel);
  if (outputSerial) MIDIserial.sendNoteOff(note, velocity, channel);
}

void MidiHandler::sendControlChange(byte channel, byte control, byte value) {
  if (outputUSB) usbMIDI.sendControlChange(control, value, channel);
  if (outputSerial) MIDIserial.sendControlChange(control, value, channel);
}

void MidiHandler::sendPitchBend(byte channel, int value) {
  if (outputUSB) usbMIDI.sendPitchBend(value, channel);
  if (outputSerial) MIDIserial.sendPitchBend(value, channel);
}

void MidiHandler::sendAfterTouch(byte channel, byte pressure) {
  if (outputUSB) usbMIDI.sendAfterTouch(pressure, channel);
  if (outputSerial) MIDIserial.sendAfterTouch(pressure, channel);
}

// Clock + transport messages
void MidiHandler::sendClock() {
  if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Clock);
  if (outputSerial) MIDIserial.sendRealTime(midi::Clock);
}

void MidiHandler::sendStart() {
  if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Start);
  if (outputSerial) MIDIserial.sendRealTime(midi::Start);
}

void MidiHandler::sendStop() {
  if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Stop);
  if (outputSerial) MIDIserial.sendRealTime(midi::Stop);
}

void MidiHandler::sendContinueMIDI() {
  if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Continue);
  if (outputSerial) MIDIserial.sendRealTime(midi::Continue);
}

void MidiHandler::setOutputUSB(bool enable) {
  outputUSB = enable;
}

void MidiHandler::setOutputSerial(bool enable) {
  outputSerial = enable;
}
