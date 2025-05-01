#include "Globals.h"
#include <MIDI.h>
#include "ClockManager.h"
#include "TrackManager.h"
#include "MidiHandler.h"


MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDIserial); // Teensy Serial8 for 5-pin DIN MIDI

MidiHandler midiHandler;  // Global instance

MidiHandler::MidiHandler()
  : outputUSB(true), outputSerial(true) {}

void MidiHandler::setup() {
  MIDIserial.begin(MIDI_CHANNEL_OMNI); // Listen to all channels
}

void MidiHandler::handleMidiInput() {
  // --- USB MIDI Input ---
  while (usbMIDI.read()) {
    handleMidiMessage(
      usbMIDI.getType(),
      usbMIDI.getChannel(),
      usbMIDI.getData1(),
      usbMIDI.getData2(),
      SOURCE_USB
    );
  }

  // --- Serial MIDI Input (DIN) ---
  while (MIDIserial.read()) {
    handleMidiMessage(
      MIDIserial.getType(),
      MIDIserial.getChannel(),
      MIDIserial.getData1(),
      MIDIserial.getData2(),
      SOURCE_SERIAL
    );
  }
}

void MidiHandler::handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source) {
  uint32_t tickNow = clockManager.getCurrentTick();

  switch (type) {
    case midi::NoteOn:
      if (data2 > 0)
        handleNoteOn(channel, data1, data2, tickNow);
      else
        handleNoteOff(channel, data1, data2, tickNow); // velocity 0 = NoteOff
      break;

    case midi::NoteOff:
      handleNoteOff(channel, data1, data2, tickNow);
      break;

    case midi::ControlChange:
      handleControlChange(channel, data1, data2, tickNow);
      break;

    case midi::PitchBend:
      handlePitchBend(channel, (data2 << 7) | data1); // 14-bit value
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
      break; // Ignore unsupported messages
  }
}

// --- Individual Message Handlers ---
void MidiHandler::handleNoteOn(byte channel, byte note, byte velocity, uint32_t tickNow) {
  trackManager.getSelectedTrack().noteOn(channel, note, velocity, tickNow);
}

void MidiHandler::handleNoteOff(byte channel, byte note, byte velocity, uint32_t tickNow) {
  trackManager.getSelectedTrack().noteOff(channel, note, velocity, tickNow);
}

void MidiHandler::handleControlChange(byte channel, byte control, byte value, uint32_t tickNow) {
  trackManager.getSelectedTrack().recordMidiEvents(midi::ControlChange, channel, control, value, tickNow);
}

void MidiHandler::handlePitchBend(byte channel, int pitchValue) {
  // Not yet implemented
}

void MidiHandler::handleAfterTouch(byte channel, byte pressure) {
  // Not yet implemented
}

void MidiHandler::handleMidiStart() {
  clockManager.onMidiStart();
}

void MidiHandler::handleMidiStop() {
  clockManager.onMidiStop();
}

void MidiHandler::handleMidiContinue() {
  clockManager.setExternalClockPresent(true);
  clockManager.setLastMidiClockTime(micros());
}

// --- MIDI Output ---
void MidiHandler::sendNoteOn(byte channel, byte note, byte velocity) {
  if (outputUSB)    usbMIDI.sendNoteOn(note, velocity, channel);
  if (outputSerial) MIDIserial.sendNoteOn(note, velocity, channel);
}

void MidiHandler::sendNoteOff(byte channel, byte note, byte velocity) {
  if (outputUSB)    usbMIDI.sendNoteOff(note, velocity, channel);
  if (outputSerial) MIDIserial.sendNoteOff(note, velocity, channel);
}

void MidiHandler::sendControlChange(byte channel, byte control, byte value) {
  if (outputUSB)    usbMIDI.sendControlChange(control, value, channel);
  if (outputSerial) MIDIserial.sendControlChange(control, value, channel);
}

void MidiHandler::sendPitchBend(byte channel, int value) {
  if (outputUSB)    usbMIDI.sendPitchBend(value, channel);
  if (outputSerial) MIDIserial.sendPitchBend(value, channel);
}

void MidiHandler::sendAfterTouch(byte channel, byte pressure) {
  if (outputUSB)    usbMIDI.sendAfterTouch(pressure, channel);
  if (outputSerial) MIDIserial.sendAfterTouch(pressure, channel);
}

// --- Clock / Transport Output ---
void MidiHandler::sendClock() {
  if (outputUSB)    usbMIDI.sendRealTime(usbMIDI.Clock);
  if (outputSerial) MIDIserial.sendRealTime(midi::Clock);
}

void MidiHandler::sendStart() {
  if (outputUSB)    usbMIDI.sendRealTime(usbMIDI.Start);
  if (outputSerial) MIDIserial.sendRealTime(midi::Start);
}

void MidiHandler::sendStop() {
  if (outputUSB)    usbMIDI.sendRealTime(usbMIDI.Stop);
  if (outputSerial) MIDIserial.sendRealTime(midi::Stop);
}

void MidiHandler::sendContinueMIDI() {
  if (outputUSB)    usbMIDI.sendRealTime(usbMIDI.Continue);
  if (outputSerial) MIDIserial.sendRealTime(midi::Continue);
}

// --- Output Routing ---
void MidiHandler::setOutputUSB(bool enable) {
  outputUSB = enable;
}

void MidiHandler::setOutputSerial(bool enable) {
  outputSerial = enable;
}
