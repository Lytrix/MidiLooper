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
#include "DeferredJobScheduler.h"
#include "SlotLoadSession.h"
#include "LoadLoopBudget.h"
#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"
#include "ControlSurfaceManager.h"
#include "Utils/PerformanceMonitor.h"  // Performance monitoring
#include "Utils/MemoryMonitor.h"
#include "Utils/MemoryPressureLevel.h"
#include "Utils/MemoryPool.h"
#include "LoopEventStore.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/BootTelemetry.h"
#include "Utils/MidiServiceDrain.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include <cstdio>

// noinline: a single call site would otherwise inline this into loop() and stay in ITCM.
FLASHMEM __attribute__((noinline)) static void maybeUpdateDisplayForNoteEditSelection(
    uint32_t now, uint32_t& lastDisplayUpdate) {
  if (!editManager.shouldForceNoteEditDisplayUpdate()) {
    return;
  }
  lastDisplayUpdate = now;
  displayManager.update();
}

#if defined(SESSION_CAPTURE)
FLASHMEM __attribute__((noinline)) static void maybeRecordLoopPrefixRemainder(uint32_t startUs) {
  if (!trackManager.anyLoopPrefixMeasureAfterUndo()) {
    return;
  }
  DebugSessionCapture::recordLoopRemainderSpan("loop_prefix", micros() - startUs);
}

FLASHMEM __attribute__((noinline)) static void maybeRecordPrefixChild(bool measure,
                                                                     const char* span,
                                                                     uint32_t startUs) {
  if (!measure) {
    return;
  }
  DebugSessionCapture::recordLoopRemainderSpan(span, micros() - startUs);
}

FLASHMEM __attribute__((noinline)) static void notePostRemainderWindows() {
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    trackManager.getTrack(i).notePlayingMidiDrainAfterOverdubStopIdle();
    trackManager.getTrack(i).noteLoopPrefixMeasureAfterUndo();
  }
}
#endif

// BAR→LED prefix children. FLASHMEM so child rem does not cross the RAM1 32KB ITCM page.
FLASHMEM __attribute__((noinline)) static void runLoopPrefixAfterBar(uint32_t now,
                                                                     uint32_t& lastDisplayUpdate) {
#if defined(SESSION_CAPTURE)
  const bool measure = trackManager.anyLoopPrefixMeasureAfterUndo();
  uint32_t childStartUs = 0;
#endif

#if defined(SESSION_CAPTURE)
  if (measure) {
    childStartUs = micros();
  }
#endif
  looperState.update();
#if defined(SESSION_CAPTURE)
  maybeRecordPrefixChild(measure, "looper_state", childStartUs);
  if (measure) {
    childStartUs = micros();
  }
#endif
  midiButtonManager.update();
#if defined(SESSION_CAPTURE)
  maybeRecordPrefixChild(measure, "midi_buttons", childStartUs);
  if (measure) {
    childStartUs = micros();
  }
#endif
  midiFaderManager.update();
#if defined(SESSION_CAPTURE)
  maybeRecordPrefixChild(measure, "midi_faders", childStartUs);
  if (measure) {
    childStartUs = micros();
  }
#endif
  barStepButtonHandler.update();
#if defined(SESSION_CAPTURE)
  maybeRecordPrefixChild(measure, "bar_step", childStartUs);
  if (measure) {
    childStartUs = micros();
  }
#endif
  controlSurfaceManager.update();
#if defined(SESSION_CAPTURE)
  maybeRecordPrefixChild(measure, "control_surface", childStartUs);
  if (measure) {
    childStartUs = micros();
  }
#endif
  maybeUpdateDisplayForNoteEditSelection(now, lastDisplayUpdate);
#if defined(SESSION_CAPTURE)
  maybeRecordPrefixChild(measure, "note_edit_disp", childStartUs);
  if (measure) {
    childStartUs = micros();
  }
#endif
#if defined(ENABLE_GPIO_BUTTONS)
  gpioButtonManager.update();
#if defined(SESSION_CAPTURE)
  maybeRecordPrefixChild(measure, "gpio_buttons", childStartUs);
  if (measure) {
    childStartUs = micros();
  }
#endif
#endif
  looper.update();
#if defined(SESSION_CAPTURE)
  maybeRecordPrefixChild(measure, "looper_update", childStartUs);
#endif

  static uint32_t lastLedUpdate = 0;
  constexpr uint32_t LED_UPDATE_INTERVAL_MS = 8;
  if (now - lastLedUpdate >= LED_UPDATE_INTERVAL_MS) {
    lastLedUpdate = now;
#if defined(SESSION_CAPTURE)
    if (measure) {
      childStartUs = micros();
    }
#endif
    trackManager.updateMidiLedsDeferred();
    midiHandler.processDroidUsbHostOutbound();
#if defined(SESSION_CAPTURE)
    maybeRecordPrefixChild(measure, "midi_leds", childStartUs);
#endif
  }
}

