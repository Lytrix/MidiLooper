//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "MidiHandler.h"
#include "MidiButtonActions.h"
#include "Logger.h"
#include "MidiEvent.h"
#include "MidiButtonManager.h"
#include "MidiFaderManager.h"
#include "BarStepButtonHandler.h"
#include "MidiConfig.h"
#include "ControlSurfaceManager.h"
#include "Utils/DebugSessionCapture.h"
#include "LooperState.h"
#include "Utils/MidiDispatchOrder.h"
#include "Utils/RuntimeTimingEnvelope.h"

MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDIserial);  // Teensy Serial8 for 5-pin DIN MIDI

MidiHandler midiHandler;  // Global instance
MidiHandler* MidiHandler::instance = nullptr;  // Static instance pointer

namespace {
bool isMotorFaderUsbHostServiceMessage(uint8_t type, uint8_t channel, uint8_t data1) {
  if (channel != MidiConfig::Fader::COARSE_CHANNEL &&
      channel != MidiConfig::Fader::SELECT_CHANNEL &&
      channel != MidiConfig::Fader::FINE_CHANNEL) {
    return false;
  }
  if (type == midi::PitchBend) {
    return channel == MidiConfig::Fader::COARSE_CHANNEL ||
           channel == MidiConfig::Fader::SELECT_CHANNEL;
  }
  if (type != midi::NoteOn && type != midi::NoteOff) {
    return false;
  }
  if (channel == MidiConfig::Fader::SELECT_CHANNEL ||
      channel == MidiConfig::Fader::COARSE_CHANNEL) {
    return data1 == MidiConfig::Fader::MOTOR_TRIGGER_NOTE;
  }
  return data1 == MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE ||
         data1 == MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE;
}

bool isDroidFaderFeedbackOnlyPitchbend(uint8_t channel) {
  return channel == MidiConfig::Fader::COARSE_CHANNEL ||
         channel == MidiConfig::Fader::SELECT_CHANNEL;
}

bool isMidiThruExcludedChannel(byte channel) {
  return channel >= MidiConfig::THRU_EXCLUDE_MIN && channel <= MidiConfig::THRU_EXCLUDE_MAX;
}

bool isDroidFaderFeedbackOnlyControlChange(uint8_t channel, uint8_t cc) {
  if (channel == MidiConfig::Fader::FINE_CHANNEL) {
    return cc == MidiConfig::Fader::FINE_CC || cc == MidiConfig::Fader::NOTE_VALUE_CC;
  }
  if (channel == MidiConfig::LoopEdit::LENGTH_CC_CHANNEL) {
    return cc == MidiConfig::LoopEdit::LENGTH_CC_NUMBER;
  }
  return false;
}
}  // namespace

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

namespace {

constexpr size_t kMidiInputBatchMax = 128;

struct MidiInputMsg {
  byte type;
  byte channel;
  byte data1;
  byte data2;
  InputSource source;
};

// External sequencers (e.g. BeatStep Pro) may send NoteOn before MIDI Start on the
// same downbeat. Process Start/Stop/Continue first within each poll batch so armed
// recording starts before channel messages in that batch are recorded.
// Clock stays FIFO with channel messages (RC-C A): Clock, NoteOn, Clock must not
// become Clock, Clock, NoteOn — note ticks come from getCurrentTick() at process time.
void dispatchMidiBatch(MidiInputMsg* batch, size_t count) {
  if (count == 0) {
    return;
  }
  size_t order[kMidiInputBatchMax];
  uint8_t types[kMidiInputBatchMax];
  for (size_t i = 0; i < count; ++i) {
    types[i] = batch[i].type;
  }
  MidiDispatchOrder::planDispatchOrder(types, count, order);
  for (size_t i = 0; i < count; ++i) {
    const MidiInputMsg& msg = batch[order[i]];
    midiHandler.handleMidiMessage(msg.type, msg.channel, msg.data1, msg.data2, msg.source);
  }
}

}  // namespace

void MidiHandler::setup() {
  Serial8.begin(31250);  // DIN MIDI out/in (explicit before MIDI library wraps Serial8)
  MIDIserial.begin(MidiConfig::CHANNEL_OMNI);  // Listen to all channels
  instance = this;  // Set static instance pointer for callbacks
}

