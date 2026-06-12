//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef CLOCKMANAGER_H
#define CLOCKMANAGER_H

#include <Arduino.h>

enum ClockSource {
  CLOCK_INTERNAL,
  CLOCK_EXTERNAL
};

/**
 * @class ClockManager
 * @brief Provides and manages the global timing (tick) source for the MIDI looper.
 *
 * Generates an internal clock based on microsecond timing (configurable BPM and ticks-per-quarter-note)
 * and/or processes external MIDI clock pulses. Handles MIDI Start/Stop commands, tracks the
 * current playback tick, and detects external clock presence with a timeout. Other modules call
 * getCurrentTick() to synchronize playback, recording, and UI updates.
 */
class ClockManager {
public:
  ClockManager();

  // --- State flags ---
  bool pendingStart;

  // --- Public methods ---
  void setup();
  void updateInternalClock();
  void onMidiClockPulse();
  void onMidiStart();
  void onMidiStop();
  void checkClockSource();
  void setBpm(uint16_t newBpm);
  void setBpmFloat(float newBpm);
  void setTicksPerQuarterNote(uint16_t newTicks);
  void handleMidiClock();  // Handle incoming MIDI clock messages
  void requestTransitionTo(ClockSource target);

  // --- Accessors ---
  uint32_t getCurrentTick() const;
  bool isExternalClockPresent() const;
  ClockSource getClockSource() const;
  bool isClockRunning() const;
  bool isTransportRunning() const;
  /// True when the global tick advances on bar boundaries (quantize record start / arm next bar).
  bool shouldQuantizeRecordStart() const;
  void toggleTransport();
  void resetToLoopStart();
  void setCurrentTick(uint32_t tick);
  uint32_t setLastMidiClockTime(uint32_t lastMidiClockTime);

private:
  void actuallyTransition(ClockSource from, ClockSource to);

  // --- Timing data ---
  uint32_t microsPerTick;
  volatile uint32_t currentTick;
  volatile uint32_t lastMidiClockTime;
  volatile uint32_t lastInternalTickTime;
  volatile bool firstPulseAfterStart;  // Hold tick at 0 through first Clock after Start

  // --- Clock source state machine ---
  ClockSource clockSource;
  ClockSource pendingClockSource;
  bool transitionPending;

  // --- BPM from MIDI clock (sliding window: 25 timestamps = 24 intervals = 1 quarter note) ---
  static const uint8_t PULSE_BUF_SIZE = 25;  // 24 intervals + 1
  uint32_t pulseTimestamps[PULSE_BUF_SIZE];
  uint8_t pulseHead;
  uint8_t pulseFillCount;
  float bpmSmoothed;

  // --- Clock detection ---
  const uint32_t midiClockTimeout = 500000; // 500ms: timeout for external clock
};

extern ClockManager clockManager;

#endif