// Keep LoadLoopJob + OLED orchestration out of ITCM — RAM1 is at the 32KB page edge.
// noinline: a single call site would otherwise inline this into loop() and stay in ITCM.
FLASHMEM __attribute__((noinline)) static void runDeferredLoadAndDisplayFrame(
    uint32_t now, uint32_t& lastDisplayUpdate, bool timingCriticalTrackActive) {
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
  // Phase 5.2: focus High while PLAYING; background Low only when transport idle
  // (canRunBackgroundLoadLoopNow — PLAYING skips Low; 223713 freeze). Capture/tap block all.
  const bool slotRestoreTurnFree =
      !midiButtonManager.hasPendingTapAction() && !captureActive;
  const bool allowDeferredSlotRestore =
      slotRestoreTurnFree &&
      !StorageManager::isRevisionLoadHeldForWorkspaceDirty();
  bool skipDisplayAfterFocusCommit = false;

  const bool backgroundOnlyLoad =
      !bootSlotLoadRefreshPending && !focusSlotRestoreWork;
  // Paint before any background-only LoadLoopJob frame (PLAYING or STOPPED fill).
  const bool playBackgroundLoad = backgroundOnlyLoad;
  // session_20260812_012342: never skip OLED during capture (RC-A).
  // session_20260814_014156: never skip while STOPPED — boot/background slot restore
  // left the panel on sparse frame-1 for ~6.8s while load_frame ran.
  const bool skipFocusLoadForSlotSession =
      focusSlotRestoreWork && SlotLoadSession::isActive() && !captureActive &&
      timingCriticalTrackActive;

  // Paint before background LoadLoopJob while PLAYING (session_20260718_213044).
  // Skip OLED only on focus Commit / focus-load session — not every SlotLoadSession.
  if (playBackgroundLoad) {
    const bool skipFocusLoad =
        skipDisplayAfterFocusCommit || skipFocusLoadForSlotSession;
    if (!skipFocusLoad && (editManager.shouldForceNoteEditDisplayUpdate() ||
                           now - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL)) {
      lastDisplayUpdate = now;
#if defined(SESSION_CAPTURE)
      const uint32_t displayStartUs = micros();
#endif
      displayManager.update();
#if defined(SESSION_CAPTURE)
      RuntimeTimingTelemetry::recordLoadFrameChildRem(0, displayStartUs);
#endif
    }
  }

  // Finish focus LoadLoopJob (especially Committing) even when tap/revision hold would
  // otherwise skip the deferred frame — 000205 starved apply after parse_leave.
  if (allowDeferredSlotRestore || focusSlotRestoreWork) {
    const uint32_t budgetUs =
        allowDeferredSlotRestore
            ? LoadLoopBudget::resolveLoadLoopSliceBudgetUs(
                  bootSlotLoadRefreshPending, focusSlotRestoreWork, captureActive)
            : LoadLoopBudget::FocusRestoreUs;
#if defined(SESSION_CAPTURE)
    const uint32_t loadJobStartUs = micros();
#endif
    DeferredJobScheduler::runFrame(budgetUs);
#if defined(SESSION_CAPTURE)
    RuntimeTimingTelemetry::recordLoadFrameChildRem(1, loadJobStartUs);
#endif
    if (!focusHadCommittedPasses &&
        trackManager.getTrack(focusTrack).getLoop(focusSlot).hasCommittedPasses()) {
      skipDisplayAfterFocusCommit = true;
      // Same-frame windowed prewarm — buffer already freed at commit_armed; trySlot is
      // fail-soft. Deferring to next frame raced save/reclaim (000659).
      Track& prewarmTrackRef = trackManager.getTrack(focusTrack);
#if defined(SESSION_CAPTURE)
      const uint32_t firstCommitStartUs = micros();
#endif
      prewarmTrackRef.ensurePlaybackMergedEventsForSlot(focusSlot);
#if defined(SESSION_CAPTURE)
      RuntimeTimingTelemetry::recordLoadFrameChildRem(2, firstCommitStartUs);
#endif
      displayManager.invalidateLiveDisplayCache();
    }
    if (allowDeferredSlotRestore && !timingCriticalTrackActive &&
        !SlotLoadSession::isActive()) {
      StorageManager::processDeferredUndoSnapshots();
      StorageManager::processEditAutosave(looperState.getLooperState());
      trackManager.reclaimUnreferencedDisabledPasses();
    }
  }

  if (!playBackgroundLoad) {
    const bool skipFocusLoad =
        skipDisplayAfterFocusCommit || skipFocusLoadForSlotSession;
    if (!skipFocusLoad && (editManager.shouldForceNoteEditDisplayUpdate() ||
                           now - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL)) {
      lastDisplayUpdate = now;
#if defined(SESSION_CAPTURE)
      const uint32_t displayStartUs = micros();
#endif
      displayManager.update();
#if defined(SESSION_CAPTURE)
      RuntimeTimingTelemetry::recordLoadFrameChildRem(0, displayStartUs);
#endif
    }
  }

  if (bootSlotLoadRefreshPending && StorageManager::bootInteractiveReady()) {
#if defined(SESSION_CAPTURE)
    const uint32_t bootCommitStartUs = micros();
#endif
    bootSlotLoadRefreshPending = false;
    StorageManager::setBootTitleLoadDrain(false);
    StorageManager::enqueueRemainingLoopSlotRestoresFromSd();
    displayManager.finishBootSetup();
    if (!midiHandler.isUsbHostReady()) {
      midiHandler.beginUsbHost();
      emitBootMilestone("usb_host", "begin");
    }
    trackManager.clearMidiLeds();
    if (editManager.isLoopEditSession()) {
      midiHandler.sendLedFeedbackNoteOn(100, 64);
      midiHandler.sendLedFeedbackNoteOff(100);
    }
    trackManager.onBootSlotLoadComplete();
    midiHandler.processDroidUsbHostOutbound();
    displayManager.update();
    lastDisplayUpdate = now;
#if defined(SESSION_CAPTURE)
    RuntimeTimingTelemetry::recordLoadFrameChildRem(3, bootCommitStartUs);
#endif
  }
}

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
  MemoryMonitor::logStatus(true);  // Log heap after loops allocated

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
  
  // Connect ControlSurfaceManager to MidiFaderProcessor
  controlSurfaceManager.setFaderProcessor(&midiFaderManager.getProcessor());
  controlSurfaceManager.setDisplayManager(&displayManager);
  editManager.setEditEventListener(&controlSurfaceManager);
  
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
  editManager.sendEditSessionChange(EditSessionType::Loop, true);

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
  MemoryMonitor::logStatus(true);  // Log heap after full setup
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
  // Poll MIDI input
  midiHandler.handleMidiInput();