void MidiHandler::beginUsbHost() {
  if (usbHostReady_) {
    return;
  }
  logger.info("Starting USB Host MIDI...");
  usbHost.begin();

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
  usbHostReady_ = true;

  // Cold-plug: DROID may already be attached at power-on. Pump Task() so enumeration can
  // progress without SDIO contention (beginUsbHost runs after deferred slot restore).
  const uint32_t enumerateDeadlineMs = millis() + 300;
  while (millis() < enumerateDeadlineMs) {
    usbHost.Task();
    if (static_cast<bool>(usbHostMIDI)) {
      break;
    }
    yield();
  }
}

void MidiHandler::handleMidiInput() {
  const uint32_t inputEnterUs = micros();
  RuntimeTimingEnvelope::noteMidiInputEnter(inputEnterUs);

  MidiInputMsg batch[kMidiInputBatchMax];
  size_t count = 0;

  // --- USB MIDI Input (transport before notes within each poll) ---
  RuntimeTimingEnvelope::beginUsbDeviceNested();
  uint32_t segmentStartUs = micros();
  while (count < kMidiInputBatchMax && usbMIDI.read()) {
    batch[count++] = {usbMIDI.getType(), usbMIDI.getChannel(), usbMIDI.getData1(),
                      usbMIDI.getData2(), SOURCE_USB};
  }
  RuntimeTimingEnvelope::noteUsbDeviceRead(micros() - segmentStartUs);
  const uint32_t dispatchStartUs = micros();
  dispatchMidiBatch(batch, count);
  RuntimeTimingEnvelope::noteUsbDeviceDispatch(micros() - dispatchStartUs);
  RuntimeTimingEnvelope::commitUsbDeviceNested();
  RuntimeTimingEnvelope::noteUsbDeviceDrain(micros() - segmentStartUs);

  // --- Serial MIDI Input (DIN) ---
  count = 0;
  segmentStartUs = micros();
  while (count < kMidiInputBatchMax && MIDIserial.read()) {
    batch[count++] = {MIDIserial.getType(), MIDIserial.getChannel(), MIDIserial.getData1(),
                      MIDIserial.getData2(), SOURCE_SERIAL};
  }
  dispatchMidiBatch(batch, count);
  RuntimeTimingEnvelope::noteDinDrain(micros() - segmentStartUs);

  if (!usbHostReady_) {
    RuntimeTimingEnvelope::noteMidiInputExit(micros());
    return;
  }

  // --- USB Host MIDI Input ---
  // USBHost_t36 delivers one MIDI message per read() via callbacks. Drain a bounded
  // batch each handleMidiInput() call (same cap as USB-device / DIN) so DROID NoteOn/NoteOff
  // pairs are not left queued across a long main-loop frame.
  segmentStartUs = micros();
  usbHost.Task();
  RuntimeTimingEnvelope::noteUsbHostTask(micros() - segmentStartUs);

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

  segmentStartUs = micros();
  size_t hostReads = 0;
  while (hostReads < kMidiInputBatchMax && usbHostMIDI.read()) {
    ++hostReads;
  }
  RuntimeTimingEnvelope::noteUsbHostDrain(micros() - segmentStartUs);
  if (hostReads >= kMidiInputBatchMax) {
    logger.log(CAT_MIDI, LOG_DEBUG,
               "USB Host MIDI drain hit batch cap (%u)",
               static_cast<unsigned>(kMidiInputBatchMax));
  }

  RuntimeTimingEnvelope::noteMidiInputExit(micros());
}

