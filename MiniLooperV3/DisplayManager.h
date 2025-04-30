#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#pragma once

#include <Arduino.h>
#include <LiquidCrystal.h>
#include "TrackManager.h"
#include "Track.h"

// --------------------
// DisplayManager handles all LCD UI
// --------------------
class DisplayManager {
public:
    DisplayManager();
    void setup();     // Call once in setup()
    void update();    // Call periodically to refresh display

private:
    void showTrackStates();  // Top row: Track statuses
};

// --------------------
// Global instance
// --------------------
extern DisplayManager displayManager;

// --------------------
// Utility display functions
// --------------------
//void updateNoteDisplay(uint32_t currentTick, const std::vector<NoteEvent>& notes, uint32_t loopLengthTicks, uint32_t startLoopTick);

void displayNoteBarAllInOneLine(const Track& track, LiquidCrystal& lcd);
void displaySimpleNoteBar(const std::vector<NoteEvent>& notes, uint32_t currentTick, uint32_t loopLengthTicks, uint32_t startLoopTick, LiquidCrystal& lcd);
void drawNotePageWithNoteEvents(const std::vector<NoteEvent>& notes, uint32_t loopLengthTicks, uint32_t currentTick, uint32_t startLoopTick);

#endif  // DISPLAY_MANAGER_H
