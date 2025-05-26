#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "MidiHandler.h"
#include "Logger.h"
#include "MidiEvent.h"

MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDIserial);  // Teensy Serial8 for 5-pin DIN MIDI

MidiHandler midiHandler;  // Global instance

MidiHandler::MidiHandler()
  : outputUSB(true), outputSerial(true) {}

void MidiHandler::setup() {
  MIDIserial.begin(MidiConfig::CHANNEL_OMNI);  // Listen to all channels
}

void MidiHandler::handleMidiInput() {
  // --- USB MIDI Input ---
  while (usbMIDI.read()) {
    handleMidiMessage(
      usbMIDI.getType(),
      usbMIDI.getChannel(),
      usbMIDI.getData1(),
      usbMIDI.getData2(),
      SOURCE_USB);
  }

  // --- Serial MIDI Input (DIN) ---
  while (MIDIserial.read()) {
    handleMidiMessage(
      MIDIserial.getType(),
      MIDIserial.getChannel(),
      MIDIserial.getData1(),
      MIDIserial.getData2(),
      SOURCE_SERIAL);
  }
}

void MidiHandler::handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source) {
  uint32_t tickNow = clockManager.getCurrentTick();

  switch (type) {
    case midi::NoteOn:
      if (data2 > 0)
        handleNoteOn(channel, data1, data2, tickNow);
      else
        handleNoteOff(channel, data1, data2, tickNow);  // velocity 0 = NoteOff
      break;

    case midi::NoteOff:
      handleNoteOff(channel, data1, data2, tickNow);
      break;

    case midi::ControlChange:
      handleControlChange(channel, data1, data2, tickNow);
      break;

    case midi::PitchBend:
      handlePitchBend(channel, (data2 << 7) | data1, tickNow);  // 14-bit value
      break;

    case midi::AfterTouchChannel:
      handleAfterTouch(channel, data1, tickNow);
      break;

    case midi::ProgramChange:
      handleProgramChange(channel, data1, tickNow);
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
      break;  // Ignore unsupported messages
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

void MidiHandler::handlePitchBend(byte channel, int pitchValue, uint32_t tickNow) {
  // Not yet implemented
}

void MidiHandler::handleAfterTouch(byte channel, byte pressure, uint32_t tickNow) {
  // Not yet implemented
}

void MidiHandler::handleProgramChange(byte channel, byte program, uint32_t tickNow) {
  trackManager.getSelectedTrack().recordMidiEvents(midi::ProgramChange, channel, program, 0, tickNow);
}

void MidiHandler::handleMidiStart() {
  clockManager.onMidiStart();
}

void MidiHandler::handleMidiStop() {
  // broadcast All-Notes-Off on every track
  for (size_t i = 0; i < trackManager.getTrackCount(); ++i) {
    Track &t = trackManager.getTrack(i);
    t.sendAllNotesOff();
    t.stopPlaying();  // update LCD state to STOPPED if desired
  }
  clockManager.onMidiStop();
}

void MidiHandler::handleMidiContinue() {
  clockManager.setExternalClockPresent(true);
  clockManager.setLastMidiClockTime(micros());
}

// --- MIDI Output ---
void MidiHandler::sendMidiEvent(const MidiEvent& event) {
    // Route and send the event to both USB and Serial as appropriate
    switch (event.type) {
        case midi::NoteOn:
            if (outputUSB) usbMIDI.sendNoteOn(event.data.noteData.note, event.data.noteData.velocity, event.channel);
            if (outputSerial) MIDIserial.sendNoteOn(event.data.noteData.note, event.data.noteData.velocity, event.channel);
            break;
        case midi::NoteOff:
            if (outputUSB) usbMIDI.sendNoteOff(event.data.noteData.note, event.data.noteData.velocity, event.channel);
            if (outputSerial) MIDIserial.sendNoteOff(event.data.noteData.note, event.data.noteData.velocity, event.channel);
            break;
        case midi::ControlChange:
            if (outputUSB) usbMIDI.sendControlChange(event.data.ccData.cc, event.data.ccData.value, event.channel);
            if (outputSerial) MIDIserial.sendControlChange(event.data.ccData.cc, event.data.ccData.value, event.channel);
            break;
        case midi::PitchBend:
            if (outputUSB) usbMIDI.sendPitchBend(event.data.pitchBend, event.channel);
            if (outputSerial) MIDIserial.sendPitchBend(event.data.pitchBend, event.channel);
            break;
        case midi::AfterTouchChannel:
            if (outputUSB) usbMIDI.sendAfterTouch(event.data.channelPressure, event.channel);
            if (outputSerial) MIDIserial.sendAfterTouch(event.data.channelPressure, event.channel);
            break;
        case midi::ProgramChange:
            if (outputUSB) usbMIDI.sendProgramChange(event.data.program, event.channel);
            if (outputSerial) MIDIserial.sendProgramChange(event.data.program, event.channel);
            break;
        case midi::SystemExclusive:
            if (outputUSB) usbMIDI.sendSysEx(event.data.sysexData.length, event.data.sysexData.data, true);
            if (outputSerial) MIDIserial.sendSysEx(event.data.sysexData.length, event.data.sysexData.data, true);
            break;
        case midi::TimeCodeQuarterFrame:
            if (outputUSB) usbMIDI.sendRealTime(0xF1);
            if (outputSerial) MIDIserial.sendRealTime(0xF1);
            break;
        case midi::SongPosition:
            if (outputUSB) usbMIDI.sendSongPosition(event.data.songPosition);
            if (outputSerial) MIDIserial.sendSongPosition(event.data.songPosition);
            break;
        case midi::SongSelect:
            if (outputUSB) usbMIDI.sendSongSelect(event.data.songNumber);
            if (outputSerial) MIDIserial.sendSongSelect(event.data.songNumber);
            break;
        case midi::Clock:
            if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Clock);
            if (outputSerial) MIDIserial.sendRealTime(midi::Clock);
            break;
        case midi::Start:
            if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Start);
            if (outputSerial) MIDIserial.sendRealTime(midi::Start);
            break;
        case midi::Stop:
            if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Stop);
            if (outputSerial) MIDIserial.sendRealTime(midi::Stop);
            break;
        case midi::Continue:
            if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Continue);
            if (outputSerial) MIDIserial.sendRealTime(midi::Continue);
            break;
        default:
            // Unsupported or unhandled event type
            break;
    }
}

// For immediate output, tick is set to 0 because the event is sent right now and the value is no used in the function
void MidiHandler::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    sendMidiEvent(MidiEvent::NoteOn(0, channel, note, velocity));
}

void MidiHandler::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    sendMidiEvent(MidiEvent::NoteOff(0, channel, note, velocity));
}

void MidiHandler::sendControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    sendMidiEvent(MidiEvent::ControlChange(0, channel, control, value));
}

void MidiHandler::sendPitchBend(uint8_t channel, int16_t value) {
    sendMidiEvent(MidiEvent::PitchBend(0, channel, value));
}

void MidiHandler::sendAfterTouch(uint8_t channel, uint8_t pressure) {
    sendMidiEvent(MidiEvent::ChannelAftertouch(0, channel, pressure));
}

void MidiHandler::sendProgramChange(uint8_t channel, uint8_t program) {
    sendMidiEvent(MidiEvent::ProgramChange(0, channel, program));
}

// --- Clock / Transport Output ---
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

// --- Output Routing ---
void MidiHandler::setOutputUSB(bool enable) {
  outputUSB = enable;
}

void MidiHandler::setOutputSerial(bool enable) {
  outputSerial = enable;
}