void MidiHandler::handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source) {
  // Clock is high-rate (24 PPQN); skip synchronous capture to keep timing paths lean.
  if (type != midi::Clock) {
    if (source == SOURCE_USB) {
      const uint32_t captureStartUs = micros();
      SC_MIDI_IN('U', type, channel, data1, data2);
      RuntimeTimingEnvelope::addUsbDeviceCapture(micros() - captureStartUs);
    } else {
      SC_MIDI_IN(source == SOURCE_SERIAL ? 'S' : 'H', type, channel, data1, data2);
    }
  }

#if defined(MIDI_USB_FADER_PROBE_PASSTHROUGH)
  if (source == SOURCE_USB) {
    mirrorUsbFaderProbePassthrough(type, channel, data1, data2);
  }
#endif

  // Per-note Serial DEBUG saturates USB CDC on capture builds (~960 lines/30s in
  // session_20260811_021117) and starves #CAP flush. Keep #CAP,MI; skip DEBUG text.
#if !defined(SESSION_CAPTURE)
  if (type != midi::Clock) {
    const char* sourceStr = (source == SOURCE_USB) ? "USB" :
                            (source == SOURCE_SERIAL) ? "Serial" :
                            (source == SOURCE_USB_HOST) ? "USB Host" : "Unknown";
    logger.log(CAT_MIDI, LOG_DEBUG, "%s MIDI: type=%s ch=%d d1=%d d2=%d",
               sourceStr, getMidiTypeName(type), channel, data1, data2);

    if (type == midi::NoteOn || type == midi::NoteOff) {
      const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
      int octave = (data1 / 12) - 1;
      const char* noteName = noteNames[data1 % 12];
      logger.log(CAT_MIDI, LOG_DEBUG, "  -> Note: %s%d (MIDI note %d), Velocity: %d",
                 noteName, octave, data1, data2);
    }
  }
#endif

  // MIDI Thru: pass channel messages to USB/Serial/USB-host on the selected track channel —
  // except DROID control plane (ch13-16), which must never be remapped onto track output.
  uint8_t outCh = trackManager.getSelectedTrack().getMidiChannel();
  bool isChannelMessage = (type == midi::NoteOn || type == midi::NoteOff || type == midi::ControlChange ||
                           type == midi::PitchBend || type == midi::AfterTouchChannel || type == midi::ProgramChange);
  if (isChannelMessage && !isMidiThruExcludedChannel(channel)) {
    if (source == SOURCE_USB) {
      const uint32_t thruStartUs = micros();
      sendMidiThru(type, outCh, data1, data2);
      RuntimeTimingEnvelope::addUsbDeviceThru(micros() - thruStartUs);
    } else {
      sendMidiThru(type, outCh, data1, data2);
    }
  }

  // Dispatch transport/clock messages first so tick is up-to-date
  // before channel messages read it
  switch (type) {
    case midi::Clock:
      if (source == SOURCE_USB) {
        const uint32_t clockStartUs = micros();
        clockManager.onMidiClockPulse();
        RuntimeTimingEnvelope::addUsbDeviceClock(micros() - clockStartUs);
      } else {
        clockManager.onMidiClockPulse();
      }
      return;

    case midi::Start:
    case midi::Stop:
    case midi::Continue: {
      const uint32_t transportStartUs = (source == SOURCE_USB) ? micros() : 0;
      if (type == midi::Start) {
        handleMidiStart();
      } else if (type == midi::Stop) {
        handleMidiStop();
      } else {
        handleMidiContinue();
      }
      if (source == SOURCE_USB) {
        RuntimeTimingEnvelope::addUsbDeviceTransport(micros() - transportStartUs);
      }
      return;
    }

    default:
      break;
  }

  uint32_t tickNow = clockManager.getCurrentTick();
  const uint32_t channelStartUs = (source == SOURCE_USB) ? micros() : 0;

  switch (type) {
    case midi::NoteOn:
      if (data2 > 0)
        handleNoteOn(channel, data1, data2, tickNow);
      else
        handleNoteOff(channel, data1, data2, tickNow);
      break;

    case midi::NoteOff:
      handleNoteOff(channel, data1, data2, tickNow);
      break;

    case midi::ControlChange:
      handleControlChange(channel, data1, data2, tickNow);
      break;

    case midi::PitchBend:
      handlePitchBend(channel, (data2 << 7) | data1, tickNow);
      break;

    case midi::AfterTouchChannel:
      handleAfterTouch(channel, data1, tickNow);
      break;

    case midi::ProgramChange:
      handleProgramChange(channel, data1, tickNow);
      break;

    default:
      break;
  }

  if (source == SOURCE_USB) {
    const uint32_t channelDurationUs = micros() - channelStartUs;
    if (type == midi::NoteOn || type == midi::NoteOff) {
      RuntimeTimingEnvelope::addUsbDeviceNote(channelDurationUs);
    } else if (type == midi::ControlChange || type == midi::PitchBend ||
               type == midi::AfterTouchChannel || type == midi::ProgramChange) {
      RuntimeTimingEnvelope::addUsbDeviceCc(channelDurationUs);
    }
  }
}

