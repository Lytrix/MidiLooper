// ClockManager.h
#ifndef CLOCKMANAGER_H
#define CLOCKMANAGER_H

#include <Arduino.h>

enum ClockSource {
  CLOCK_INTERNAL,
  CLOCK_EXTERNAL
};

class ClockManager {
public:
  ClockManager();

  uint32_t getCurrentTick();

  // --- Order these exactly as they should be initialized ---
  uint32_t microsPerTick;                        // 1
  volatile uint32_t currentTick;                // 2
  volatile uint32_t lastMidiClockTime;          // 3
  volatile uint32_t lastInternalTickTime;       // 4
  const uint32_t midiClockTimeout = 500000;     // 5
  bool pendingStart = false;                    // 6
  bool externalClockPresent;                    // 7

  // --- Methods ---
  void setupClock();
  void updateInternalClock();
  void onMidiClockPulse();
  void checkClockSource();
  void onMidiStart();
  void onMidiStop();
  void setBpm(uint16_t newBpm);
  void setTicksPerQuarterNote(uint16_t newTicks);

private:
  void onMidiClockTick();
  void updateAllTracks(uint32_t currentTick);
};

extern ClockManager clockManager;

#endif
