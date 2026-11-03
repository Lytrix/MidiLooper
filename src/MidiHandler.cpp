//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include "Utils/LaneGapSpans.h"
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
#include "Utils/DroidHostFeedbackRing.h"
#include "Utils/MidiBatchComposition.h"
#include "Utils/MidiNoteName.h"
#include "Utils/MidiDispatchOrder.h"
#include "Utils/RuntimeTimingTelemetry.h"

#if MIDI_INPUT_CONTINUATION_QUALIFICATION
#include "Utils/MidiInputContinuation.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include <new>
#endif

namespace {

/// The feedback queue's state (timing-path review, items 2/3). Two placement decisions, both forced by this
/// tree's RAM1 rule rather than chosen:
///   - the **event store** stays in `EXTMEM` (PSRAM) exactly as the queue it replaces did -- 256 entries is
///     3 KB and nothing about ordering wants it closer;
///   - the **ring's own state** is `DMAMEM`, because it is mutable state and the rule the capture-side
///     counters already follow is that mutable state must not grow DTCM (`PersistenceWorkItemPersist.cpp`
///     records the same reason: `scripts/ram1_budget.json`'s variables line is checked byte-for-byte).
/// `DroidHostFeedbackRing` owns no clock and no policy beyond the ordering rule, so the key and the timestamp
/// are the caller's, computed below.
constexpr uint16_t kDeferredDroidHostCapacity = 256;
EXTMEM DroidHostFeedbackEvent gDeferredDroidHostQueue[kDeferredDroidHostCapacity];
DMAMEM uint32_t gReportedDroidFeedbackOverflow = 0;
DMAMEM DroidHostFeedbackRing gDeferredDroidHostRing;

/// Declared here because the predicate itself lives with the other DROID-channel predicates below (same
/// translation unit, same anonymous namespace): the key is built from the *same* predicates, so it can never
/// claim a target the send path does not treat as fader state.
bool isDroidFaderFeedbackOnlyControlChange(uint8_t channel, uint8_t cc);

/// The coalescing key for a **state** (fader-feedback) event, or 0 for a gesture. It is the caller's job
/// because "the same target" is a `MidiConfig` question (`isDroidFaderFeedbackOnlyPitchbend` /
/// `...ControlChange`), and the ring's job to honour; the pairs are enumerated rather than hashed so two
/// different targets can never share a key.
uint8_t droidFeedbackCoalesceKey(uint8_t type, uint8_t channel, uint8_t data1) {
  if (type == midi::PitchBend) {
    if (channel == MidiConfig::Fader::COARSE_CHANNEL) return 1;
    if (channel == MidiConfig::Fader::SELECT_CHANNEL) return 2;
    return 0;
  }
  if (type != midi::ControlChange || !isDroidFaderFeedbackOnlyControlChange(channel, data1)) {
    return 0;
  }
  if (channel == MidiConfig::Fader::FINE_CHANNEL) {
    return data1 == MidiConfig::Fader::FINE_CC ? 3 : 4;
  }
  return data1 == MidiConfig::LoopEdit::LENGTH_CC_NUMBER ? 5 : 6;
}

}  // namespace

MIDI_CREATE_INSTANCE(HardwareSerial, Serial8, MIDIserial);  // Teensy Serial8 for 5-pin DIN MIDI

MidiHandler midiHandler;  // Global instance
MidiHandler* MidiHandler::instance = nullptr;  // Static instance pointer