// --- Helper Functions ---
bool MidiHandler::isControlChannel(byte channel) {
  return (channel >= MidiConfig::RECORD_EXCLUDE_MIN && channel <= MidiConfig::RECORD_EXCLUDE_MAX);
}

bool MidiHandler::isLedChannel(byte channel) {
  return (channel >= MidiConfig::LED_CHANNEL_MIN && channel <= MidiConfig::LED_CHANNEL_MAX);
}

#if defined(MIDI_USB_FADER_PROBE_PASSTHROUGH)
void MidiHandler::mirrorUsbFaderProbePassthrough(byte type, byte channel, byte data1, byte data2) {
  const bool isFaderMotorChannel =
      channel == MidiConfig::Fader::COARSE_CHANNEL || channel == MidiConfig::Fader::SELECT_CHANNEL;
  const bool isEditModeLedTrigger =
      channel == MidiConfig::Channels::LED_FEEDBACK &&
      (type == midi::NoteOn || type == midi::NoteOff) && data1 == 0;
  if (!isFaderMotorChannel && !isEditModeLedTrigger) {
    return;
  }
  if (!canSendUsbHostMidi()) {
    return;
  }
  paceDroidUsbHostBeforeSend();
  switch (type) {
    case midi::NoteOn:
      usbHostMIDI.sendNoteOn(data1, data2, channel);
      break;
    case midi::NoteOff:
      usbHostMIDI.sendNoteOff(data1, data2, channel);
      break;
    case midi::ControlChange:
      usbHostMIDI.sendControlChange(data1, data2, channel);
      break;
    case midi::PitchBend: {
      const int16_t pitchValue = MidiConfig::Pitchbend::logicalToWireSigned(
          static_cast<int16_t>(((static_cast<int>(data2) << 7) | data1) - 8192));
      usbHostMIDI.sendPitchBend(pitchValue, channel);
      break;
    }
    case midi::ProgramChange:
      usbHostMIDI.sendProgramChange(data1, channel);
      break;
    default:
      return;
  }
  serviceUsbHostAfterOutboundPacket();
}
#endif

void MidiHandler::paceDroidUsbHostBeforeSend() {
  const uint32_t now = micros();
  if (lastDroidUsbHostSendMicros_ == 0) {
    return;
  }
  const uint32_t elapsed = now - lastDroidUsbHostSendMicros_;
  const uint32_t gap = MidiConfig::DroidUsbHost::MIN_PACKET_GAP_MICROS;
  if (elapsed < gap) {
    delayMicroseconds(static_cast<uint16_t>(gap - elapsed));
  }
}

void MidiHandler::markDroidUsbHostSent() {
  lastDroidUsbHostSendMicros_ = micros();
}

void MidiHandler::queueLedUsbHostFeedback(uint8_t note, uint8_t velocityOrZero) {
  for (size_t i = 0; i < ledPendingCount_; ++i) {
    if (ledPendingNotes_[i] == note) {
      ledPendingVelocities_[i] = velocityOrZero;
      return;
    }
  }
  if (ledPendingCount_ >= MidiConfig::DroidUsbHost::LED_PENDING_MAX) {
  // Drop oldest entry when full; coalescing keeps the queue small in normal use.
    ledPendingCount_--;
    for (size_t i = 0; i < ledPendingCount_; ++i) {
      ledPendingNotes_[i] = ledPendingNotes_[i + 1];
      ledPendingVelocities_[i] = ledPendingVelocities_[i + 1];
    }
  }
  ledPendingNotes_[ledPendingCount_] = note;
  ledPendingVelocities_[ledPendingCount_] = velocityOrZero;
  ledPendingCount_++;
}

