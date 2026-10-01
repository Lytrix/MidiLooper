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

static MidiServiceDrain::DeferredIdleOwnerCursor sDeferredIdleOwnerCursor{};

/// S4f.2/B06: the eligibility decision lives on `Track` (`hasRealtimeDeferredIdleWork`) so it is
/// testable and so a later stage adds its term in one place rather than editing this scan.
static bool hasRealtimeIdleWork(const Track& track) {
  return track.hasRealtimeDeferredIdleWork();
}

FLASHMEM __attribute__((noinline)) static bool anySnapshotPublicationBacklog() {
  const uint8_t trackCount = trackManager.getTrackCount();
  for (uint8_t i = 0; i < trackCount; ++i) {
    if (trackManager.getTrack(i).hasSnapshotPublicationBacklogOnAnySlot()) {
      return true;
    }
  }
  return false;
}

FLASHMEM __attribute__((noinline)) static bool anyPendingSessionExitSnapshot() {
  const uint8_t trackCount = trackManager.getTrackCount();
  for (uint8_t i = 0; i < trackCount; ++i) {
    if (trackManager.getTrack(i).hasPendingSessionExitSnapshotOnAnySlot()) {
      return true;
    }
  }
  return false;
}

FLASHMEM __attribute__((noinline)) static MidiServiceDrain::DeferredIdleOwner
takeDeferredIdleOwnerTurn(bool postOverdubPlayingMidiDrain) {
  return MidiServiceDrain::selectDeferredIdleOwner(
      sDeferredIdleOwnerCursor, anySnapshotPublicationBacklog(),
      postOverdubPlayingMidiDrain && anyPendingSessionExitSnapshot());
}

// noinline: a single call site would otherwise inline this into loop() and stay in ITCM.
FLASHMEM __attribute__((noinline)) static void processKindBoundaryUndoWarmAfterDisplay() {
  editManager.processKindBoundaryUndoWarm(trackManager.getSelectedTrack());
}

FLASHMEM __attribute__((noinline)) static void finishAdmittedDisplayPaintTurn(
    uint32_t now, uint32_t& lastDisplayUpdate) {
#if defined(SESSION_CAPTURE)
  const uint32_t displayStartUs = micros();
#endif
  displayManager.update();
#if defined(SESSION_CAPTURE)
  RUNTIME_TIMING_RECORD_LOAD_FRAME_CHILD_REM(0, displayStartUs);
#endif
  if (!displayManager.hasPendingPaintPhase()) {
    lastDisplayUpdate = now;
    processKindBoundaryUndoWarmAfterDisplay();
  }
}

FLASHMEM __attribute__((noinline)) static bool admitDisplayPaintTurn(
    uint32_t now, uint32_t lastDisplayUpdate) {
  return DisplayWindowUtils::admitDisplayFrameTurn(
      displayManager.hasPendingPaintPhase(),
      now - lastDisplayUpdate >= LCD::DISPLAY_UPDATE_INTERVAL,
      editManager.shouldForceNoteEditDisplayUpdate());
}