namespace {
#if defined(MIDI_USB_FADER_PROBE_PASSTHROUGH)
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
#endif

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
    // Only the loop-length CC remains on this channel; the tempo fader is a ch15 pitchbend now.
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

#if MIDI_INGRESS_RECEIPTS_ENABLED
#define INPUT_RECEIPT_ARGUMENT(value) , value
#define INPUT_RECEIPT_PARAMETER , const MidiIngressReceipt& receipt
#else
#define INPUT_RECEIPT_ARGUMENT(value)
#define INPUT_RECEIPT_PARAMETER
#endif
#if defined(PIO_UNIT_TEST_NATIVE) && MIDI_INGRESS_RECEIPTS_ENABLED
MidiHandler::IngressDispatchObserver ingressDispatchObserver = nullptr;
#endif

constexpr size_t kMidiInputBatchMax = 128;
constexpr size_t kPostOverdubDrainMidiInputBatchMax = 32;

struct MidiInputMsg {
  byte type;
  byte channel;
  byte data1;
  byte data2;
  InputSource source;
#if MIDI_INGRESS_RECEIPTS_ENABLED
  MidiIngressReceipt ingress;
#endif
};
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
// One MidiHandler-owned native qualification role; never target DTCM state.
MidiInputContinuation* qualifiedInput = nullptr;
bool qualificationPrewarmDenied = false;

void pollQualifiedInput(MidiHandler& owner, bool hostReady, USBHost& host, MIDIDevice_BigBuffer& hostMidi) {
  auto& state = *qualifiedInput;
  if (state.acquiring || state.dispatching) return; // Outer poll/handler owns completion.
  if (!state.active) {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
      const uint8_t route = state.nextRoute;
      const uint8_t cap = trackManager.anyPlayingMidiDrainAfterOverdubStop()
          ? kPostOverdubDrainMidiInputBatchMax : kMidiInputBatchMax;
      if (!state.begin(route, cap, clockManager.getPlaybackDeadlineClock().epoch, micros())) return;
      state.acquiring = true;
      if (route == SOURCE_USB) {
        while (state.count < cap && usbMIDI.read())
          if (!state.append({usbMIDI.getType(), usbMIDI.getChannel(), usbMIDI.getData1(), usbMIDI.getData2()}, micros()
              INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB)))) std::abort();
      } else if (route == SOURCE_SERIAL) {
        while (state.count < cap && MIDIserial.read())
          if (!state.append({MIDIserial.getType(), MIDIserial.getChannel(), MIDIserial.getData1(), MIDIserial.getData2()}, micros()
              INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_SERIAL)))) std::abort();
      } else if (hostReady) {
        state.acquiringHost = true;
        host.Task();
        size_t reads = 0;
        while (reads < cap && state.count < cap && hostMidi.read()) ++reads;
        state.acquiringHost = false;
      }
      state.acquiring = false;
      state.active = state.count != 0;
      if (state.active) break;
    }
  }
  MidiInputContinuation::Row row{};
  if (state.next(row)) {
    owner.handleMidiMessage(row.type, row.channel, row.data1, row.data2,
        static_cast<InputSource>(state.route) INPUT_RECEIPT_ARGUMENT(state.ingressReceiptForDispatch()));
    state.acknowledge();
  }
}
#endif


// External sequencers (e.g. BeatStep Pro) may send NoteOn before MIDI Start on the
// same downbeat. Process Start/Stop/Continue first within each poll batch so armed
// recording starts before channel messages in that batch are recorded.
// Clock stays FIFO with channel messages (RC-C A): Clock, NoteOn, Clock must not
// become Clock, Clock, NoteOn — note ticks come from getCurrentTick() at process time.
#if defined(LANE_GAP_MIDI_SPLIT)
// C5.3b MIDI input split (owner ruling 2026-11-03, measurement image only): per transport phase and message type,
// counts, total and longest dispatch; per `handleMidiInput` invocation, the totals and the longest message; the 16 worst
// invocations. RAM2; printed only after the transport stops (`dumpMidiSpans`).
namespace {
enum MidiKind : uint8_t { KindClock = 0, KindNote, KindCc, KindTransport, KindOther, KindCount };
enum MidiPhase : uint8_t { PhasePlay = 0, PhaseRecord, PhaseOverdub, PhasePostStop, PhaseCount };
struct MidiKindStat { uint32_t count; uint64_t totalUs; uint32_t maxUs; };
struct MidiInvocation {
  uint32_t totalUs, usbUs, dinUs, hostTaskUs, hostDrainUs, maxMsgUs;
  uint16_t counts[KindCount];
  uint16_t hostReads;
  uint8_t maxMsgKind, phase;
};
struct MidiSpans {
  MidiKindStat stat[PhaseCount][KindCount];
  uint32_t invocations[PhaseCount];
  MidiInvocation current;
  MidiInvocation worst[16];
};
DMAMEM MidiSpans sMidiSpans;
uint8_t midiKindOf(uint8_t type) {
  if (type == midi::Clock) return KindClock;
  if (type == midi::NoteOn || type == midi::NoteOff) return KindNote;
  if (type == midi::ControlChange) return KindCc;
  if (type == midi::Start || type == midi::Stop || type == midi::Continue) return KindTransport;
  return KindOther;
}
uint8_t midiPhaseNow() {
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    const Track& t = trackManager.getTrack(i);
    if (t.isOverdubbing()) return PhaseOverdub;
    if (t.isRecording()) return PhaseRecord;
  }
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i)
    if (trackManager.getTrack(i).hasPendingDeferredOverdubStop()) return PhasePostStop;
  return trackManager.anyPlayingMidiDrainAfterOverdubStop() ? PhasePostStop : PhasePlay;
}
/// One `handleMidiInput` invocation, finalized on every return path.
struct MidiInvocationScope {
  uint32_t startUs;
  MidiInvocationScope() : startUs(micros()) {
    sMidiSpans.current = MidiInvocation{};
    sMidiSpans.current.phase = midiPhaseNow();
  }
  ~MidiInvocationScope() {
    auto& m = sMidiSpans;
    m.current.totalUs = micros() - startUs;
    ++m.invocations[m.current.phase];
    uint8_t least = 0;
    for (uint8_t i = 1; i < 16; ++i) if (m.worst[i].totalUs < m.worst[least].totalUs) least = i;
    if (m.current.totalUs > m.worst[least].totalUs) m.worst[least] = m.current;
  }
};
void noteMidiMessage(uint8_t type, uint32_t us) {
  auto& m = sMidiSpans;
  const uint8_t kind = midiKindOf(type);
  auto& st = m.stat[m.current.phase][kind];
  ++st.count;
  st.totalUs += us;
  if (us > st.maxUs) st.maxUs = us;
  if (m.current.counts[kind] != UINT16_MAX) ++m.current.counts[kind];
  if (us > m.current.maxMsgUs) { m.current.maxMsgUs = us; m.current.maxMsgKind = kind; }
}
}  // namespace