void MidiHandler::processDroidUsbHostOutbound() {
  if (!usbHostReady_ || !usbHostMIDI || ledPendingCount_ == 0 || droidMotorOutboundPriority_) {
    return;
  }
  constexpr uint8_t ch = MidiConfig::Led::CHANNEL;
  uint8_t drained = 0;
  while (ledPendingCount_ > 0 &&
         drained < MidiConfig::DroidUsbHost::LED_DRAIN_MAX_PER_LOOP) {
    const uint8_t note = ledPendingNotes_[0];
    const uint8_t velocity = ledPendingVelocities_[0];
    ledPendingCount_--;
    for (size_t i = 0; i < ledPendingCount_; ++i) {
      ledPendingNotes_[i] = ledPendingNotes_[i + 1];
      ledPendingVelocities_[i] = ledPendingVelocities_[i + 1];
    }

    paceDroidUsbHostBeforeSend();
    if (velocity > 0) {
      usbHostMIDI.sendNoteOn(note, velocity, ch);
    } else {
      usbHostMIDI.sendNoteOn(note, 0, ch);
    }
    serviceUsbHostAfterOutboundPacket();
    drained++;
  }
}

void MidiHandler::setDroidMotorOutboundPriority(bool active) {
  droidMotorOutboundPriority_ = active;
}

void MidiHandler::serviceUsbHostAfterOutboundPacket() {
  if (!usbHostReady_) {
    return;
  }
  usbHost.Task();
  markDroidUsbHostSent();
}

void MidiHandler::sendMidiThru(byte type, byte channel, byte data1, byte data2) {
  switch (type) {
    case midi::NoteOn:
      if (outputUSB) usbMIDI.sendNoteOn(data1, data2, channel);
      if (outputSerial) MIDIserial.sendNoteOn(data1, data2, channel);
      if (canSendUsbHostMidi()) usbHostMIDI.sendNoteOn(data1, data2, channel);
      break;
    case midi::NoteOff:
      if (outputUSB) usbMIDI.sendNoteOff(data1, data2, channel);
      if (outputSerial) MIDIserial.sendNoteOff(data1, data2, channel);
      if (canSendUsbHostMidi()) usbHostMIDI.sendNoteOff(data1, data2, channel);
      break;
    case midi::ControlChange:
      if (outputUSB) usbMIDI.sendControlChange(data1, data2, channel);
      if (outputSerial) MIDIserial.sendControlChange(data1, data2, channel);
      if (canSendUsbHostMidi()) usbHostMIDI.sendControlChange(data1, data2, channel);
      break;
    case midi::PitchBend: {
      const int16_t pitchValue = MidiConfig::Pitchbend::logicalToWireSigned(
          static_cast<int16_t>(((data2 << 7) | data1) - 8192));
      if (outputUSB) usbMIDI.sendPitchBend(pitchValue, channel);
      if (outputSerial) MIDIserial.sendPitchBend(pitchValue, channel);
      if (canSendUsbHostMidi()) usbHostMIDI.sendPitchBend(pitchValue, channel);
      break;
    }
    case midi::AfterTouchChannel:
      if (outputUSB) usbMIDI.sendAfterTouch(data1, channel);
      if (outputSerial) MIDIserial.sendAfterTouch(data1, channel);
      if (canSendUsbHostMidi()) usbHostMIDI.sendAfterTouch(data1, channel);
      break;
    case midi::ProgramChange:
      if (outputUSB) usbMIDI.sendProgramChange(data1, channel);
      if (outputSerial) MIDIserial.sendProgramChange(data1, channel);
      if (canSendUsbHostMidi()) usbHostMIDI.sendProgramChange(data1, channel);
      break;
    default:
      break;
  }
}

// --- Individual Message Handlers ---
void MidiHandler::handleNoteOn(byte channel, byte note, byte velocity, uint32_t tickNow) {
  // Bar/step buttons (notes 0-15, 17-24 on ch16) go to BarStepButtonHandler, not MidiButtonManager
  if (barStepButtonHandler.isBarStepButtonNote(channel, note)) {
    barStepButtonHandler.handleMidiNote(channel, note, velocity, true);
    return;
  }
  if (channel == MidiConfig::Channels::TRACK_SELECT) {
    midiButtonManager.handleMidiNote(channel, note, velocity, true);
  }
  if (!isControlChannel(channel)) {
  trackManager.getSelectedTrack().noteOn(channel, note, velocity, tickNow);
  }
}

void MidiHandler::handleNoteOff(byte channel, byte note, byte velocity, uint32_t tickNow) {
  if (barStepButtonHandler.isBarStepButtonNote(channel, note)) {
    barStepButtonHandler.handleMidiNote(channel, note, velocity, false);
    return;
  }
  if (channel == MidiConfig::Channels::TRACK_SELECT) {
    midiButtonManager.handleMidiNote(channel, note, velocity, false);
  }
  if (!isControlChannel(channel)) {
  trackManager.getSelectedTrack().noteOff(channel, note, velocity, tickNow);
  }
}

