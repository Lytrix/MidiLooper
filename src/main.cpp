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
#include "Utils/BootRestoreGate.h"
#include "StorageManagerInternal.h"
#include "DeferredJobScheduler.h"
#include "SlotLoadSession.h"
#include "LoadLoopBudget.h"
#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"
#include "ControlSurfaceManager.h"
#include "Utils/PerformanceMonitor.h"  // Performance monitoring
#include "Utils/MemoryMonitor.h"
#include "Utils/HeapPeakTrace.h"
#include "Utils/MemoryPressureLevel.h"
#include "Utils/MemoryPool.h"
#include "LoopEventStore.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/BootTelemetry.h"
#include "Utils/MidiServiceDrain.h"
#if REVISION_STORE_PROBE
#include "RevisionStore/RevisionShadow.h"
#endif
#include "BootGate.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "Utils/RuntimeProfiler.h"
#include "Utils/PlaybackTickContext.h"
#include <cstdio>
#include "Utils/LaneGapSpans.h"

static MidiServiceDrain::DeferredIdleOwnerCursor sDeferredIdleOwnerCursor{};

/// DEC-108 §13b / task 3.10: the **holder rotation** position for the window-build owner (the caller-owned shape
/// DEC-108 §13b / task 3.10: the **holder rotation** position for the window-build owner (the caller-owned shape
/// `sDeferredIdleOwnerCursor` uses -- the owner owns neither the rotation nor its persistence). One byte is enough:
/// the selection examines every track every turn, so the rotation only breaks ties -- which is what bounds every
/// track's wait. Two guards shaped it: it is **not** an array per track, and it is `DMAMEM` rather than DTCM, because
/// the RAM1 budget guard rejected both the 8-cursor array and the single DTCM byte (+32 B of `variables`) and the
/// approved line may not grow for a tie-break. This owner's turn is off-clock (DEC-108 §13b's placement rule: an
/// additional counter is `DMAMEM`/`FLASHMEM`).
DMAMEM static MidiServiceDrain::PlaybackHolderCursor sWindowHolderCursor{};
/// The per-turn offer set the selection reads: one entry per track (the manager's own maximum), no DTCM cost -- it
/// lives on this function's frame, not in a static.
static constexpr uint8_t kWindowHolderOffers = 8;
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
  LANE_GAP_SPAN(LoopPrefix);
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
  // S4b.6/S4b.7: the **restore** lane's own provisioning point. This request is a slot *restore's* prewarm
  // (S1.3/S2b.3's census checkpoint waits on it), not a switch's: a switch's inbound occupancy belongs to
  // the `SlotProvision` deferred-idle turn (`TrackManager::servicePendingSlotProvision`). Both call the same
  // owner, `provisionSlotForSwitch`, because `ensurePlaybackMergedEventsForSlot` no longer allocates on any
  // path -- which is what keeps the switch commit and the MIDI edges allocation-free.
  (void)trackManager.provisionSlotForSwitch(trackIndex, slotIndex);
  prewarmTrackRef.ensurePlaybackMergedEventsForSlot(slotIndex);
#if defined(SESSION_CAPTURE)
  RUNTIME_TIMING_RECORD_LOAD_FRAME_CHILD_REM(2, firstCommitStartUs);
#endif
  displayManager.invalidateLiveDisplayCache();
}

// First-paint gate for the boot restore sweep (DEC-182). RAM2: two DTCM words tripped the approved RAM1 budget.
#if defined(__IMXRT1062__)
DMAMEM uint32_t gBootSweepDueMs;
DMAMEM uint8_t gBootSweepEnqueued;
// DEC-210: one report per boot when the playback-set visual gate opens (RAM2 like the sweep gate).
DMAMEM uint8_t gBootVisualCacheReported;
DMAMEM uint32_t gPaintGateLastMs;
// DEC-189: `#STOP_TRACE` rate stamp. RAM2 because a new plain static costs DTCM and the budget gate refuses it.
DMAMEM uint32_t gStopTraceLastMs;
// DEC-216: realtime idle-walk rotation. Bit 7 is the priority phase, bits 0-6 the next background track;
// RAM2 like the gates above, because one DTCM section byte costs 32 and the budget refuses growth.
DMAMEM uint8_t gRealtimeIdleRotation;

#if defined(SESSION_CAPTURE)
/// DEC-237 Slice 2: the VisualCache owner turn is the display producer's service opportunity. Record the
/// interval between those turns so C6.4's T is measured, not assumed. RAM2 only.
struct VisualCacheOwnerCadence {
  uint32_t turns;
  uint32_t maxUs;
  uint32_t lastUs;
  uint32_t lastStartUs;
  uint32_t lastEmitMs;
};
DMAMEM static VisualCacheOwnerCadence sVisualCacheCadence;
#endif
// DEC-196: deadline for the playback-set boot hold (see `runDeferredLoadAndDisplayFrame`). RAM2 like the sweep gate.
DMAMEM uint32_t gBootPlaybackSetHoldDeadlineMs;
#else
uint32_t gBootSweepDueMs;
uint8_t gBootSweepEnqueued;
uint8_t gBootVisualCacheReported;
uint32_t gPaintGateLastMs;
uint32_t gBootPlaybackSetHoldDeadlineMs;
uint8_t gRealtimeIdleRotation;
#endif

// DEC-196: the longest the boot panel waits for every track's playback slot. The measured playback set is ~2-3 s of
// drain; 15 s leaves room for a slow card while still releasing a set that can never settle (corrupt/refused slot).
constexpr uint32_t kBootPlaybackSetHoldMaxMs = 15000u;

// The remaining-slot sweep restores every other slot and re-dirties a cache per slot. It must not run before the
// selected slot's first cache build, or the panel waits for the whole set. Device (`session_20261028_211734.log`,
// boot1): restore committed 7.87 s, first build 11.88 s, **first content 11.99 s**, and the sweep's first request
// only at 13.07 s -- paint before sweep. Without the gate the same build started at 15.05 s.
FLASHMEM __attribute__((noinline)) static bool bootSweepMayRun(uint32_t now) {
  if (gBootSweepEnqueued) return false;
  const uint8_t focusTrackIndex = trackManager.getSelectedTrackIndex();
  bool focusCacheHasNotes = false;
  if (focusTrackIndex < trackManager.getTrackCount()) {
    Track& focusTrack = trackManager.getTrack(focusTrackIndex);
    if (focusTrack.loopsAllocated()) {
      focusCacheHasNotes = !focusTrack.getActiveLoop().visualCache.notes.empty();
    }
  }
  // DEC-210: do not start the sweep until the whole **playback set** has its first paint window
  // prepared, not just the focus slot: a track switched to while PLAYING must paint its prewarmed
  // notes instead of waiting for the idle lane (session_20261029_174940: switches painted n=0).
  // The 3 s bound from the release still ends the wait for a slot that cannot prepare.
  const bool boundReached = static_cast<int32_t>(now - gBootSweepDueMs) >= 0;
  const bool visualSetReady = trackManager.playbackSetVisualCachesReady();
  const bool mayRun = BootGate::bootSweepMayRun(focusCacheHasNotes, visualSetReady, boundReached);
  if (mayRun && !gBootVisualCacheReported) {
    // One row per boot, so the device proves whether the set was prepared or the bound released it.
    gBootVisualCacheReported = 1;
    Serial.printf_P(PSTR("BOOT,visual_cache,ready=%u,bound=%u,tracks=%u\r\n"),
                    visualSetReady ? 1u : 0u, boundReached ? 1u : 0u,
                    static_cast<unsigned>(trackManager.getTrackCount()));
  }
  return mayRun;
}

