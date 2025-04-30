#include "ClockManager.h"
#include "MidiHandler.h"
#include "TrackManager.h"
#include "Looper.h"
#include "ButtonManager.h"
#include "DisplayManager.h"

void setup() {
  clockManager.setupClock();
  midiHandler.setup();
  looper.setup();
  buttonManager.setup({9, 10});
  displayManager.setup();
}

void loop() {
  clockManager.checkClockSource();  // keep clock source updated
  midiHandler.handleMidiInput();
  
  uint32_t tickNow = clockManager.getCurrentTick();
  trackManager.handleQuantizedStart(tickNow); 
  trackManager.handleQuantizedStop(tickNow);
  trackManager.updateAllTracks(tickNow);
   
  buttonManager.update();
  //buttonManager.updateButtons(); // perform logic
  looper.update();

  static uint32_t lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 50) { // Update display every ~30ms
    displayManager.update();
    lastDisplayUpdate = millis();
  }
  
}