void MidiHandler::handleControlChange(byte channel, byte control, byte value, uint32_t tickNow) {
  if (!looperState.isLoadSaveModeActive()) {
    controlSurfaceManager.handleMidiCC(channel, control, value);
  }

  // Route to track recording (skip control channels 13-16)
  if (!isControlChannel(channel)) {
  trackManager.getSelectedTrack().recordMidiEvents(midi::ControlChange, channel, control, value, tickNow);
  }
}

void MidiHandler::handlePitchBend(byte channel, int pitchValue, uint32_t tickNow) {
  const int16_t signedPitchValue =
      MidiConfig::Pitchbend::unsignedToLogical(static_cast<uint16_t>(pitchValue));

  if (!looperState.isLoadSaveModeActive()) {
    controlSurfaceManager.handleMidiPitchbend(channel, signedPitchValue);
  }

  // Route to track recording (skip control channels 13-16)
  if (!isControlChannel(channel)) {
    trackManager.getSelectedTrack().recordMidiEvents(
        midi::PitchBend, channel, pitchValue & 0x7F, (pitchValue >> 7) & 0x7F, tickNow);
  }
}

void MidiHandler::handleAfterTouch(byte channel, byte pressure, uint32_t tickNow) {
  // Not yet implemented
}

void MidiHandler::handleProgramChange(byte channel, byte program, uint32_t tickNow) {
  // Route to track recording (skip control channels 13-16)
  if (!isControlChannel(channel)) {
  trackManager.getSelectedTrack().recordMidiEvents(midi::ProgramChange, channel, program, 0, tickNow);
  }
}

void MidiHandler::handleMidiStart() {
  clockManager.onMidiStart();
  midiButtonActions.syncTransportLed();
}

void MidiHandler::handleMidiStop() {
  trackManager.handleTransportStop();  // Handles RECORDING->stopped, OVERDUBBING->stopped, PLAYING->stopped, ARMED->empty
  clockManager.onMidiStop();
  midiButtonActions.syncTransportLed();
}

void MidiHandler::handleMidiContinue() {
  clockManager.requestTransitionTo(CLOCK_EXTERNAL);
  clockManager.setLastMidiClockTime(micros());
}

