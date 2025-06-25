//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <Arduino.h>
#include "Globals.h"
#include "Logger.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "TrackManager.h"
#include "ButtonManager.h"
#include "MidiButtonManager.h"
#include "DisplayManager.h"
#include "LooperState.h"
#include "Looper.h"
#include "Track.h"
#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"

void setup() {
  // Simple led Check to see if Teensy is responding
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Turn on LED for 200ms
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  
  // Setup MIDI Button Manager instead of physical buttons
  midiButtonManager.setup();
  
  Serial.begin(115200);
  while (!Serial && millis() < 2000) delay(10);  // Teensy-safe wait

  // Initialize logger first with Serial.begin
  logger.setup(LOG_DEBUG);  // Set to LOG_INFO for production
  logger.setCategoryEnabled(CAT_MIDI, true);  // Ensure MIDI logging is enabled
  // Initialize looper and load last project and states
  looper.setup();
  //loadConfig();

  trackManager.setup();

  // // Log initial track states
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    TrackState state = trackManager.getTrack(i).getState();
    logger.debug("Track %d state: %s", i, trackManager.getTrack(i).getStateName(state));
  }


  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    Track &track = trackManager.getTrack(t);
    // ... after setState ...
    Serial.print("Track "); Serial.print(t); Serial.print(" loaded state: ");
    Serial.println(track.getStateName(track.getState()));
}

  // Initialize other components
  clockManager.setup();
  midiHandler.setup();
 
  displayManager.setup();
  Serial.println("Main: Dispaly Setup done");
  looper.setup();

  
}

void loop() {
  //Serial.println("Main: Loop");
  uint32_t now = millis();
  // Poll MIDI input
  midiHandler.handleMidiInput();

  // Update looper state to set button logic
  looperState.update();

  // Update MIDI button manager instead of physical buttons
  midiButtonManager.update();
  looper.update();

  // Update SELECT mode for overdubbing if active
  if (editManager.getCurrentState() == editManager.getSelectNoteState()) {
    auto* selectState = static_cast<EditSelectNoteState*>(editManager.getSelectNoteState());
    selectState->updateForOverdubbing(editManager, trackManager.getSelectedTrack());
  }
  
  // Only update display if enough time has passed (steady-rate)
  if (now - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    displayManager.update();
  }
}