void resetMidiSpansImpl() { sMidiSpans = MidiSpans{}; }

FLASHMEM void dumpMidiSpansImpl() {
  auto& m = sMidiSpans;
  static const char* const kPhase[PhaseCount] = {"play", "record", "overdub", "post_stop"};
  for (uint8_t p = 0; p < PhaseCount; ++p) {
    if (m.invocations[p] == 0) continue;
    Serial.printf("#CAP,%lu,DIAG,midi_spans,phase=%s,invocations=%lu,kinds=", (unsigned long)micros(), kPhase[p],
                  (unsigned long)m.invocations[p]);
    for (uint8_t k = 0; k < KindCount; ++k)
      Serial.printf("%s%lu:%llu:%lu", k ? "/" : "", (unsigned long)m.stat[p][k].count,
                    (unsigned long long)m.stat[p][k].totalUs, (unsigned long)m.stat[p][k].maxUs);
    Serial.println();
  }
  for (const auto& w : m.worst) {
    if (w.totalUs == 0) continue;
    Serial.printf("#CAP,%lu,DIAG,midi_spans,invocation,phase=%s,us=%lu,usb=%lu,din=%lu,host_task=%lu,host_drain=%lu,"
                  "host_reads=%u,max_msg_us=%lu,max_kind=%u,counts=%u/%u/%u/%u/%u\r\n",
                  (unsigned long)micros(), kPhase[w.phase < PhaseCount ? w.phase : 0], (unsigned long)w.totalUs,
                  (unsigned long)w.usbUs, (unsigned long)w.dinUs, (unsigned long)w.hostTaskUs,
                  (unsigned long)w.hostDrainUs, unsigned(w.hostReads), (unsigned long)w.maxMsgUs, unsigned(w.maxMsgKind),
                  unsigned(w.counts[0]), unsigned(w.counts[1]), unsigned(w.counts[2]), unsigned(w.counts[3]),
                  unsigned(w.counts[4]));
  }
}
#endif

#if MIDI_INGRESS_RECEIPTS_ENABLED || defined(LANE_GAP_MIDI_SPLIT)
// Receipt-profile input work only, never the tick's output path. (The measurement image's MIDI split also places it in
// flash, which frees ITCM rather than using it.)
FLASHMEM
#endif
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
#if defined(LANE_GAP_MIDI_SPLIT)
    const uint32_t messageStartUs = micros();
#endif
    midiHandler.handleMidiMessage(msg.type, msg.channel, msg.data1, msg.data2, msg.source INPUT_RECEIPT_ARGUMENT(msg.ingress));
#if defined(LANE_GAP_MIDI_SPLIT)
    noteMidiMessage(msg.type, micros() - messageStartUs);
#endif
  }
}

}  // namespace

#if defined(LANE_GAP_MIDI_SPLIT)
void LaneGapSpans::resetMidiSpans() { resetMidiSpansImpl(); }
FLASHMEM void LaneGapSpans::dumpMidiSpans() { dumpMidiSpansImpl(); }
#endif

void MidiHandler::setup() {
#if MIDI_INGRESS_RECEIPTS_ENABLED
  MidiIngress::setup(&Serial8, &usbHostMIDI);
#endif
  Serial8.begin(31250);  // DIN MIDI out/in (explicit before MIDI library wraps Serial8)
  MIDIserial.begin(MidiConfig::CHANNEL_OMNI);  // Listen to all channels
  // Attach the DROID feedback queue's storage once, before any event can be queued (the queue is only
  // reachable while the USB host is ready, which `beginUsbHost` establishes later still).
  gDeferredDroidHostRing.init(gDeferredDroidHostQueue, kDeferredDroidHostCapacity);
  instance = this;  // Set static instance pointer for callbacks
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
  if (qualifiedInput == nullptr) {
    CommittedChunkIdAllocator<MidiInputContinuation> allocator;
    if (!qualificationPrewarmDenied)
      if (auto* memory = allocator.allocate(1)) qualifiedInput = new (memory) MidiInputContinuation;
  }
  if (qualifiedInput) qualifiedInput->resetForSetup(); // Explicit cold session, never transport cancellation.
#endif
}

