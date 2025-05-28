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
  void checkClockSource();  // TODO: Implement clock source detection and switching
  void setBpm(uint16_t newBpm);
  void setTicksPerQuarterNote(uint16_t newTicks);
  void handleMidiClock();  // Handle incoming MIDI clock messages

  // --- Accessors ---
  uint32_t getCurrentTick() const;
  bool isExternalClockPresent() const;
  void setExternalClockPresent(bool present);
  bool isClockRunning() const; // Returns true if either the internal or external clock is running
  uint32_t setLastMidiClockTime(uint32_t lastMidiClockTime);

private:
  // --- Timing data ---
  uint32_t microsPerTick;
  volatile uint32_t currentTick;
  volatile uint32_t lastMidiClockTime;
  volatile uint32_t lastInternalTickTime;

  // --- Clock detection ---
  bool externalClockPresent;
  const uint32_t midiClockTimeout = 500000; // 500ms: timeout for external clock
};

extern ClockManager clockManager;

#endif