// DEC-196: **one report per boot, on the release turn.** The question the owner asked is "can every track sound
// from here?", which is a fact about the moment the panel/USB becomes interactive — not a periodic sample. (It
// replaces the DEC-194 probe that printed every 4 s for the first minute; RAM1 is on its bank boundary, so this
// lives in flash and keeps no state.) `settled=0` means the hold expired instead of the set settling, so the rows
// tell the truth about a boot that was released by the 15 s deadline.
FLASHMEM __attribute__((noinline)) static void reportBootPlaybackSetReady(bool settled) {
  const uint8_t trackCount = trackManager.getTrackCount();
  Serial.printf_P(PSTR("BOOT,ready,settled=%u,tracks=%u\r\n"), settled ? 1u : 0u,
                  static_cast<unsigned>(trackCount));
  for (uint8_t t = 0; t < trackCount; ++t) {
    Track& readyTrack = trackManager.getTrack(t);
    if (!readyTrack.loopsAllocated()) {
      Serial.printf_P(PSTR("BOOT,ready,track=%u,no_loops\r\n"), static_cast<unsigned>(t));
      continue;
    }
    const uint8_t slot = readyTrack.getActiveLoopIndex();
    const Loop& loop = readyTrack.getLoop(slot);
    Serial.printf_P(PSTR("BOOT,ready,track=%u,slot=%u,state=%u,enabled=%u,passes=%u,root=%u,vcache=%u\r\n"),
                    static_cast<unsigned>(t), static_cast<unsigned>(slot),
                    readyTrack.isPlaying() ? 1u : 0u,
                    trackManager.isSlotEnabled(t, slot) ? 1u : 0u,
                    loop.hasCommittedPasses() ? 1u : 0u,
                    loop.publishedSnapshotIdentity() != nullptr ? 1u : 0u,
                    static_cast<unsigned>(loop.visualCache.notes.size()));
  }
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

  // DEC-184 -- boot title release. The hold used to end only when the whole boot restore queue was empty
  // (`bootInteractiveReady()`), and the boot queue is each track's selected slot plus its two neighbours: 8 x 3 =
  // 24 restores on device, ~4 s after the slot the panel actually shows has already committed
  // (`session_20261028_214829_paintgate5.log`: selected slot committed 6.85 s, title released 10.89 s).
  // DEC-196 -- the hold now ends at the **playback set**, not the focused slot: one slot per track must be committed,
  // otherwise a Play pressed right after boot sounds the focused track alone (`session_20261029_113133.log`: Play at
  // 11.67 s with one slot restored; the rest then sat in `waiting_commit_gate` because the transport was running).
  // The rest of the queue keeps restoring behind the paint-first background path. An empty playback slot is never
  // owed, so that boot keeps the old contract -- and the enqueue is idempotent
  // (`queueDeferredLoopSlotRestoreWithPayloadSource` skips pending entries and slots that no longer need a load), so
  // the post-boot sweep cannot double-queue the remainder.
  const bool focusSettledForBootRelease = BootGate::focusSlotSettled(
      trackManager.getTrack(focusTrack).getLoop(focusSlot).hasCommittedPasses(),
      StorageManager::isFocusedLoopSlotRestoreWork(), SlotLoadSession::isActive(),
      StorageManagerInternal::anyLoadLoopJobActive());
  // Only asked while the boot still owes the release: after it, the queue holds the background sweep and this walk
  // would be pure per-turn cost.
  bool playbackSetSettledForBootRelease = true;
  if (bootSlotLoadRefreshPending) {
    playbackSetSettledForBootRelease = BootGate::playbackSetSettled(
        focusSettledForBootRelease, StorageManager::playbackSetRestoreWorkOwed());
  }
  if (bootSlotLoadRefreshPending && gBootPlaybackSetHoldDeadlineMs == 0) {
    // RAM2 deadline: a set that cannot settle (corrupt slot, refused payload) must release the panel rather than
    // hold it forever. The whole-queue fallback still ends the hold earlier in every healthy boot.
    gBootPlaybackSetHoldDeadlineMs = now + kBootPlaybackSetHoldMaxMs;
  }
  const bool playbackSetHoldExpired =
      gBootPlaybackSetHoldDeadlineMs != 0 &&
      static_cast<int32_t>(now - gBootPlaybackSetHoldDeadlineMs) >= 0;
  if (bootSlotLoadRefreshPending &&
      (BootGate::bootTitleMayRelease(StorageManager::bootInteractiveReady(),
                                     playbackSetSettledForBootRelease) ||
       playbackSetHoldExpired) &&
      !timingCriticalTrackActive) {
#if defined(SESSION_CAPTURE)
    const uint32_t bootCommitStartUs = micros();
#endif
    bootSlotLoadRefreshPending = false;
    gBootPlaybackSetHoldDeadlineMs = 0;
    StorageManager::setBootTitleLoadDrain(false);
    reportBootPlaybackSetReady(playbackSetSettledForBootRelease);
    gBootSweepDueMs = now + 3000;
    gBootSweepEnqueued = 0;
    gBootVisualCacheReported = 0;
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

  // Continue incremental background-fill queue admission across loop turns after title clear, but only after the
  // selected slot has painted (or the bound lapses, so an empty selected slot cannot hold the set back).
  if (!bootSlotLoadRefreshPending && bootSweepMayRun(now)) {
    gBootSweepEnqueued = 1;
    StorageManager::enqueueRemainingLoopSlotRestoresFromSd();
  }
}

// Keep STOPPED idle-maintenance admission policy out of ITCM.
#if defined(SESSION_CAPTURE)
/// DEC-216: the realtime walk is bounded per **turn**, not only per child. When one track unit alone
/// exceeds twice the declared owner budget (8 ms), the residual that used to hide inside
/// `idle_maint,breakdown`'s `residual` field is named here with the track that spent it. Flash-resident
/// and `PSTR`-formatted so no literal or buffer lands in DTCM (the `idle_maint,breakdown` rule).
FLASHMEM __attribute__((noinline)) static void reportSlowIdleTurnUnit(uint8_t trackIndex, uint32_t unitUs,
                                                                     uint32_t turnUs, uint8_t units) {
  if (unitUs < 2u * MidiServiceDrain::kOwnerWorkBudgetUs) return;
  char line[96];
  snprintf_P(line, sizeof(line), PSTR("#CAP,%lu,DIAG,idle_turn,track=%u,us=%lu,turn=%lu,units=%u"),
             (unsigned long)micros(), static_cast<unsigned>(trackIndex), (unsigned long)unitUs,
             (unsigned long)turnUs, static_cast<unsigned>(units));
  DebugSessionCapture::appendCaptureTextLine(line);
}
#else
#define reportSlowIdleTurnUnit(trackIndex, unitUs, turnUs, units) ((void)0)
#endif

#if PLAYBACK_ROLLING_SUCCESSOR_PROBE && defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
/// C5.3b measurement (owner ruling 2026-11-03): **what owns the main loop between two window-lane turns, measured
/// near-silently.** Each loop iteration folds its buckets (`LaneGapSpans`: MIDI, capture flush, capture immediate rows,
/// playback fallback, loop prefix, edge effects; plus idle maintenance and the load/display frame from this file;
/// "other" is the rest) into RAM2 totals, keeps the 16 worst iterations and the 8 worst lane gaps, and prints nothing
/// while the transport runs. When it stops, one `lane_spans` dump: totals, then the worst iterations and gaps.
DMAMEM uint32_t LaneGapSpans::gIteration[LaneGapSpans::Count];
namespace {
constexpr uint8_t kSpanBuckets = LaneGapSpans::Count + 3;  // + idle, load/display frame, other
struct LaneSpanRecord { uint64_t tick; uint32_t totalUs; uint32_t gapTicks; uint32_t us[kSpanBuckets]; };
struct LaneSpanMeasure {
  bool running = false;
  uint64_t lastLaneTick = 0;
  uint32_t lastIterationUs = 0, loadFrameUs = 0;
  uint32_t iterations = 0, gapsOver96 = 0, maxGapTicks = 0, maxIterationUs = 0;
  uint64_t totals[kSpanBuckets] = {};
  uint32_t gapUs[kSpanBuckets] = {};  // the current gap's sums
  uint32_t gapTotalUs = 0;
  LaneSpanRecord worstIterations[16] = {};
  LaneSpanRecord worstGaps[8] = {};
};
DMAMEM LaneSpanMeasure sLaneSpans;

FLASHMEM __attribute__((noinline)) void keepWorst(LaneSpanRecord* records, uint8_t count, const LaneSpanRecord& r,
                                                  bool byGap) {
  uint8_t least = 0;
  for (uint8_t i = 1; i < count; ++i)
    if ((byGap ? records[i].gapTicks : records[i].totalUs) < (byGap ? records[least].gapTicks : records[least].totalUs))
      least = i;
  if ((byGap ? r.gapTicks : r.totalUs) > (byGap ? records[least].gapTicks : records[least].totalUs)) records[least] = r;
}

FLASHMEM __attribute__((noinline)) void dumpLaneSpans() {
  auto& m = sLaneSpans;
  Serial.printf("#CAP,%lu,DIAG,lane_spans,totals,iter=%lu,gaps96=%lu,max_gap=%lu,max_iter_us=%lu,us=",
                (unsigned long)micros(), (unsigned long)m.iterations, (unsigned long)m.gapsOver96,
                (unsigned long)m.maxGapTicks, (unsigned long)m.maxIterationUs);
  for (uint8_t b = 0; b < kSpanBuckets; ++b) Serial.printf("%s%llu", b ? "/" : "", (unsigned long long)m.totals[b]);
  Serial.println();
  for (uint8_t i = 0; i < 16; ++i) {
    const auto& r = m.worstIterations[i];
    if (r.totalUs == 0) continue;
    Serial.printf("#CAP,%lu,DIAG,lane_spans,iteration,tick=%llu,us=%lu,parts=", (unsigned long)micros(),
                  (unsigned long long)r.tick, (unsigned long)r.totalUs);
    for (uint8_t b = 0; b < kSpanBuckets; ++b) Serial.printf("%s%lu", b ? "/" : "", (unsigned long)r.us[b]);
    Serial.println();
  }
  for (uint8_t i = 0; i < 8; ++i) {
    const auto& r = m.worstGaps[i];
    if (r.gapTicks == 0) continue;
    Serial.printf("#CAP,%lu,DIAG,lane_spans,gap,tick=%llu,ticks=%lu,us=%lu,parts=", (unsigned long)micros(),
                  (unsigned long long)r.tick, (unsigned long)r.gapTicks, (unsigned long)r.totalUs);
    for (uint8_t b = 0; b < kSpanBuckets; ++b) Serial.printf("%s%lu", b ? "/" : "", (unsigned long)r.us[b]);
    Serial.println();
  }
}
}  // namespace

FLASHMEM __attribute__((noinline)) static void noteLaneGapIteration(MidiServiceDrain::DeferredIdleOwner owner,
                                                                   bool allowed, uint32_t idleUs) {
  auto& m = sLaneSpans;
  const PlaybackDeadlineClock clock = clockManager.getPlaybackDeadlineClock();
  // The epoch survives a stop, so the transport state is the clock's own (device: an epoch test never dumped).
  const bool running = clockManager.isClockRunning() && clock.epoch != 0;
  if (!running) {
    if (m.running) { dumpLaneSpans(); LaneGapSpans::dumpMidiSpans(); }  // after the transport, never during it
    if (m.running) LaneGapSpans::resetMidiSpans();
    if (m.running || m.iterations != 0) m = LaneSpanMeasure{};
    for (auto& v : LaneGapSpans::gIteration) v = 0;
    return;
  }
  if (!m.running) { m = LaneSpanMeasure{}; m.running = true; m.lastLaneTick = clock.tick; m.lastIterationUs = micros(); }
  const uint32_t nowUs = micros();
  const uint32_t totalUs = nowUs - m.lastIterationUs;
  m.lastIterationUs = nowUs;
  LaneSpanRecord r{};
  r.tick = clock.tick;
  r.totalUs = totalUs;
  uint32_t named = 0;
  for (uint8_t b = 0; b < LaneGapSpans::Count; ++b) { r.us[b] = LaneGapSpans::gIteration[b]; named += r.us[b]; }
  for (auto& v : LaneGapSpans::gIteration) v = 0;
  r.us[LaneGapSpans::Count] = allowed ? idleUs : 0;
  r.us[LaneGapSpans::Count + 1] = m.loadFrameUs;
  named += r.us[LaneGapSpans::Count] + r.us[LaneGapSpans::Count + 1];
  m.loadFrameUs = 0;
  r.us[LaneGapSpans::Count + 2] = totalUs > named ? totalUs - named : 0;
  ++m.iterations;
  if (totalUs > m.maxIterationUs) m.maxIterationUs = totalUs;
  for (uint8_t b = 0; b < kSpanBuckets; ++b) { m.totals[b] += r.us[b]; m.gapUs[b] += r.us[b]; }
  m.gapTotalUs += totalUs;
  keepWorst(m.worstIterations, 16, r, false);
  if (!allowed || owner != MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance) return;
  const uint64_t gap = clock.tick > m.lastLaneTick ? clock.tick - m.lastLaneTick : 0;
  if (gap > 96) {
    ++m.gapsOver96;
    LaneSpanRecord g{};
    g.tick = clock.tick;
    g.gapTicks = gap > UINT32_MAX ? UINT32_MAX : uint32_t(gap);
    g.totalUs = m.gapTotalUs;
    for (uint8_t b = 0; b < kSpanBuckets; ++b) g.us[b] = m.gapUs[b];
    keepWorst(m.worstGaps, 8, g, true);
  }
  if (gap > m.maxGapTicks) m.maxGapTicks = gap > UINT32_MAX ? UINT32_MAX : uint32_t(gap);
  for (auto& v : m.gapUs) v = 0;
  m.gapTotalUs = 0;
  m.lastLaneTick = clock.tick;
}
FLASHMEM __attribute__((noinline)) static void noteLaneGapLoadFrame(uint32_t loadFrameUs) {
  sLaneSpans.loadFrameUs += loadFrameUs;  // folded into the next iteration
}
#define NOTE_LANE_GAP_ITERATION(owner, allowed, idleUs) noteLaneGapIteration((owner), (allowed), (idleUs))
#define NOTE_LANE_GAP_LOAD_FRAME(loadFrameUs) noteLaneGapLoadFrame(loadFrameUs)
#else
#define NOTE_LANE_GAP_ITERATION(owner, allowed, idleUs) ((void)0)
#define NOTE_LANE_GAP_LOAD_FRAME(loadFrameUs) ((void)0)
#endif

FLASHMEM __attribute__((noinline)) static void runDeferredIdleMaintenance(
    uint32_t now, bool postOverdubPlayingMidiDrain,
    MidiServiceDrain::DeferredIdleOwner deferredOwner, bool timingCritical, uint32_t turnStartUs) {
#if defined(SESSION_CAPTURE)
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache) {
    const uint32_t cadenceNowUs = micros();
    if (sVisualCacheCadence.lastStartUs != 0) {
      const uint32_t delta = cadenceNowUs - sVisualCacheCadence.lastStartUs;
      sVisualCacheCadence.lastUs = delta;
      if (delta > sVisualCacheCadence.maxUs) {
        sVisualCacheCadence.maxUs = delta;
      }
    }
    sVisualCacheCadence.lastStartUs = cadenceNowUs;
    ++sVisualCacheCadence.turns;
  }
  if (sVisualCacheCadence.lastEmitMs == 0) {
    sVisualCacheCadence.lastEmitMs = now;
  }
  if (now - sVisualCacheCadence.lastEmitMs >= 5000u) {
    char cadenceLine[176];
    snprintf_P(cadenceLine, sizeof(cadenceLine),
               PSTR("#CAP,%lu,DIAG,disp_owner_cadence,vcache_turns=%lu,max_us=%lu,last_us=%lu,"
                    "win_ms=%lu"),
               static_cast<unsigned long>(micros()),
               static_cast<unsigned long>(sVisualCacheCadence.turns),
               static_cast<unsigned long>(sVisualCacheCadence.maxUs),
               static_cast<unsigned long>(sVisualCacheCadence.lastUs),
               static_cast<unsigned long>(now - sVisualCacheCadence.lastEmitMs));
    DebugSessionCapture::appendCaptureTextLine(cadenceLine);
    sVisualCacheCadence.turns = 0;
    sVisualCacheCadence.maxUs = 0;
    sVisualCacheCadence.lastUs = 0;
    sVisualCacheCadence.lastEmitMs = now;
  }
#endif
  // DEC-237 Slice 3a/3c1: the staged display base's producer is a declared idle-owner child. It is
  // served only when coverage has no consumer due tick inside the owner lead, and only under the
  // idle-child admission gate. One bounded grain owns the VisualCache turn when selected; the frame
  // adoption is Slice 3c2.
  // DEC-238 -- MIDI timing first: the shipping value is false. With the arbitration on, the owner rig's
  // 12-overdub chain ran at a 281 ms median / 664 ms worst MIDI service gap (session_20261031_212428,
  // image 0ebccccc) against 81 ms / 197 ms with it off (session_20261031_213255, image 2c7bb5d3), and the
  // long idle turns persisted after the candidates completed. It may be enabled again only when an
  // on/off run of the owner rig chain shows no MIDI-gap or late-note regression.
  constexpr bool kDisplayBaseArbitrationEnabled = false;
  if (kDisplayBaseArbitrationEnabled &&
      deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache) {
    const uint8_t selectedTrackIndex = trackManager.getSelectedTrackIndex();
    if (selectedTrackIndex < trackManager.getTrackCount()) {
      Track& selectedTrack = trackManager.getTrack(selectedTrackIndex);
      if (selectedTrack.loopsAllocated()) {
        const uint8_t displaySlot = selectedTrack.getActiveLoopIndex();
        const Loop& selectedLoop = selectedTrack.getLoop(displaySlot);
        uint32_t currentPhase = 0;
        if (selectedLoop.loopLengthTicks != 0) {
          const uint32_t loopStart =
              selectedLoop.loopStartTick % selectedLoop.loopLengthTicks;
          currentPhase = (clockManager.getCurrentTick() + selectedLoop.loopLengthTicks - loopStart) %
                         selectedLoop.loopLengthTicks;
        }
        const bool coverageRequiredNow = selectedLoop.overdubCoverageRequiredNow(
            currentPhase, MidiServiceDrain::kCoverageLeadTicks);
        const bool displayOwed = displayManager.stagedDisplayBaseOwed(selectedTrack, displaySlot);
        if (MidiServiceDrain::chooseCoverageOrDisplay(coverageRequiredNow, displayOwed) ==
            MidiServiceDrain::CoverageDisplayChoice::Display) {
          const uint32_t spentUs = micros() - turnStartUs;
          const uint32_t remainingUs =
              spentUs < MidiServiceDrain::kOwnerWorkBudgetUs
                  ? MidiServiceDrain::kOwnerWorkBudgetUs - spentUs
                  : 0;
          if (MidiServiceDrain::boundedIdleOwnerChildMayRun(
                  true, timingCritical, 4000, remainingUs, 1000)) {
            (void)displayManager.serviceStagedDisplayBaseTurn(
                selectedTrack, displaySlot, 0,
                DisplayBasePreparation::kMaxEventsAppendedPerStep);
            // The display child owns this owner turn; coverage is not due this opportunity.
            return;
          }
        }
      }
    }
  }
  // S4b.7 (Rev 20.3/21.7): the `SlotProvision` turn is served **before** the all-stopped guard below and
  // returns through it -- the contract exempts it, because a queued switch is retained exactly while the
  // track's state is unsettled. It takes the pending switch's inbound occupancy off-clock and builds that
  // slot's merged window; nothing else moves here.
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::SlotProvision) {
    (void)trackManager.servicePendingSlotProvision();
    return;
  }
  // DEC-108 item 1 / `ExecutionBounds.md` rows 27-29: the window-build owner's turn. Served here, **before**
  // the stopped/playing split, for the same reason `SlotProvision` is: an owed window must be reachable in
  // the states a pending window sits in -- playing, recording, stopped, and beside a queued switch -- while
  // the stopped path below routes its work through `processDeferredIdleMaintenance`, which is not this owner.
  // DEC-108 §13b / task 3.10: **one declared grain per admitted turn, and the opportunity serves one TRACK per
  // turn**, chosen by the shared policy from every track's offer -- so a lowest-index track whose interval is a
  // long way off no longer monopolises the turn while a later track's interval is imminent. Nothing is published
  // yet, so no consumer's behaviour depends on this turn.
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance) {
    const uint8_t windowTrackCount = trackManager.getTrackCount();
    const PlaybackDeadlineClock windowClock = clockManager.getPlaybackDeadlineClock();
    const uint32_t windowPhase = static_cast<uint32_t>(windowClock.tick);
    const uint8_t offerCount =
        windowTrackCount < kWindowHolderOffers ? windowTrackCount : kWindowHolderOffers;
    MidiServiceDrain::PlaybackHolder offers[kWindowHolderOffers] = {};
    for (uint8_t i = 0; i < offerCount; ++i) {
#if PLAYBACK_WINDOW_ROLLING_ENABLED
      // 1.4 production demand: a **playing**, settled track asks for the window its **active demoted** slot
      // needs, with this turn's qualified clock. Whole-take slots request nothing here, a ready window is left
      // alone, and every refusal keeps the existing bridge/merged fallback serving (no branch is removed).
      Track& demandTrack = trackManager.getTrack(i);
      if (demandTrack.isPlaying() && !demandTrack.isRecording() && !demandTrack.isOverdubbing()) {
        (void)demandTrack.requestActiveCommittedPlaybackWindow(windowPhase, windowClock);
      }
#endif
      // Deadlines outlive worker eligibility (completed candidates may await revalidation).
#if PLAYBACK_WINDOW_ROLLING_ENABLED
      (void)trackManager.getTrack(i).serviceRollingPlaybackWindow(windowClock);
      // Task 3.15d (slice 2): the **session lane's** cadence, on the same unconditional per-track visit as the
      // committed requester above -- the coverage question is cheap and idempotent (it returns as soon as a window at
      // the session's revision still contains the playhead), and running it here rather than only while a build is
      // pending is what lets the lane re-cover the playhead before the served interval ends. A track without a
      // NOTE_EDIT session returns at once.
      trackManager.getTrack(i).servicePreviewWindowForNoteEditSession();
      // Task 3.15e (slice 2b-ii-b): the **overdub overlay lane's** own owner turn, on the same per-track visit. It
      // borrows the stopped-oracle companion instance only while OVERDUBBING, retires a candidate whose identity moved
      // before anything can reuse that storage, and drives one grain of the base fill + continuation. Nothing is
      // adopted and nothing is emitted (slice 3 owns the audible switch).
      (void)trackManager.getTrack(i).serviceOverdubOverlayWindow();
#endif
      trackManager.getTrack(i).serviceAdmittedDeadlineMisses(windowClock);
      offers[i] = trackManager.getTrack(i).playbackWindowServiceOffer(windowPhase, windowClock);
    }
    // The holder here is a **track**: across holders the rule is **round-robin** (the spec's fairness sentence), so the
    // cursor decides which track is served and urgency is deliberately *not* an ordering key -- a track whose interval is
    // permanently earlier than another's would otherwise be chosen every turn and starve it (the defect this fixes).
    // Urgency still decides one level down: the offer above is the track's own most urgent owing holder, which is the
    // "within a holder, earliest outstanding deadline first" half of the same requirement.
    sWindowHolderCursor.next = static_cast<uint8_t>(sWindowHolderCursor.next % (offerCount > 0 ? offerCount : 1));
    uint8_t chosenTrack = MidiServiceDrain::kNoPlaybackHolder;
#if defined(SESSION_CAPTURE)
    const bool windowRunning = clockManager.isTransportRunning();
    const uint32_t windowWorkerStart = micros();
#endif
    Track::PlaybackViewTurnResult windowResult = Track::PlaybackViewTurnResult::Idle;
    // DEC-246 amendment 27 (owner ruling): round-robin, bounded so a deadline successor is denied at most three
    // consecutive eligible opportunities.
    if (MidiServiceDrain::selectPlaybackHolderBounded(sWindowHolderCursor, offers, offerCount, chosenTrack) ==
        MidiServiceDrain::PlaybackHolderSelection::Service) {
#if PLAYBACK_ROLLING_SUCCESSOR_PROBE
      // DEC-246 amendment 26 (measurement image): a withheld grant serves no one this turn. The injector models a
      // competing owner, so it too is bound by the successor's service contract.
      const bool boundForces =
          MidiServiceDrain::playbackHolderMustBeServed(sWindowHolderCursor, chosenTrack, offers[chosenTrack]);
      if (boundForces) RollingSuccessorProbe::noteForcedGrant(chosenTrack);
      if (!boundForces && RollingSuccessorProbe::denyThisGrant(chosenTrack, windowClock.tick)) {
        MidiServiceDrain::notePlaybackHolderWithheld(sWindowHolderCursor, chosenTrack, offers[chosenTrack]);
        chosenTrack = MidiServiceDrain::kNoPlaybackHolder;
      } else {
#endif
      // The turn also consumes the clock: the deadlines the admission promised are serviced there (blocker 3, half 2).
      windowResult =
          trackManager.getTrack(chosenTrack).stepPlaybackViewMaintenance(windowPhase, nullptr, false, windowClock,
              []() { return clockManager.getPlaybackDeadlineClock(); });
      MidiServiceDrain::notePlaybackHolderServed(sWindowHolderCursor, chosenTrack);
#if PLAYBACK_ROLLING_SUCCESSOR_PROBE
      RollingSuccessorProbe::noteGrant(chosenTrack, windowClock.tick);
      }
#endif
    }
#if PLAYBACK_ROLLING_SUCCESSOR_PROBE
    RollingSuccessorProbe::noteSuccessorOpportunities(offers, offerCount, chosenTrack, windowClock.tick);
#endif
#if defined(SESSION_CAPTURE)
    uint8_t windowWantMask = 0;
    for (uint8_t i = 0; i < offerCount && i < 8; ++i)
      if (offers[i].eligible && offers[i].canProgress) windowWantMask = static_cast<uint8_t>(windowWantMask | (1u << i));
    Track::samplePlaybackLaneOpportunity(windowClock, windowRunning,
        chosenTrack != MidiServiceDrain::kNoPlaybackHolder, windowResult,
        chosenTrack != MidiServiceDrain::kNoPlaybackHolder ? micros() - windowWorkerStart : 0, windowWantMask,
        chosenTrack);
#else
    (void)windowResult;
#endif
    return;
  }
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Persistence ||
      deferredOwner == MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay) {
    return;
  }
  bool anyRealtimeTrackActive = false;
  bool anyPendingSnapshotPublish = false;
  bool anyPendingStop = false;
  bool anyConvergenceOwed = false;
  bool anyViewPrewarmPending = false;
  bool pendingSnapshotByTrack[Config::NUM_TRACKS] = {};
  uint8_t pendingStopMask = 0;
  uint8_t convergenceMask = 0;
  uint8_t viewPrewarmMask = 0;
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
    const bool pendingStop = t.hasPendingDeferredOverdubStop();
    if (i < Config::NUM_TRACKS && pendingStop) {
      pendingStopMask = static_cast<uint8_t>(pendingStopMask | (1u << i));
    }
    anyPendingStop = anyPendingStop || pendingStop;
    const bool convergenceOwed = t.hasDeferredConvergenceObligation();
    if (i < Config::NUM_TRACKS && convergenceOwed) {
      convergenceMask = static_cast<uint8_t>(convergenceMask | (1u << i));
    }
    anyConvergenceOwed = anyConvergenceOwed || convergenceOwed;
    const bool viewPrewarmPending = t.hasViewPrewarmPending();
    if (i < Config::NUM_TRACKS && viewPrewarmPending) {
      viewPrewarmMask = static_cast<uint8_t>(viewPrewarmMask | (1u << i));
    }
    anyViewPrewarmPending = anyViewPrewarmPending || viewPrewarmPending;
  }
  const bool allTracksStopped = !anyRealtimeTrackActive;
  // I1 (2026-10-30 plan): a pending deferred stop is authoritative work even while every track is stopped
  // and the host keeps clocking. The old all-stopped refusal below starved the stop lane until the host
  // released the clock (DEC-224 measured 4 grains in 60 s; the rig still carries that workaround). Service
  // one bounded stop-lane turn per idle pass, on the track that owns the job, before any other stopped-
  // state policy. The stop grain path is bounded by its own burst budget; this does not admit snapshot or
  // visual construction under a running host clock.
  static uint8_t sStoppedIdleTrackCursor = 0;
  if (allTracksStopped && clockManager.isClockRunning() && anyPendingStop) {
    if (sStoppedIdleTrackCursor >= scannedTrackCount) {
      sStoppedIdleTrackCursor = 0;
    }
    uint8_t trackIndex = sStoppedIdleTrackCursor;
    for (uint8_t offset = 0; offset < scannedTrackCount; ++offset) {
      const uint8_t candidate =
          static_cast<uint8_t>((sStoppedIdleTrackCursor + offset) % scannedTrackCount);
      if ((pendingStopMask & (1u << candidate)) != 0) {
        trackIndex = candidate;
        break;
      }
    }
    trackManager.getTrack(trackIndex)
        .processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner,
                                        MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn,
                                        timingCritical);
    sStoppedIdleTrackCursor = static_cast<uint8_t>((trackIndex + 1) % scannedTrackCount);
    return;
  }
  // A held host clock must not starve the DEC-172 obligation either: once no stop job is pending, the
  // same all-stopped pass admits **one** bounded convergence grain (the service call is at most one
  // slot per invocation). Without this, a 4 s companion budget retired raw and the owed companions
  // never rebuilt while the rig kept the clock running (`midifile-chain-22/23`, edit 11/7).
  if (allTracksStopped && clockManager.isClockRunning() && anyConvergenceOwed) {
    if (sStoppedIdleTrackCursor >= scannedTrackCount) {
      sStoppedIdleTrackCursor = 0;
    }
    for (uint8_t offset = 0; offset < scannedTrackCount; ++offset) {
      const uint8_t candidate =
          static_cast<uint8_t>((sStoppedIdleTrackCursor + offset) % scannedTrackCount);
      if ((convergenceMask & (1u << candidate)) != 0) {
        (void)trackManager.getTrack(candidate).serviceDeferredCanonicalConvergence();
        sStoppedIdleTrackCursor = static_cast<uint8_t>((candidate + 1) % scannedTrackCount);
        // Do not let the obligation monopolize the all-stopped pass: a pending Record-target
        // preparation must get one step per pass too. Fall through to the view-prewarm branch
        // below when one is owed; otherwise the convergence keeps its previous exclusive turn.
        if (!anyViewPrewarmPending) {
          return;
        }
        break;
      }
    }
  }
  // DEC-231: the Record target's view preparation is admitted under the same all-stopped/host-clock
  // condition as the stop lane and convergence above. Without this branch the clear's preparation request
  // never advances, the Record edge correctly stays pending, and no recording can start
  // (`midifile-chain-29`: RECA entered=0). One natural unit per pass; no content work at the edge.
  if (allTracksStopped && clockManager.isClockRunning() && anyViewPrewarmPending) {
    if (sStoppedIdleTrackCursor >= scannedTrackCount) {
      sStoppedIdleTrackCursor = 0;
    }
    for (uint8_t offset = 0; offset < scannedTrackCount; ++offset) {
      const uint8_t candidate =
          static_cast<uint8_t>((sStoppedIdleTrackCursor + offset) % scannedTrackCount);
      if ((viewPrewarmMask & (1u << candidate)) != 0) {
        (void)trackManager.getTrack(candidate).stepViewPrewarmForIdle();
        sStoppedIdleTrackCursor = static_cast<uint8_t>((candidate + 1) % scannedTrackCount);
        return;
      }
    }
  }
