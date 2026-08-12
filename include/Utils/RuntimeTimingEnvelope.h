//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file RuntimeTimingEnvelope.h
 * @brief S0 observation-only timing envelope telemetry.
 *
 * Measures MSI gaps and MIDI/Clock/track durations. Does not admit work,
 * change service density, or drive any scheduling decision.
 *
 * See docs/Plans/runtime_scheduling_admission_model_architecture.md §26–31.
 */
#pragma once

#include <cstdint>

namespace RuntimeTimingEnvelope {

/** Rate-limit DIAG emission to once per this many microseconds. */
constexpr uint32_t kEmitIntervalUs = 5000000u;

/**
 * Soft ceiling used only to count `overCount` samples in S0 telemetry.
 * Not an admission contract and not an MSI ceiling (see architecture §25).
 */
constexpr uint32_t kObservationalSoftCeilingUs = 5000u;

struct Snapshot {
  uint32_t msiMaxUs = 0;
  uint32_t msiOverCount = 0;
  uint32_t midisvcMaxUs = 0;
  uint32_t midisvcOverCount = 0;
  uint32_t clkMaxUs = 0;
  uint32_t clkOverCount = 0;
  uint32_t tracksMaxUs = 0;
  uint32_t tracksOverCount = 0;
  uint32_t clockPulses = 0;
  uint32_t windowElapsedUs = 0;
};

void resetForTest();

/** Call on entry to MidiHandler::handleMidiInput — records MSI gap from prior exit. */
void noteMidiServiceEnter(uint32_t nowUs);

/** Call on exit from MidiHandler::handleMidiInput — records service duration. */
void noteMidiServiceExit(uint32_t nowUs);

/** Record duration of ClockManager::onMidiClockPulse (includes nested updateAllTracks). */
void noteClockDispatch(uint32_t durationUs);

/** Record duration of TrackManager::updateAllTracks (accumulate only; ISR-safe). */
void noteTracksUpdate(uint32_t durationUs);

/** Count one external MIDI Clock pulse for clockrate. */
void noteClockPulse();

/**
 * Emit Tier-A DIAG lines if the emit interval has elapsed.
 * Returns true when a window was emitted and counters reset.
 * Safe to call from main loop only (not from ISR).
 */
bool maybeEmit(uint32_t nowUs);

Snapshot peek(uint32_t nowUs = 0);

}  // namespace RuntimeTimingEnvelope
