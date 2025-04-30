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

  // --- State flags ---
  bool pendingStart;

  // --- Public methods ---
  void setupClock();
  void updateInternalClock();
  void onMidiClockPulse();
  void onMidiStart();
  void onMidiStop();
  void checkClockSource();
  void setBpm(uint16_t newBpm);
  void setTicksPerQuarterNote(uint16_t newTicks);

  // --- Accessors ---
  uint32_t getCurrentTick() const;
  bool isExternalClockPresent() const;
  void setExternalClockPresent(bool present);
  void setLastMidiClockTime(uint32_t time);

private:
  // --- Timing data ---
  uint32_t microsPerTick;
  volatile uint32_t currentTick;
  volatile uint32_t lastMidiClockTime;
  volatile uint32_t lastInternalTickTime;

  // --- Clock detection ---
  bool externalClockPresent;
  const uint32_t midiClockTimeout = 500000; // 500ms: timeout for external clock  
  //  it’s not dependent on dynamic input, there’s no need to initialize it in the constructor.

  // --- Internal handlers ---
  void onMidiClockTick();
  void updateAllTracks(uint32_t currentTick);
};

extern ClockManager clockManager;

#endif
