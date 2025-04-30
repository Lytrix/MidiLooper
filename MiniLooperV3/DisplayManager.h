#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#pragma once

#include <Arduino.h>
#include <LiquidCrystal.h>
#include "TrackManager.h"
#include "Track.h"

class DisplayManager {
public:
    DisplayManager();
    void setup();
    void update();
    
private:
    void showTrackStates();
};

// This function is used to render the simple bar display for a track
void updateNoteDisplay(uint32_t currentTick, const std::vector<NoteEvent>& notes, uint32_t loopLengthTicks);

// Declare Track::displayNoteBarAllInOneLine here so it can be used externally
// class Track;  // Forward declaration

void displayNoteBarAllInOneLine(const Track& track, LiquidCrystal& lcd);
void displaySimpleNoteBar(const std::vector<NoteEvent>& notes, uint32_t currentTick, uint32_t loopLengthTicks, uint32_t startLoopTick, LiquidCrystal& lcd);


extern DisplayManager displayManager;

#endif  // DISPLAY_MANAGER_H
