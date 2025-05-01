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
  // Update clock first
  clockManager.updateInternalClock();
  uint32_t currentTick = clockManager.getCurrentTick();
  
  // Update other components
  midiHandler.handleMidiInput();
  buttonManager.update();
  trackManager.updateAllTracks(currentTick);
  displayManager.update();
  looper.update();
}

