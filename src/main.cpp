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
#include "SlotLoadSession.h"
#include "LoadLoopBudget.h"
#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"
#include "NoteEditManager.h"  // Keep temporarily for move note logic
#include "Utils/PerformanceMonitor.h"  // Performance monitoring
#include "Utils/MemoryMonitor.h"
#include "Utils/MemoryPressureLevel.h"
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
  displayManager.drawBootScreen();
  looper.setup();  // SD + loadState; USB + piano roll deferred until full slot drain

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

  HotPathTelemetry::emitSummary("startup");

  // finishBootSetup() runs in loop() with USB after bootInteractiveReady() — keep
  // OSTINATIX title until the restore queue is empty (no piano roll during drain).

  SC_SESSION_HEADER();
}

void loop() {
  // Start performance monitoring for this loop iteration
  // PerformanceMonitor::globalPerformanceMonitor.beginLoop();
  
  //Serial.println("Main: Loop");
  uint32_t now = millis();
  MemoryMonitor::updateAdvisoryPressureLevel(now);
  const MemoryPressureLevel pressure = MemoryMonitor::getAdvisoryPressureLevel();
  if (pressure >= MemoryPressureLevel::Low) {
    trackManager.tryReclaimDerivedViewCachesUnderPressure(pressure);
  }
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
    if (t.isRecording() || t.isOverdubbing() || t.isPlaying()) {
      timingCriticalTrackActive = true;
    }
  }

  HotPathTelemetry::processDeferredSummary();

  SC_CAPTURE_FLUSH(timingCriticalTrackActive ? 8 : 64);

  // Update SELECT mode for overdubbing if active
  if (editManager.getCurrentState() == editManager.getSelectNoteState()) {
    auto* selectState = static_cast<EditSelectNoteState*>(editManager.getSelectNoteState());
    selectState->updateForOverdubbing(editManager, trackManager.getSelectedTrack());
  }

  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    trackManager.getTrack(i).processDeferredIdleMaintenance(now);
  }

  // LoadLoopJob frame before display (Phase A): µs budgets, demote/park on focus.
  static bool bootSlotLoadRefreshPending = true;
  StorageManager::setBootTitleLoadDrain(bootSlotLoadRefreshPending);
  bool captureActive = false;
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    const Track& t = trackManager.getTrack(i);
    if (t.isRecording() || t.isOverdubbing()) {
      captureActive = true;
      break;
    }
  }
  const uint8_t focusTrack = trackManager.getSelectedTrackIndex();
  const uint8_t focusSlot = trackManager.getSelectedSlotIndex(focusTrack);
  const bool focusHadCommittedPasses =
      trackManager.getTrack(focusTrack).getLoop(focusSlot).hasCommittedPasses();
  const bool focusSlotRestoreWork = StorageManager::isFocusedLoopSlotRestoreWork();
  const bool slotRestoreTurnFree =
      !midiButtonManager.hasPendingTapAction() &&
      (!timingCriticalTrackActive || focusSlotRestoreWork);
  const bool allowDeferredSlotRestore =
      slotRestoreTurnFree &&
      !StorageManager::isRevisionLoadHeldForWorkspaceDirty();
  // After focus Commit of a long SD slot, skip OLED on this same turn — stacking
  // Commit + invalidateForSlotChange + update hard-faulted (session_20260718_204439 1/3).
  // Same-turn ensurePlaybackMergedEventsForSlot after focus done 1/3 also hard-faults
  // (session_20260718_210946); background done 1/3 without focus prewarm survives (205600).
  static int8_t deferredPlaybackPrewarmTrack = -1;
  static uint8_t deferredPlaybackPrewarmSlot = 0;
  bool skipDisplayAfterFocusCommit = false;
  if (deferredPlaybackPrewarmTrack >= 0) {
    const uint8_t prewarmTrack = static_cast<uint8_t>(deferredPlaybackPrewarmTrack);
    const uint8_t prewarmSlot = deferredPlaybackPrewarmSlot;
    deferredPlaybackPrewarmTrack = -1;
    skipDisplayAfterFocusCommit = true;
#if defined(SESSION_CAPTURE) || defined(PERF_TELEMETRY)
    Serial.print("[main] deferred playback prewarm enter ");
    Serial.print(prewarmTrack);
    Serial.print('/');
    Serial.println(prewarmSlot);
#endif
    trackManager.getTrack(prewarmTrack).ensurePlaybackMergedEventsForSlot(prewarmSlot);
    displayManager.invalidateLiveDisplayCache();
#if defined(SESSION_CAPTURE) || defined(PERF_TELEMETRY)
    Serial.println("[main] deferred playback prewarm leave");
#endif
  }
  if (allowDeferredSlotRestore) {
    const uint32_t budgetUs = LoadLoopBudget::resolveLoadLoopSliceBudgetUs(
        bootSlotLoadRefreshPending, focusSlotRestoreWork, captureActive);
    StorageManager::runDeferredFrame(budgetUs);
    if (!focusHadCommittedPasses &&
        trackManager.getTrack(focusTrack).getLoop(focusSlot).hasCommittedPasses()) {
      // Schedule prewarm for the *next* main turn — not stacked on Commit teardown.
      deferredPlaybackPrewarmTrack = static_cast<int8_t>(focusTrack);
      deferredPlaybackPrewarmSlot = focusSlot;
      skipDisplayAfterFocusCommit = true;
#if defined(SESSION_CAPTURE) || defined(PERF_TELEMETRY)
      Serial.print("[main] focus Commit; playback prewarm deferred ");
      Serial.print(focusTrack);
      Serial.print('/');
      Serial.println(focusSlot);
#endif
    }
    if (!timingCriticalTrackActive && !SlotLoadSession::isActive()) {
      StorageManager::processDeferredUndoSnapshots();
      StorageManager::processEditAutosave(looperState.getLooperState());
      trackManager.reclaimUnreferencedDisabledPasses();
    }
  }

  // Title screen stays until bootInteractiveReady(); update() early-returns while
  // bootSetupComplete_ is false. Skip OLED while a SlotLoadSession owns the SD bus
  // or on the turn focus just Committed (windowed paint runs next interval).
  if (!skipDisplayAfterFocusCommit && !SlotLoadSession::isActive() &&
      now - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    displayManager.update();
  }

  // Audible set COMMITTED: clear title, start USB, then enqueue remaining for background fill.
  if (bootSlotLoadRefreshPending && StorageManager::bootInteractiveReady()) {
    bootSlotLoadRefreshPending = false;
    StorageManager::setBootTitleLoadDrain(false);
    StorageManager::enqueueRemainingLoopSlotRestoresFromSd();
    displayManager.finishBootSetup();
    if (!midiHandler.isUsbHostReady()) {
      midiHandler.beginUsbHost();
      emitBootMilestone("usb_host", "begin");
    }
    // DROID may retain LED state across reset; refresh after host is live.
    trackManager.clearLeds();
    if (editManager.isLoopEditSession()) {
      midiHandler.sendLedFeedbackNoteOn(100, 64);
      midiHandler.sendLedFeedbackNoteOff(100);
    }
    trackManager.onBootSlotLoadComplete();
    midiHandler.processDroidUsbHostOutbound();
    displayManager.update();
    lastDisplayUpdate = now;
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

