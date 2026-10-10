//  Copyright (c)  2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// S7.5a envelope instruments: the **native default for their telemetry entry points**.
//
// A native suite that compiles a production owner which reports one of these — e.g.
// `LoopPlaybackRuntime::replenishCanonicalCaptureChunk` reporting its replenish accounting, or
// `MidiHandler`/`TrackCaptureInput` reporting a capture stamp — but that does not also link
// `src/Utils/RuntimeTimingTelemetry.cpp` would otherwise fail to link. Suites that link the real module (or
// include the .cpp, as `test_runtime_timing_telemetry` does) get the **real** accounting; the rest take these
// weak no-ops, so no suite needs per-file wiring and no include order matters.
//
// Weak on purpose: symbol resolution decides. On the device this file is not compiled at all (it is added to
// native test builds only by `scripts/native_test_link.py`).
#include <cstdint>

namespace RuntimeTimingTelemetry {

__attribute__((weak)) void noteInputServiceTick(uint32_t) {}
__attribute__((weak)) void noteCaptureStampTick(uint32_t) {}
__attribute__((weak)) void noteReplenishWork(uint32_t) {}
__attribute__((weak)) void noteReplenishRefused(uint32_t) {}
__attribute__((weak)) void noteReplenishCompletion(uint32_t) {}

}  // namespace RuntimeTimingTelemetry
