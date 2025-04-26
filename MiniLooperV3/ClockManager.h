// ClockManager.h
#ifndef CLOCKMANAGER_H
#define CLOCKMANAGER_H

#include <Arduino.h>

enum ClockSource {
  CLOCK_INTERNAL,
  CLOCK_EXTERNAL
};

extern uint32_t microsPerTick;
extern volatile uint32_t lastMidiClockTime;
extern volatile uint32_t lastInternalTickTime;
const uint32_t midiClockTimeout = 500000; // 500ms = if no MIDI clock pulses for 0.5s, assume external clock lost

void setupClock();
void updateInternalClock();
void onMidiClockPulse(); // call this from your MIDI handler when MIDI clock message received
void checkClockSource();

extern ClockSource clockSource;

#endif
