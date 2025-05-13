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
  // clockManager.setup(); internal clock
  midiHandler.setup();
  buttonManager.setup({Buttons::RECORD, Buttons::PLAY});
  displayManager.setup();
  displayManager2.setup();
  looper.setup();
  
  
}

static uint32_t lastDraw = 0;
  
void loop() {

  // Update clock first — will internally TrackManager.updateAlltracks if tick advanced both internal and external Midi
  //clockManager.updateInternalClock();
  midiHandler.handleMidiInput();

  // Update less time sensitive modules
  buttonManager.update();
  looper.update();
  // Only update display if enough time has passed
  if (millis() - lastDraw >= 30) {   // redraw at ≤30 Hz
    lastDraw = millis();
    displayManager.update();
    auto t0 = micros();
    displayManager2.update();
    auto dt = micros() - t0;
    //logger.debug("draw time: %d µs", dt);
  }
}