/// FLASH-resident: the USB-host bring-up runs from `setup`/the main loop and is never on the tick ISR path,
/// which is the only partition this file needs (the ISR-reachable half -- `sendMidiEvent`, the realtime sends,
/// the feedback enqueue -- stays in ITCM). Same reason as the input path below.
FLASHMEM void MidiHandler::beginUsbHost() {
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

/// FLASH-resident: every caller is the main loop (`main.cpp`), so this belongs to the headroom half of the
/// partition rather than to ITCM. The tick ISR reaches this file only through the playback sends, and the
/// functions it can reach are the ones that stay in ITCM.
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
bool MidiHandler::qualificationInputContinuationReady() const { return qualifiedInput && qualifiedInput->generation != 0; }
uint16_t MidiHandler::qualificationInputPendingCount() const { return qualifiedInput ? qualifiedInput->pending() : 0; }
uint64_t MidiHandler::qualificationInputGeneration() const { return qualifiedInput ? qualifiedInput->generation : 0; }
const void* MidiHandler::qualificationInputStorageIdentity() const { return qualifiedInput; }
size_t MidiHandler::qualificationInputStateBytes() const { return sizeof(MidiInputContinuation); }
void MidiHandler::setQualificationInputPrewarmDeniedForTests(bool denied) { qualificationPrewarmDenied = denied; }
#elif defined(PIO_UNIT_TEST_NATIVE)
bool MidiHandler::qualificationInputContinuationReady() const { return false; }
uint16_t MidiHandler::qualificationInputPendingCount() const { return 0; }
uint64_t MidiHandler::qualificationInputGeneration() const { return 0; }
const void* MidiHandler::qualificationInputStorageIdentity() const { return nullptr; }
size_t MidiHandler::qualificationInputStateBytes() const { return 0; }
void MidiHandler::setQualificationInputPrewarmDeniedForTests(bool) {}
#endif

FLASHMEM void MidiHandler::handleMidiInput() {
  LANE_GAP_SPAN(Midi);
#if defined(LANE_GAP_MIDI_SPLIT)
  MidiInvocationScope midiInvocation;
  uint32_t splitStartUs = micros();
#endif
#if RUNTIME_TIMING_ENABLED
  const uint32_t inputEnterUs = micros();
  RUNTIME_TIMING_NOTE_MIDI_INPUT_ENTER(inputEnterUs);
  // S7.5a envelope instrument: the playback tick this input service started at. Each captured event compares its
  // own stamped tick with this one (`noteCaptureStampTick`): the capture timestamp error, in ticks.
  RUNTIME_TIMING_NOTE_INPUT_SERVICE_TICK(clockManager.getCurrentTick());
#endif
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
  if (qualificationInputContinuationReady()) {
    pollQualifiedInput(*this, usbHostReady_, usbHost, usbHostMIDI);
    RUNTIME_TIMING_NOTE_MIDI_INPUT_EXIT(micros());
    return;
  }
#endif

  MidiInputMsg batch[kMidiInputBatchMax];
  size_t count = 0;
  // Post-overdub PLAYING drain: reduce one-turn USB dispatch monopolization so
  // load/display slices and playback note deadlines are not delayed by 128-message bursts.
  const size_t midiInputBatchMax =
      trackManager.anyPlayingMidiDrainAfterOverdubStop()
          ? kPostOverdubDrainMidiInputBatchMax
          : kMidiInputBatchMax;

  // --- USB MIDI Input (transport before notes within each poll) ---
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_BEGIN_USB_DEVICE_NESTED();
  uint32_t segmentStartUs = micros();
  uint8_t usbTypes[kMidiInputBatchMax];
#endif
  while (count < midiInputBatchMax && usbMIDI.read()) {
    batch[count] = {usbMIDI.getType(), usbMIDI.getChannel(), usbMIDI.getData1(),
                    usbMIDI.getData2(), SOURCE_USB INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB))};
#if RUNTIME_TIMING_ENABLED
    usbTypes[count] = batch[count].type;
#endif
    ++count;
  }
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_NOTE_USB_DEVICE_READ(micros() - segmentStartUs);
  const uint32_t dispatchStartUs = micros();
  const MidiBatchComposition::Summary usbComposition =
      MidiBatchComposition::summarize(usbTypes, count);
  RUNTIME_TIMING_NOTE_EXTERNAL_CLOCK_BATCH(
      dispatchStartUs, usbComposition.clockCount,
      clockManager.getMicrosPerTick() * Config::TICKS_PER_CLOCK);
#endif
  dispatchMidiBatch(batch, count);
#if defined(LANE_GAP_MIDI_SPLIT)
  sMidiSpans.current.usbUs = micros() - splitStartUs;
  splitStartUs = micros();
