//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDIHANDLER_H
#define MIDIHANDLER_H

#include <Arduino.h>
#include "MidiEvent.h"
#include <USBHost_t36.h>
#include "Utils/MidiIngressReceipt.h"

#ifndef MIDI_INPUT_CONTINUATION_QUALIFICATION
#define MIDI_INPUT_CONTINUATION_QUALIFICATION 0
#endif
#if MIDI_INPUT_CONTINUATION_QUALIFICATION != 0 && MIDI_INPUT_CONTINUATION_QUALIFICATION != 1
#error "input continuation qualification must be zero or one"
#endif
#if MIDI_INPUT_CONTINUATION_QUALIFICATION && !defined(PIO_UNIT_TEST_NATIVE)
#error "input continuation qualification is native-only until target timing/main integration is approved"
#endif

enum InputSource {
  SOURCE_USB,
  SOURCE_SERIAL,
  SOURCE_USB_HOST
};

#if defined(PIO_UNIT_TEST_NATIVE)
/// Native test boundary: records every event that reaches the wire, so playback
/// emission can be asserted end to end without a device. Compiled only for the
/// host test build; firmware never sees it.
struct MidiSendCapture {
  static constexpr size_t kCapacity = 8192;
  static void reset();
  static size_t count();
  static MidiEvent at(size_t index);
  static void record(const MidiEvent& event);
};
#endif

/**
 * @class MidiHandler
 * @brief Central MIDI input/output router and dispatcher.
 *
 * Reads incoming MIDI messages from USB or Serial sources, parses them, and
 * dispatches to internal handlers (note on/off, control change, pitch bend,
 * aftertouch, program change, clock, start/stop/continue). Output methods
 * provide a unified API (sendMidiEvent/sendNoteOn/sendClock/etc.) with USB and
 * Serial routing controlled by internal outputUSB/outputSerial flags.
 */
class MidiHandler {
public:
  MidiHandler();

  // --- Initialization ---
  void setup();
  /// Start USB Host MIDI after workspace load and deferred slot restore (avoids SDIO contention).
  void beginUsbHost();
  bool isUsbHostReady() const { return usbHostReady_; }

  // --- Input Handling ---
  void handleMidiInput();
  void handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source
#if MIDI_INGRESS_RECEIPTS_ENABLED
                        , const MidiIngressReceipt& receipt = {}
#endif
                        );
#if defined(PIO_UNIT_TEST_NATIVE)
  bool qualificationInputContinuationReady() const;
  uint16_t qualificationInputPendingCount() const;
  uint64_t qualificationInputGeneration() const;
  const void* qualificationInputStorageIdentity() const;
  size_t qualificationInputStateBytes() const;
  void setQualificationInputPrewarmDeniedForTests(bool denied); // Native allocator fault boundary only.
#if MIDI_INGRESS_RECEIPTS_ENABLED
  using IngressDispatchObserver = void (*)(byte, byte, byte, byte, InputSource, const MidiIngressReceipt&);
  void setIngressDispatchObserverForTests(IngressDispatchObserver observer);
#endif
#endif

  // --- MIDI Output ---
  // Use the new MidiEvent constructors for all MIDI output
  void sendMidiEvent(const MidiEvent& event); // Unified event-based output
  /// Playback boundary output: USB/serial is immediate; DROID host feedback is queued.
  void sendRealtimePlaybackEvent(const MidiEvent& event);

  void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);

  /// Channel 15 only: LED feedback for DROID over USB host — per-packet Task() + padding;
  /// host path uses Note-On velocity 0 instead of Note-Off (many controllers swallow Off).
  void sendLedFeedbackNoteOn(uint8_t note, uint8_t velocity);
  void sendLedFeedbackNoteOff(uint8_t note);
  void sendControlChange(uint8_t channel, uint8_t control, uint8_t value);
  void sendPitchBend(uint8_t channel, int16_t value);
  void sendProgramChange(uint8_t channel, uint8_t program);

  // --- Clock / Transport Output ---
  void sendClock();
  void sendStart();
  void sendStop();

  /** Drain paced LED feedback queue to DROID USB host (call from main loop). */
  void processDroidUsbHostOutbound();

  /** When true, LED queue drain is paused and fader motor outbound skips MIN_PACKET_GAP pacing. */
  void setDroidMotorOutboundPriority(bool active);

  bool isOutputSerialEnabled() const;

  // --- Static USB Host MIDI Callbacks ---
  static void usbHostNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  static void usbHostNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
  static void usbHostControlChange(uint8_t channel, uint8_t control, uint8_t value);
  static void usbHostProgramChange(uint8_t channel, uint8_t program);  
  static void usbHostPitchChange(uint8_t channel, int pitch);
  static void usbHostAfterTouchChannel(uint8_t channel, uint8_t pressure);
  static void usbHostClock();
  static void usbHostStart();
  static void usbHostStop();
  static void usbHostContinue();

private:
  bool outputUSB = true;
  bool outputSerial = true;

  // USB Host objects
  USBHost usbHost;
  USBHub hub1;
  MIDIDevice_BigBuffer usbHostMIDI;

  // Static instance pointer for callbacks
  static MidiHandler* instance;

  // --- Helper Functions ---
  bool isControlChannel(byte channel);
  bool isLedChannel(byte channel);
  void sendMidiThru(byte type, byte channel, byte data1, byte data2);
#if defined(MIDI_USB_FADER_PROBE_PASSTHROUGH)
  void mirrorUsbFaderProbePassthrough(byte type, byte channel, byte data1, byte data2);
#endif
  void serviceUsbHostAfterLedPacket();
  void serviceUsbHostAfterOutboundPacket();
  void paceDroidUsbHostBeforeSend();
  void markDroidUsbHostSent();
  void queueLedUsbHostFeedback(uint8_t note, uint8_t velocityOrZero);
  /// Enqueue one outbound DROID host event. **ISR-reachable** (playback events are queued from the MIDI tick
  /// ISR), so it stays out of flash and does no logging, no scanning and no formatting: the queue's ordering
  /// and coalescing live in `Utils/DroidHostFeedbackRing.h`, and what it cannot queue is counted, not printed.
  bool queueDeferredDroidHostEvent(const MidiEvent& event);
  void sendDeferredDroidHostEvent(uint8_t type, uint8_t channel, uint8_t data1,
                                  uint8_t data2, int16_t pitchBend);

  bool canSendUsbHostMidi() { return usbHostReady_ && static_cast<bool>(usbHostMIDI); }

  uint32_t lastDroidUsbHostSendMicros_ = 0;
  uint8_t ledPendingNotes_[MidiConfig::DroidUsbHost::LED_PENDING_MAX];
  uint8_t ledPendingVelocities_[MidiConfig::DroidUsbHost::LED_PENDING_MAX];
  size_t ledPendingCount_ = 0;
  bool droidMotorOutboundPriority_ = false;
  bool usbHostReady_ = false;
  bool deferDroidHost_ = false;

  // --- Message Handlers ---
  void handleNoteOn(byte channel, byte note, byte velocity, uint32_t tickNow);
  void handleNoteOff(byte channel, byte note, byte velocity, uint32_t tickNow);
  void handleControlChange(byte channel, byte control, byte value, uint32_t tickNow);
  void handlePitchBend(byte channel, int pitchValue, uint32_t tickNow);
  void handleAfterTouch(byte channel, byte pressure, uint32_t tickNow);
  void handleProgramChange(byte channel, byte program, uint32_t tickNow);
  void handleMidiStart();
  void handleMidiStop();
  void handleMidiContinue();
};

extern MidiHandler midiHandler;

#endif // MIDIHANDLER_H
