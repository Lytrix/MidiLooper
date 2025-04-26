// Globals.h
#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

extern volatile uint32_t currentTick;  // Te most important tick of all!

extern float bpm;
extern volatile uint32_t currentTick;  // updated by timer interrupt or MIDI clock
extern uint32_t ticksPerQuarterNote;   // usually 24 for standard MIDI clock
extern uint32_t quartersPerBar;
extern const uint32_t ticksPerBar; // 4/4

void setupGlobals();
bool isBarBoundary();                 // Used By LooperState transistion function

#endif