FLASHMEM __attribute__((noinline)) static void maybeUpdateDisplayForNoteEditSelection(
    uint32_t now, uint32_t& lastDisplayUpdate) {
  if (!editManager.shouldForceNoteEditDisplayUpdate()) {
    return;
  }
  displayManager.update();
  if (!displayManager.hasPendingPaintPhase()) {
    lastDisplayUpdate = now;
    processKindBoundaryUndoWarmAfterDisplay();
  }
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

// Keep first-commit playback prewarm out of ITCM — same owner as the load-frame site.
FLASHMEM __attribute__((noinline)) static void prewarmPlaybackMergedEventsForSlot(
    uint8_t trackIndex, uint8_t slotIndex) {
  Track& prewarmTrackRef = trackManager.getTrack(trackIndex);
#if defined(SESSION_CAPTURE)
  const uint32_t firstCommitStartUs = micros();
#endif
  // S4b.6/S4b.7: this lane is the off-clock owner of a slot's inbound occupancy, so it *provisions* before
  // it prewarms. `ensurePlaybackMergedEventsForSlot` no longer allocates on any path, which is what keeps
  // the switch commit and the MIDI edges allocation-free.
  (void)trackManager.provisionSlotForSwitch(trackIndex, slotIndex);
  prewarmTrackRef.ensurePlaybackMergedEventsForSlot(slotIndex);
#if defined(SESSION_CAPTURE)
  RUNTIME_TIMING_RECORD_LOAD_FRAME_CHILD_REM(2, firstCommitStartUs);
#endif
  displayManager.invalidateLiveDisplayCache();
}

// Keep LoadLoopJob + OLED orchestration out of ITCM — RAM1 is at the 32KB page edge.
// noinline: a single call site would otherwise inline this into loop() and stay in ITCM.
FLASHMEM __attribute__((noinline)) static void runDeferredLoadAndDisplayFrame(
    uint32_t now, uint32_t& lastDisplayUpdate, bool timingCriticalTrackActive,
    bool postOverdubPlayingMidiDrain) {
  static bool bootSlotLoadRefreshPending = true;
  static MidiServiceDrain::ActiveLoadDisplayContinuation activeLoadDisplayContinuation{};
  StorageManager::setBootTitleLoadDrain(bootSlotLoadRefreshPending);
  bool captureActive = false;
  bool overdubStopping = false;
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    const Track& t = trackManager.getTrack(i);
    if (t.isRecording() || t.isOverdubbing() || t.hasPendingDeferredOverdubStop()) {
      captureActive = true;
    }
    overdubStopping = overdubStopping || t.hasPendingDeferredOverdubStop();
  }
  if (overdubStopping) {
    // Stop preparation can span many turns. Keep the existing sliced paint
    // continuation alive on its own owner turn, without starting a load job.
    if (admitDisplayPaintTurn(now, lastDisplayUpdate)) {
      finishAdmittedDisplayPaintTurn(now, lastDisplayUpdate);
    }
    return;
  }
  // Task 2.4 slice 2: the active clock path only *requests* a prepared pair; the
  // gather/order build and the atomic pair publication happen on this off-MIDI
  // owner turn. One content-sized preparation per turn, so a request cannot stack
  // behind the display or load children.
  if (trackManager.hasPendingPreparedPlaybackPairRequest()) {
    (void)trackManager.servicePendingPreparedPlaybackPair();
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

  const bool pollDuringDeferredLoadFrame =
      MidiServiceDrain::aroundDeferredLoadFrame(postOverdubPlayingMidiDrain, overdubStopping);
  const bool loadJobRequested =
      (allowDeferredSlotRestore &&
       (!timingCriticalTrackActive || StorageManager::hasPendingLoopSlotRestore())) ||
      focusSlotRestoreWork;
  // Publish currently transfers a complete loaded slot and frees parser storage
  // synchronously. It has no content-independent active-transport bound yet, so
  // leave it pending until the STOPPED policy admits it.
  const bool activeCommitPending = StorageManager::hasLoadLoopJobCommitPending();
  const bool loadJobPending =
      loadJobRequested &&
      MidiServiceDrain::admitLoadJobChild(timingCriticalTrackActive, activeCommitPending);
  const bool displaySkippedAtTurnStart = skipFocusLoadForSlotSession;
  const bool displayDueAtTurnStart =
      !displaySkippedAtTurnStart && admitDisplayPaintTurn(now, lastDisplayUpdate);
  const MidiServiceDrain::ActiveLoadDisplayChild activeChild =
      activeLoadDisplayContinuation.select(timingCriticalTrackActive, displayDueAtTurnStart,
                                           loadJobPending);

  // A prewarm created by an active Commit is deliberately retained until a
  // noncritical LoadAndDisplay turn. It must never become a second hidden child
  // inside the committing turn. The pending request is owned by `trackManager` so the
  // post-restore census checkpoint can wait for it (S1.3/S2b.3).
  if (!timingCriticalTrackActive && !StorageManager::hasLoadLoopJobCommitPending()) {
    uint8_t prewarmTrack = 0;
    uint8_t prewarmSlot = 0;
    if (trackManager.takeDeferredPlaybackPrewarm(prewarmTrack, prewarmSlot)) {
      prewarmPlaybackMergedEventsForSlot(prewarmTrack, prewarmSlot);
    }
  }

  if (timingCriticalTrackActive &&
      activeChild == MidiServiceDrain::ActiveLoadDisplayChild::Display) {
    finishAdmittedDisplayPaintTurn(now, lastDisplayUpdate);
    activeLoadDisplayContinuation.finish(
        activeChild, displayManager.hasPendingPaintPhase());
    if (pollDuringDeferredLoadFrame) {
      midiHandler.handleMidiInput();
    }
  }

  // Paint before background LoadLoopJob while PLAYING (session_20260718_213044).
  // Skip OLED only on focus Commit / focus-load session — not every SlotLoadSession.
  if (playBackgroundLoad && !timingCriticalTrackActive) {
    if (displayDueAtTurnStart) {
      finishAdmittedDisplayPaintTurn(now, lastDisplayUpdate);
      if (pollDuringDeferredLoadFrame) {
        midiHandler.handleMidiInput();
      }
    }
  }

  // Resume the admitted Read/Parse LoadLoopJob even when tap/revision hold would
  // otherwise skip the deferred frame. Commit remains STOPPED-only until bounded.
  if (loadJobPending &&
      (!timingCriticalTrackActive ||
       activeChild == MidiServiceDrain::ActiveLoadDisplayChild::LoadJob)) {
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
    RUNTIME_TIMING_RECORD_LOAD_FRAME_CHILD_REM(1, loadJobStartUs);
#endif
    // Permission to restore is not unfinished work: retaining the load child
    // on permission alone used to starve OLED updates throughout PLAYING.
    const bool loadJobStillPending =
        (allowDeferredSlotRestore && StorageManager::hasPendingLoopSlotRestore()) ||
        StorageManager::isFocusedLoopSlotRestoreWork();
    if (timingCriticalTrackActive) {
      activeLoadDisplayContinuation.finish(activeChild, loadJobStillPending);
    }
    if (pollDuringDeferredLoadFrame) {
      midiHandler.handleMidiInput();
    }
    if (!focusHadCommittedPasses &&
        trackManager.getTrack(focusTrack).getLoop(focusSlot).hasCommittedPasses()) {
      skipDisplayAfterFocusCommit = true;
      // Commit makes the loop visible, but its session/parser cleanup is a
      // separate owner turn. Queue prewarm only after that continuation has
      // retired; doing it here would stack a third expensive child behind
      // Commit and Cleanup.
      trackManager.requestDeferredPlaybackPrewarm(focusTrack, focusSlot);
    }
    if (allowDeferredSlotRestore && !timingCriticalTrackActive &&
        !SlotLoadSession::isActive()) {
      StorageManager::processDeferredUndoSnapshots();
      StorageManager::processEditAutosave(looperState.getLooperState());
      trackManager.reclaimUnreferencedDisabledPasses();
    }
  }

  if (!playBackgroundLoad && !timingCriticalTrackActive) {
    const bool skipFocusLoad =
        skipDisplayAfterFocusCommit || skipFocusLoadForSlotSession;
    if (!skipFocusLoad && admitDisplayPaintTurn(now, lastDisplayUpdate)) {
      finishAdmittedDisplayPaintTurn(now, lastDisplayUpdate);
      if (pollDuringDeferredLoadFrame) {
        midiHandler.handleMidiInput();
      }
    }
  }

  if (bootSlotLoadRefreshPending && StorageManager::bootInteractiveReady() &&
      !timingCriticalTrackActive) {
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
    if (!displayManager.hasPendingPaintPhase()) {
      lastDisplayUpdate = now;
    }
#if defined(SESSION_CAPTURE)
    RUNTIME_TIMING_RECORD_LOAD_FRAME_CHILD_REM(3, bootCommitStartUs);
#endif
  }

  // Continue incremental background-fill queue admission across loop turns after title clear.
  if (!bootSlotLoadRefreshPending) {
    StorageManager::enqueueRemainingLoopSlotRestoresFromSd();
  }
}

// Keep STOPPED idle-maintenance admission policy out of ITCM.
FLASHMEM __attribute__((noinline)) static void runDeferredIdleMaintenance(
    uint32_t now, bool postOverdubPlayingMidiDrain,
    MidiServiceDrain::DeferredIdleOwner deferredOwner) {
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Persistence ||
      deferredOwner == MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay) {
    return;
  }
  bool anyRealtimeTrackActive = false;
  bool anyPendingSnapshotPublish = false;
  bool pendingSnapshotByTrack[Config::NUM_TRACKS] = {};
  const uint8_t scannedTrackCount = trackManager.getTrackCount();
  for (uint8_t i = 0; i < scannedTrackCount; ++i) {
    const Track& t = trackManager.getTrack(i);
    if (t.isAudiblePlaybackActive() || t.isRecording() || t.isStoppedRecording()) {
      anyRealtimeTrackActive = true;
    }
    const bool pendingSnapshot = t.hasPendingSnapshotPublishOnAnySlot();
    if (i < Config::NUM_TRACKS) {
      pendingSnapshotByTrack[i] = pendingSnapshot;
    }
    anyPendingSnapshotPublish = anyPendingSnapshotPublish || pendingSnapshot;
  }
  const bool allTracksStopped = !anyRealtimeTrackActive;
  if (allTracksStopped && clockManager.isClockRunning() &&
      deferredOwner != MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend &&
      !(deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot &&
        MidiServiceDrain::allowStoppedClockSnapshotStep(true, true,
                                                       anyPendingSnapshotPublish))) {
    return;
  }
  if (allTracksStopped) {
    const uint8_t trackCount = scannedTrackCount;
    if (trackCount == 0) {
      return;
    }
    static uint8_t sStoppedIdleTrackCursor = 0;
    if (sStoppedIdleTrackCursor >= trackCount) {
      sStoppedIdleTrackCursor = 0;
    }
    if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot) {
      if (!MidiServiceDrain::allowSnapshotIdleDuringBootTitleDrain(
              StorageManager::isBootTitleLoadDrain(),
              StorageManager::bootInteractiveReady())) {
        return;
      }
      const uint8_t trackIndex = MidiServiceDrain::selectStoppedSnapshotTrack(
          sStoppedIdleTrackCursor, trackCount, pendingSnapshotByTrack);
      trackManager.getTrack(trackIndex)
          .processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner);
      sStoppedIdleTrackCursor = static_cast<uint8_t>((trackIndex + 1) % trackCount);
      return;
    }
    if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend) {
      for (uint8_t offset = 0; offset < trackCount; ++offset) {
        const uint8_t trackIndex =
            static_cast<uint8_t>((sStoppedIdleTrackCursor + offset) % trackCount);
        Track& track = trackManager.getTrack(trackIndex);
        if (track.loopsAllocated() && track.getActiveLoop().hasDeferredVisualCacheWrapAppend()) {
          track.processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner);
          sStoppedIdleTrackCursor = static_cast<uint8_t>((trackIndex + 1) % trackCount);
          return;
        }
      }
      return;
    }
    // Boot title drain: keep non-Snapshot idle on the focus track. Loaded
    // snapshot hydration stays off after boot (`023222`). After title, fill
    // the viewed loop's empty dirty cache before walking neighbors (`021247`).
    const uint8_t focusTrack = trackManager.getSelectedTrackIndex();
    Track& focusTrackRef = trackManager.getTrack(focusTrack);
    bool focusVisualCacheNeedsFirstPaint = false;
    if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache &&
        focusTrackRef.loopsAllocated()) {
      const Loop& focusLoop = focusTrackRef.getActiveLoop();
      focusVisualCacheNeedsFirstPaint = focusLoop.hasCommittedPasses() &&
                                        focusLoop.visualCacheDirty &&
                                        focusLoop.visualCache.notes.empty();
    }
    if (StorageManager::isBootTitleLoadDrain() || focusVisualCacheNeedsFirstPaint) {
      focusTrackRef.processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain,
                                                   deferredOwner);
      return;
    }
    trackManager.getTrack(sStoppedIdleTrackCursor)
        .processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner);
    sStoppedIdleTrackCursor = static_cast<uint8_t>((sStoppedIdleTrackCursor + 1) % trackCount);
    return;
  }
  const uint8_t realtimeTrackCount = trackManager.getTrackCount();
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot) {
    const uint8_t selectedTrackIndex = trackManager.getSelectedTrackIndex();
    if (selectedTrackIndex < realtimeTrackCount) {
      Track& selectedTrack = trackManager.getTrack(selectedTrackIndex);
      const bool selectedTrackIsRealtime =
          selectedTrack.isPlaying() || selectedTrack.isRecording() ||
          selectedTrack.isOverdubbing() || selectedTrack.hasPendingDeferredOverdubStop() ||
          selectedTrack.isStoppedRecording();
      // Preserve the selected track's existing snapshot behaviour. This is
      // the current interaction target, including its live-record path.
      if (selectedTrackIsRealtime) {
        selectedTrack.processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain,
                                                      deferredOwner);
      }
    }

    // A background snapshot may run only when no capture is active, and only
    // one construction unit is admitted per Snapshot owner turn. Previously
    // every realtime track could take the normal two-unit allowance in the
    // same loop turn, multiplying the display stall on multi-track playback.
    if (!trackManager.anyTrackRecordingOrOverdubbing()) {
      static uint8_t sBackgroundSnapshotTrackCursor = 0;
      if (realtimeTrackCount != 0) {
        for (uint8_t offset = 0; offset < realtimeTrackCount; ++offset) {
          const uint8_t trackIndex = static_cast<uint8_t>(
              (sBackgroundSnapshotTrackCursor + offset) % realtimeTrackCount);
          if (trackIndex == selectedTrackIndex) {
            continue;
          }
          Track& track = trackManager.getTrack(trackIndex);
          const bool backgroundTrackEligible =
              (track.isPlaying() || track.isStoppedRecording()) &&
              !track.isRecording() && !track.isOverdubbing() && !track.hasPendingDeferredOverdubStop();
          if (!backgroundTrackEligible) {
            continue;
          }
          track.processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner,
                                               1);
          sBackgroundSnapshotTrackCursor =
              static_cast<uint8_t>((trackIndex + 1) % realtimeTrackCount);
          break;
        }
      }
    }
    return;
  }
  static uint8_t sCanonicalPrefixTrackCursor = 0;
  if (MidiServiceDrain::serviceCanonicalPrefixTurn(
          deferredOwner, realtimeTrackCount, sCanonicalPrefixTrackCursor,
          [](uint8_t index) {
            Track& track = trackManager.getTrack(index);
            if (track.hasPendingDeferredOverdubStop() || track.isRecording()) return true;
            if ((!track.isAudiblePlaybackActive() && !track.isStoppedRecording()) ||
                !track.loopsAllocated()) return false;
            const Loop& loop = track.getActiveLoop();
            return MidiServiceDrain::canonicalPreparationHasPriorityWork(
                track.isOverdubbing(), loop.visualCacheDirty, loop.hasDeferredVisualCacheWrapAppend(),
                loop.overdubSourceSpanCacheReady());
          },
          [](uint8_t index) { return trackManager.getTrack(index).processDeferredCanonicalPrefix(); })) {
    return; // No other track, visual cleanup, or idle child stacks with this grain.
  }
  for (uint8_t i = 0; i < realtimeTrackCount; ++i) {
    Track& track = trackManager.getTrack(i);
    // Stopped-slot reconstruction is background work. Running every loaded
    // track here stacked 120–281 ms turns into the first recording bars.
    if (!hasRealtimeIdleWork(track)) continue;
    track.processDeferredIdleMaintenance(
        now, postOverdubPlayingMidiDrain, deferredOwner);
  }
}

