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
#include "Utils/NoteMovementWrap.h"
#include "Utils/NoteMovementUtils.h"
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

void DisplayManager::invalidateForSlotChange(uint8_t trackIndex, uint8_t previousSlot,
                                             uint8_t newSlot) {
    if (trackIndex >= trackManager.getTrackCount()) {
        return;
    }
    invalidateLiveDisplayCache();
    Track& track = trackManager.getTrack(trackIndex);
    const uint8_t activeSlot = track.getActiveLoopIndex();
    const bool playbackActive = track.isPlaying() || track.isOverdubbing();

    auto invalidateSlotDisplay = [&](uint8_t slot) {
        if (slot >= Config::MAX_LOOPS_PER_TRACK) {
            return;
        }
        Loop& loop = track.getLoop(slot);
        if (slot == newSlot || (playbackActive && slot == activeSlot)) {
            loop.invalidateDisplayCaches();
        } else {
            loop.markDisplayCachesStale();
        }
    };

    invalidateSlotDisplay(previousSlot);
    invalidateSlotDisplay(newSlot);

    if (editManager.isNoteEditActive()) {
        track.invalidateCaches();
    } else if (!playbackActive) {
        // Stopped: full cache invalidate on focus change is fine.
        track.invalidateCaches();
    }
    // Playing + LOOP_EDIT preview: slot display caches already updated above — do not
    // invalidate the active playing loop's playback/note caches (causes start hitch).

    if (playbackActive && trackIndex == trackManager.getSelectedTrackIndex() &&
        !isPreviewPlayheadPending(newSlot, activeSlot, playbackActive)) {
        centerDetailedWindowOnPlayhead(track, newSlot, clockManager.getCurrentTick());
    }
}

void DisplayManager::refreshViewportAfterRecordStop(Track& track, uint8_t displaySlot,
                                                   uint32_t storagePhaseTickInLoop) {
    invalidateLiveDisplayCache();
    const Loop& loop = track.getLoop(displaySlot);
    if (!loop.visualCacheDirty && !loop.visualCache.notes.empty()) {
        liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = resolveTrackIndex(track);
    }
    const uint32_t loopLength =
        resolveDisplayLoopLength(track, displaySlot, clockManager.getCurrentTick());
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (track.isJamming() || loopLength <= boundedThreshold || displaySlot >= kDisplaySlotCount) {
        return;
    }
    const uint8_t windowBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                                 DisplayWindowUtils::kMaxDetailedWindowBars);
    const uint32_t windowLength = static_cast<uint32_t>(windowBars) * Config::TICKS_PER_BAR;
    detailedWindowStartTick_[displaySlot] = DisplayWindowUtils::resolveCenteredWindowStart(
        storagePhaseTickInLoop, windowLength, loopLength);
}

void DisplayManager::invalidateLiveDisplayCache() {
    liveDisplayCacheEventCount = static_cast<size_t>(-1);
    liveDisplayCacheCommittedNoteCount_ = 0;
    liveDisplayCacheCaptureRevision = 0;
    liveDisplayCacheLoopLength = 0;
    liveDisplayCacheSlot = 255;
    liveDisplayCacheTrackState = NUM_TRACK_STATES;
    liveDisplayCacheOpenNotes.clear();
    liveDisplayNotes.clear();
    liveDisplayEventBuffer.clear();
    livePlaybackDisplaySlot_ = 255;
    livePlaybackDisplayTrack_ = 255;
    liveMergePlaybackRevision_ = UINT32_MAX;
    liveMergeCaptureRevision_ = 0;
    liveWindowGatherStart_ = 0;
    liveWindowGatherLength_ = 0;
    liveWindowGatherLoopLength_ = 0;
    liveWindowGatherValid_ = false;
    invalidateNoteEditDisplayCache();
}

void DisplayManager::invalidateNoteEditDisplayCache() {
    editManager.invalidateProjectedNoteEditDisplayCache();
    liveDisplayNotes.clear();
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

void DisplayManager::requestNoteInfoRefresh(Track& track) {
    invalidateNoteEditDisplayCache();
    // NOTE_EDIT piano roll uses projectedNoteEditDisplayNotes — avoid getCachedNotes() here
    // (full loop materialize stalls the OLED path during live edit).
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        (void)track.getCachedNotes();
    }
}

void DisplayManager::applyWorkspaceDisplayRefreshPending(uint32_t currentTick) {
    if (!workspaceDisplayRefreshPending_) {
        return;
    }
    workspaceDisplayRefreshPending_ = false;
    invalidateLiveDisplayCache();
    // Do not mark every track/slot visual cache stale here — that forced a full
    // rematerialize on the next frame under ~64 KB internal heap and hung the OLED
    // after the first good piano-roll paint (grid-only freeze).
    Track& selectedTrack = trackManager.getSelectedTrack();
    editManager.rematerializeNoteEditSessionAfterWorkspaceReload(selectedTrack);
    trackManager.forceLedUpdate(currentTick);
}

void DisplayManager::update() {
    const uint32_t telemetryStartUs = micros();
    uint32_t now = millis();
    if (bootScreenVisible_) {
        if (!bootSetupComplete_ || now < bootScreenHoldUntilMs_) {
            return;
        }
        bootScreenVisible_ = false;
    }

    uint32_t currentTick = clockManager.getCurrentTick();
    const bool loadSaveActive = looperState.isLoadSaveModeActive();
    if (!loadSaveActive && StorageManager::consumeRevisionLoadDisplayRefreshPending()) {
        workspaceDisplayRefreshPending_ = true;
    }
    Track& selTrack = trackManager.getSelectedTrack();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    uint32_t displayTick = selTrack.getEffectivePlaybackTick(currentTick);
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

    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);

    if (loadSaveActive) {
        drawLoadSaveView(now);
        _display.api.display();
        HotPathTelemetry::recordDisplayUpdate(micros() - telemetryStartUs);
        return;
    }

    drawTrackStatus(trackManager.getSelectedTrackIndex(), now);
    const DisplayNoteVec& frameNotes = resolveDisplayNotes(selTrack, displaySlot, displayTick);
#if defined(SESSION_CAPTURE)
    maybeEmitDisplayCaptureOnChange(selTrack, displaySlot, displayTick, frameNotes);
#endif
    drawPianoRoll(displayTick, selTrack, displaySlot, frameNotes);
    drawSidebar(selTrack, displaySlot);
    drawInfoArea(displayTick, selTrack, displaySlot, now);
    drawNoteInfo(displayTick, selTrack, displaySlot, frameNotes);

   _display.api.display();
#if defined(SESSION_CAPTURE)
    {
        static uint32_t dframeCounter = 0;
        if (++dframeCounter % 30U == 0U) {
            SC_DFRAME(static_cast<uint32_t>(frameNotes.size()),
                      static_cast<uint32_t>(micros() - telemetryStartUs), dframeCounter);
        }
    }
#endif
    HotPathTelemetry::recordDisplayUpdate(micros() - telemetryStartUs);
    if (editManager.isNoteEditActive()) {
        editManager.markNoteEditDisplayPainted();
    }
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