// --- MIDI Output ---
void MidiHandler::sendMidiEvent(const MidiEvent& event) {
    if (event.type != midi::Clock) {
        SC_MIDI_OUT_EVENT(event);
    }
    // Route and send the event to both USB and Serial as appropriate
    switch (event.type) {
        case midi::NoteOn: {
            if (outputUSB) usbMIDI.sendNoteOn(event.data.noteData.note, event.data.noteData.velocity, event.channel);
            if (outputSerial) MIDIserial.sendNoteOn(event.data.noteData.note, event.data.noteData.velocity, event.channel);
            if (canSendUsbHostMidi()) {
                const bool queueAsLed =
                    isLedChannel(event.channel) && !droidMotorOutboundPriority_;
                if (queueAsLed) {
                    queueLedUsbHostFeedback(event.data.noteData.note, event.data.noteData.velocity);
                } else {
                    paceDroidUsbHostBeforeSend();
                    usbHostMIDI.sendNoteOn(event.data.noteData.note, event.data.noteData.velocity, event.channel);
                    serviceUsbHostAfterOutboundPacket();
                }
            }
            if (!isLedChannel(event.channel) || droidMotorOutboundPriority_) {
#if !defined(SESSION_CAPTURE)
                logger.log(CAT_MIDI, LOG_DEBUG,
                    "OUT NoteOn  usb=%d ser=%d host=%d ch=%u note=%u vel=%u",
                    outputUSB ? 1 : 0, outputSerial ? 1 : 0, usbHostMIDI ? 1 : 0,
                    (unsigned)event.channel, (unsigned)event.data.noteData.note,
                    (unsigned)event.data.noteData.velocity);
#endif
            }
            // Log LED updates (ch15: 0-31 tick/16th, 40-47 bar, 50-67 track/loop)
            if (event.channel == MidiConfig::Led::CHANNEL && (event.data.noteData.note <= 31 || (event.data.noteData.note >= 40 && event.data.noteData.note <= 47) || (event.data.noteData.note >= 50 && event.data.noteData.note <= 67))) {
                logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED NoteOn ch=%d note=%d vel=%d -> usb=%d serial=%d usbHost=%d",
                    event.channel, event.data.noteData.note, event.data.noteData.velocity, outputUSB ? 1 : 0, outputSerial ? 1 : 0, usbHostMIDI ? 1 : 0);
            }
            break;
        }
        case midi::NoteOff: {
            if (outputUSB) usbMIDI.sendNoteOff(event.data.noteData.note, event.data.noteData.velocity, event.channel);
            if (outputSerial) MIDIserial.sendNoteOff(event.data.noteData.note, event.data.noteData.velocity, event.channel);
            if (canSendUsbHostMidi()) {
                const bool queueAsLed =
                    isLedChannel(event.channel) && !droidMotorOutboundPriority_;
                if (queueAsLed) {
                    queueLedUsbHostFeedback(event.data.noteData.note, 0);
                } else {
                    paceDroidUsbHostBeforeSend();
                    usbHostMIDI.sendNoteOff(event.data.noteData.note, event.data.noteData.velocity, event.channel);
                    serviceUsbHostAfterOutboundPacket();
                }
            }
            if (!isLedChannel(event.channel) || droidMotorOutboundPriority_) {
#if !defined(SESSION_CAPTURE)
                logger.log(CAT_MIDI, LOG_DEBUG,
                    "OUT NoteOff usb=%d ser=%d host=%d ch=%u note=%u vel=%u",
                    outputUSB ? 1 : 0, outputSerial ? 1 : 0, usbHostMIDI ? 1 : 0,
                    (unsigned)event.channel, (unsigned)event.data.noteData.note,
                    (unsigned)event.data.noteData.velocity);
#endif
            }
            if (event.channel == MidiConfig::Led::CHANNEL && (event.data.noteData.note <= 31 || (event.data.noteData.note >= 40 && event.data.noteData.note <= 47) || (event.data.noteData.note >= 50 && event.data.noteData.note <= 67))) {
                logger.log(CAT_MIDI_LED, LOG_DEBUG, "LED NoteOff ch=%d note=%d -> usb=%d serial=%d usbHost=%d",
                    event.channel, event.data.noteData.note, outputUSB ? 1 : 0, outputSerial ? 1 : 0, usbHostMIDI ? 1 : 0);
            }
            break;
        }
        case midi::ControlChange: {
            // Skip CC 123 (All Notes Off) on LED channels - preserves DROID button LEDs on USB
            const bool skipCc123OnLedCh = (event.data.ccData.cc == 123 && isLedChannel(event.channel));
            const bool droidFaderFeedbackOnly =
                canSendUsbHostMidi() &&
                isDroidFaderFeedbackOnlyControlChange(event.channel, event.data.ccData.cc);
            if (outputUSB && !skipCc123OnLedCh && !droidFaderFeedbackOnly) {
                usbMIDI.sendControlChange(event.data.ccData.cc, event.data.ccData.value, event.channel);
            }
            if (outputSerial && !droidFaderFeedbackOnly) {
                MIDIserial.sendControlChange(event.data.ccData.cc, event.data.ccData.value, event.channel);
            }
            if (canSendUsbHostMidi() && !skipCc123OnLedCh) {
                paceDroidUsbHostBeforeSend();
                usbHostMIDI.sendControlChange(event.data.ccData.cc, event.data.ccData.value, event.channel);
                serviceUsbHostAfterOutboundPacket();
            }
            break;
        }
        case midi::PitchBend: {
            const int16_t wirePitch =
                MidiConfig::Pitchbend::logicalToWireSigned(event.data.pitchBend);
            const bool droidFaderFeedbackOnly =
                canSendUsbHostMidi() && isDroidFaderFeedbackOnlyPitchbend(event.channel);
            if (outputUSB && !droidFaderFeedbackOnly) {
                usbMIDI.sendPitchBend(wirePitch, event.channel);
            }
            if (outputSerial && !droidFaderFeedbackOnly) {
                MIDIserial.sendPitchBend(wirePitch, event.channel);
            }
            if (canSendUsbHostMidi()) {
                paceDroidUsbHostBeforeSend();
                usbHostMIDI.sendPitchBend(wirePitch, event.channel);
                serviceUsbHostAfterOutboundPacket();
            }
            break;
        }
        case midi::AfterTouchChannel:
            if (outputUSB) usbMIDI.sendAfterTouch(event.data.channelPressure, event.channel);
            if (outputSerial) MIDIserial.sendAfterTouch(event.data.channelPressure, event.channel);
            if (canSendUsbHostMidi()) usbHostMIDI.sendAfterTouch(event.data.channelPressure, event.channel);
            break;
        case midi::ProgramChange:
            if (outputUSB) usbMIDI.sendProgramChange(event.data.program, event.channel);
            if (outputSerial) MIDIserial.sendProgramChange(event.data.program, event.channel);
            if (canSendUsbHostMidi()) {
                paceDroidUsbHostBeforeSend();
                usbHostMIDI.sendProgramChange(event.data.program, event.channel);
                serviceUsbHostAfterOutboundPacket();
            }
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

void MidiHandler::serviceUsbHostAfterLedPacket() {
  serviceUsbHostAfterOutboundPacket();
}

void MidiHandler::sendLedFeedbackNoteOn(uint8_t note, uint8_t velocity) {
  SC_LED_OUT(true, note, velocity);
  constexpr uint8_t ch = MidiConfig::Led::CHANNEL;
  if (outputUSB) usbMIDI.sendNoteOn(note, velocity, ch);
  if (outputSerial) MIDIserial.sendNoteOn(note, velocity, ch);
  if (canSendUsbHostMidi()) {
    queueLedUsbHostFeedback(note, velocity);
  }
}

void MidiHandler::sendLedFeedbackNoteOff(uint8_t note) {
  SC_LED_OUT(false, note, 0);
  constexpr uint8_t ch = MidiConfig::Led::CHANNEL;
  if (outputUSB) usbMIDI.sendNoteOff(note, 0, ch);
  if (outputSerial) MIDIserial.sendNoteOff(note, 0, ch);
  if (canSendUsbHostMidi()) {
    queueLedUsbHostFeedback(note, 0);
  }
}

void MidiHandler::sendControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    sendMidiEvent(MidiEvent::ControlChange(0, channel, control, value));
}