#if defined(SESSION_CAPTURE)
FLASHMEM __attribute__((noinline)) static void emitBootResetCapture(bool hadCrashReport) {
  char line[64];
  snprintf(line, sizeof(line), "#CAP,%lu,DIAG,boot_reset,crash=%u",
           static_cast<unsigned long>(micros()), hadCrashReport ? 1u : 0u);
  Serial.println(line);
  DebugSessionCapture::appendCaptureTextLine(line);
}
#endif

FLASHMEM void setup() {
  HOT_PATH_TELEMETRY_RESET();
  delay(500);  // USB re-enumeration after reset
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);
  const bool hadCrashReport = static_cast<bool>(CrashReport);
  if (hadCrashReport) {
    // CrashReport::printTo clears the core's retained report. Render it once
    // to both destinations so a missed USB reconnect still leaves an SD copy.
    File crashLog;
    if (SD.begin(BUILTIN_SDCARD)) {
      crashLog = SD.open("crashlog.txt", FILE_WRITE);
    }
    class CrashReportOutput final : public Print {
     public:
      explicit CrashReportOutput(File& log) : log_(log) {}
      FLASHMEM size_t write(uint8_t byte) override { return write(&byte, 1); }
      FLASHMEM size_t write(const uint8_t* data, size_t size) override {
        Serial.write(data, size);
        if (log_) {
          log_.write(data, size);
        }
        return size;
      }
     private:
      File& log_;
    } output(crashLog);
    output.print(CrashReport);
    if (crashLog) {
      crashLog.close();
    }
    Serial.println("--- CrashReport above ---");
    delay(500);
  }
