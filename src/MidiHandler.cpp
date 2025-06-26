//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "MidiHandler.h"
#include "Logger.h"
#include "MidiEvent.h"
#include "MidiButtonManager.h"

MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDIserial);  // Teensy Serial8 for 5-pin DIN MIDI

MidiHandler midiHandler;  // Global instance
MidiHandler* MidiHandler::instance = nullptr;  // Static instance pointer

// Helper function to get readable MIDI message type name
const char* getMidiTypeName(byte type) {
  switch (type) {
    case midi::NoteOn: return "NoteOn";
    case midi::NoteOff: return "NoteOff";
    case midi::ControlChange: return "CC";
    case midi::PitchBend: return "PitchBend";
    case midi::AfterTouchChannel: return "AfterTouch";
    case midi::ProgramChange: return "ProgChange";
    case midi::Clock: return "Clock";
    case midi::Start: return "Start";
    case midi::Stop: return "Stop";
    case midi::Continue: return "Continue";
    default: return "Unknown";
  }
}

MidiHandler::MidiHandler()
  : outputUSB(true), outputSerial(true), hub1(usbHost), usbHostMIDI(usbHost) {}

void MidiHandler::setup() {
  MIDIserial.begin(MidiConfig::CHANNEL_OMNI);  // Listen to all channels
  
  // Setup USB Host MIDI
  logger.info("Starting USB Host MIDI...");
  usbHost.begin();
  instance = this;  // Set static instance pointer for callbacks
  
  // Set up USB Host MIDI message handlers for logging
  usbHostMIDI.setHandleNoteOn(usbHostNoteOn);
  usbHostMIDI.setHandleNoteOff(usbHostNoteOff);
  usbHostMIDI.setHandleControlChange(usbHostControlChange);
  usbHostMIDI.setHandleProgramChange(usbHostProgramChange);
  usbHostMIDI.setHandlePitchChange(usbHostPitchChange);
  usbHostMIDI.setHandleAfterTouchChannel(usbHostAfterTouchChannel);
  usbHostMIDI.setHandleClock(usbHostClock);
  usbHostMIDI.setHandleStart(usbHostStart);
  usbHostMIDI.setHandleStop(usbHostStop);
  usbHostMIDI.setHandleContinue(usbHostContinue);
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
  
  // --- USB Host MIDI Input ---
  usbHost.Task();  // Update USB host state
  
  // Check if USB Host MIDI device is connected and log status changes
  static bool lastConnected = false;
  bool currentlyConnected = usbHostMIDI;
  if (currentlyConnected != lastConnected) {
    if (currentlyConnected) {
      logger.info("USB Host MIDI device connected!");
    } else {
      logger.info("USB Host MIDI device disconnected!");
    }
    lastConnected = currentlyConnected;
  }
  
  usbHostMIDI.read();  // Process USB host MIDI messages (handlers will be called)
}

void MidiHandler::handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source) {
  uint32_t tickNow = clockManager.getCurrentTick();

  // Log incoming MIDI messages
  const char* sourceStr = (source == SOURCE_USB) ? "USB" : 
                          (source == SOURCE_SERIAL) ? "Serial" : 
                          (source == SOURCE_USB_HOST) ? "USB Host" : "Unknown";
  logger.log(CAT_MIDI, LOG_DEBUG, "%s MIDI: type=%s ch=%d d1=%d d2=%d", 
             sourceStr, getMidiTypeName(type), channel, data1, data2);

  // Additional detailed logging for note messages
  if (type == midi::NoteOn || type == midi::NoteOff) {
    const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (data1 / 12) - 1;
    const char* noteName = noteNames[data1 % 12];
    logger.log(CAT_MIDI, LOG_DEBUG, "  -> Note: %s%d (MIDI note %d), Velocity: %d", 
               noteName, octave, data1, data2);
  }

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
  // Route button notes to MidiButtonManager
  midiButtonManager.handleMidiNote(channel, note, velocity, true);
  
  // Route to track recording
  trackManager.getSelectedTrack().noteOn(channel, note, velocity, tickNow);
}

void MidiHandler::handleNoteOff(byte channel, byte note, byte velocity, uint32_t tickNow) {
  // Route button notes to MidiButtonManager
  midiButtonManager.handleMidiNote(channel, note, velocity, false);
  
  // Route to track recording
  trackManager.getSelectedTrack().noteOff(channel, note, velocity, tickNow);
}

void MidiHandler::handleControlChange(byte channel, byte control, byte value, uint32_t tickNow) {
  // Route encoder CC to MidiButtonManager
  midiButtonManager.handleMidiEncoder(channel, control, value);
  
  // Route to track recording
  trackManager.getSelectedTrack().recordMidiEvents(midi::ControlChange, channel, control, value, tickNow);
}

void MidiHandler::handlePitchBend(byte channel, int pitchValue, uint32_t tickNow) {
  // Route pitchbend to MidiButtonManager for navigation
  midiButtonManager.handleMidiPitchbend(channel, pitchValue);
  
  // Route to track recording
  trackManager.getSelectedTrack().recordMidiEvents(midi::PitchBend, channel, pitchValue & 0x7F, (pitchValue >> 7) & 0x7F, tickNow);
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
            if (outputUSB) usbMIDI.sendRealTime(midi::MidiType::TimeCodeQuarterFrame);
            if (outputSerial) MIDIserial.sendRealTime(midi::MidiType::TimeCodeQuarterFrame);
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

// --- Static USB Host MIDI Callbacks ---
void MidiHandler::usbHostNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (instance) {
    // Route button notes to MidiButtonManager
    midiButtonManager.handleMidiNote(channel, note, velocity, true);
    
    // Route to regular MIDI handling
    instance->handleMidiMessage(midi::NoteOn, channel, note, velocity, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (instance) {
    // Route button notes to MidiButtonManager
    midiButtonManager.handleMidiNote(channel, note, velocity, false);
    
    // Route to regular MIDI handling
    instance->handleMidiMessage(midi::NoteOff, channel, note, velocity, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostControlChange(uint8_t channel, uint8_t control, uint8_t value) {
  if (instance) {
    // Route encoder CC to MidiButtonManager
    midiButtonManager.handleMidiEncoder(channel, control, value);
    
    // Route to regular MIDI handling
    instance->handleMidiMessage(midi::ControlChange, channel, control, value, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostProgramChange(uint8_t channel, uint8_t program) {
  if (instance) {
    instance->handleMidiMessage(midi::ProgramChange, channel, program, 0, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostPitchChange(uint8_t channel, int pitch) {
  if (instance) {
    // Route pitchbend to MidiButtonManager for navigation
    midiButtonManager.handleMidiPitchbend(channel, pitch);
    
    // Route to regular MIDI handling
    instance->handleMidiMessage(midi::PitchBend, channel, pitch & 0x7F, (pitch >> 7) & 0x7F, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostAfterTouchChannel(uint8_t channel, uint8_t pressure) {
  if (instance) {
    instance->handleMidiMessage(midi::AfterTouchChannel, channel, pressure, 0, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostClock() {
  if (instance) {
    instance->handleMidiMessage(midi::Clock, 0, 0, 0, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostStart() {
  if (instance) {
    instance->handleMidiMessage(midi::Start, 0, 0, 0, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostStop() {
  if (instance) {
    instance->handleMidiMessage(midi::Stop, 0, 0, 0, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostContinue() {
  if (instance) {
    instance->handleMidiMessage(midi::Continue, 0, 0, 0, SOURCE_USB_HOST);
  }
}
