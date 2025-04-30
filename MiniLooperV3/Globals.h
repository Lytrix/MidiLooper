// Globals.h
#ifndef GLOBALS_H
#define GLOBALS_H

#pragma once

#include <Arduino.h>

extern bool DEBUG;
extern bool DEBUG_MIDI;
extern bool DEBUG_NOTES;
extern bool DEBUG_BUTTONS;
extern bool DEBUG_DISPLAY;

#define NUM_TRACKS 4  // Change this to however many tracks you want

// 🛠 Define internal PPQN scale
#define INTERNAL_PPQN 192
#define MIDI_CLOCK_PPQN 24
#define TICKS_PER_CLOCK (INTERNAL_PPQN / MIDI_CLOCK_PPQN)

// LCD Pin definitions
const int LCD_RS = 12;
const int LCD_ENABLE = 11;
const int LCD_D4 = 32;
const int LCD_D5 = 31;
const int LCD_D6 = 30;
const int LCD_D7 = 29;

// Timing
extern float bpm;
extern uint32_t ticksPerQuarterNote;   // usually 24 for standard MIDI clock
extern uint32_t quartersPerBar;
extern const uint32_t ticksPerBar; // 4/4

void setupGlobals();
bool isBarBoundary();                 // Used By LooperState transistion function

#endif
