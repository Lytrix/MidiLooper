//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RuntimeTimingTelemetry.h"

#include "Globals.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/MidiBatchComposition.h"

#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)

#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#include <Arduino.h>
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
#include "Utils/InvestigationCaptureLog.h"
#endif

#if defined(__IMXRT1062__)
#define RT_FLASHMEM_FN __attribute__((noinline, section(".flashmem")))
#else
#define RT_FLASHMEM_FN
#endif

namespace RuntimeTimingTelemetry {
namespace {

struct Accumulator {
  uint32_t maxUs = 0;
  uint32_t overCount = 0;
};

void recordSample(Accumulator& acc, uint32_t durationUs) {
  if (durationUs > acc.maxUs) {
    acc.maxUs = durationUs;
  }
  if (durationUs > kObservationalSoftCeilingUs) {
    ++acc.overCount;
  }
}

// ISR-called (IntervalTimer updateInternalClock / sendClock / sendMidiEvent).
// Must stay in ITCM with noteClockPulse — FLASHMEM from that ISR starved USB
// after transport start (session_20260817_212654).
void recordLateness(Accumulator& acc, uint32_t latenessUs) {
  if (latenessUs > acc.maxUs) {
    acc.maxUs = latenessUs;
  }
  if (latenessUs > 0) {
    ++acc.overCount;
  }
}

uint32_t latenessAfterDeadline(uint32_t nowUs, uint32_t dueUs, uint32_t periodUs) {
  if (periodUs == 0) {
    return 0;
  }
  const uint32_t deadlineUs = dueUs + periodUs;
  if (static_cast<int32_t>(nowUs - deadlineUs) <= 0) {
    return 0;
  }
  return nowUs - deadlineUs;
}

#if defined(__IMXRT1062__)
DMAMEM Accumulator trackUpdates[Config::NUM_TRACKS];
#else
Accumulator trackUpdates[Config::NUM_TRACKS];
#endif

RT_FLASHMEM_FN void resetTrackUpdates() {
  for (Accumulator& trackUpdate : trackUpdates) {
    trackUpdate = Accumulator{};
  }
}

RT_FLASHMEM_FN Accumulator& trackUpdateAt(uint8_t trackIndex) {
#if defined(__IMXRT1062__)
  static bool initialized = false;
  if (!initialized) {
    resetTrackUpdates();
    initialized = true;
  }
#endif
  return trackUpdates[trackIndex];
}

struct State {
  Accumulator midiGap;
  Accumulator midiInput;
  Accumulator clk;
  Accumulator tracks;
  Accumulator usbdev;
  Accumulator din;
  Accumulator hosttask;
  Accumulator hostdrain;
  Accumulator usbread;
  Accumulator usbdisp;
  Accumulator usbcap;
  Accumulator usbthru;
  Accumulator usbclk;
  Accumulator usbnote;
  Accumulator usbcc;
  Accumulator usbtrans;
  Accumulator noteappend;
  Accumulator notechg;
  Accumulator noterecon;
  Accumulator notepair;
  Accumulator idleMaint;
  Accumulator loadFrame;
  Accumulator persistSave;
  Accumulator lateOn;
  Accumulator lateOff;
  Accumulator lateClk;
  Accumulator extclkArr;
  Accumulator extclkDisp;
  uint32_t clkBatchMax = 0;
  uint32_t clkBatchCount = 0;
  uint32_t windowServicePeriodUs = 0;
  uint32_t usbBatchMax = 0;
  uint32_t usbBatchSaturatedCount = 0;
  uint32_t usbBatchClockMax = 0;
  uint32_t usbBatchConsecutiveClockMax = 0;
  uint32_t usbBatchSizeBuckets[MidiBatchComposition::kBucketCount] = {};
  uint32_t usbBatchClockBuckets[MidiBatchComposition::kBucketCount] = {};
  uint32_t usbBatchClockDispatchMaxUs[MidiBatchComposition::kBucketCount] = {};
  Accumulator trackRebuild;
  Accumulator trackOrder;
  Accumulator trackSend;
  Accumulator trackSlot;
  Accumulator trackResid;
  uint32_t trackPathRebuildUs = 0;
  uint32_t trackPathOrderUs = 0;
  uint32_t trackPathSendUs = 0;
  uint32_t trackPathSlotUs = 0;
  bool trackPathNestedActive = false;
  uint32_t extclkArrivalDueUs = 0;
  uint32_t extclkArrivalPeriodUs = 0;
  bool extclkArrivalCadenceActive = false;
  uint32_t extclkBatchArrivalUs = 0;
  uint32_t extclkBatchRemaining = 0;
  uint32_t usbNestedCaptureUs = 0;
  uint32_t usbNestedThruUs = 0;
  uint32_t usbNestedClockUs = 0;
  uint32_t usbNestedNoteUs = 0;
  uint32_t usbNestedCcUs = 0;
  uint32_t usbNestedTransportUs = 0;
  uint32_t usbNestedNoteAppendUs = 0;
  uint32_t usbNestedNoteChangeUs = 0;
  uint32_t usbNestedNoteReconUs = 0;
  uint32_t usbNestedNotePairUs = 0;
  bool usbNestedActive = false;
  uint32_t clockPulses = 0;
  uint32_t lastInputExitUs = 0;
  uint32_t inputEnterUs = 0;
  bool inputActive = false;
  uint32_t windowStartUs = 0;
  uint32_t lastEmitUs = 0;
  uint32_t serviceDueUs = 0;
  uint32_t servicePeriodUs = 0;
  bool serviceCadenceActive = false;
  uint32_t clockDueUs = 0;
  uint32_t clockPeriodUs = 0;
  bool clockCadenceActive = false;
  bool lateOneShotArmed = true;
  bool pendingLateEvent = false;
  uint8_t pendingLateClass = 0;  // 0=on, 1=off, 2=clk
  uint32_t pendingLateUs = 0;
  uint32_t pendingLateTick = 0;
  bool pendingPlaybackRebuild = false;
  uint32_t pendingRebuildDurationUs = 0;
  uint32_t pendingRebuildWindowStartTick = 0;
  uint32_t pendingRebuildWindowLengthTicks = 0;
  uint32_t pendingRebuildRevision = 0;
  uint32_t pendingRebuildCount = 0;
  uint8_t pendingRebuildTrack = 255;
  uint8_t pendingRebuildSlot = 255;
  PlaybackRebuildPath pendingRebuildPath = PlaybackRebuildPath::CompatibilityPasses;
};

State& state() {
#if defined(__IMXRT1062__) && EXTMEM_AVAILABLE
  // FLASHMEM emitters read this; PSRAM is legal, DMAMEM is not.
  static EXTMEM State s;
  static bool initialized = false;
  if (!initialized) {
    s = State{};
    initialized = true;
  }
  return s;
#else
  static State s;
  return s;
#endif
}

void queueFirstLate(State& s, uint8_t lateClass, uint32_t latenessUs, uint32_t tick) {
  if (latenessUs == 0 || !s.lateOneShotArmed) {
    return;
  }
  s.lateOneShotArmed = false;
  s.pendingLateEvent = true;
  s.pendingLateClass = lateClass;
  s.pendingLateUs = latenessUs;
  s.pendingLateTick = tick;
}

RT_FLASHMEM_FN const char* playbackRebuildPathLabel(PlaybackRebuildPath path) {
#if defined(__IMXRT1062__)
  switch (path) {
    case PlaybackRebuildPath::PublishedSnapshot:
      return PSTR("snapshot");
    case PlaybackRebuildPath::GatherMissHoldEmpty:
      return PSTR("hold_empty");
    case PlaybackRebuildPath::CompatibilityPasses:
      return PSTR("compatibility");
    case PlaybackRebuildPath::NoteEditPreview:
      return PSTR("note_edit");
    case PlaybackRebuildPath::IncrementalOverdubSeal:
      return PSTR("incremental");
    case PlaybackRebuildPath::StaleRootOverlay:
      return PSTR("overlay");
  }
  return PSTR("unknown");
#else
  switch (path) {
    case PlaybackRebuildPath::PublishedSnapshot:
      return "snapshot";
    case PlaybackRebuildPath::GatherMissHoldEmpty:
      return "hold_empty";
    case PlaybackRebuildPath::CompatibilityPasses:
      return "compatibility";
    case PlaybackRebuildPath::NoteEditPreview:
      return "note_edit";
    case PlaybackRebuildPath::IncrementalOverdubSeal:
      return "incremental";
    case PlaybackRebuildPath::StaleRootOverlay:
      return "overlay";
  }
  return "unknown";
#endif
}

RT_FLASHMEM_FN void clearWindow(State& s) {
  s.midiGap = Accumulator{};
  s.midiInput = Accumulator{};
  s.clk = Accumulator{};
  s.tracks = Accumulator{};
  resetTrackUpdates();
  s.usbdev = Accumulator{};
  s.din = Accumulator{};
  s.hosttask = Accumulator{};
  s.hostdrain = Accumulator{};
  s.usbread = Accumulator{};
  s.usbdisp = Accumulator{};
  s.usbcap = Accumulator{};
  s.usbthru = Accumulator{};
  s.usbclk = Accumulator{};
  s.usbnote = Accumulator{};
  s.usbcc = Accumulator{};
  s.usbtrans = Accumulator{};
  s.noteappend = Accumulator{};
  s.notechg = Accumulator{};
  s.noterecon = Accumulator{};
  s.notepair = Accumulator{};
  s.idleMaint = Accumulator{};
  s.loadFrame = Accumulator{};
  s.persistSave = Accumulator{};
  s.lateOn = Accumulator{};
  s.lateOff = Accumulator{};
  s.lateClk = Accumulator{};
  s.extclkArr = Accumulator{};
  s.extclkDisp = Accumulator{};
  s.clkBatchMax = 0;
  s.clkBatchCount = 0;
  s.windowServicePeriodUs = 0;
  s.usbBatchMax = 0;
  s.usbBatchSaturatedCount = 0;
  s.usbBatchClockMax = 0;
  s.usbBatchConsecutiveClockMax = 0;
  for (uint8_t i = 0; i < MidiBatchComposition::kBucketCount; ++i) {
    s.usbBatchSizeBuckets[i] = 0;
    s.usbBatchClockBuckets[i] = 0;
    s.usbBatchClockDispatchMaxUs[i] = 0;
  }
  s.trackRebuild = Accumulator{};
  s.trackOrder = Accumulator{};
  s.trackSend = Accumulator{};
  s.trackSlot = Accumulator{};
  s.trackResid = Accumulator{};
  s.clockPulses = 0;
}

RT_FLASHMEM_FN void emitWindow(const State& s, uint32_t nowUs, uint32_t windowElapsedUs) {
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::runtimeTimingTelemetry("midi_gap", s.midiGap.maxUs, s.midiGap.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("midi_input", s.midiInput.maxUs, s.midiInput.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("clk", s.clk.maxUs, s.clk.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("tracks", s.tracks.maxUs, s.tracks.overCount);
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    const Accumulator& trackUpdate = trackUpdateAt(trackIndex);
    DebugSessionCapture::runtimeTimingTrackUpdate(
        trackIndex, trackUpdate.maxUs, trackUpdate.overCount);
  }
  DebugSessionCapture::runtimeTimingTelemetry("usbdev", s.usbdev.maxUs, s.usbdev.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("din", s.din.maxUs, s.din.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("hosttask", s.hosttask.maxUs, s.hosttask.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("hostdrain", s.hostdrain.maxUs, s.hostdrain.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbread", s.usbread.maxUs, s.usbread.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbdisp", s.usbdisp.maxUs, s.usbdisp.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbcap", s.usbcap.maxUs, s.usbcap.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbthru", s.usbthru.maxUs, s.usbthru.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbclk", s.usbclk.maxUs, s.usbclk.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbnote", s.usbnote.maxUs, s.usbnote.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbcc", s.usbcc.maxUs, s.usbcc.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbtrans", s.usbtrans.maxUs, s.usbtrans.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("noteappend", s.noteappend.maxUs, s.noteappend.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("notechg", s.notechg.maxUs, s.notechg.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("noterecon", s.noterecon.maxUs, s.noterecon.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("notepair", s.notepair.maxUs, s.notepair.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("idle_maint", s.idleMaint.maxUs, s.idleMaint.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("load_frame", s.loadFrame.maxUs, s.loadFrame.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("persist_save", s.persistSave.maxUs,
                                             s.persistSave.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("late_on", s.lateOn.maxUs, s.lateOn.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("late_off", s.lateOff.maxUs, s.lateOff.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("late_clk", s.lateClk.maxUs, s.lateClk.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("extclk_arrival", s.extclkArr.maxUs, s.extclkArr.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("extclk_dispatch", s.extclkDisp.maxUs, s.extclkDisp.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("clk_batch", s.clkBatchMax, s.clkBatchCount);
  DebugSessionCapture::runtimeTimingTelemetry("service_period", s.windowServicePeriodUs, 0);
  DebugSessionCapture::runtimeTimingUsbBatch(s.usbBatchMax, s.usbBatchSaturatedCount,
                                            s.usbBatchClockMax, s.usbBatchConsecutiveClockMax);
  DebugSessionCapture::runtimeTimingUsbBatchBuckets("usb_bkt_size", s.usbBatchSizeBuckets);
  DebugSessionCapture::runtimeTimingUsbBatchBuckets("usb_bkt_clk", s.usbBatchClockBuckets);
  DebugSessionCapture::runtimeTimingUsbBatchBuckets("usb_bkt_disp", s.usbBatchClockDispatchMaxUs);
  DebugSessionCapture::runtimeTimingTelemetry("track_rebuild", s.trackRebuild.maxUs,
                                             s.trackRebuild.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("track_order", s.trackOrder.maxUs,
                                             s.trackOrder.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("track_send", s.trackSend.maxUs,
                                             s.trackSend.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("track_slot", s.trackSlot.maxUs,
                                             s.trackSlot.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("track_resid", s.trackResid.maxUs,
                                             s.trackResid.overCount);
  uint32_t pulsesPerSecond = 0;
  if (windowElapsedUs > 0) {
    pulsesPerSecond = static_cast<uint32_t>(
        (static_cast<uint64_t>(s.clockPulses) * 1000000ull) / windowElapsedUs);
  }
  DebugSessionCapture::runtimeTimingClockrate(pulsesPerSecond);
  (void)nowUs;
#else
  (void)s;
  (void)nowUs;
  (void)windowElapsedUs;
#endif
}

}  // namespace

void resetForTest() {
  State& s = state();
  s = State{};
  resetTrackUpdates();
}

void noteMidiInputEnter(uint32_t nowUs) {
  State& s = state();
  if (s.windowStartUs == 0) {
    s.windowStartUs = nowUs;
  }
  if (s.lastInputExitUs != 0) {
    recordSample(s.midiGap, nowUs - s.lastInputExitUs);
  }
  s.inputEnterUs = nowUs;
  s.inputActive = true;
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
  InvestigationCaptureLog::appendSnapshotIdleReserveOnMidiEnter(nowUs);
#endif
}

void noteMidiInputExit(uint32_t nowUs) {
  State& s = state();
  if (s.inputActive) {
    recordSample(s.midiInput, nowUs - s.inputEnterUs);
    s.inputActive = false;
  }
  s.lastInputExitUs = nowUs;
}

void noteClockDispatch(uint32_t durationUs) {
  recordSample(state().clk, durationUs);
}

void noteTracksUpdate(uint32_t durationUs) {
  recordSample(state().tracks, durationUs);
}

RT_FLASHMEM_FN void noteTrackUpdate(uint8_t trackIndex, uint32_t durationUs) {
  if (trackIndex < Config::NUM_TRACKS) {
    recordSample(trackUpdateAt(trackIndex), durationUs);
  }
}

namespace {

uint32_t saturatingSub(uint32_t lhs, uint32_t rhs) {
  return lhs > rhs ? lhs - rhs : 0;
}

}  // namespace

RT_FLASHMEM_FN void beginTrackPathNested() {
  State& s = state();
  s.trackPathRebuildUs = 0;
  s.trackPathOrderUs = 0;
  s.trackPathSendUs = 0;
  s.trackPathSlotUs = 0;
  s.trackPathNestedActive = true;
}

RT_FLASHMEM_FN void addTrackPathRebuild(uint32_t durationUs) {
  State& s = state();
  if (s.trackPathNestedActive) {
    s.trackPathRebuildUs += durationUs;
  }
}

RT_FLASHMEM_FN void addTrackPathOrder(uint32_t durationUs) {
  State& s = state();
  if (s.trackPathNestedActive) {
    s.trackPathOrderUs += durationUs;
  }
}

RT_FLASHMEM_FN void addTrackPathSend(uint32_t durationUs) {
  State& s = state();
  if (s.trackPathNestedActive) {
    s.trackPathSendUs += durationUs;
  }
}

RT_FLASHMEM_FN void addTrackPathSlot(uint32_t durationUs) {
  State& s = state();
  if (s.trackPathNestedActive) {
    s.trackPathSlotUs += durationUs;
  }
}

RT_FLASHMEM_FN uint32_t trackPathSendUs() {
  const State& s = state();
  return s.trackPathNestedActive ? s.trackPathSendUs : 0;
}

RT_FLASHMEM_FN uint32_t trackPathExclusiveUs(uint32_t wallUs, uint32_t sendBeforeUs) {
  const State& s = state();
  if (!s.trackPathNestedActive) {
    return 0;
  }
  return saturatingSub(wallUs, saturatingSub(s.trackPathSendUs, sendBeforeUs));
}

RT_FLASHMEM_FN void commitTrackPathNested(uint32_t trackDurationUs) {
  State& s = state();
  if (!s.trackPathNestedActive) {
    return;
  }
  recordSample(s.trackRebuild, s.trackPathRebuildUs);
  recordSample(s.trackOrder, s.trackPathOrderUs);
  recordSample(s.trackSend, s.trackPathSendUs);
  recordSample(s.trackSlot, s.trackPathSlotUs);
  const uint32_t attributed = s.trackPathRebuildUs + s.trackPathOrderUs + s.trackPathSendUs +
                              s.trackPathSlotUs;
  recordSample(s.trackResid, saturatingSub(trackDurationUs, attributed));
  s.trackPathRebuildUs = 0;
  s.trackPathOrderUs = 0;
  s.trackPathSendUs = 0;
  s.trackPathSlotUs = 0;
  s.trackPathNestedActive = false;
}

void noteUsbDeviceDrain(uint32_t durationUs) {
  recordSample(state().usbdev, durationUs);
}

void noteDinDrain(uint32_t durationUs) {
  recordSample(state().din, durationUs);
}

void noteUsbHostTask(uint32_t durationUs) {
  recordSample(state().hosttask, durationUs);
}

void noteUsbHostDrain(uint32_t durationUs) {
  recordSample(state().hostdrain, durationUs);
}

void noteUsbDeviceRead(uint32_t durationUs) {
  recordSample(state().usbread, durationUs);
}

void noteUsbDeviceDispatch(uint32_t durationUs) {
  recordSample(state().usbdisp, durationUs);
}

void beginUsbDeviceNested() {
  State& s = state();
  s.usbNestedCaptureUs = 0;
  s.usbNestedThruUs = 0;
  s.usbNestedClockUs = 0;
  s.usbNestedNoteUs = 0;
  s.usbNestedCcUs = 0;
  s.usbNestedTransportUs = 0;
  s.usbNestedNoteAppendUs = 0;
  s.usbNestedNoteChangeUs = 0;
  s.usbNestedNoteReconUs = 0;
  s.usbNestedNotePairUs = 0;
  s.usbNestedActive = true;
}

void addUsbDeviceCapture(uint32_t durationUs) {
  state().usbNestedCaptureUs += durationUs;
}

void addUsbDeviceThru(uint32_t durationUs) {
  state().usbNestedThruUs += durationUs;
}

void addUsbDeviceClock(uint32_t durationUs) {
  state().usbNestedClockUs += durationUs;
}

void addUsbDeviceNote(uint32_t durationUs) {
  state().usbNestedNoteUs += durationUs;
}

void addUsbDeviceCc(uint32_t durationUs) {
  state().usbNestedCcUs += durationUs;
}

void addUsbDeviceTransport(uint32_t durationUs) {
  state().usbNestedTransportUs += durationUs;
}

void addNoteAppend(uint32_t durationUs) {
  State& s = state();
  if (s.usbNestedActive) {
    s.usbNestedNoteAppendUs += durationUs;
  }
}

void addNoteChange(uint32_t durationUs) {
  State& s = state();
  if (s.usbNestedActive) {
    s.usbNestedNoteChangeUs += durationUs;
  }
}

void addNoteRecon(uint32_t durationUs) {
  State& s = state();
  if (s.usbNestedActive) {
    s.usbNestedNoteReconUs += durationUs;
  }
}

void addNotePair(uint32_t durationUs) {
  State& s = state();
  if (s.usbNestedActive) {
    s.usbNestedNotePairUs += durationUs;
  }
}

void commitUsbDeviceNested() {
  State& s = state();
  recordSample(s.usbcap, s.usbNestedCaptureUs);
  recordSample(s.usbthru, s.usbNestedThruUs);
  recordSample(s.usbclk, s.usbNestedClockUs);
  recordSample(s.usbnote, s.usbNestedNoteUs);
  recordSample(s.usbcc, s.usbNestedCcUs);
  recordSample(s.usbtrans, s.usbNestedTransportUs);
  recordSample(s.noteappend, s.usbNestedNoteAppendUs);
  recordSample(s.notechg, s.usbNestedNoteChangeUs);
  recordSample(s.noterecon, s.usbNestedNoteReconUs);
  recordSample(s.notepair, s.usbNestedNotePairUs);
  s.usbNestedCaptureUs = 0;
  s.usbNestedThruUs = 0;
  s.usbNestedClockUs = 0;
  s.usbNestedNoteUs = 0;
  s.usbNestedCcUs = 0;
  s.usbNestedTransportUs = 0;
  s.usbNestedNoteAppendUs = 0;
  s.usbNestedNoteChangeUs = 0;
  s.usbNestedNoteReconUs = 0;
  s.usbNestedNotePairUs = 0;
  s.usbNestedActive = false;
}

void noteClockPulse() {
  State& s = state();
  ++s.clockPulses;
}

void noteExternalClockBatch(uint32_t arrivalUs, uint32_t clockCount, uint32_t clockPeriodUs) {
  if (clockCount == 0) {
    return;
  }
  State& s = state();
  s.extclkBatchArrivalUs = arrivalUs;
  s.extclkBatchRemaining = clockCount;
  if (clockCount > s.clkBatchMax) {
    s.clkBatchMax = clockCount;
  }
  ++s.clkBatchCount;
  if (clockPeriodUs == 0) {
    return;
  }
  if (!s.extclkArrivalCadenceActive) {
    s.extclkArrivalDueUs = arrivalUs;
    s.extclkArrivalPeriodUs = clockPeriodUs;
    s.extclkArrivalCadenceActive = true;
    recordLateness(s.extclkArr, 0);
  } else {
    s.extclkArrivalPeriodUs = clockPeriodUs;
    s.extclkArrivalDueUs += clockPeriodUs;
    uint32_t latenessUs = 0;
    if (static_cast<int32_t>(arrivalUs - s.extclkArrivalDueUs) > 0) {
      latenessUs = arrivalUs - s.extclkArrivalDueUs;
    }
    recordLateness(s.extclkArr, latenessUs);
  }
  if (clockCount > 1) {
    s.extclkArrivalDueUs += clockPeriodUs * (clockCount - 1);
  }
}

void noteExternalClockDispatchEnter(uint32_t nowUs) {
  State& s = state();
  if (s.extclkBatchRemaining == 0) {
    return;
  }
  --s.extclkBatchRemaining;
  recordSample(s.extclkDisp, nowUs - s.extclkBatchArrivalUs);
}

void noteUsbDeviceBatchComposition(uint32_t size, uint32_t clockCount, uint32_t nonClockCount,
                                   uint32_t maxConsecutiveClocks, bool saturated,
                                   uint32_t dispatchUs) {
  if (size == 0) {
    return;
  }
  (void)nonClockCount;
  State& s = state();
  if (size > s.usbBatchMax) {
    s.usbBatchMax = size;
  }
  if (saturated) {
    ++s.usbBatchSaturatedCount;
  }
  ++s.usbBatchSizeBuckets[MidiBatchComposition::countBucket(size)];
  if (clockCount > s.usbBatchClockMax) {
    s.usbBatchClockMax = clockCount;
  }
  if (maxConsecutiveClocks > s.usbBatchConsecutiveClockMax) {
    s.usbBatchConsecutiveClockMax = maxConsecutiveClocks;
  }
  if (clockCount == 0) {
    return;
  }
  const uint8_t clockBucket = MidiBatchComposition::countBucket(clockCount);
  ++s.usbBatchClockBuckets[clockBucket];
  if (dispatchUs > s.usbBatchClockDispatchMaxUs[clockBucket]) {
    s.usbBatchClockDispatchMaxUs[clockBucket] = dispatchUs;
  }
}

void notePlaybackServiceEnter(uint32_t nowUs, uint32_t tickPeriodUs) {
  State& s = state();
  s.servicePeriodUs = tickPeriodUs;
  if (tickPeriodUs != 0) {
    s.windowServicePeriodUs = tickPeriodUs;
  }
  if (!s.serviceCadenceActive || tickPeriodUs == 0) {
    s.serviceDueUs = nowUs;
    s.serviceCadenceActive = tickPeriodUs != 0;
    return;
  }
  s.serviceDueUs += tickPeriodUs;
}

RT_FLASHMEM_FN void resetPlaybackDeadlineCadence() {
  State& s = state();
  s.serviceDueUs = 0;
  s.servicePeriodUs = 0;
  s.serviceCadenceActive = false;
  s.clockDueUs = 0;
  s.clockPeriodUs = 0;
  s.clockCadenceActive = false;
  s.extclkArrivalDueUs = 0;
  s.extclkArrivalPeriodUs = 0;
  s.extclkArrivalCadenceActive = false;
  s.extclkBatchArrivalUs = 0;
  s.extclkBatchRemaining = 0;
}

uint32_t remainingUntilNextPlaybackServiceUs(uint32_t nowUs) {
  const State& s = state();
  if (!s.serviceCadenceActive || s.servicePeriodUs == 0) {
    return 0xFFFFFFFFu;
  }
  const uint32_t deadlineUs = s.serviceDueUs + s.servicePeriodUs;
  if (static_cast<int32_t>(nowUs - deadlineUs) >= 0) {
    return 0;
  }
  return deadlineUs - nowUs;
}

void recordNoteSendLateness(bool isNoteOn, uint32_t nowUs, uint32_t tick) {
  State& s = state();
  const uint32_t latenessUs =
      latenessAfterDeadline(nowUs, s.serviceDueUs, s.servicePeriodUs);
  recordLateness(isNoteOn ? s.lateOn : s.lateOff, latenessUs);
  queueFirstLate(s, isNoteOn ? 0 : 1, latenessUs, tick);
}

void recordOutgoingClockSend(uint32_t nowUs, uint32_t clockPeriodUs, uint32_t onTimeWindowUs,
                             uint32_t tick) {
  State& s = state();
  if (clockPeriodUs == 0) {
    return;
  }
  if (!s.clockCadenceActive) {
    s.clockDueUs = nowUs;
    s.clockPeriodUs = clockPeriodUs;
    s.clockCadenceActive = true;
    recordLateness(s.lateClk, 0);
    return;
  }
  s.clockPeriodUs = clockPeriodUs;
  s.clockDueUs += clockPeriodUs;
  const uint32_t latenessUs = latenessAfterDeadline(nowUs, s.clockDueUs, onTimeWindowUs);
  recordLateness(s.lateClk, latenessUs);
  queueFirstLate(s, 2, latenessUs, tick);
}

RT_FLASHMEM_FN void recordPlaybackRebuild(uint32_t durationUs, uint32_t windowStartTick,
                                          uint32_t windowLengthTicks, uint32_t revision,
                                          uint8_t trackIndex, uint8_t slotIndex,
                                          PlaybackRebuildPath path) {
  State& s = state();
  ++s.pendingRebuildCount;
  if (!s.pendingPlaybackRebuild || durationUs > s.pendingRebuildDurationUs) {
    s.pendingRebuildDurationUs = durationUs;
    s.pendingRebuildWindowStartTick = windowStartTick;
    s.pendingRebuildWindowLengthTicks = windowLengthTicks;
    s.pendingRebuildRevision = revision;
    s.pendingRebuildTrack = trackIndex;
    s.pendingRebuildSlot = slotIndex;
    s.pendingRebuildPath = path;
  }
  s.pendingPlaybackRebuild = true;
}

RT_FLASHMEM_FN void noteIdleMaint(uint32_t durationUs) {
  recordSample(state().idleMaint, durationUs);
}

RT_FLASHMEM_FN void noteLoadFrame(uint32_t durationUs) {
  recordSample(state().loadFrame, durationUs);
}

RT_FLASHMEM_FN void notePersistSave(uint32_t durationUs) {
  recordSample(state().persistSave, durationUs);
}

RT_FLASHMEM_FN void emitPendingDeadlineOneShots() {
  State& s = state();
#if defined(SESSION_CAPTURE)
#if defined(__IMXRT1062__)
  const char* midiClass = PSTR("on");
  if (s.pendingLateClass == 1) {
    midiClass = PSTR("off");
  } else if (s.pendingLateClass == 2) {
    midiClass = PSTR("clk");
  }
#else
  const char* midiClass = "on";
  if (s.pendingLateClass == 1) {
    midiClass = "off";
  } else if (s.pendingLateClass == 2) {
    midiClass = "clk";
  }
#endif
  if (s.pendingLateEvent) {
    DebugSessionCapture::midiDeadlineLateEvent(midiClass, s.pendingLateUs, s.pendingLateTick);
  }
  // A first take has no committed source, so its expected hold-empty probe can
  // recur every loop.  It carries no timing-failure signal and must not flood
  // the capture ring while the display is sharing the live-capture budget.
  if (s.pendingPlaybackRebuild &&
      s.pendingRebuildPath != PlaybackRebuildPath::GatherMissHoldEmpty) {
    DebugSessionCapture::playbackRebuild(s.pendingRebuildDurationUs, s.pendingRebuildWindowStartTick,
                                         s.pendingRebuildWindowLengthTicks, s.pendingRebuildRevision,
                                         s.pendingRebuildTrack, s.pendingRebuildSlot,
                                         s.pendingRebuildCount,
                                         playbackRebuildPathLabel(s.pendingRebuildPath));
  }
#endif
  s.pendingLateEvent = false;
  s.pendingPlaybackRebuild = false;
  s.pendingRebuildCount = 0;
  s.pendingRebuildDurationUs = 0;
  s.pendingRebuildTrack = 255;
  s.pendingRebuildSlot = 255;
  s.pendingRebuildPath = PlaybackRebuildPath::CompatibilityPasses;
}

RT_FLASHMEM_FN void recordLoopRemainderIfMeasuring(bool measure, const char* span, uint32_t startUs) {
#if defined(SESSION_CAPTURE)
  if (!measure) {
    return;
  }
  DebugSessionCapture::recordLoopRemainderSpan(span, micros() - startUs);
#else
  (void)measure;
  (void)span;
  (void)startUs;
#endif
}

RT_FLASHMEM_FN void recordMidiLedHelperRem(bool measure, uint8_t helper, uint32_t startUs) {
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
  // PROGMEM (.progmem) stays in flash. Ordinary string literals become .rodata
  // and the Teensy 4 linker places all .rodata* in DTCM, which sits on the
  // _VectorsRam 1 KB align edge (+4 KB RAM1).
  const char* span = PSTR("midi_led_lookup");
  if (helper == 1) {
    span = PSTR("midi_led_analyze");
  } else if (helper == 2) {
    span = PSTR("midi_led_bars");
  } else if (helper == 3) {
    span = PSTR("midi_led_gather");
  }
  recordLoopRemainderIfMeasuring(measure, span, startUs);
#elif defined(SESSION_CAPTURE)
  const char* span = "midi_led_lookup";
  if (helper == 1) {
    span = "midi_led_analyze";
  } else if (helper == 2) {
    span = "midi_led_bars";
  } else if (helper == 3) {
    span = "midi_led_gather";
  }
  recordLoopRemainderIfMeasuring(measure, span, startUs);
#else
  (void)measure;
  (void)helper;
  (void)startUs;
#endif
}

RT_FLASHMEM_FN void recordLoadFrameChildRem(uint8_t child, uint32_t startUs) {
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
  const char* span = PSTR("display_frame");
  if (child == 1) {
    span = PSTR("load_job");
  } else if (child == 2) {
    span = PSTR("first_commit");
  } else if (child == 3) {
    span = PSTR("boot_commit");
  }
  recordLoopRemainderIfMeasuring(true, span, startUs);
#elif defined(SESSION_CAPTURE)
  const char* span = "display_frame";
  if (child == 1) {
    span = "load_job";
  } else if (child == 2) {
    span = "first_commit";
  } else if (child == 3) {
    span = "boot_commit";
  }
  recordLoopRemainderIfMeasuring(true, span, startUs);
#else
  (void)child;
  (void)startUs;
#endif
}

RT_FLASHMEM_FN void recordIdleMaintChildRem(uint8_t child, uint32_t startUs) {
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
  const char* span = PSTR("idle_gather");
  if (child == 1) {
    span = PSTR("idle_reconstruct");
  } else if (child == 2) {
    span = PSTR("idle_append");
  }
  recordLoopRemainderIfMeasuring(true, span, startUs);
#elif defined(SESSION_CAPTURE)
  const char* span = "idle_gather";
  if (child == 1) {
    span = "idle_reconstruct";
  } else if (child == 2) {
    span = "idle_append";
  }
  recordLoopRemainderIfMeasuring(true, span, startUs);
#else
  (void)child;
  (void)startUs;
#endif
}

RT_FLASHMEM_FN bool maybeEmit(uint32_t nowUs) {
  emitPendingDeadlineOneShots();
  State& s = state();
  if (s.windowStartUs == 0) {
    s.windowStartUs = nowUs;
    s.lastEmitUs = nowUs;
    return false;
  }
  if (s.lastEmitUs != 0 && (nowUs - s.lastEmitUs) < kEmitIntervalUs) {
    return false;
  }
  const uint32_t windowElapsedUs = nowUs - s.windowStartUs;
  emitWindow(s, nowUs, windowElapsedUs);
  clearWindow(s);
  s.lateOneShotArmed = true;
  s.windowStartUs = nowUs;
  s.lastEmitUs = nowUs;
  return true;
}

RT_FLASHMEM_FN Snapshot peek(uint32_t nowUs) {
  const State& s = state();
  Snapshot out;
  out.midiGapMaxUs = s.midiGap.maxUs;
  out.midiGapOverCount = s.midiGap.overCount;
  out.midiInputMaxUs = s.midiInput.maxUs;
  out.midiInputOverCount = s.midiInput.overCount;
  out.clkMaxUs = s.clk.maxUs;
  out.clkOverCount = s.clk.overCount;
  out.tracksMaxUs = s.tracks.maxUs;
  out.tracksOverCount = s.tracks.overCount;
  out.usbdevMaxUs = s.usbdev.maxUs;
  out.usbdevOverCount = s.usbdev.overCount;
  out.dinMaxUs = s.din.maxUs;
  out.dinOverCount = s.din.overCount;
  out.hosttaskMaxUs = s.hosttask.maxUs;
  out.hosttaskOverCount = s.hosttask.overCount;
  out.hostdrainMaxUs = s.hostdrain.maxUs;
  out.hostdrainOverCount = s.hostdrain.overCount;
  out.usbreadMaxUs = s.usbread.maxUs;
  out.usbreadOverCount = s.usbread.overCount;
  out.usbdispMaxUs = s.usbdisp.maxUs;
  out.usbdispOverCount = s.usbdisp.overCount;
  out.usbcapMaxUs = s.usbcap.maxUs;
  out.usbcapOverCount = s.usbcap.overCount;
  out.usbthruMaxUs = s.usbthru.maxUs;
  out.usbthruOverCount = s.usbthru.overCount;
  out.usbclkMaxUs = s.usbclk.maxUs;
  out.usbclkOverCount = s.usbclk.overCount;
  out.usbnoteMaxUs = s.usbnote.maxUs;
  out.usbnoteOverCount = s.usbnote.overCount;
  out.usbccMaxUs = s.usbcc.maxUs;
  out.usbccOverCount = s.usbcc.overCount;
  out.usbtransMaxUs = s.usbtrans.maxUs;
  out.usbtransOverCount = s.usbtrans.overCount;
  out.noteappendMaxUs = s.noteappend.maxUs;
  out.noteappendOverCount = s.noteappend.overCount;
  out.notechgMaxUs = s.notechg.maxUs;
  out.notechgOverCount = s.notechg.overCount;
  out.notereconMaxUs = s.noterecon.maxUs;
  out.notereconOverCount = s.noterecon.overCount;
  out.notepairMaxUs = s.notepair.maxUs;
  out.notepairOverCount = s.notepair.overCount;
  out.idleMaintMaxUs = s.idleMaint.maxUs;
  out.idleMaintOverCount = s.idleMaint.overCount;
  out.loadFrameMaxUs = s.loadFrame.maxUs;
  out.loadFrameOverCount = s.loadFrame.overCount;
  out.persistSaveMaxUs = s.persistSave.maxUs;
  out.persistSaveOverCount = s.persistSave.overCount;
  out.lateOnMaxUs = s.lateOn.maxUs;
  out.lateOnCount = s.lateOn.overCount;
  out.lateOffMaxUs = s.lateOff.maxUs;
  out.lateOffCount = s.lateOff.overCount;
  out.lateClkMaxUs = s.lateClk.maxUs;
  out.lateClkCount = s.lateClk.overCount;
  out.extclkArrivalMaxUs = s.extclkArr.maxUs;
  out.extclkArrivalCount = s.extclkArr.overCount;
  out.extclkDispatchMaxUs = s.extclkDisp.maxUs;
  out.extclkDispatchOverCount = s.extclkDisp.overCount;
  out.clkBatchMax = s.clkBatchMax;
  out.clkBatchCount = s.clkBatchCount;
  out.servicePeriodUs = s.windowServicePeriodUs;
  out.usbBatchMax = s.usbBatchMax;
  out.usbBatchSaturatedCount = s.usbBatchSaturatedCount;
  out.usbBatchClockMax = s.usbBatchClockMax;
  out.usbBatchConsecutiveClockMax = s.usbBatchConsecutiveClockMax;
  out.trackRebuildMaxUs = s.trackRebuild.maxUs;
  out.trackRebuildOverCount = s.trackRebuild.overCount;
  out.trackOrderMaxUs = s.trackOrder.maxUs;
  out.trackOrderOverCount = s.trackOrder.overCount;
  out.trackSendMaxUs = s.trackSend.maxUs;
  out.trackSendOverCount = s.trackSend.overCount;
  out.trackSlotMaxUs = s.trackSlot.maxUs;
  out.trackSlotOverCount = s.trackSlot.overCount;
  out.trackResidMaxUs = s.trackResid.maxUs;
  out.trackResidOverCount = s.trackResid.overCount;
  for (uint8_t i = 0; i < MidiBatchComposition::kBucketCount; ++i) {
    out.usbBatchSizeBuckets[i] = s.usbBatchSizeBuckets[i];
    out.usbBatchClockBuckets[i] = s.usbBatchClockBuckets[i];
    out.usbBatchClockDispatchMaxUs[i] = s.usbBatchClockDispatchMaxUs[i];
  }
  out.lateOneShotArmed = s.lateOneShotArmed;
  out.pendingLateEvent = s.pendingLateEvent;
  out.pendingPlaybackRebuild = s.pendingPlaybackRebuild;
  out.pendingRebuildDurationUs = s.pendingRebuildDurationUs;
  out.pendingRebuildCount = s.pendingRebuildCount;
  out.pendingRebuildTrack = s.pendingRebuildTrack;
  out.pendingRebuildSlot = s.pendingRebuildSlot;
  out.pendingRebuildPath = s.pendingRebuildPath;
  out.clockPulses = s.clockPulses;
  if (s.windowStartUs != 0 && nowUs != 0) {
    out.windowElapsedUs = nowUs - s.windowStartUs;
  }
  return out;
}

}  // namespace RuntimeTimingTelemetry

#endif  // SESSION_CAPTURE || PIO_UNIT_TEST_NATIVE