#if defined(SESSION_CAPTURE)
  const uint32_t loopPrefixStartUs = micros();
#endif

  midiHandler.processDroidUsbHostOutbound();

  // Detect clock source changes (external timeout -> internal fallback)
  clockManager.checkClockSource();

  // Session capture: bar boundary marker for tick<->micros alignment (no-op without SESSION_CAPTURE)
  SC_UPDATE(clockManager.getCurrentTick(), Config::TICKS_PER_BAR);

  runLoopPrefixAfterBar(now, lastDisplayUpdate);

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

  bool captureActiveForMidiDrain = false;
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    const Track& t = trackManager.getTrack(i);
    if (t.isRecording() || t.isOverdubbing()) {
      captureActiveForMidiDrain = true;
      break;
    }
  }
  const bool postOverdubPlayingMidiDrain = trackManager.anyPlayingMidiDrainAfterOverdubStop();
#if defined(SESSION_CAPTURE)
  maybeRecordLoopPrefixRemainder(loopPrefixStartUs);
#endif

  // Post-overdub PLAYING only: poll before idle so visual-cache work cannot start a stacked gap.
  if (MidiServiceDrain::aroundIdleMaintenance(postOverdubPlayingMidiDrain)) {
    midiHandler.handleMidiInput();
  }
#if defined(SESSION_CAPTURE)
  uint32_t remainderStartUs = micros();
