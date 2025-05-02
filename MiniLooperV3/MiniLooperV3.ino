#include "Globals.h"
#include "Logger.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "TrackManager.h"
#include "ButtonManager.h"
#include "DisplayManager.h"
#include "Looper.h"

void setup() {
  // Initialize logger first
  logger.setup(LOG_DEBUG);  // Set to LOG_INFO for production
  
  // Load configuration
  loadConfig();
  
  // Initialize other components
  clockManager.setup();
  midiHandler.setup();
  buttonManager.setup({Buttons::RECORD, Buttons::PLAY});
  displayManager.setup();
  looper.setup();
  
  logger.info("MiniLooperV3 initialized");
}

void loop() {
  // Update clock first — will internally TrackManager.updateAlltracks if tick advanced both internal and external Midi
  clockManager.updateInternalClock();
  midiHandler.handleMidiInput();

  // Update less time sensitive modules
  buttonManager.update();
  looper.update();
  
  // Only update display if enough time has passed
  if (now - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL) {
    displayManager.update();
    lastDisplayUpdate = now;
  }
}