#endif
#if RUNTIME_TIMING_ENABLED
  const uint32_t dispatchUs = micros() - dispatchStartUs;
  RUNTIME_TIMING_NOTE_USB_DEVICE_DISPATCH(dispatchUs);
  RUNTIME_TIMING_NOTE_USB_DEVICE_BATCH_COMPOSITION(
      usbComposition.size, usbComposition.clockCount, usbComposition.nonClockCount,
      usbComposition.maxConsecutiveClocks, count == midiInputBatchMax, dispatchUs);
  RUNTIME_TIMING_COMMIT_USB_DEVICE_NESTED();
  RUNTIME_TIMING_NOTE_USB_DEVICE_DRAIN(micros() - segmentStartUs);
#endif

  // --- Serial MIDI Input (DIN) ---
  count = 0;
#if RUNTIME_TIMING_ENABLED
  segmentStartUs = micros();
#endif
  while (count < midiInputBatchMax && MIDIserial.read()) {
    batch[count++] = {MIDIserial.getType(), MIDIserial.getChannel(), MIDIserial.getData1(),
                      MIDIserial.getData2(), SOURCE_SERIAL INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_SERIAL))};
  }
  dispatchMidiBatch(batch, count);
#if defined(LANE_GAP_MIDI_SPLIT)
  sMidiSpans.current.dinUs = micros() - splitStartUs;
  splitStartUs = micros();
#endif
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_NOTE_DIN_DRAIN(micros() - segmentStartUs);
#endif

  if (!usbHostReady_) {
    RUNTIME_TIMING_NOTE_MIDI_INPUT_EXIT(micros());
    return;
  }

  // --- USB Host MIDI Input ---
  // USBHost_t36 delivers one MIDI message per read() via callbacks. Drain a bounded
  // batch each handleMidiInput() call (same cap as USB-device / DIN) so DROID NoteOn/NoteOff
  // pairs are not left queued across a long main-loop frame.
#if RUNTIME_TIMING_ENABLED
  segmentStartUs = micros();
#endif
  usbHost.Task();
#if defined(LANE_GAP_MIDI_SPLIT)
  sMidiSpans.current.hostTaskUs = micros() - splitStartUs;
#endif
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_NOTE_USB_HOST_TASK(micros() - segmentStartUs);
#endif

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

#if RUNTIME_TIMING_ENABLED
  segmentStartUs = micros();
#endif
  size_t hostReads = 0;
#if defined(LANE_GAP_MIDI_SPLIT)
  splitStartUs = micros();
#endif
  while (hostReads < midiInputBatchMax && usbHostMIDI.read()) {
    ++hostReads;
  }
#if defined(LANE_GAP_MIDI_SPLIT)
  sMidiSpans.current.hostDrainUs = micros() - splitStartUs;
  sMidiSpans.current.hostReads = static_cast<uint16_t>(hostReads);
#endif
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_NOTE_USB_HOST_DRAIN(micros() - segmentStartUs);
#endif
  if (hostReads >= midiInputBatchMax) {
    logger.log(CAT_MIDI, LOG_DEBUG,
               "USB Host MIDI drain hit batch cap (%u)",
               static_cast<unsigned>(midiInputBatchMax));
  }

  RUNTIME_TIMING_NOTE_MIDI_INPUT_EXIT(micros());
}

#if defined(PIO_UNIT_TEST_NATIVE) && MIDI_INGRESS_RECEIPTS_ENABLED
void MidiHandler::setIngressDispatchObserverForTests(IngressDispatchObserver observer) {
  ingressDispatchObserver = observer;
}
#endif

