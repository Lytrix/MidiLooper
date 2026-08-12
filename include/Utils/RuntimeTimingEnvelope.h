//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file RuntimeTimingEnvelope.h
 * @brief S0 observation-only timing envelope telemetry.
 *
 * Measures MSI gaps, MIDI/Clock/track durations, S0b MIDI-service drain
 * segments, S0c USB-device sub-segments, and S0d handleMidiMessage
 * remainder sums. Does not admit work, change service density, or drive
 * any scheduling decision.
 *
 * See docs/Plans/runtime_scheduling_admission_model_architecture.md §26–31l.
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
  uint32_t usbdevMaxUs = 0;
  uint32_t usbdevOverCount = 0;
  uint32_t dinMaxUs = 0;
  uint32_t dinOverCount = 0;
  uint32_t hosttaskMaxUs = 0;
  uint32_t hosttaskOverCount = 0;
  uint32_t hostdrainMaxUs = 0;
  uint32_t hostdrainOverCount = 0;
  uint32_t usbreadMaxUs = 0;
  uint32_t usbreadOverCount = 0;
  uint32_t usbdispMaxUs = 0;
  uint32_t usbdispOverCount = 0;
  uint32_t usbcapMaxUs = 0;
  uint32_t usbcapOverCount = 0;
  uint32_t usbthruMaxUs = 0;
  uint32_t usbthruOverCount = 0;
  uint32_t usbclkMaxUs = 0;
  uint32_t usbclkOverCount = 0;
  uint32_t usbnoteMaxUs = 0;
  uint32_t usbnoteOverCount = 0;
  uint32_t usbccMaxUs = 0;
  uint32_t usbccOverCount = 0;
  uint32_t usbtransMaxUs = 0;
  uint32_t usbtransOverCount = 0;
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

/** S0b: USB-device read loop + dispatchMidiBatch. */
void noteUsbDeviceDrain(uint32_t durationUs);

/** S0b: DIN (Serial8) read loop + dispatchMidiBatch. */
void noteDinDrain(uint32_t durationUs);

/** S0b: usbHost.Task() stack service. */
void noteUsbHostTask(uint32_t durationUs);

/** S0b: usbHostMIDI.read() drain (callbacks into handleMidiMessage). */
void noteUsbHostDrain(uint32_t durationUs);

/** S0c: usbMIDI.read() loop only (USB-device drain, before dispatch). */
void noteUsbDeviceRead(uint32_t durationUs);

/** S0c: dispatchMidiBatch for the USB-device batch. */
void noteUsbDeviceDispatch(uint32_t durationUs);

/** S0c: start summing nested USB-device dispatch work. */
void beginUsbDeviceNested();

/** S0c: add one SOURCE_USB SC_MIDI_IN duration into the current dispatch sum. */
void addUsbDeviceCapture(uint32_t durationUs);

/** S0c: add one SOURCE_USB sendMidiThru duration into the current dispatch sum. */
void addUsbDeviceThru(uint32_t durationUs);

/** S0d: add one SOURCE_USB Clock / onMidiClockPulse duration into the current dispatch sum. */
void addUsbDeviceClock(uint32_t durationUs);

/** S0d: add one SOURCE_USB NoteOn/NoteOff handler duration into the current dispatch sum. */
void addUsbDeviceNote(uint32_t durationUs);

/** S0d: add one SOURCE_USB CC/pitch/AT/PC handler duration into the current dispatch sum. */
void addUsbDeviceCc(uint32_t durationUs);

/** S0d: add one SOURCE_USB Start/Stop/Continue handler duration into the current dispatch sum. */
void addUsbDeviceTransport(uint32_t durationUs);

/** S0c/S0d: record the nested sums as one sample each and reset them. */
void commitUsbDeviceNested();

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