#endif
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    trackManager.getTrack(i).processDeferredIdleMaintenance(now);
  }
#if defined(SESSION_CAPTURE)
  const uint32_t idleMaintUs = micros() - remainderStartUs;
  RuntimeTimingTelemetry::noteIdleMaint(idleMaintUs);
  DebugSessionCapture::recordLoopRemainderSpan("idle_maint", idleMaintUs);
#endif
  if (MidiServiceDrain::aroundIdleMaintenance(postOverdubPlayingMidiDrain)) {
    midiHandler.handleMidiInput();
  }
#if defined(SESSION_CAPTURE)
  notePostRemainderWindows();
#else
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    trackManager.getTrack(i).notePlayingMidiDrainAfterOverdubStopIdle();
  }
#endif
#if defined(SESSION_CAPTURE)
  remainderStartUs = micros();
#endif

  // Load/Commit/prewarm before pressure reclaim — reclaim after a 64-bar Commit raced the
  // deferred prewarm path (000659: commit_prewarm_q then silence).
  runDeferredLoadAndDisplayFrame(now, lastDisplayUpdate, timingCriticalTrackActive);
#if defined(SESSION_CAPTURE)
  const uint32_t loadFrameUs = micros() - remainderStartUs;
  RuntimeTimingTelemetry::noteLoadFrame(loadFrameUs);
  DebugSessionCapture::recordLoopRemainderSpan("load_frame", loadFrameUs);
#endif

  // RC-C C: RECORD/OVERDUB after OLED. Same site also covers the post-overdub PLAYING window
  // so load_frame cannot stack with persist. Not all PLAYING.
  if (MidiServiceDrain::afterDeferredDisplay(captureActiveForMidiDrain,
                                             postOverdubPlayingMidiDrain)) {
    midiHandler.handleMidiInput();
  }

  controlSurfaceManager.processDeferredFaderMotorSync();

  MemoryMonitor::updateAdvisoryPressureLevel(now);
  const MemoryPressureLevel pressure = MemoryMonitor::getAdvisoryPressureLevel();
  if (pressure >= MemoryPressureLevel::Low) {
    trackManager.tryReclaimDerivedViewCachesUnderPressure(pressure);
  }
  if (pressure >= MemoryPressureLevel::Critical) {
    // Policy: Critical reclaims disabled-pass chunks (memory_pressure_reclaim_refinement §Policy).
    // Idle-only reclaim at line ~114 misses chunk pressure during RECORDING/PLAYING/OVERDUBBING
    // (session_20260811_021117: append failures with heap headroom, pool at CHUNK_RESERVE).
    trackManager.reclaimUnreferencedDisabledPasses(nullptr, true);
  }

#if defined(SESSION_CAPTURE)
  const uint32_t persistSaveStartUs = micros();
#endif
  StorageManager::processDeferredSaveState(looperState.getLooperState());
#if defined(SESSION_CAPTURE)
  const uint32_t persistSaveUs = micros() - persistSaveStartUs;
  RuntimeTimingTelemetry::notePersistSave(persistSaveUs);
  DebugSessionCapture::recordLoopRemainderSpan("persist_save", persistSaveUs);
#endif

  // Poll USB host again after deferred SD/display work so DROID button note-ons are not
  // dropped when the main loop was busy (session_20260810_234059: note-off without note-on).
  midiHandler.handleMidiInput();
  midiButtonManager.update();

#if defined(SESSION_CAPTURE)
  StorageManager::processHitlSerialCommands();
#endif

  // S0: observation-only timing telemetry emission (no scheduling decisions).
  // maybeEmit drains pending first-late / rebuild one-shots, then the 5 s window.
  RuntimeTimingTelemetry::maybeEmit(micros());

  // Log memory every 60 seconds. Reports O(1) fields only: the external-pool free/used walk
  // (sm_malloc_stats_pool) blocked the loop 593 ms in 141815 and lost external MIDI clock.
  // timingCriticalTrackActive is derived from track state, so it cannot tell whether a clock
  // is still streaming — the walk stays in setup, where no transport can run.
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