#if MIDI_INGRESS_RECEIPTS_ENABLED
FLASHMEM
#endif
void MidiHandler::handleMidiMessage(byte type, byte channel, byte data1, byte data2, InputSource source INPUT_RECEIPT_PARAMETER) {
#if MIDI_INPUT_CONTINUATION_QUALIFICATION
  if (source == SOURCE_USB_HOST && qualifiedInput && qualifiedInput->acquiringHost) {
    if (!qualifiedInput->append({type, channel, data1, data2}, micros() INPUT_RECEIPT_ARGUMENT(receipt))) std::abort();
    return; // Real callback data is routed exactly once when the owner resumes it.
  }
#endif
#if MIDI_INGRESS_RECEIPTS_ENABLED
  MidiIngress::noteDispatch(static_cast<uint8_t>(source), receipt);
#if defined(PIO_UNIT_TEST_NATIVE)
  if (ingressDispatchObserver) ingressDispatchObserver(type, channel, data1, data2, source,
      MidiIngress::lastDispatchedReceipt(static_cast<uint8_t>(source)));
#endif
#endif
  // Clock is high-rate (24 PPQN); skip synchronous capture to keep timing paths lean.
  if (type != midi::Clock) {
    if (source == SOURCE_USB) {
#if RUNTIME_TIMING_ENABLED
      const uint32_t captureStartUs = micros();
#endif
      SC_MIDI_IN('U', type, channel, data1, data2);
#if RUNTIME_TIMING_ENABLED
      RUNTIME_TIMING_ADD_USB_DEVICE_CAPTURE(micros() - captureStartUs);
#endif
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

    if ((type == midi::NoteOn || type == midi::NoteOff) && Logger::isEnabled(CAT_MIDI, LOG_DEBUG)) {
      // The same pitch-class/octave pair the playback hot path prints (`Utils/MidiNoteName.h`), behind the same
      // gate, so the input path stops paying for the table when DEBUG is filtered out.
      logger.log(CAT_MIDI, LOG_DEBUG, "  -> Note: %s%d (MIDI note %d), Velocity: %d",
                 MidiNoteName::pitchClassName(data1), MidiNoteName::octave(data1), data1, data2);
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
#if RUNTIME_TIMING_ENABLED
      const uint32_t thruStartUs = micros();
#endif
      sendMidiThru(type, outCh, data1, data2);
#if RUNTIME_TIMING_ENABLED
      RUNTIME_TIMING_ADD_USB_DEVICE_THRU(micros() - thruStartUs);
#endif
    } else {
      sendMidiThru(type, outCh, data1, data2);
    }
  }

  // Dispatch transport/clock messages first so tick is up-to-date
  // before channel messages read it
  switch (type) {
    case midi::Clock:
      if (source == SOURCE_USB) {
#if RUNTIME_TIMING_ENABLED
        const uint32_t clockStartUs = micros();
#endif
        clockManager.onMidiClockPulse();
#if RUNTIME_TIMING_ENABLED
        RUNTIME_TIMING_ADD_USB_DEVICE_CLOCK(micros() - clockStartUs);
#endif
      } else {
        clockManager.onMidiClockPulse();
      }
      return;

    case midi::Start:
    case midi::Stop:
    case midi::Continue: {
#if RUNTIME_TIMING_ENABLED
      const uint32_t transportStartUs = (source == SOURCE_USB) ? micros() : 0;
#endif
      if (type == midi::Start) {
        handleMidiStart();
      } else if (type == midi::Stop) {
        handleMidiStop();
      } else {
        handleMidiContinue();
      }
#if RUNTIME_TIMING_ENABLED
      if (source == SOURCE_USB) {
        RUNTIME_TIMING_ADD_USB_DEVICE_TRANSPORT(micros() - transportStartUs);
      }
#endif
      return;
    }

    default:
      break;
  }

  uint32_t tickNow = clockManager.getCurrentTick();
#if RUNTIME_TIMING_ENABLED
  const uint32_t channelStartUs = (source == SOURCE_USB) ? micros() : 0;
#endif

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
#if RUNTIME_TIMING_ENABLED
    const uint32_t channelDurationUs = micros() - channelStartUs;
    if (type == midi::NoteOn || type == midi::NoteOff) {
      RUNTIME_TIMING_ADD_USB_DEVICE_NOTE(channelDurationUs);
    } else if (type == midi::ControlChange || type == midi::PitchBend ||
               type == midi::AfterTouchChannel || type == midi::ProgramChange) {
      RUNTIME_TIMING_ADD_USB_DEVICE_CC(channelDurationUs);
    }
#endif
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
  const bool isFaderMotorChannel = isMotorFaderUsbHostServiceMessage(type, channel, data1);
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

/// **The ISR-reachable enqueue** (timing-path review, items 2 and 3). What changed and why:
///   - the body is no longer `FLASHMEM`: `RuntimeTimingTelemetry.cpp` records that code reached from this ISR
///     must stay in ITCM (a flash-resident body starved USB after transport start). The body is now small
///     enough for that to be free rather than a trade;
///   - the fader-state coalescing scan moved to the main-loop drain (`processDroidUsbHostOutbound`), so the
///     ISR's cost no longer scales with what is queued -- it maps one event and calls one O(1) push;
///   - **no logging and no telemetry here**: the overflow is counted (the ring's own counter) and reported by
///     the drain, which is the only place allowed to format a message or touch the capture ring. The old body
///     could `logger.log` a warning from the ISR over the shared 128-byte format buffer.
/// The key is the caller's half of the coalescing rule ("which events are *state*"), and `queuedUs` is the
/// caller's half of the age row, because the ring owns neither a clock nor `MidiConfig`.
bool MidiHandler::queueDeferredDroidHostEvent(const MidiEvent& event) {
  DroidHostFeedbackEvent queued;
  queued.type = static_cast<uint8_t>(event.type);
  queued.channel = event.channel;
  if (event.isNoteOn() || event.isNoteOff()) {
    queued.data1 = event.data.noteData.note;
    queued.data2 = event.data.noteData.velocity;
  } else if (event.type == midi::ControlChange) {
    queued.data1 = event.data.ccData.cc;
    queued.data2 = event.data.ccData.value;
  } else if (event.type == midi::PitchBend) {
    queued.pitchBend = event.data.pitchBend;
  } else if (event.type == midi::ProgramChange) {
    queued.data1 = event.data.program;
  } else if (event.type == midi::AfterTouchChannel) {
    queued.data1 = event.data.channelPressure;
  } else {
    return false;
  }
  queued.coalesceKey = droidFeedbackCoalesceKey(queued.type, queued.channel, queued.data1);
  queued.queuedUs = micros();
  return gDeferredDroidHostRing.push(queued);
}

FLASHMEM void MidiHandler::sendDeferredDroidHostEvent(uint8_t type, uint8_t channel, uint8_t data1,
                                                       uint8_t data2, int16_t pitchBend) {
  paceDroidUsbHostBeforeSend();
  switch (static_cast<midi::MidiType>(type)) {
    case midi::NoteOn:
      usbHostMIDI.sendNoteOn(data1, data2, channel);
      break;
    case midi::NoteOff:
      usbHostMIDI.sendNoteOff(data1, data2, channel);
      break;
    case midi::ControlChange:
      usbHostMIDI.sendControlChange(data1, data2, channel);
      break;
    case midi::PitchBend:
      usbHostMIDI.sendPitchBend(MidiConfig::Pitchbend::logicalToWireSigned(pitchBend), channel);
      break;
    case midi::ProgramChange:
      usbHostMIDI.sendProgramChange(data1, channel);
      break;
    case midi::AfterTouchChannel:
      usbHostMIDI.sendAfterTouch(data1, channel);
      break;
    default:
      return;
  }
  serviceUsbHostAfterOutboundPacket();
}

FLASHMEM void MidiHandler::processDroidUsbHostOutbound() {
  if (!usbHostReady_ || !usbHostMIDI || droidMotorOutboundPriority_) {
    return;
  }
  uint8_t drained = 0;
  // Recycle first: a state entry whose target has a newer queued value was marked `superseded` by the send
  // step below, and recycling it is O(1) (the O(depth) scan lives in `takeOldestForSend`, once per sent state
  // entry, not once per queued duplicate). Bounded by the ring's capacity.
  while (gDeferredDroidHostRing.recycleSupersededOldest()) {
  }
  while (drained < MidiConfig::DroidUsbHost::LED_DRAIN_MAX_PER_LOOP) {
    // The coalescing rule itself: the oldest queued update keeps its position in the ordered stream and
    // carries the newest value for its target (the rule this queue has always kept for fader feedback, moved
    // off the ISR path by the timing-path review's item 3).
    DroidHostFeedbackEvent pending;
    if (!gDeferredDroidHostRing.takeOldestForSend(pending)) {
      break;
    }
    sendDeferredDroidHostEvent(pending.type, pending.channel, pending.data1, pending.data2,
                               pending.pitchBend);
    ++drained;
  }
  constexpr uint8_t ch = MidiConfig::Led::CHANNEL;
  while (ledPendingCount_ > 0 && drained < MidiConfig::DroidUsbHost::LED_DRAIN_MAX_PER_LOOP) {
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
  // The queue's own evidence row and its overflow warning, both emitted **here** -- the drain is the only
  // place allowed to format a message or write to the capture ring, and the enqueue is reachable from the MIDI
  // tick ISR (the row family's cadence follows the drain now: one row per drain call that left the queue
  // non-empty, instead of one per enqueue/coalesce).
  if (!gDeferredDroidHostRing.empty()) {
    SC_DROID_FEEDBACK_QUEUE(gDeferredDroidHostRing.depth(),
                            gDeferredDroidHostRing.oldestAgeUs(micros()),
                            gDeferredDroidHostRing.overflowCount());
  }
  const uint32_t overflowCount = gDeferredDroidHostRing.overflowCount();
  if (overflowCount != gReportedDroidFeedbackOverflow) {
    gReportedDroidFeedbackOverflow = overflowCount;
    SC_DROID_FEEDBACK_QUEUE(gDeferredDroidHostRing.depth(), 0, overflowCount);
    logger.log(CAT_MIDI, LOG_WARNING, "DROID host feedback queue overflow (%lu)",
               static_cast<unsigned long>(overflowCount));
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
  // Route to track recording (skip control channels 13-16)
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
  // Route to track recording (skip control channels 13-16)
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
#if defined(PIO_UNIT_TEST_NATIVE)
namespace {
MidiEvent sMidiSendCapture[MidiSendCapture::kCapacity];
size_t sMidiSendCaptureCount = 0;
}  // namespace

void MidiSendCapture::reset() { sMidiSendCaptureCount = 0; }

size_t MidiSendCapture::count() { return sMidiSendCaptureCount; }

MidiEvent MidiSendCapture::at(size_t index) {
    return index < sMidiSendCaptureCount ? sMidiSendCapture[index] : MidiEvent{};
}

void MidiSendCapture::record(const MidiEvent& event) {
    if (sMidiSendCaptureCount < kCapacity) {
        sMidiSendCapture[sMidiSendCaptureCount++] = event;
    }
}
#endif

void MidiHandler::sendMidiEvent(const MidiEvent& event) {
#if defined(PIO_UNIT_TEST_NATIVE)
    MidiSendCapture::record(event);
#endif
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
                } else if (deferDroidHost_) {
                    queueDeferredDroidHostEvent(event);
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
                } else if (deferDroidHost_) {
                    queueDeferredDroidHostEvent(event);
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
                if (deferDroidHost_) {
                    queueDeferredDroidHostEvent(event);
                } else {
                    paceDroidUsbHostBeforeSend();
                    usbHostMIDI.sendControlChange(event.data.ccData.cc, event.data.ccData.value, event.channel);
                    serviceUsbHostAfterOutboundPacket();
                }
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
                if (deferDroidHost_) {
                    queueDeferredDroidHostEvent(event);
                } else {
                    paceDroidUsbHostBeforeSend();
                    usbHostMIDI.sendPitchBend(wirePitch, event.channel);
                    serviceUsbHostAfterOutboundPacket();
                }
            }
            break;
        }
        case midi::AfterTouchChannel:
            if (outputUSB) usbMIDI.sendAfterTouch(event.data.channelPressure, event.channel);
            if (outputSerial) MIDIserial.sendAfterTouch(event.data.channelPressure, event.channel);
            if (canSendUsbHostMidi()) {
                if (deferDroidHost_) {
                    queueDeferredDroidHostEvent(event);
                } else {
                    usbHostMIDI.sendAfterTouch(event.data.channelPressure, event.channel);
                }
            }
            break;
        case midi::ProgramChange:
            if (outputUSB) usbMIDI.sendProgramChange(event.data.program, event.channel);
            if (outputSerial) MIDIserial.sendProgramChange(event.data.program, event.channel);
            if (canSendUsbHostMidi()) {
                if (deferDroidHost_) {
                    queueDeferredDroidHostEvent(event);
                } else {
                    paceDroidUsbHostBeforeSend();
                    usbHostMIDI.sendProgramChange(event.data.program, event.channel);
                    serviceUsbHostAfterOutboundPacket();
                }
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

void MidiHandler::sendRealtimePlaybackEvent(const MidiEvent& event) {
  const bool previous = deferDroidHost_;
  deferDroidHost_ = true;
  sendMidiEvent(event);
  deferDroidHost_ = previous;
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
#if RUNTIME_TIMING_ENABLED
  const uint32_t tickUs = clockManager.getMicrosPerTick();
  RUNTIME_TIMING_RECORD_OUTGOING_CLOCK_SEND(micros(), tickUs * Config::TICKS_PER_CLOCK, tickUs,
                                            clockManager.getCurrentTick());
#endif
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
    instance->handleMidiMessage(midi::NoteOn, channel, note, velocity, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}

void MidiHandler::usbHostNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (instance) {
    instance->handleMidiMessage(midi::NoteOff, channel, note, velocity, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}

void MidiHandler::usbHostControlChange(uint8_t channel, uint8_t control, uint8_t value) {
  if (instance) {
    // Single path: handleControlChange routes to ControlSurfaceManager + track recording (no duplicate CC).
    instance->handleMidiMessage(midi::ControlChange, channel, control, value, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}

void MidiHandler::usbHostProgramChange(uint8_t channel, uint8_t program) {
  if (instance) {
    instance->handleMidiMessage(midi::ProgramChange, channel, program, 0, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
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
                                SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}

void MidiHandler::usbHostAfterTouchChannel(uint8_t channel, uint8_t pressure) {
  if (instance) {
    instance->handleMidiMessage(midi::AfterTouchChannel, channel, pressure, 0, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}

void MidiHandler::usbHostClock() {
  if (instance) {
    instance->handleMidiMessage(midi::Clock, 0, 0, 0, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}

void MidiHandler::usbHostStart() {
  if (instance) {
    instance->handleMidiMessage(midi::Start, 0, 0, 0, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}

void MidiHandler::usbHostStop() {
  if (instance) {
    instance->handleMidiMessage(midi::Stop, 0, 0, 0, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}

void MidiHandler::usbHostContinue() {
  if (instance) {
    instance->handleMidiMessage(midi::Continue, 0, 0, 0, SOURCE_USB_HOST INPUT_RECEIPT_ARGUMENT(MidiIngress::lastReceipt(SOURCE_USB_HOST)));
  }
}
