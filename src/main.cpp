#include <Arduino.h>
#include "Globals.h"
#include "Logger.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "TrackManager.h"
#include "ButtonManager.h"
#include "DisplayManager.h"
#include "DisplayManager2.h"
#include "Looper.h"
#include "Track.h"

void setup() {
  // Simple led Check to see if Teensy is responding
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Turn on LED for 200ms
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  while (!Serial && millis() < 2000) delay(10);  // Teensy-safe wait

  // Initialize logger first with Serial.begin
  logger.setup(LOG_DEBUG);  // Set to LOG_INFO for production
  // Load configuration
  loadConfig();

  trackManager.setup();

  // Log initial track states
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    TrackState state = trackManager.getTrack(i).getState();
    logger.debug("Track %d state: %s", i, trackManager.getTrack(i).getStateName(state));
  }

  // Initialize other components
  clockManager.setup();
  midiHandler.setup();
  buttonManager.setup({Buttons::RECORD, Buttons::PLAY});
  displayManager2.setup();
  Serial.println("Main: Dispaly Setup done");
  looper.setup();

  
}

void loop() {
  //Serial.println("Main: Loop");
  uint32_t now = millis();
  // Poll MIDI input
  midiHandler.handleMidiInput();

  // Update less time sensitive modules
  buttonManager.update();
  looper.update();

  // Only update display if enough time has passed (steady-rate)
  if (now - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    displayManager2.update();
  }
}