#if defined(SESSION_CAPTURE)
  emitBootResetCapture(hadCrashReport);
#endif

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
  // Arm before loop(): idle maintenance runs before the load-frame setter.
  // 145953 paid idle_gather=136084342 on the first STOPPED turn while the flag
  // was still false, so title drain never reached boot_commit.
  StorageManager::setBootTitleLoadDrain(true);
  looper.setup();  // SD + loadState; USB + piano roll deferred until full slot drain
  if (StorageManager::bootRecoveryBlocked()) return;

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

  HOT_PATH_TELEMETRY_EMIT_SUMMARY("startup");

  // finishBootSetup() runs in loop() with USB after bootInteractiveReady() — keep
  // OSTINATIX title until the restore queue is empty (no piano roll during drain).

  SC_SESSION_HEADER();
}

void loop() {
  if (StorageManager::bootRecoveryBlocked()) return;
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
    if (t.isRecording() || t.isAudiblePlaybackActive()) {
      timingCriticalTrackActive = true;
    }
  }

  HOT_PATH_TELEMETRY_PROCESS_DEFERRED_SUMMARY();

  // A timing-critical turn must not write queued capture text to USB Serial.  The
  // capture ring's <= 8 budget only sheds non-critical records; larger budgets
  // drain with Serial.println and visibly stall the OLED during live capture.
  SC_CAPTURE_FLUSH(timingCriticalTrackActive ? 8 : 64);

  // Update SELECT mode for overdubbing if active
  if (editManager.getCurrentState() == editManager.getSelectNoteState()) {
    auto* selectState = static_cast<EditSelectNoteState*>(editManager.getSelectNoteState());
    selectState->updateForOverdubbing(editManager, trackManager.getSelectedTrack());
  }

  bool captureActiveForMidiDrain = false;
  bool overdubStoppingForMidiDrain = false;
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    const Track& t = trackManager.getTrack(i);
    if (t.isRecording() || t.isOverdubbing() || t.hasPendingDeferredOverdubStop()) {
      captureActiveForMidiDrain = true;
    }
    overdubStoppingForMidiDrain = overdubStoppingForMidiDrain || t.hasPendingDeferredOverdubStop();
  }
  const bool postOverdubPlayingMidiDrain = trackManager.anyPlayingMidiDrainAfterOverdubStop();
  const MidiServiceDrain::DeferredIdleOwner deferredIdleOwner =
      takeDeferredIdleOwnerTurn(postOverdubPlayingMidiDrain);
  bool visualCacheWrapAppendPending = false;
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    const Track& track = trackManager.getTrack(i);
    // A deferred background append cannot block the active track's display.
    // It resumes with the existing stopped-track owner after transport work.
    if (timingCriticalTrackActive && !hasRealtimeIdleWork(track)) continue;
    if (track.loopsAllocated() && track.getActiveLoop().hasDeferredVisualCacheWrapAppend()) {
      visualCacheWrapAppendPending = true;
      break;
    }
  }
  const bool deferredIdleOwnerAllowed =
      MidiServiceDrain::allowDeferredIdleOwner(deferredIdleOwner, visualCacheWrapAppendPending);
