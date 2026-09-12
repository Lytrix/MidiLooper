//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// DisplayManager.cpp
#include "DisplayManager.h"
#include "TrackManager.h"
#include "ClockManager.h"
#include "Globals.h"
#include "SSD1322_Config.h"
#include "TrackUndo.h"
#include "Logger.h"
#include "Utils/NoteUtils.h"
#include "TickPhase.h"
#include "ControlSurfaceManager.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "MidiHandler.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/SlotFocusDisplay.h"
#include "TrackStateMachine.h"
#include "MidiButtonManager.h"
#include "MidiConfig.h"
#include "StorageManager.h"
#include "SlotLoadSession.h"
#include "SetBrowserOverlayPolicy.h"
#include "SetRevisionCatalog.h"
#include "RtcTime.h"
#include "HitlDisplayBridge.h"
#include "DisplayManagerInternal.h"

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#else
#define DISP_COLD_MEM
#endif

using namespace DisplayManagerInternal;

#include "DeferredSaveDisplayStatus.h"
#include "LooperState.h"
#include "SavedSetCatalog.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>

#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define DISP_CAPTURE_MEM FLASHMEM
#else
#define DISP_CAPTURE_MEM
#endif

DMAMEM DisplayManager displayManager;

DisplayManager::DisplayManager() : _display() {
    // Don't initialize SPI here - it will be done in setup()
}

// Helper function to clear the display buffer
void DisplayManager::clearDisplayBuffer() {
    Serial.println("DisplayManager: Clearing display buffer");
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);
    Serial.println("DisplayManager: Display buffer cleared");
    Serial.println("DisplayManager: Displaying buffer");   
   _display.api.display();
    Serial.println("DisplayManager: Display buffer displayed");
}

void DisplayManager::setup() {
    Serial.println("DisplayManager: Setting up SSD1322 display...");

    // Initialize display
    _display.begin();

    // Set buffer size and clear display
    Serial.println("DisplayManager: Setting buffer size");
    _display.gfx.set_buffer_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    clearDisplayBuffer();
}

bool DisplayManager::hasPendingPaintPhase() const {
    return DisplayWindowUtils::displayFramePhaseIsPending(pendingPaintPhase_);
}

const DisplayNoteVec& DisplayManager::resolvedPaintNotes() const {
    return resolvedPaintUsesPending_ ? liveDisplayPendingPaintNotes_ : liveDisplayNotes;
}

void DisplayManager::runPaintResolvePhase(uint32_t now) {
#if defined(SESSION_CAPTURE) || HOT_PATH_TELEMETRY_ENABLED
    paintFrameStartUs_ = micros();
    paintResolveUs_ = 0;
    paintDrawUs_ = 0;
    paintTransferUs_ = 0;
    const uint32_t phaseStartUs = paintFrameStartUs_;
#endif
    const uint32_t currentTick = clockManager.getCurrentTick();
    const bool loadSaveActive = looperState.isLoadSaveModeActive();
    if (!loadSaveActive && StorageManager::consumeRevisionLoadDisplayRefreshPending()) {
        workspaceDisplayRefreshPending_ = true;
    }
    paintTrackIndex_ = trackManager.getSelectedTrackIndex();
    paintSlot_ = trackManager.getSelectedSlotIndex(paintTrackIndex_);
    Track& selTrack = trackManager.getSelectedTrack();
    paintDisplayTick_ = selTrack.getEffectivePlaybackTick(currentTick);
    refreshAutoSaveBeforeLoadToast(now);

    if (loadSaveActive && !loadSaveModeWasActive_) {
        const bool preserveNavigation = SetBrowserOverlayPolicy::shouldPreserveOverlayNavigationOnEnter(
            StorageManager::getSetBrowserOverlayPersistencePhase());
        if (!preserveNavigation) {
            StorageManager::resetSetBrowserOverlayNavigation();
            loadSaveListSelection_ = 0;
            loadSaveListScrollOffset_ = 0;
            invalidateLoadSaveRevisionListCache();
        }
        if (!loadSaveListCacheValid_ && StorageManager::isOverlayCatalogReadAllowed()) {
            refreshLoadSaveListCache();
        }
        invalidateLoadSaveDetailCache();
    } else if (!loadSaveActive && loadSaveModeWasActive_) {
        if (StorageManager::isRevisionLoadHeldForWorkspaceDirty()) {
            StorageManager::cancelRevisionLoadRequest();
        }
        StorageManager::resetSetBrowserOverlayNavigation();
        invalidateLoadSaveRevisionListCache();
        invalidateLoadSaveDetailCache();
    }
    loadSaveModeWasActive_ = loadSaveActive;
    applyWorkspaceDisplayRefreshPending(currentTick);

    paintLoadSaveActive_ = loadSaveActive;
    resolvedPaintUsesPending_ = false;
    if (!loadSaveActive) {
        const DisplayNoteVec& resolved =
            resolveDisplayNotes(selTrack, paintSlot_, paintDisplayTick_);
        resolvedPaintUsesPending_ = (&resolved == &liveDisplayPendingPaintNotes_);
    }
#if defined(SESSION_CAPTURE) || HOT_PATH_TELEMETRY_ENABLED
    paintResolveUs_ = micros() - phaseStartUs;
#endif
}

