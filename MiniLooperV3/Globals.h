// Globals.h
#ifndef GLOBALS_H
#define GLOBALS_H

#pragma once

#include <Arduino.h>

#define NUM_TRACKS 4  // Change this to however many tracks you want

// LCD Pin definitions
const int LCD_RS = 12;
const int LCD_ENABLE = 11;
const int LCD_D4 = 32;
const int LCD_D5 = 31;
const int LCD_D6 = 30;
const int LCD_D7 = 29;


extern uint8_t selectedTrack;

// Timing
extern float bpm;
extern volatile uint32_t currentTick;  // updated by timer interrupt or MIDI clock
extern uint32_t ticksPerQuarterNote;   // usually 24 for standard MIDI clock
extern uint32_t quartersPerBar;
extern const uint32_t ticksPerBar; // 4/4

void setupGlobals();
bool isBarBoundary();                 // Used By LooperState transistion function

#endif