#if REVISION_STORE_PROBE
  // DEC-246 C2b/C3a: the shadow store's one call site, on the Snapshot owner turn in both regimes
  // (`RevisionEditIndexGrain` bounded when timing-critical; the import runs only while the transport is stopped).
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot) {
    RevisionShadow::serviceIdleTurn(timingCritical, clockManager.isTransportRunning());
  }
#endif
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
          .processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner,
                                               MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn,
                                               timingCritical);
      sStoppedIdleTrackCursor = static_cast<uint8_t>((trackIndex + 1) % trackCount);
      return;
    }
    if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend) {
      for (uint8_t offset = 0; offset < trackCount; ++offset) {
        const uint8_t trackIndex =
            static_cast<uint8_t>((sStoppedIdleTrackCursor + offset) % trackCount);
        Track& track = trackManager.getTrack(trackIndex);
        if (track.loopsAllocated() && track.getActiveLoop().hasDeferredVisualCacheWrapAppend()) {
          track.processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner,
                                               MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn,
                                               timingCritical);
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
      // A restored slot may carry only a published root (no materialised capture passes), so requiring
      // `hasCommittedPasses()` here silently dropped the selected slot out of the focus lane: on boot the
      // round-robin then walked 63 other slots first and the panel painted an empty focused loop for tens of
      // seconds. The selected slot owns the first paint whenever it has any committed content at all.
      const bool focusHasContent = focusLoop.hasCommittedPasses() ||
                                   focusLoop.publishedSnapshotIdentity() != nullptr;
      focusVisualCacheNeedsFirstPaint = focusHasContent && focusLoop.visualCacheDirty &&
                                        focusLoop.visualCache.notes.empty();
    }
    if (StorageManager::isBootTitleLoadDrain() || focusVisualCacheNeedsFirstPaint) {
      focusTrackRef.processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain,
                                                   deferredOwner,
                                                   MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn,
                                                   timingCritical);
      return;
    }
    trackManager.getTrack(sStoppedIdleTrackCursor)
        .processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner,
                                               MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn,
                                               timingCritical);
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
                                                      deferredOwner,
                                                      MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn,
                                                      timingCritical);
      }
    }

    // A background snapshot may run only when no live capture or unqualified
    // stop is active. A stopped RecordPublished job whose queued root is needed
    // to retire its frozen interval may borrow this existing one-unit turn;
    // never stack a second background construction unit.
    if (trackManager.backgroundSnapshotWorkAllowed()) {
      static uint8_t sBackgroundSnapshotTrackCursor = 0;
      if (realtimeTrackCount != 0) {
        for (uint8_t offset = 0; offset < realtimeTrackCount; ++offset) {
          const uint8_t trackIndex = static_cast<uint8_t>(
              (sBackgroundSnapshotTrackCursor + offset) % realtimeTrackCount);
          if (trackIndex == selectedTrackIndex) {
            continue;
          }
          Track& track = trackManager.getTrack(trackIndex);
          // One bounded background unit, including a stopped RecordPublished
          // job whose queued root alone can release its finalization hold.
          const bool backgroundTrackEligible = track.canServiceBackgroundSnapshotOwnerTurn();
          if (!backgroundTrackEligible) {
            continue;
          }
          track.processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner, 1,
                                               timingCritical);
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
  // The selected track is the interaction and paint target, so it gets the realtime idle opportunity before
  // the background walk (the same priority the stopped lane gives the focus slot). Without this the panel's
  // own slot waited behind every other loaded track's maintenance.
  //
  // DEC-216 (ExecutionBounds row 19): the walk is bounded by the **turn**. `kOwnerWorkBudgetUs` bounded one
  // child, never the sum of the tracks this loop visits, so one turn could spend 283 ms of deferred work on
  // the thread that also emits playback MIDI and drives the panel (measured: `late_on`/`late_off` 294-326 ms
  // and ~300 late events per 5 s in `session_20261030_114009.log`). One unit always runs and every *further*
  // unit must fit the turn budget, so the turn is bounded to one unit's own cost plus the budget.
  //
  // The **first** unit alternates between the selected track and the rotating background cursor. Without the
  // alternation a selected track that alone spends the budget would consume the turn every time and the
  // background walk would never reach its tail -- the starvation the old unconditional walk avoided by
  // paying for all of it. At the loop's turn rate one skipped turn is sub-millisecond, so the selected
  // track's priority is preserved in practice.
  //
  // RAM2: `.bss.dma` is NOLOAD, so the value may start as garbage; the modulo below makes any index valid and
  // either phase is correct. Deliberately not DTCM: RAM1 has no growth allowance
  // (`scripts/ram1_budget.json`), and one section byte costs 32.
  const bool selectedFirst = (gRealtimeIdleRotation & 0x80u) != 0;
  uint8_t cursor = static_cast<uint8_t>(gRealtimeIdleRotation & 0x7Fu);
  if (realtimeTrackCount != 0) {
    cursor = static_cast<uint8_t>(cursor % realtimeTrackCount);
  }
  const uint8_t selectedRealtimeTrack = trackManager.getSelectedTrackIndex();
  uint8_t turnUnits = 0;
  const auto runRealtimeUnit = [&](uint8_t index) {
    Track& track = trackManager.getTrack(index);
    if (!hasRealtimeIdleWork(track)) return false;
    const uint32_t unitStartUs = micros();
    track.processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredOwner,
                                         MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn,
                                         timingCritical);
    ++turnUnits;
    reportSlowIdleTurnUnit(index, micros() - unitStartUs, micros() - turnStartUs, turnUnits);
    return true;
  };
  const auto turnStartSelected = [&]() {
    return selectedRealtimeTrack < realtimeTrackCount && runRealtimeUnit(selectedRealtimeTrack);
  };
  // One background unit, resuming at the cursor. Stopped-slot reconstruction is background work: running
  // every loaded track here stacked 120-281 ms turns into the first recording bars.
  const auto runOneBackgroundUnit = [&]() {
    for (uint8_t offset = 0; offset < realtimeTrackCount; ++offset) {
      const uint8_t index = static_cast<uint8_t>((cursor + offset) % realtimeTrackCount);
      if (index == selectedRealtimeTrack) continue;
      if (runRealtimeUnit(index)) {
        cursor = static_cast<uint8_t>((index + 1) % realtimeTrackCount);
        return true;
      }
    }
    return false;
  };
  // DEC-219: a pending deferred stop is served **first inside the walk**, not before the owner dispatch.
  // Ordering it ahead of every owner (the first attempt) preempted `LoadAndDisplay`/`Persistence`, and one
  // stop press then froze the panel: `session_20261030_133920.log` has `disp_turn` holes of 16.2 / 16.1 /
  // **34.1 s** and the owner was starved. The stop lane lives inside `processDeferredIdleMaintenance`, so it
  // only needs to win the *rotation* -- the owner rotation keeps its own turns, which is what the display
  // and save lanes depend on. Measured before any priority: 113 `stop_grain` rows in 100 s (mean gap
  // 962 ms) and a head parked 75 s. Its own branch stays bounded (one to four grains, 600 us).
  uint8_t pendingStopTrack = 0xFF;
  for (uint8_t i = 0; i < realtimeTrackCount; ++i) {
    if (trackManager.getTrack(i).hasPendingDeferredOverdubStop()) {
      pendingStopTrack = i;
      break;
    }
  }
  if (pendingStopTrack != 0xFF) {
    (void)runRealtimeUnit(pendingStopTrack);
  }
  if (selectedFirst) {
    if (pendingStopTrack != selectedRealtimeTrack) turnStartSelected();
    if (MidiServiceDrain::idleOwnerTurnMayStartAnotherUnit(micros() - turnStartUs)) {
      (void)runOneBackgroundUnit();
    }
  } else {
    (void)runOneBackgroundUnit();
    if (MidiServiceDrain::idleOwnerTurnMayStartAnotherUnit(micros() - turnStartUs)) {
      if (pendingStopTrack != selectedRealtimeTrack) turnStartSelected();
    }
  }
  while (MidiServiceDrain::idleOwnerTurnMayStartAnotherUnit(micros() - turnStartUs)) {
    if (!runOneBackgroundUnit()) break;
  }
  gRealtimeIdleRotation = static_cast<uint8_t>(cursor | (selectedFirst ? 0u : 0x80u));
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
#if defined(SESSION_CAPTURE)
  Track::resetPlaybackLaneMeasurement();
#endif
  // DEC-183: DMAMEM is not guaranteed zero-initialised here, and a garbage rate-limit stamp makes
  // `now - stamp` negative for the whole boot, silently suppressing every `paint_gate` row.
  gPaintGateLastMs = 0;
  gBootVisualCacheReported = 0;
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
  // DEC-241: the tick-context state is RAM2 (NOLOAD); clear it before the clock or any playback can run.
  PlaybackTickContext::initAtBoot();
#if RUNTIME_PROFILE_SCOPES
  // DEC-239: the profiler's RAM2 state is NOLOAD; clear it, calibrate and emit the boot row before
  // `prewarmPlaybackRuntime()` below, the first call that can reach an instrumented owner.
  RuntimeProfiler::initAtBoot();
#endif
#if REVISION_STORE_PROBE
  RevisionShadow::initAtBoot();  // DEC-246 Stage C shadow store: PSRAM, once
#endif
#if PLAYBACK_ROLLING_SUCCESSOR_PROBE
  // DEC-246 amendment 25 (measurement image only): the successor-lead probe's RAM2 state, before any playback.
  RollingSuccessorProbe::installAtBoot();
#endif
  // DEC-190: after a fault, park **only** the slot whose restore/first paint was in flight. The file is not
  // touched, so the rest of the set restores and this slot can be retried once the cause is fixed. A normal
  // reboot leaves no crash report, so a stale breadcrumb is ignored and cleared.
  StorageManagerInternal::resetRestoreFaultSkips();
  if (hadCrashReport) {
    uint8_t faultTrack = 0;
    uint8_t faultSlot = 0;
    if (StorageManagerInternal::takeFaultedLoopSlot(faultTrack, faultSlot)) {
      (void)StorageManagerInternal::markRestoreFaultSkip(faultTrack, faultSlot);
      Serial.printf_P(PSTR("[StorageManager] Fault during slot restore: track=%u slot=%u parked for this boot\r\n"),
                      static_cast<unsigned>(faultTrack), static_cast<unsigned>(faultSlot));
    }
  } else {
    StorageManagerInternal::clearLoopSlotRestoreBreadcrumb();
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
  // One role-owned entry opportunity, bracketed by real MIDI service. Never drain.
  editManager.stepCurrentRootNoteEditEntry();
  midiHandler.handleMidiInput();
#if defined(SESSION_CAPTURE)
  const uint32_t loopPrefixStartUs = micros();
#endif

  midiHandler.processDroidUsbHostOutbound();

  // Timing-path review, item 1: the MIDI tick edge (an `IntervalTimer` ISR while the clock is internal)
  // records what it decided -- the slot-switch commit row, the panel's invalidation, the LED refresh -- and
  // this is where that work is done, beside the other deferred outbound work. It sits here so a row is
  // emitted before this iteration's `SC_CAPTURE_FLUSH` rather than after it.
  trackManager.drainEdgeEffects();
  // DEC-241: build, off-clock, any merged playback fallback the internal-clock ISR tick found stale (it never builds).
  trackManager.serviceOwedTickPlaybackFallbacks();
  trackManager.reportPlaybackRouteRejects(millis());

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
  if (!editManager.hasPendingCurrentRootNoteEditEntry() &&
      editManager.getCurrentState() == editManager.getSelectNoteState()) {
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
  // DEC-183: report-only. Emitted **unconditionally once per second while the boot title drain is active** (plus
  // whenever a selected slot owes its first paint), because the question is which owner the rotation takes during the
  // window *before* the first paint -- and a predicate keyed on "owed" proved false there (the selected slot is not
  // the display slot yet during the drain).
  {
    const bool drainActive = StorageManager::isBootTitleLoadDrain();
    const uint8_t gateTrack = trackManager.getSelectedTrackIndex();
    bool owed = false;
    uint16_t focusNotes = 0;
    if (gateTrack < trackManager.getTrackCount() && trackManager.getTrack(gateTrack).loopsAllocated()) {
      const Loop& gateLoop = trackManager.getTrack(gateTrack).getActiveLoop();
      focusNotes = static_cast<uint16_t>(gateLoop.visualCache.notes.size());
      owed = gateLoop.hasCommittedPasses() && gateLoop.visualCacheDirty &&
             gateLoop.visualCache.notes.empty();
    }
    if ((drainActive || owed) && static_cast<int32_t>(now - gPaintGateLastMs) >= 1000) {
      gPaintGateLastMs = now;
      SC_PAINT_GATE(static_cast<uint8_t>(deferredIdleOwner), deferredIdleOwnerAllowed ? 1u : 0u,
                    drainActive ? 1u : 0u, clockManager.isClockRunning() ? 1u : 0u,
                    StorageManager::hasPendingLoopSlotRestore() ? 1u : 0u,
                    StorageManager::hasLoadLoopJobCommitPending() ? 1u : 0u,
                    StorageManagerInternal::pendingLoopSlotRestoreCount(),
                    focusNotes, owed ? 1u : 0u);
    }
  }
#endif

  // Drain transport commands before a STOPPING grain validates its destination,
  // and preserve the post-overdub PLAYING polls at these same call sites.
  if (MidiServiceDrain::aroundIdleMaintenance(postOverdubPlayingMidiDrain,
                                              overdubStoppingForMidiDrain)) {
    midiHandler.handleMidiInput();
  }
  // DEC-216: the turn start is taken in **every** build. The realtime walk bounds itself against it, so the
  // aggregate ceiling must not depend on `SESSION_CAPTURE` being compiled in.
  const uint32_t idleTurnStartUs = micros();
  if (deferredIdleOwnerAllowed) {
    RuntimeTimingTelemetry::beginIdleMaintBreakdown();
    runDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain, deferredIdleOwner,
                              timingCriticalTrackActive, idleTurnStartUs);
  }
#if defined(SESSION_CAPTURE)
  const uint32_t idleMaintUs = micros() - idleTurnStartUs;
  NOTE_LANE_GAP_ITERATION(deferredIdleOwner, deferredIdleOwnerAllowed, idleMaintUs);
  RUNTIME_TIMING_NOTE_IDLE_MAINT(idleMaintUs);
  DebugSessionCapture::recordLoopRemainderSpan("idle_maint", idleMaintUs);
  // Task 5.4: near-bar turns only (3x the owner budget), one row per 100 ms. The row names the
  // admitted child sum and the residual; `mask` is the child bitmap (0 snapshot, 1 verification,
  // 2 source view, 3 visual-cache slice, 4 wrap append, 5 prewarm, 6 record revts, 7 stop/session
  // obligation -- DEC-216).
  if (deferredIdleOwnerAllowed && idleMaintUs >= 24000u &&
      RuntimeTimingTelemetry::idleMaintBreakdownShouldEmit(static_cast<uint32_t>(now))) {
    const uint32_t childUs = RuntimeTimingTelemetry::idleMaintChildUs();
    char breakdownLine[144];
    snprintf_P(breakdownLine, sizeof(breakdownLine),
               PSTR("#CAP,%lu,DIAG,idle_maint,breakdown,total=%lu,child=%lu,residual=%lu,mask=0x%x"),
               static_cast<unsigned long>(micros()), static_cast<unsigned long>(idleMaintUs),
               static_cast<unsigned long>(childUs),
               static_cast<unsigned long>(idleMaintUs > childUs ? idleMaintUs - childUs : 0),
               static_cast<unsigned>(RuntimeTimingTelemetry::idleMaintChildMask()));
    DebugSessionCapture::appendCaptureTextLine(breakdownLine);
  }
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

  // Load/Commit/prewarm before pressure reclaim — reclaim after a 64-bar Commit raced the
  // deferred prewarm path (000659: commit_prewarm_q then silence).
  uint32_t loadFrameUs = 0;
  // **DEC-194 — boot drain cadence.** During the boot title drain one bounded load slice may run on every frame; the
  // slice budget is untouched. Exactly one service per frame: when the deferred owner is already LoadAndDisplay the two
  // terms describe the same call. At title release the flag clears and normal arbitration resumes unchanged.
  const bool bootLoadFrame =
      bootLoadAdmitsEveryFrame(StorageManager::isBootTitleLoadDrain(),
                               StorageManager::hasPendingLoopSlotRestore());
  if (bootLoadServicesThisFrame(
          bootLoadFrame, deferredIdleOwner == MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay) &&
      deferredIdleOwnerAllowed) {
    const uint32_t loadFrameStartUs = micros();
    runDeferredLoadAndDisplayFrame(now, lastDisplayUpdate, timingCriticalTrackActive,
                                   postOverdubPlayingMidiDrain);
    loadFrameUs = micros() - loadFrameStartUs;
  }
#if defined(SESSION_CAPTURE)
  RUNTIME_TIMING_NOTE_LOAD_FRAME(loadFrameUs);
  NOTE_LANE_GAP_LOAD_FRAME(loadFrameUs);
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
#if defined(SESSION_CAPTURE)
  // Task 4.20 allocation census: one aggregated Tier-A row per diagnostics interval, never per turn. This is the
  // device-side half of the question the 128-cycle host run could not answer — the host proved persistent pass
  // cardinality is linear but cannot reproduce a tens-of-KB internal heap, so the correlation (`heap`/`hmin` against
  // `ib`/`eb`/`ub` and the pass counts) has to be read here. Rate-limited on `now` (ms) so nothing samples on the
  // interval path; `min_ever` carries the true floor between samples.
  {
    constexpr uint32_t kAllocationCensusIntervalMs = 2000;
    static uint32_t sLastAllocationCensusMs = 0;
    static bool sAllocationCensusStarted = false;
    if (!sAllocationCensusStarted ||
        static_cast<uint32_t>(now - sLastAllocationCensusMs) >= kAllocationCensusIntervalMs) {
      sAllocationCensusStarted = true;
      sLastAllocationCensusMs = now;
      const Track::AllocationCensus census = trackManager.getSelectedTrack().allocationCensus();
      SC_ALLOC_CENSUS(MemoryMonitor::getInternalHeapFreeBytes(),
                      MemoryMonitor::getInternalHeapMinEverFreeBytes(),
                      LoopEventStore::freeChunkCount(), census.capturePasses, census.editPasses,
                      census.undoEntries, census.internalBytes, census.editPassBytes,
                      census.undoIdBytes, census.externalBytes, editManager.isLoopEditSession());
      HeapPeakTrace::emitPending(6);
    }
    // DEC-189: one direct-serial row per second while a stop job or a publication is pending. `#CAP` rows for
      // both lanes live in the ring, which a busy run sheds; `phase`/`a`/`q` staying flat across rows is the
      // proof that a lane is not advancing.
    {
      constexpr uint32_t kStopTraceIntervalMs = 1000;
      const Track::StallCensus stall = trackManager.getSelectedTrack().stallCensus();
      // `.bss.dma` is NOLOAD: a zero stamp means "not armed yet", and a garbage stamp at worst delays one row.
      if ((stall.stopPending || stall.publishPending) &&
          (gStopTraceLastMs == 0 ||
           static_cast<uint32_t>(now - gStopTraceLastMs) >= kStopTraceIntervalMs)) {
        gStopTraceLastMs = now;
        DebugSessionCapture::stopTraceRow(now, stall.stopPending, stall.stopMode, stall.stopPhase,
            stall.stopGeneration, stall.orderGrains, stall.canonicalGrains, stall.rankedGrains,
            stall.undoGrains, stall.publishPending, stall.publishQueued, stall.publishApplied,
            stall.publishInFlight, stall.hasCommittedPasses, stall.loopLengthTicks);
      }
    }
  }
#endif
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
    // Do **not** force the reclaim row here: this branch runs every loop while pressure is Critical,
    // and `true` wrote 10k identical `DIAG,reclaim` rows (`session_20261030_181524.log`), evicting
    // Tier-A evidence. The emitter's change gate still names pin/reclaim events.
    trackManager.reclaimUnreferencedDisabledPasses(nullptr, false);
  }

#if defined(SESSION_CAPTURE)
  const uint32_t persistSaveStartUs = micros();
#endif
  // `root-bearing-loop-persist`: a slot whose body was written without a root is re-admitted here once
  // its publication settles, so the card converges to a root-bearing body without another gesture. One
  // scan of a 64-slot record that is normally all-clear, on the same idle lane.
  StorageManager::serviceRootlessSlotRepersist(looperState.getLooperState());
  // Offer the existing owner for workspace, revision commit/load, or sealed chunks.
  // The worker retains its own SD/load/capture exclusion and slice budget.
  (void)MidiServiceDrain::serviceDeferredPersistenceTurn<StorageManager>(
      looperState.getLooperState(), deferredIdleOwner, deferredIdleOwnerAllowed,
      timingCriticalTrackActive, loadFrameUs, postOverdubPlayingMidiDrain,
      overdubStoppingForMidiDrain);
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
