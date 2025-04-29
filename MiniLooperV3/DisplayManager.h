#pragma once

#include <Arduino.h>
#include "TrackManager.h"

class DisplayManager {
public:
    DisplayManager();
    void setup();
    void update();

    void setSelectedTrack(uint8_t trackIndex);

private:
    uint8_t selectedTrack;
    
    void showTrackStates();
    void showPianoRoll();
    
    char noteName(uint8_t midiNote); 
};

extern DisplayManager displayManager;