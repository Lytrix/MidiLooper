//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <Arduino.h>
#include <SD.h>
#include "Globals.h"
#include "Logger.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "TrackManager.h"
#include "GpioButtonManager.h"
#include "MidiButtonManager.h"
#include "MidiFaderManager.h"
#include "BarStepButtonHandler.h"
#include "DisplayManager.h"
#include "LooperState.h"
#include "Looper.h"
#include "StorageManager.h"
#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"
#include "NoteEditManager.h"  // Keep temporarily for move note logic
#include "Utils/PerformanceMonitor.h"  // Performance monitoring
#include "Utils/MemoryMonitor.h"
#include "Utils/MemoryPool.h"
#include "LoopEventStore.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/BootTelemetry.h"

void setup() {
  HotPathTelemetry::reset();
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
    delay(500);
  }

  // Initialise the global MIDI event pool now that the Teensy core has completed
  // PSRAM hardware initialisation. This must happen before any Track/Loop allocations.
  MemoryPool::globalMidiEventPool.init();
  LoopEventStore::initPool();

  // Allocate Loop arrays immediately - before USB Host, faders, etc. consume heap
  trackManager.allocateLoopsEarly();
  trackManager.prewarmPlaybackRuntime();
  MemoryMonitor::logStatus();  // Log heap after loops allocated

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Turn on LED for 200ms
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  
  midiButtonManager.setup();
  
  // Setup new V2 MIDI Fader Manager for fader handling
  midiFaderManager.setup();
  
  barStepButtonHandler.setup();
  // Manual test only: enable BarStepButton debug output (see docs/Guides/MANUAL_TEST_BAR_STEP_BUTTONS.md)
  barStepButtonHandler.setTestLoggingEnabled(false);
  logger.setCategoryEnabled(CAT_BAR_STEP_BUTTON, false);
  
  // Connect NoteEditManager to MidiFaderProcessor
  noteEditManager.setFaderProcessor(&midiFaderManager.getProcessor());
  noteEditManager.setDisplayManager(&displayManager);
  
  // Keep old manager temporarily for move note logic
  //midiButtonManager.setup();

  // Initialize logger (Serial already begun above)
#if defined(SESSION_CAPTURE)
  logger.setup(LOG_DEBUG);
  logger.setCategoryEnabled(CAT_MIDI, true);
#else
  logger.setup(LOG_WARNING);
  logger.setCategoryEnabled(CAT_MIDI, false);
#endif
  logger.setCategoryEnabled(CAT_MIDI_LED, false);  // LED update logging (channel/destinations)
  logger.setCategoryEnabled(CAT_STORAGE, false);  // StorageManager v3 per-slot save progress (verbose)

  midiHandler.setup();
  emitBootMilestone("usb_host", "deferred");
  trackManager.setup();
  displayManager.beginBootOled();
  displayManager.drawBootStatusMessage("Loading...");
  looper.setup();  // SD + loadState; LED side effects suppressed until USB host ready

  midiHandler.beginUsbHost();
  emitBootMilestone("usb_host", "begin");

  displayManager.finishBootSetup();

  // Startup policy: enter LOOP_EDIT deterministically and sync DROID explicitly.
  editManager.sendEditSessionChange(EditSessionType::Loop);

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
#if defined(ENABLE_GPIO_BUTTONS)
  gpioButtonManager.setup({Buttons::BUTTON_A_PIN, Buttons::BUTTON_B_PIN, Buttons::BUTTON_C_PIN,
                           Buttons::BUTTON_D_PIN, Buttons::ENCODER_BUTTON_PIN});
#endif
  Serial.println("Main: Display Setup done");

  logger.info("Performance monitoring initialized");
  MemoryMonitor::resetInternalHeapWatermark();
  MemoryMonitor::logStatus();  // Log heap after full setup
  {
    char heapDetail[16];
    snprintf(heapDetail, sizeof(heapDetail), "%lu",
             static_cast<unsigned long>(MemoryMonitor::getInternalHeapFreeBytes()));
    emitBootMilestone("heap", heapDetail);
  }

  // Clear all bar/16th LEDs for a clean start (DROID may retain state from before disconnect)
  trackManager.clearLeds();
  // Send initial 16th-note LEDs on startup (otherwise only sent when switching tracks or clock runs)
  trackManager.forceLedUpdate(clockManager.getCurrentTick());

  HotPathTelemetry::emitSummary("startup");

  SC_SESSION_HEADER();
}

