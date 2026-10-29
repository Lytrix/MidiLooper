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
        // A fresh overlay session gets a fresh retry budget (I4 — Set browser freshness).
        loadSaveListRefreshState_ = SetBrowserOverlayPolicy::ListRefreshState{};
        invalidateLoadSaveDetailCache();
    } else if (!loadSaveActive && loadSaveModeWasActive_) {
        if (StorageManager::isRevisionLoadHeldForWorkspaceDirty()) {
            StorageManager::cancelRevisionLoadRequest();
        }
        StorageManager::resetSetBrowserOverlayNavigation();
        invalidateLoadSaveRevisionListCache();
        invalidateLoadSaveDetailCache();
    }
    if (loadSaveActive) {
        // Cache freshness (I4 — Set browser freshness): a catalog publication invalidates the cached Set
        // rows, and a refresh the catalog gate skipped -- or one that failed to read the index -- is retried
        // under a bounded budget instead of being served (or cached) as "no Sets on this card".
        if (StorageManager::consumeSetCatalogChangePending()) {
            loadSaveListCacheValid_ = false;
            invalidateLoadSaveRevisionListCache();
            loadSaveListRefreshState_ = SetBrowserOverlayPolicy::ListRefreshState{};
        }
        const SetBrowserOverlayPolicy::ListRefreshAction listRefreshAction =
            SetBrowserOverlayPolicy::resolveListRefreshAction(
                true, loadSaveListCacheValid_, StorageManager::isOverlayCatalogReadAllowed(),
                loadSaveListRefreshState_, now);
        if (listRefreshAction == SetBrowserOverlayPolicy::ListRefreshAction::Refresh) {
            refreshLoadSaveListCache();
        }
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
    paintTransferDeferred_ = false;
#ifdef __IMXRT1062__
    // Bank a frame that finished since the last update, then hand this one over only if
    // nothing is on the wire. A busy transport refuses the hand-off and the frame stays
    // pending (advanceDisplayFramePhase) instead of being consumed, so the newest
    // completed framebuffer is what the next admitted transfer sends.
    _display.api.pollDisplayTransfer();
    if (DisplayWindowUtils::admitDisplayTransfer(_display.api.isDisplayTransferActive())) {
        _display.api.display();
    } else {
        paintTransferDeferred_ = true;
    }
#else
    _display.api.display();
#endif
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

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
// RAM2 staging diagnostics. `.bss.dma` has no guard slack, and
// `DisplayManagerInternal::liveDisplayEventBuffer` is linked flush against this
// object's tail, so an overrun past `displayManager` corrupts a snapshot-gather
// destination. Report the first clobber together with the neighbour's vector
// header instead of letting it surface later as a DACCVIOL inside the fill.
// Kept out of line and flash-resident (`printf_P` + `PSTR`): `update()` runs on
// a deep stack and DTCM `.rodata` is charged against the RAM1 stack budget.
DISP_COLD_MEM __attribute__((noinline)) void DisplayManager::checkDmaStagingDiagnostics() {
    static bool baselineLogged = false;
    static uint32_t lastReportUs = 0;
    const uint32_t nowUs = micros();
    const long neighbourGapBytes =
        reinterpret_cast<const char*>(&DisplayManagerInternal::liveDisplayEventBuffer) -
        reinterpret_cast<const char*>(this);
    if (!baselineLogged) {
        baselineLogged = true;
        Serial.printf_P(
            PSTR("#DMA_STAGING_BASE,%lu,display_manager=0x%08lx,object_size=%lu,"
                 "neighbour=0x%08lx,gap_bytes=%ld,canary=0x%08lx\r\n"),
            static_cast<unsigned long>(nowUs), reinterpret_cast<unsigned long>(this),
            static_cast<unsigned long>(sizeof(DisplayManager)),
            reinterpret_cast<unsigned long>(&DisplayManagerInternal::liveDisplayEventBuffer),
            neighbourGapBytes, static_cast<unsigned long>(dmaTailCanary_));
    }
    const bool canaryIntact = dmaTailCanary_ == kDmaTailCanary;
    const bool neighbourIntact =
        DisplayManagerInternal::liveDisplayEventBuffer.capacity() == 0 ||
        DisplayManagerInternal::liveDisplayEventBuffer.data() != nullptr;
    if ((!canaryIntact || !neighbourIntact) && (nowUs - lastReportUs) > 200000u) {
        lastReportUs = nowUs;
        Serial.printf_P(
            PSTR("#DMA_STAGING_CORRUPT,%lu,canary_expected=0x%08lx,canary_actual=0x%08lx,"
                 "display_manager=0x%08lx,object_size=%lu,neighbour=0x%08lx,gap_bytes=%ld,"
                 "buffer_data=0x%08lx,buffer_size=%lu,buffer_capacity=%lu\r\n"),
            static_cast<unsigned long>(nowUs), static_cast<unsigned long>(kDmaTailCanary),
            static_cast<unsigned long>(dmaTailCanary_), reinterpret_cast<unsigned long>(this),
            static_cast<unsigned long>(sizeof(DisplayManager)),
            reinterpret_cast<unsigned long>(&DisplayManagerInternal::liveDisplayEventBuffer),
            neighbourGapBytes,
            reinterpret_cast<unsigned long>(DisplayManagerInternal::liveDisplayEventBuffer.data()),
            static_cast<unsigned long>(DisplayManagerInternal::liveDisplayEventBuffer.size()),
            static_cast<unsigned long>(
                DisplayManagerInternal::liveDisplayEventBuffer.capacity()));
    }
    // The display window's **transition receipt** (`DIAG,disp_window`) samples the display's own state on every update
    // (4 ms cadence inside the sampler), so a window that stops repainting still reports itself. It rides this
    // flash-resident per-update capture hook instead of a call site in `update()`: that call measured +16 B of ITCM,
    // which is exactly the qualification image's padding above the approved 840 B floor (`code 392,376 + padding 840`
    // -> `392,392 + 824`). Same build set (`SESSION_CAPTURE && ARDUINO`); the sampler is a no-op outside capture builds.
    observeDetailedWindowTransition();
    reportPaintBeatTransport();
}
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && defined(__IMXRT1062__)
// DEC-187 diagnostic: one row per second of **what the paint path handed to the glass** against where the
// playhead actually is. A capture cannot answer this from `#CAP` receipts alone: `DISP,3` dedupes on note
// count, `DFRAME`/`disp_turn` are ring records that a busy capture sheds, and `RING,shed` then makes their
// absence non-evidence. This row is written straight to USB (like `#DMA_XFER`), so it cannot be shed: the two
// tick fields decide the branch -- `paint` lagging `play` means the paint input is frozen, `paint == play`
// with a stale panel means the transfer path is. `phase` is the pending paint phase, `xfer` the transport's
// own busy flag, and `dma` proves frames still complete while the panel looks frozen.
/// `DMAMEM` is NOLOAD (not zero-initialised), so `lastOut` is never trusted as an initialiser: a wrapped or
/// garbage value only ever yields a clamped delta, and `lastMs == 0` means "not armed yet".
struct PaintBeatState {
    uint32_t lastMs;
    uint32_t lastOut;
    uint32_t armedMagic;
};
#if defined(__IMXRT1062__)
DMAMEM PaintBeatState gPaintBeat;
#else
PaintBeatState gPaintBeat;
#endif
constexpr uint32_t kPaintBeatArmedMagic = 0x50425431u;  // 'PBT1'

void DisplayManager::reportPaintBeatTransport() {
    static constexpr uint32_t kPaintBeatIntervalMs = 1000u;
    const uint32_t nowMs = millis();
    if (gPaintBeat.armedMagic == kPaintBeatArmedMagic &&
        nowMs - gPaintBeat.lastMs < kPaintBeatIntervalMs) return;
    const Track& selected = trackManager.getSelectedTrack();
    if (!selected.isAudiblePlaybackActive() && !selected.isRecording()) return;
    // DEC-188: the display cache's own state, so a frozen painted set is attributable in one run: `dirty=1`
    // with a stale `notes` means the owning lane never drained the marked bars (scheduling); `dirty=0` with
    // `vcache` still holding the undone rows means the rebuild ran against a stale source.
    uint32_t cacheDirty = 1u;
    uint32_t cacheNotes = 0u;
    if (paintSlot_ < Config::MAX_LOOPS_PER_TRACK) {
        const Loop& paintedLoop = selected.getLoop(paintSlot_);
        cacheDirty = paintedLoop.visualCacheDirty ? 1u : 0u;
        cacheNotes = static_cast<uint32_t>(paintedLoop.visualCache.notes.size());
    }
    const uint32_t outNow = DebugSessionCapture::midiOutNoteOnCount();
    const uint32_t outDelta = (gPaintBeat.armedMagic == kPaintBeatArmedMagic && outNow >= gPaintBeat.lastOut)
                                  ? outNow - gPaintBeat.lastOut
                                  : outNow;
    gPaintBeat.lastMs = nowMs;
    gPaintBeat.lastOut = outNow;
    gPaintBeat.armedMagic = kPaintBeatArmedMagic;
    const uint8_t state = static_cast<uint8_t>(selected.getState());
    const uint32_t playTick = clockManager.getCurrentTick();
    const uint32_t winStart = paintSlot_ < kDisplaySlotCount ? detailedWindowStartTick_[paintSlot_] : 0u;
    const uint8_t winBars = paintSlot_ < kDisplaySlotCount ? detailedWindowBars_[paintSlot_] : 0u;
    Serial.printf_P(
        PSTR("#PAINT_BEAT,%lu,state=%u,paint=%lu,play=%lu,win=%lu/%u,notes=%lu,phase=%u,xfer=%u,dma=%lu,"
             "out=%lu,last=%u,dirty=%u,vcache=%lu\r\n"),
        static_cast<unsigned long>(nowMs), static_cast<unsigned>(state),
        static_cast<unsigned long>(paintDisplayTick_), static_cast<unsigned long>(playTick),
        static_cast<unsigned long>(winStart), static_cast<unsigned>(winBars),
        static_cast<unsigned long>(resolvedPaintNotes().size()),
        static_cast<unsigned>(pendingPaintPhase_),
        _display.api.isDisplayTransferActive() ? 1u : 0u,
        static_cast<unsigned long>(_display.api.dmaCompletedFrameCount()),
        static_cast<unsigned long>(outDelta),
        static_cast<unsigned>(DebugSessionCapture::midiOutLastNote()),
        static_cast<unsigned>(cacheDirty), static_cast<unsigned long>(cacheNotes));
}
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && defined(__IMXRT1062__)
// Bounded transport census. The library logs transitions (ready / timeout / rejected /
// degraded / recovered); this is the aggregate that answers "is DMA running, or has it
// fallen back to blocking SPI?" at a point in time, at most once a second and only when
// a counter moved. Kept out of line and flash-resident for the same RAM1 reason as the
// canary reporter above.
DISP_COLD_MEM __attribute__((noinline)) void DisplayManager::reportDisplayDmaTransport() {
    static uint32_t lastReportMs = 0;
    static uint32_t lastCompleted = 0;
    static uint32_t lastBlocking = 0;
    static uint32_t lastTimeouts = 0;
    static uint32_t lastRejections = 0;
    static uint32_t lastRecoveries = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastReportMs < 1000u) {
        return;
    }
    const uint32_t completed = _display.api.dmaCompletedFrameCount();
    const uint32_t blocking = _display.api.dmaBlockingFrameCount();
    const uint32_t timeouts = _display.api.dmaTimeoutCount();
    const uint32_t rejections = _display.api.dmaRejectionCount();
    const uint32_t recoveries = _display.api.dmaRecoveryCount();
    if (completed == lastCompleted && blocking == lastBlocking && timeouts == lastTimeouts &&
        rejections == lastRejections && recoveries == lastRecoveries) {
        return;
    }
    lastReportMs = nowMs;
    lastCompleted = completed;
    lastBlocking = blocking;
    lastTimeouts = timeouts;
    lastRejections = rejections;
    lastRecoveries = recoveries;
    Serial.printf_P(
        PSTR("#DMA_XFER,%lu,init=%u,degraded=%u,active=%u,dma_frames=%lu,blocking_frames=%lu,"
             "timeouts=%lu,rejections=%lu,recoveries=%lu\r\n"),
        static_cast<unsigned long>(nowMs), _display.api.isDmaInitialized() ? 1u : 0u,
        _display.api.isDmaDegraded() ? 1u : 0u,
        _display.api.isDisplayTransferActive() ? 1u : 0u,
        static_cast<unsigned long>(completed), static_cast<unsigned long>(blocking),
        static_cast<unsigned long>(timeouts), static_cast<unsigned long>(rejections),
        static_cast<unsigned long>(recoveries));
}
#endif

void DisplayManager::update() {
#ifdef __IMXRT1062__
    // Complete a DMA frame promptly even when the next paint has not yet been admitted.
    _display.api.pollDisplayTransfer();
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    checkDmaStagingDiagnostics();
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && defined(__IMXRT1062__)
    reportDisplayDmaTransport();
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
        DisplayWindowUtils::advanceDisplayFramePhase(ran, paintTransferDeferred_);
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
