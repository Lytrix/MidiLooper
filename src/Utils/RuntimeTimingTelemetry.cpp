//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RuntimeTimingTelemetry.h"

#include "Utils/DebugSessionCapture.h"

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
  Accumulator displayFrame;
  Accumulator loadJob;
  Accumulator firstCommit;
  Accumulator bootCommit;
  Accumulator persistSave;
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
};

State& state() {
  static State s;
  return s;
}

void clearWindow(State& s) {
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
  s.displayFrame = Accumulator{};
  s.loadJob = Accumulator{};
  s.firstCommit = Accumulator{};
  s.bootCommit = Accumulator{};
  s.persistSave = Accumulator{};
  s.clockPulses = 0;
}

void emitWindow(const State& s, uint32_t nowUs, uint32_t windowElapsedUs) {
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
  DebugSessionCapture::runtimeTimingTelemetry("display_frame", s.displayFrame.maxUs,
                                             s.displayFrame.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("load_job", s.loadJob.maxUs, s.loadJob.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("first_commit", s.firstCommit.maxUs,
                                             s.firstCommit.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("boot_commit", s.bootCommit.maxUs,
                                             s.bootCommit.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("persist_save", s.persistSave.maxUs,
                                             s.persistSave.overCount);
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

void noteIdleMaint(uint32_t durationUs) {
  recordSample(state().idleMaint, durationUs);
}

void noteLoadFrame(uint32_t durationUs) {
  recordSample(state().loadFrame, durationUs);
}

void noteDisplayFrame(uint32_t durationUs) {
  recordSample(state().displayFrame, durationUs);
}

void noteLoadJob(uint32_t durationUs) {
  recordSample(state().loadJob, durationUs);
}

void noteFirstCommit(uint32_t durationUs) {
  recordSample(state().firstCommit, durationUs);
}

void noteBootCommit(uint32_t durationUs) {
  recordSample(state().bootCommit, durationUs);
}

void notePersistSave(uint32_t durationUs) {
  recordSample(state().persistSave, durationUs);
}

void recordLoopRemainderIfMeasuring(bool measure, const char* span, uint32_t startUs) {
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

#if defined(__IMXRT1062__)
#define RT_FLASHMEM_FN __attribute__((noinline, section(".flashmem")))
#else
#define RT_FLASHMEM_FN
#endif

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

bool maybeEmit(uint32_t nowUs) {
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
  s.windowStartUs = nowUs;
  s.lastEmitUs = nowUs;
  return true;
}

Snapshot peek(uint32_t nowUs) {
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
  out.displayFrameMaxUs = s.displayFrame.maxUs;
  out.displayFrameOverCount = s.displayFrame.overCount;
  out.loadJobMaxUs = s.loadJob.maxUs;
  out.loadJobOverCount = s.loadJob.overCount;
  out.firstCommitMaxUs = s.firstCommit.maxUs;
  out.firstCommitOverCount = s.firstCommit.overCount;
  out.bootCommitMaxUs = s.bootCommit.maxUs;
  out.bootCommitOverCount = s.bootCommit.overCount;
  out.persistSaveMaxUs = s.persistSave.maxUs;
  out.persistSaveOverCount = s.persistSave.overCount;
  out.clockPulses = s.clockPulses;
  if (s.windowStartUs != 0 && nowUs != 0) {
    out.windowElapsedUs = nowUs - s.windowStartUs;
  }
  return out;
}

}  // namespace RuntimeTimingTelemetry
