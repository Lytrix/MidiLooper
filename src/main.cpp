//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <Arduino.h>
#include <SD.h>
#include "Globals.h"
#include "Logger.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "TrackManager.h"
#include "ButtonManager.h"
#include "MidiButtonManager.h"
#include "MidiFaderManager.h"
#include "BarStepButtonHandler.h"
#include "DisplayManager.h"
#include "LooperState.h"
#include "Looper.h"
#include "Track.h"
#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"
#include "NoteEditManager.h"  // Keep temporarily for move note logic
#include "Utils/PerformanceMonitor.h"  // Performance monitoring
#include "Utils/MemoryMonitor.h"

void setup() {
  delay(500);  // USB re-enumeration after reset
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  if (CrashReport) {
    Serial.print(CrashReport);
    Serial.println("--- CrashReport above ---");
    // Also log to SD (Teensy 4.1 built-in) in case Serial output is missed
    if (SD.begin(BUILTIN_SDCARD)) {
      File f = SD.open("crashlog.txt", FILE_WRITE);
      if (f) {
        f.print(CrashReport);
        f.close();
      }
    }
    delay(5000);
  }

  // Allocate Loop arrays immediately - before USB Host, faders, etc. consume heap
  trackManager.allocateLoopsEarly();
  MemoryMonitor::logStatus();  // Log heap after loops allocated

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Turn on LED for 200ms
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  
  midiButtonManager.setup();
  
  // Setup new V2 MIDI Fader Manager for fader handling
  midiFaderManager.setup();
  
  barStepButtonHandler.setup();
  // Manual test: enable for BarStepButton debug output over Serial
  barStepButtonHandler.setTestLoggingEnabled(true);
  logger.setCategoryEnabled(CAT_BAR_STEP_BUTTON, true);
  
  // Connect NoteEditManager to MidiFaderProcessor
  noteEditManager.setFaderProcessor(&midiFaderManager.getProcessor());
  
  // Keep old manager temporarily for move note logic
  //midiButtonManager.setup();

  // Initialize logger (Serial already begun above)
  logger.setup(LOG_DEBUG);  // Set to LOG_INFO for production
  logger.setCategoryEnabled(CAT_MIDI, false);  // Ensure MIDI logging is enabled
  logger.setCategoryEnabled(CAT_MIDI_LED, false);  // LED update logging (channel/destinations)
  logger.setCategoryEnabled(CAT_STORAGE, false);  // StorageManager v3 per-slot save progress (verbose)

  midiHandler.setup();
  trackManager.setup();
  looper.setup();  // SD + loadState; setSelectedTrack triggers forceLedUpdate (midi now ready)

  // Startup policy: enter LOOP_EDIT deterministically and sync DROID explicitly.
  noteEditManager.sendMainEditModeChange(NoteEditManager::MAIN_MODE_LOOP_EDIT);

  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    TrackState state = trackManager.getTrack(i).getState();
    logger.debug("Track %d state: %s", i, trackManager.getTrack(i).getStateName(state));
  }

  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    Track &track = trackManager.getTrack(t);
    Serial.print("Track "); Serial.print(t); Serial.print(" loaded state: ");
    Serial.println(track.getStateName(track.getState()));
  }

  clockManager.setup();
  displayManager.setup();
  Serial.println("Main: Display Setup done");

  logger.info("Performance monitoring initialized");
  MemoryMonitor::logStatus();  // Log heap after full setup

  // Clear all bar/16th LEDs for a clean start (DROID may retain state from before disconnect)
  trackManager.clearLeds();
  // Send initial 16th-note LEDs on startup (otherwise only sent when switching tracks or clock runs)
  trackManager.forceLedUpdate(clockManager.getCurrentTick());
}

void loop() {
  // Start performance monitoring for this loop iteration
  // PerformanceMonitor::globalPerformanceMonitor.beginLoop();
  
  //Serial.println("Main: Loop");
  uint32_t now = millis();
  // Poll MIDI input
  midiHandler.handleMidiInput();

  // LED updates (decoupled from clock path - runs in main loop)
  static uint32_t lastLedUpdate = 0;
  constexpr uint32_t LED_UPDATE_INTERVAL_MS = 8;
  if (now - lastLedUpdate >= LED_UPDATE_INTERVAL_MS) {
    lastLedUpdate = now;
    trackManager.updateLedsDeferred();
  }

  // Detect clock source changes (external timeout -> internal fallback)
  clockManager.checkClockSource();

  // Update looper state to set button logic
  looperState.update();

  // Update new V2 MIDI button manager for button handling
  midiButtonManager.update();
  
  // Update new V2 MIDI fader manager for fader handling
  midiFaderManager.update();
  
  barStepButtonHandler.update();
  
  noteEditManager.update();
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

  // Log memory every 60 seconds to console (non-blocking, after display)
  static uint32_t lastMemoryLog = 0;
  constexpr uint32_t MEMORY_LOG_INTERVAL_MS = 60000;
  if (now - lastMemoryLog >= MEMORY_LOG_INTERVAL_MS) {
    lastMemoryLog = now;
    MemoryMonitor::logStatus();
    if (MemoryMonitor::isLowMemory(20 * 1024)) {
      logger.warning("[Memory] Low heap (<20 KB free) - consider reducing undo or freeing slots");
    }
  }

  // End performance monitoring for this loop iteration
  // PerformanceMonitor::globalPerformanceMonitor.endLoop();
  
  // Log performance warnings if system is under stress
  // if (PerformanceMonitor::globalPerformanceMonitor.isSystemStressed()) {
  //   const auto& metrics = PerformanceMonitor::globalPerformanceMonitor.getCurrentMetrics();
  //   logger.warning("Performance stress detected: CPU=%d%%, Loop=%luμs, RAM=%luKB", 
  //                  metrics.cpuUsagePercent, metrics.loopTimeMicros, metrics.freeRAMBytes / 1024);
    
  //   // Get optimization suggestions
  //   auto suggestions = PerformanceMonitor::globalPerformanceMonitor.getOptimizationSuggestions();
  //   for (const auto& suggestion : suggestions) {
  //     logger.info("Performance suggestion: %s", suggestion.c_str());
  //   }
  // }
}

