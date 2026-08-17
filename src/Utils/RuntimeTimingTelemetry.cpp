//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RuntimeTimingTelemetry.h"

#include "Utils/DebugSessionCapture.h"

#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#include <Arduino.h>
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

RT_FLASHMEM_FN void recordLateness(Accumulator& acc, uint32_t latenessUs) {
  if (latenessUs > acc.maxUs) {
    acc.maxUs = latenessUs;
  }
  if (latenessUs > 0) {
    ++acc.overCount;
  }
}

RT_FLASHMEM_FN uint32_t latenessAfterDeadline(uint32_t nowUs, uint32_t dueUs, uint32_t periodUs) {
  if (periodUs == 0) {
    return 0;
  }
  const uint32_t deadlineUs = dueUs + periodUs;
  if (static_cast<int32_t>(nowUs - deadlineUs) <= 0) {
    return 0;
  }
  return nowUs - deadlineUs;
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
};

State& state() {
  static State s;
  return s;
}

RT_FLASHMEM_FN void queueFirstLate(State& s, uint8_t lateClass, uint32_t latenessUs, uint32_t tick) {
  if (latenessUs == 0 || !s.lateOneShotArmed) {
    return;
  }
  s.lateOneShotArmed = false;
  s.pendingLateEvent = true;
  s.pendingLateClass = lateClass;
  s.pendingLateUs = latenessUs;
  s.pendingLateTick = tick;
}

RT_FLASHMEM_FN void clearWindow(State& s) {
  s.midiGap = Accumulator{};
  s.midiInput = Accumulator{};
  s.clk = Accumulator{};
  s.tracks = Accumulator{};
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
  s.clockPulses = 0;
}

RT_FLASHMEM_FN void emitWindow(const State& s, uint32_t nowUs, uint32_t windowElapsedUs) {
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::runtimeTimingTelemetry("midi_gap", s.midiGap.maxUs, s.midiGap.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("midi_input", s.midiInput.maxUs, s.midiInput.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("clk", s.clk.maxUs, s.clk.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("tracks", s.tracks.maxUs, s.tracks.overCount);
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

RT_FLASHMEM_FN void notePlaybackServiceEnter(uint32_t nowUs, uint32_t tickPeriodUs) {
  State& s = state();
  s.servicePeriodUs = tickPeriodUs;
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
}

RT_FLASHMEM_FN void recordNoteSendLateness(bool isNoteOn, uint32_t nowUs, uint32_t tick) {
  State& s = state();
  const uint32_t latenessUs =
      latenessAfterDeadline(nowUs, s.serviceDueUs, s.servicePeriodUs);
  recordLateness(isNoteOn ? s.lateOn : s.lateOff, latenessUs);
  queueFirstLate(s, isNoteOn ? 0 : 1, latenessUs, tick);
}

RT_FLASHMEM_FN void recordOutgoingClockSend(uint32_t nowUs, uint32_t clockPeriodUs,
                                            uint32_t onTimeWindowUs, uint32_t tick) {
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
                                          uint32_t windowLengthTicks, uint32_t revision) {
  State& s = state();
  s.pendingPlaybackRebuild = true;
  s.pendingRebuildDurationUs = durationUs;
  s.pendingRebuildWindowStartTick = windowStartTick;
  s.pendingRebuildWindowLengthTicks = windowLengthTicks;
  s.pendingRebuildRevision = revision;
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
  if (s.pendingPlaybackRebuild) {
    DebugSessionCapture::playbackRebuild(s.pendingRebuildDurationUs, s.pendingRebuildWindowStartTick,
                                         s.pendingRebuildWindowLengthTicks,
                                         s.pendingRebuildRevision);
  }
#endif
  s.pendingLateEvent = false;
  s.pendingPlaybackRebuild = false;
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
  out.lateOneShotArmed = s.lateOneShotArmed;
  out.pendingLateEvent = s.pendingLateEvent;
  out.pendingPlaybackRebuild = s.pendingPlaybackRebuild;
  out.clockPulses = s.clockPulses;
  if (s.windowStartUs != 0 && nowUs != 0) {
    out.windowElapsedUs = nowUs - s.windowStartUs;
  }
  return out;
}

}  // namespace RuntimeTimingTelemetry