#if defined(SESSION_CAPTURE)
  maybeRecordLoopPrefixRemainder(loopPrefixStartUs);
#endif

  // Drain transport commands before a STOPPING grain validates its destination,
  // and preserve the post-overdub PLAYING polls at these same call sites.
  if (MidiServiceDrain::aroundIdleMaintenance(postOverdubPlayingMidiDrain,
                                              overdubStoppingForMidiDrain)) {
    midiHandler.handleMidiInput();
  }
#if defined(SESSION_CAPTURE)
  uint32_t remainderStartUs = micros();
#endif
  if (deferredIdleOwnerAllowed) {
    runDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredIdleOwner);
  }
#if defined(SESSION_CAPTURE)
  const uint32_t idleMaintUs = micros() - remainderStartUs;
  RUNTIME_TIMING_NOTE_IDLE_MAINT(idleMaintUs);
  DebugSessionCapture::recordLoopRemainderSpan("idle_maint", idleMaintUs);
#endif
  if (MidiServiceDrain::aroundIdleMaintenance(postOverdubPlayingMidiDrain,
                                              overdubStoppingForMidiDrain)) {
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
  uint32_t loadFrameUs = 0;
  if (deferredIdleOwner == MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay &&
      deferredIdleOwnerAllowed) {
    const uint32_t loadFrameStartUs = micros();
    runDeferredLoadAndDisplayFrame(now, lastDisplayUpdate, timingCriticalTrackActive,
                                   postOverdubPlayingMidiDrain);
    loadFrameUs = micros() - loadFrameStartUs;
  }
#if defined(SESSION_CAPTURE)
  RUNTIME_TIMING_NOTE_LOAD_FRAME(loadFrameUs);
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
  // OVERDUB_STOPPING owns the active capture until its next deferred grain.
  // Pressure reclamation can walk derived caches and starve that owner after
  // its first turn, leaving a clean normal stop permanently pending.
  if (!overdubStoppingForMidiDrain && pressure >= MemoryPressureLevel::Low) {
    trackManager.tryReclaimDerivedViewCachesUnderPressure(pressure);
  }
  if (!overdubStoppingForMidiDrain && pressure >= MemoryPressureLevel::Critical) {
    // Policy: Critical reclaims disabled-pass chunks (memory_pressure_reclaim_refinement §Policy).
    // Idle-only reclaim at line ~114 misses chunk pressure during RECORDING/PLAYING/OVERDUBBING
    // (session_20260811_021117: append failures with heap headroom, pool at CHUNK_RESERVE).
    trackManager.reclaimUnreferencedDisabledPasses(nullptr, true);
  }

#if defined(SESSION_CAPTURE)
  const uint32_t persistSaveStartUs = micros();
#endif
  const bool deferredSavePending = StorageManager::hasDeferredSaveWork();
  const bool prioritizeWorkspaceSaveDrain = StorageManager::shouldPrioritizeWorkspaceSaveDrain();
  // A queued workspace save is urgent only while transport is idle. It must
  // never bypass record/overdub/playback timing protection (213530).
  const bool allowPrioritizedWorkspaceSaveDrain =
      prioritizeWorkspaceSaveDrain && !timingCriticalTrackActive;
  const bool allowDeferredSaveAfterLoadFrame =
      allowPrioritizedWorkspaceSaveDrain ||
      MidiServiceDrain::allowDeferredSaveAfterLoadFrame(timingCriticalTrackActive, loadFrameUs);
  // A parked workspace save is not the only reason to enter the scheduler: sealed capture chunks
  // waiting for the incremental flush are, and during a live take that is the *only* reason (the
  // workspace save is parked, so `deferredSavePending` is false). Entering on queued chunks is bounded
  // by construction: `processDeferredSaveState` flushes at most one chunk and returns immediately while
  // the transport is active, so no bundle work can run in a take window.
  const bool queuedCaptureChunks = StorageManager::hasQueuedCaptureChunksToPersist();
  const bool runDeferredSaveState =
      (deferredSavePending &&
       (allowPrioritizedWorkspaceSaveDrain ||
        (deferredIdleOwner == MidiServiceDrain::DeferredIdleOwner::Persistence &&
         deferredIdleOwnerAllowed && allowDeferredSaveAfterLoadFrame &&
         MidiServiceDrain::allowDeferredSave(postOverdubPlayingMidiDrain, deferredSavePending,
                                              overdubStoppingForMidiDrain)))) ||
      queuedCaptureChunks;
  if (runDeferredSaveState) {
    StorageManager::processDeferredSaveState(looperState.getLooperState());
  }
#if defined(SESSION_CAPTURE)
  const uint32_t persistSaveUs = micros() - persistSaveStartUs;
  RUNTIME_TIMING_NOTE_PERSIST_SAVE(persistSaveUs);
  DebugSessionCapture::recordLoopRemainderSpan("persist_save", persistSaveUs);
#endif

  // Poll USB host again after deferred SD/display work so DROID button note-ons are not
  // dropped when the main loop was busy (session_20260810_234059: note-off without note-on).
  midiHandler.handleMidiInput();
  midiButtonManager.update();

#if defined(SESSION_CAPTURE)
  StorageManager::processHitlSerialCommands();
  // S1.3/S2b.3: the one-shot post-restore census checkpoint. Idle-owner position (after MIDI service and
  // persistence for this turn) so the pool figure it reports is a settled one; the call itself decides
  // readiness and emits at most once per boot.
  StorageManager::emitPostRestoreCensusIfReady(looperState.getLooperState());
  // `restored-library-stability` 1.3: the bounded on-demand library observation. Same unconditional
  // idle-owner tail, deliberately **not** the admission-gated deferred load/restore frame: a restore
  // stuck at the commit gate must not be able to suppress the diagnostic meant to explain it. Inert
  // (one flag check) unless `!LIB_OBSERVE` requested it.
  StorageManager::serviceLibraryObservation();
#endif

  // S0: observation-only timing telemetry emission (no scheduling decisions).
  // maybeEmit drains pending first-late / rebuild one-shots, then the 5 s window.
  RUNTIME_TIMING_MAYBE_EMIT(micros());

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

#if defined(SESSION_CAPTURE)
  // External-pool accounting every 5 s. Bookkeeping only — no pool walk — so unlike
  // the block above it is safe while the transport runs, and it is deliberately not
  // gated on !timingCriticalTrackActive: an 8-bar record plus overdub exhausted the
  // pool with no trend visible at all (2026-09-17). `logStatus(true)` cannot be used
  // here; that walk costs ~593 ms and loses external MIDI clock.
  static uint32_t lastExternalAccountingLog = 0;
  constexpr uint32_t EXTERNAL_ACCOUNTING_INTERVAL_MS = 1000;
  if (now - lastExternalAccountingLog >= EXTERNAL_ACCOUNTING_INTERVAL_MS) {
    lastExternalAccountingLog = now;
    MemoryMonitor::logExternalPoolAccounting("tick");
  }

  // Full per-owner census (S1.1) on a slower cadence: the tick above carries only tags
  // 1..7, and the remaining owners were reachable solely through a pool refusal.
  static uint32_t lastOwnerCensusLog = 0;
  constexpr uint32_t OWNER_CENSUS_INTERVAL_MS = 4000;
  if (now - lastOwnerCensusLog >= OWNER_CENSUS_INTERVAL_MS) {
    lastOwnerCensusLog = now;
    MemoryMonitor::logWorkingSetOwners("owners");
  }
#endif

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
