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

  uint32_t microsPerTick;
  volatile uint32_t lastMidiClockTime;
  volatile uint32_t lastInternalTickTime;
  const uint32_t midiClockTimeout = 500000; // 500ms = if no MIDI clock pulses for 0.5s, assume external clock lost

  void setupClock();
  void updateInternalClock();
  void onMidiClockPulse(); // call this from your MIDI handler when MIDI clock message received
  void checkClockSource();
  void onMidiStart();
  void onMidiStop();

  bool pendingStart = false;
  bool externalClockPresent;
  
  void setBpm(uint16_t newBpm);
  void setTicksPerQuarterNote(uint16_t newTicks);

private:
  volatile uint32_t currentTick;
};

extern ClockManager clockManager;

#endif