void MidiHandler::sendPitchBend(uint8_t channel, int16_t value) {
    sendMidiEvent(MidiEvent::PitchBend(0, channel, value));
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
  logger.log(CAT_MIDI, LOG_INFO, "OUT MIDI Start (DIN=%d USB=%d)", outputSerial ? 1 : 0, outputUSB ? 1 : 0);
}

void MidiHandler::sendStop() {
  if (outputUSB) usbMIDI.sendRealTime(usbMIDI.Stop);
  if (outputSerial) MIDIserial.sendRealTime(midi::Stop);
  logger.log(CAT_MIDI, LOG_INFO, "OUT MIDI Stop (DIN=%d USB=%d)", outputSerial ? 1 : 0, outputUSB ? 1 : 0);
}

bool MidiHandler::isOutputSerialEnabled() const {
  return outputSerial;
}

// --- Static USB Host MIDI Callbacks ---
void MidiHandler::usbHostNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (instance) {
    instance->handleMidiMessage(midi::NoteOn, channel, note, velocity, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (instance) {
    instance->handleMidiMessage(midi::NoteOff, channel, note, velocity, SOURCE_USB_HOST);
  }
}

void MidiHandler::usbHostControlChange(uint8_t channel, uint8_t control, uint8_t value) {
  if (instance) {
    // Single path: handleControlChange routes to ControlSurfaceManager + track recording (no duplicate CC).
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
    // USBHost_t36 delivers signed pitch. Re-splitting as bytes and subtracting 8192 again
    // in handlePitchBend corrupts the value; encode logical pitch to unsigned 14-bit first.
    const uint16_t unsignedPitch = MidiConfig::Pitchbend::logicalToUnsigned(
        static_cast<int16_t>(pitch));
    instance->handleMidiMessage(midi::PitchBend, channel,
                                unsignedPitch & 0x7F, (unsignedPitch >> 7) & 0x7F,
                                SOURCE_USB_HOST);
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
