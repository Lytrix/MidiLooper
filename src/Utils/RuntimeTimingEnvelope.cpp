//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RuntimeTimingEnvelope.h"

#include "Utils/DebugSessionCapture.h"

namespace RuntimeTimingEnvelope {
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
  Accumulator msi;
  Accumulator midisvc;
  Accumulator clk;
  Accumulator tracks;
  uint32_t clockPulses = 0;
  uint32_t lastServiceExitUs = 0;
  uint32_t serviceEnterUs = 0;
  bool serviceActive = false;
  uint32_t windowStartUs = 0;
  uint32_t lastEmitUs = 0;
};

State& state() {
  static State s;
  return s;
}

void clearWindow(State& s) {
  s.msi = Accumulator{};
  s.midisvc = Accumulator{};
  s.clk = Accumulator{};
  s.tracks = Accumulator{};
  s.clockPulses = 0;
}

void emitWindow(const State& s, uint32_t nowUs, uint32_t windowElapsedUs) {
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::runtimeTimingEnvelope("msi", s.msi.maxUs, s.msi.overCount);
  DebugSessionCapture::runtimeTimingEnvelope("midisvc", s.midisvc.maxUs, s.midisvc.overCount);
  DebugSessionCapture::runtimeTimingEnvelope("clk", s.clk.maxUs, s.clk.overCount);
  DebugSessionCapture::runtimeTimingEnvelope("tracks", s.tracks.maxUs, s.tracks.overCount);
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

void noteMidiServiceEnter(uint32_t nowUs) {
  State& s = state();
  if (s.windowStartUs == 0) {
    s.windowStartUs = nowUs;
  }
  if (s.lastServiceExitUs != 0) {
    recordSample(s.msi, nowUs - s.lastServiceExitUs);
  }
  s.serviceEnterUs = nowUs;
  s.serviceActive = true;
}

void noteMidiServiceExit(uint32_t nowUs) {
  State& s = state();
  if (s.serviceActive) {
    recordSample(s.midisvc, nowUs - s.serviceEnterUs);
    s.serviceActive = false;
  }
  s.lastServiceExitUs = nowUs;
}

void noteClockDispatch(uint32_t durationUs) {
  recordSample(state().clk, durationUs);
}

void noteTracksUpdate(uint32_t durationUs) {
  recordSample(state().tracks, durationUs);
}

void noteClockPulse() {
  State& s = state();
  ++s.clockPulses;
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
  out.msiMaxUs = s.msi.maxUs;
  out.msiOverCount = s.msi.overCount;
  out.midisvcMaxUs = s.midisvc.maxUs;
  out.midisvcOverCount = s.midisvc.overCount;
  out.clkMaxUs = s.clk.maxUs;
  out.clkOverCount = s.clk.overCount;
  out.tracksMaxUs = s.tracks.maxUs;
  out.tracksOverCount = s.tracks.overCount;
  out.clockPulses = s.clockPulses;
  if (s.windowStartUs != 0 && nowUs != 0) {
    out.windowElapsedUs = nowUs - s.windowStartUs;
  }
  return out;
}

}  // namespace RuntimeTimingEnvelope