void DisplayManager::runPaintDrawPhase() {
#if defined(SESSION_CAPTURE) || HOT_PATH_TELEMETRY_ENABLED
    const uint32_t phaseStartUs = micros();
#endif
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);
    const uint32_t now = millis();
    if (paintLoadSaveActive_) {
        drawLoadSaveView(now);
    } else {
        Track& selTrack = trackManager.getTrack(paintTrackIndex_);
        const DisplayNoteVec& frameNotes = resolvedPaintNotes();
        drawTrackStatus(paintTrackIndex_, now);
        drawPianoRoll(paintDisplayTick_, selTrack, paintSlot_, frameNotes);
        drawSidebar(selTrack, paintSlot_);
        drawInfoArea(paintDisplayTick_, selTrack, paintSlot_, now);
        drawNoteInfo(paintDisplayTick_, selTrack, paintSlot_, frameNotes);
    }
#if defined(SESSION_CAPTURE) || HOT_PATH_TELEMETRY_ENABLED
    paintDrawUs_ = micros() - phaseStartUs;
#endif
}

void DisplayManager::runPaintTransferPhase() {
#if defined(SESSION_CAPTURE) || HOT_PATH_TELEMETRY_ENABLED
    const uint32_t phaseStartUs = micros();
#endif
    _display.api.display();
#if defined(SESSION_CAPTURE) || HOT_PATH_TELEMETRY_ENABLED
    paintTransferUs_ = micros() - phaseStartUs;
#endif
#if defined(SESSION_CAPTURE)
    if (!paintLoadSaveActive_) {
        Track& selTrack = trackManager.getTrack(paintTrackIndex_);
        maybeEmitDisplayCaptureOnChange(selTrack, paintSlot_, paintDisplayTick_,
                                        resolvedPaintNotes());
    }
    {
        static uint32_t dframeCounter = 0;
        if (++dframeCounter % 30U == 0U) {
            const uint32_t notes = paintLoadSaveActive_
                                       ? 0U
                                       : static_cast<uint32_t>(resolvedPaintNotes().size());
            SC_DFRAME(notes, static_cast<uint32_t>(micros() - paintFrameStartUs_), dframeCounter,
                      paintResolveUs_, paintDrawUs_, paintTransferUs_);
        }
    }
#endif
}

void DisplayManager::applyCoalescedPaintInvalidation() {
    if (!paintInvalidateCoalesced_ && !paintDeferredClearNotes_) {
        return;
    }
    paintInvalidateCoalesced_ = false;
    if (paintDeferredClearNotes_) {
        paintDeferredClearNotes_ = false;
        liveDisplayNotes.clear();
        liveDisplayCommittedPrefix_.clear();
        liveDisplayPendingPaintNotes_.clear();
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
        resolvedPaintUsesPending_ = false;
    }
}

void DisplayManager::update() {
#ifdef __IMXRT1062__
    // Complete a DMA frame promptly even when the next paint has not yet been admitted.
    _display.api.pollDisplayTransfer();
#endif
#if defined(SESSION_CAPTURE) || HOT_PATH_TELEMETRY_ENABLED
    const uint32_t telemetryStartUs = micros();
#endif
    const uint32_t now = millis();
    if (bootScreenVisible_) {
        if (!bootSetupComplete_ || now < bootScreenHoldUntilMs_) {
            return;
        }
        bootScreenVisible_ = false;
    }

    const DisplayWindowUtils::DisplayFramePhase ran =
        DisplayWindowUtils::displayFramePhaseToRun(pendingPaintPhase_);
    switch (ran) {
        case DisplayWindowUtils::DisplayFramePhase::Resolve:
            runPaintResolvePhase(now);
            break;
        case DisplayWindowUtils::DisplayFramePhase::Draw:
            runPaintDrawPhase();
            break;
        case DisplayWindowUtils::DisplayFramePhase::Transfer:
            runPaintTransferPhase();
            break;
        case DisplayWindowUtils::DisplayFramePhase::Idle:
            break;
    }
    const DisplayWindowUtils::DisplayFramePhaseAdvance advanced =
        DisplayWindowUtils::advanceDisplayFramePhase(ran);
    pendingPaintPhase_ = advanced.pending;
    if (advanced.frameComplete) {
        applyCoalescedPaintInvalidation();
        // Acknowledge unconditionally: invalidateLiveDisplayCache raises the paint request with no
        // note-edit precondition, so gating the ack on isNoteEditActive left it permanently raised
        // outside a session. maybeUpdateDisplayForNoteEditSelection has no cadence gate, so the OLED
        // then repainted every loop iteration — 152948 measured a 14 ms frame period against the
        // 30 ms interval, ~93% of loop time here, from boot onward.
        editManager.markNoteEditDisplayPainted();
    }
    HOT_PATH_TELEMETRY_RECORD_DISPLAY_UPDATE(micros() - telemetryStartUs);
    DIAG_TIMING_RECORD(DisplayUpdateTotal, micros() - telemetryStartUs);
}

#if defined(SESSION_CAPTURE)
namespace HitlDisplayBridge {

void confirmLoadSaveFocusedRow() { displayManager.confirmLoadSaveFocusedRow(); }

void adjustLoadSaveListSelection(int delta) { displayManager.adjustLoadSaveListSelection(delta); }

void openRevisionHistoryFromHitl(uint16_t setId) {
    displayManager.openRevisionHistoryFromHitl(setId);
}

void navigateLoadSaveOverlayBackFromHitl() {
    displayManager.navigateLoadSaveOverlayBackFromHitl();
}

}  // namespace HitlDisplayBridge
#endif
