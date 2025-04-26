#include "Globals.h"
#include "Looper.h"
#include "LooperState.h"
#include "Clock.h"
#include "ClockManager.h"
#include "TrackManager.h"
//#include "ButtonManager.h"
//#include "DisplayManager.h"

#include "MidiHandler.h"


void setup() {
  //setupGlobals();
  setupClock();
  setupLooper();
  //setupButtons();
  //setupDisplay();
  setupMidi();
}

void loop() {
  checkClockSource();  // keep clock source updated
  //updateButtons();
  updateLooper();
  //updateDisplay();

  handleMidiInput();
  
  uint32_t currentTick = Clock::getCurrentTick(); // PPQN ticks

  trackManager.handleQuantizedStart(currentTick); 
  trackManager.handleQuantizedStop(currentTick);

  trackManager.updateAllTracks(currentTick);
}