void loop() {
  // Start performance monitoring for this loop iteration
  // PerformanceMonitor::globalPerformanceMonitor.beginLoop();
  
  //Serial.println("Main: Loop");
  uint32_t now = millis();
  // Poll MIDI input
  midiHandler.handleMidiInput();

  midiHandler.processDroidUsbHostOutbound();

  // Detect clock source changes (external timeout -> internal fallback)
  clockManager.checkClockSource();

  // Session capture: bar boundary marker for tick<->micros alignment (no-op without SESSION_CAPTURE)
  SC_UPDATE(clockManager.getCurrentTick(), Config::TICKS_PER_BAR);

  // Update looper state to set button logic
  looperState.update();

  // Update new V2 MIDI button manager for button handling
  midiButtonManager.update();
  
  // Update new V2 MIDI fader manager for fader handling
  midiFaderManager.update();
  
  barStepButtonHandler.update();
  
  noteEditManager.update();
#if defined(ENABLE_GPIO_BUTTONS)
  gpioButtonManager.update();
#endif
  looper.update();

  // LED updates after transport tick / pending slot commit (same frame as loop boundary).
  static uint32_t lastLedUpdate = 0;
  constexpr uint32_t LED_UPDATE_INTERVAL_MS = 8;
  if (now - lastLedUpdate >= LED_UPDATE_INTERVAL_MS) {
    lastLedUpdate = now;
    trackManager.updateLedsDeferred();
    midiHandler.processDroidUsbHostOutbound();
  }

  bool timingCriticalTrackActive = false;
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    const Track& t = trackManager.getTrack(i);
    if (t.isPlaying() || t.isRecording() || t.isOverdubbing()) {
      timingCriticalTrackActive = true;
      break;
    }
  }

  HotPathTelemetry::processDeferredSummary();

  SC_CAPTURE_FLUSH(timingCriticalTrackActive ? 8 : 64);

  // Update SELECT mode for overdubbing if active
  if (editManager.getCurrentState() == editManager.getSelectNoteState()) {
    auto* selectState = static_cast<EditSelectNoteState*>(editManager.getSelectNoteState());
    selectState->updateForOverdubbing(editManager, trackManager.getSelectedTrack());
  }

  // Render display before deferred SD slices; keep updating during SD I/O.
  if (now - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    displayManager.update();
  }

  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    trackManager.getTrack(i).processDeferredIdleMaintenance(now);
  }

  if (!timingCriticalTrackActive && !StorageManager::isRevisionLoadHeldForWorkspaceDirty()) {
    const bool deferredLoopSlotRestorePending = StorageManager::hasPendingLoopSlotRestore();
    StorageManager::processDeferredLoopSlotRestore();
    StorageManager::processDeferredUndoSnapshots();
    // Slot restore reads the full loop file synchronously; repaint so the OLED does not stick.
    if (deferredLoopSlotRestorePending) {
      displayManager.update();
      lastDisplayUpdate = now;
    }
    StorageManager::processEditAutosave(looperState.getLooperState());
    trackManager.reclaimUnreferencedDisabledPasses();
  }

  static bool bootSlotLoadRefreshPending = true;
  if (bootSlotLoadRefreshPending && !StorageManager::hasPendingLoopSlotRestore()) {
    bootSlotLoadRefreshPending = false;
    trackManager.onBootSlotLoadComplete();
    midiHandler.processDroidUsbHostOutbound();
  }

  StorageManager::processDeferredSaveState(looperState.getLooperState());

#if defined(SESSION_CAPTURE)
  StorageManager::processHitlSerialCommands();
#endif

  // Log memory every 60 seconds only when transport/capture is idle.
  // Runtime PSRAM stats walk (sm_malloc_stats_pool) can take hundreds of ms
  // on large pools and must never run during PLAYING/RECORDING/OVERDUBBING.
  static uint32_t lastMemoryLog = 0;
  constexpr uint32_t MEMORY_LOG_INTERVAL_MS = 60000;
  if (!timingCriticalTrackActive && now - lastMemoryLog >= MEMORY_LOG_INTERVAL_MS) {
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

