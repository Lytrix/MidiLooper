//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <Arduino.h>
#include "SSD1322.h"
#include "Track.h"
#include "TrackManager.h"   // for trackManager
#include "ClockManager.h"   // for clockManager
#include "Globals.h"
#include "EditManager.h"
#include "Utils/DisplayWindowUtils.h"
#include <vector>
#include <cstdint>
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"
#include "SavedSetCatalog.h"
#include "RevisionLoadPolicy.h"
#include "SetRevisionCatalog.h"
#include "SetBrowserOverlayPolicy.h"
#include "DeferredSaveDisplayStatus.h"

struct DetailedWindowContext {
    bool active = false;
    TickInterval window;
};

// Shared struct for UI note representation
using DisplayNote = NoteUtils::DisplayNote;

struct LoadSaveWorkspaceDetailParams {
    uint32_t timestampUnix = 0;
    uint16_t setId = 0;
    uint16_t revisionId = 0;
    bool markRevisionUnsaved = false;
    SavedSetCatalog::SavedSetMetadata metrics{};
};

/**
 * @class DisplayManager
 * @brief Renders the graphical user interface for the MIDI looper.
 *
 * The DisplayManager draws all components of the looper UI onto the SSD1322 display:
 *  - Piano roll view with note bars and playhead cursor
 *  - Note selection bracket and highlight
 *  - Track status indicators and program/editor overlays
 *  - Info area displaying loop length, undo count, and transport position
 *  - Note detail area showing selected note parameters (pitch, length, velocity)
 *
 * It consumes MIDI event data, clock ticks, and state from EditManager and TrackManager,
 * and must have its update() method called regularly (e.g., at ~30 FPS) to refresh the display.
 */
class DisplayManager {
public:
    DisplayManager();
    void setup();
    /// Minimal OLED init for boot screen (before SD load).
    void beginBootOled();
    /// Mark boot setup complete; OLED stays on boot screen until hold expires in update().
    void finishBootSetup();
    void update();
    void clearDisplayBuffer();
    /// Boot branding screen during SD load; pushes frame to OLED immediately.
    void drawBootScreen();
    /// Emit #CAP DISP snapshot for HITL display verification (capture builds).
    void emitDisplayCaptureSnapshot(const Track& track, uint8_t displaySlot, uint32_t currentTick);
    void emitDisplayCaptureSnapshot(const Track& track, uint8_t displaySlot, uint32_t currentTick,
                                    const DisplayNoteVec& frameNotes);

    /// Bounded piano-roll window for long loops (shared with NOTE_EDIT fader nav inventory).
    DetailedWindowContext resolveDetailedWindow(const Track& track, uint8_t displaySlot,
                                                uint32_t currentTick) const;

    /// Scroll set list selection in load/save mode (encoder GPIO + HITL !OVERLAY_SCROLL).
    void adjustLoadSaveListSelection(int delta);

    uint8_t getLoadSaveListSelection() const { return loadSaveListSelection_; }

    enum class LoadSaveOverlayPressType : uint8_t {
        Short = 0,
        Double,
        Long,
    };

    /// GPIO encoder button in overlay: short/double/long row actions.
    void handleLoadSaveOverlayPress(LoadSaveOverlayPressType pressType);

    /// Confirm the focused overlay row (Save queues revision commit and exits overlay).
    void confirmLoadSaveFocusedRow();

#if defined(SESSION_CAPTURE)
    /// HITL: drill into revision history for one Set (overlay must be active).
    void openRevisionHistoryFromHitl(uint16_t setId);
    /// HITL: long-press back from revision history or loop pick drill-down.
    void navigateLoadSaveOverlayBackFromHitl();
#endif

    /// Force cached note rebuild after edit mutations (D2 display refresh).
    void requestNoteInfoRefresh(Track& track);

    /// Invalidate display caches after slot selection changes (no synchronous draw).
    void invalidateForSlotChange(uint8_t trackIndex, uint8_t previousSlot, uint8_t newSlot);

    /// Drop live frame caches so the next update re-resolves notes (no slot geometry side effects).
    void invalidateLiveDisplayCache(bool preserveDisplayNotes = false);

    /// Center the bounded detailed piano-roll window on the current playhead (long-loop loops only).
    void centerDetailedWindowOnPlayhead(Track& track, uint8_t displaySlot, uint32_t currentTick);

    /// After record stop: invalidate live cache and recenter viewport on post-rewind playhead.
    void refreshViewportAfterRecordStop(Track& track, uint8_t displaySlot,
                                        uint32_t storagePhaseTickInLoop);

    /// After overdub stop: preserve composed committed+capture frame (no capture clamp); recenter.
    void refreshViewportAfterOverdubStop(Track& track, uint8_t displaySlot,
                                         uint32_t storagePhaseTickInLoop);

    // Margin for piano roll, info area and note info
    static constexpr int TRACK_MARGIN = 22; 
    // Display buffer size
    static constexpr uint32_t DRAW_INTERVAL = 1000 / 30;  // 30 FPS
    
    uint32_t lastPlayedTick = 0;

    // Sticky last-played note: keep showing it after note-off instead of jumping to notes.back()
    DisplayNote lastPlayedDisplayNote = {0, 0, 0, 0};
    uint8_t lastPlayedTrackIndex = 255;  // Invalid so we don't use stale data on first run

    // Helper functions for piano roll rendering
    void drawGridLines(uint32_t lengthLoop, int pianoRollY0, int pianoRollY1, uint32_t windowStartTick);
    void drawNoteBar(const DisplayNote& e, int y, uint32_t s, uint32_t eTick, uint32_t lengthLoop, int noteBrightness);
    void drawAllNotes(const Track& track, uint8_t displaySlot, uint32_t currentTick, uint32_t lengthLoop,
                      int minPitch, int maxPitch, int pianoRollY0, int pianoRollY1,
                      bool windowRelativeTicks, uint32_t windowStartTick,
                      const DisplayNoteVec& notes);
    void drawBracket(uint32_t selectedTick, uint32_t lengthLoop, int pianoRollY1);

private:
    void maybeEmitDisplayCaptureOnChange(const Track& track, uint8_t displaySlot, uint32_t currentTick,
                                         const DisplayNoteVec& frameNotes);

    enum class SidebarMode : uint8_t {
        LOOP_EDIT,
        NOTE_EDIT,
        REC,
        OVERD,
        PLAY,
        STOP,
        EMPTY_STATE
    };

    // Midi output selection shown in the info line. Expanded later to USB1..USB8 / MID1..MID8.
    enum class MidiOutput : uint8_t {
        USB1,
        MID1
    };

    // Pulse and brightness for selected track
    static constexpr int minPulse = 4;       // 25% of 16 steps
    static constexpr int maxPulse = 10;      // 75% of  16 steps
    static constexpr int minBrightness = 8;  // 50%  16 steps
    static constexpr int maxBrightness = 15; // 90%   16 steps
    
    // Edit bracket and note highlight
    static constexpr int BRACKET_COLOR = 8;
    static constexpr int HIGHLIGHT_COLOR = 10;
    static constexpr int PLAYHEAD_COLOR = 10;

    uint32_t _prevDrawTick = 0;
    SSD1322 _display;
    bool bootScreenVisible_ = false;
    bool bootSetupComplete_ = false;
    unsigned long bootScreenHoldUntilMs_ = 0;
    // Blinker/pulse state for selected track
    float _pulsePhase = 0.0f; // 0..1
    unsigned long _lastPulseUpdate = 0;


    // Edit Note bracket and highlight
    int tickToScreenX(uint32_t tick);
    int noteToScreenY(uint8_t note);
    bool isLiveRecordingDisplay(const Track& track, uint8_t displaySlot) const;
    uint32_t resolveDisplayLoopLength(const Track& track, uint8_t displaySlot, uint32_t currentTick) const;
    uint32_t resolveDisplayTick(const Track& track, uint8_t displaySlot, uint32_t currentTick) const;
    /// Loop-start tick used to align piano-roll X (same in LOOP_EDIT and NOTE_EDIT).
    uint32_t resolveLoopOriginTick(const Track& track, uint8_t displaySlot) const;
    uint32_t resolvePlayheadInLoop(const Track& track, uint8_t displaySlot, uint32_t currentTick) const;
    const DisplayNoteVec& resolveDisplayNotes(const Track& track, uint8_t displaySlot,
                                              uint32_t currentTick);
    const DisplayNoteVec& resolveDisplayNotesLiveCapture(const Track& track, uint8_t displaySlot,
                                                         uint32_t currentTick);
    const DisplayNoteVec& resolveDisplayNotesCommitted(const Track& track, uint8_t displaySlot,
                                                         uint32_t currentTick);
    /// Match `drawPianoRoll` detailed-window geometry (auto-follow + clamp).
    bool syncDetailedPaintWindow(const Track& track, uint8_t displaySlot, uint32_t currentTick,
                                 uint32_t loopLength, uint32_t& outWindowStart,
                                 uint32_t& outWindowLength, uint8_t& outWindowBars);
    /// Windowed committed-pass reconstruction with gather-margin reuse across frames.
    const DisplayNoteVec& resolveWindowedDisplayNotes(const Track& track, Loop& mutLoop,
                                                     const Loop& loop, uint8_t displaySlot,
                                                     uint32_t loopLength, uint32_t windowStart,
                                                     uint32_t windowLength);
    void invalidateNoteEditDisplayCache();
    DisplayNoteVec liveDisplayNotes;
    std::vector<NoteUtils::OpenNoteOn> liveDisplayCacheOpenNotes;
    size_t liveDisplayCacheEventCount = static_cast<size_t>(-1);
    /// Committed-layer note count in `liveDisplayNotes` before capturePreview overlay (overdub).
    size_t liveDisplayCacheCommittedNoteCount_ = 0;
    /// Stable capture-preview mirror before temporary per-frame tail/head segments.
    size_t liveDisplayCacheCaptureNoteCount_ = 0;
    size_t liveDisplayCacheCaptureChangeCount_ = 0;
    size_t liveDisplayCacheBaseNoteCount_ = 0;
    uint32_t liveDisplayCacheCaptureReplacementRevision_ = UINT32_MAX;
    uint32_t liveDisplayCacheCapturePreviewRevision_ = UINT32_MAX;
    uint16_t liveDisplayCacheCaptureRevision = 0;
    uint32_t liveDisplayCacheLoopLength = 0;
    uint8_t liveDisplayCacheSlot = 255;
    TrackState liveDisplayCacheTrackState = NUM_TRACK_STATES;
    uint32_t liveMergePlaybackRevision_ = UINT32_MAX;
    uint16_t liveMergeCaptureRevision_ = 0;
    /// Slot index that `liveDisplayNotes` / `liveDisplayEventBuffer` were built for (playback path).
    uint8_t livePlaybackDisplaySlot_ = 255;
    /// Track index paired with `livePlaybackDisplaySlot_` (split-focus / track switch safety).
    uint8_t livePlaybackDisplayTrack_ = 255;
    /// Tick range last gathered for windowed reconstruction (may include margin beyond paint window).
    uint32_t liveWindowGatherStart_ = 0;
    uint32_t liveWindowGatherLength_ = 0;
    uint32_t liveWindowGatherLoopLength_ = 0;
    bool liveWindowGatherValid_ = false;
    /// Overdub committed layer came from window gather (not full visualCache).
    bool liveDisplayCommittedFromWindowGather_ = false;
    /// When window paint was filtered from `visualCache`, matches `visualCache.revision`.
    uint32_t liveWindowVisualCacheRevision_ = UINT32_MAX;

    /// RC-F follow-up: committed layer is being held because the visual cache is dirty. Cache
    /// recovery belongs to idle work, so the layer is rebuilt once the cache goes clean again.
    bool liveCommittedLayerHeldForDirtyCache_ = false;
    /// RC-H: composed overdub-stop frame is authority until the track leaves STOPPED.
    bool liveOverdubStopHandoffActive_ = false;

    /// RC-G: bounded overview density for capture states with no usable `visualCache`.
    /// One byte per loop bar; bit N set when a preview note in that bar falls in pitch band N
    /// (band = note / 16, so the 8 bands map onto the 8 overview strip rows). Built by appending
    /// only preview notes not yet processed, so cost per frame is O(new notes), never O(loop).
    VisualBarVec overviewCaptureBandMask_;
    size_t overviewCaptureProcessedNotes_ = 0;
    uint32_t overviewCaptureReplacementRevision_ = UINT32_MAX;
    uint8_t overviewCaptureSlot_ = 255;
    uint8_t overviewCaptureTrack_ = 255;

    static constexpr uint8_t kDisplaySlotCount = Config::MAX_LOOPS_PER_TRACK;
    uint32_t detailedWindowStartTick_[kDisplaySlotCount] = {};
    uint8_t detailedWindowBars_[kDisplaySlotCount] = {16, 16, 16, 16, 16, 16, 16, 16};
    uint32_t autoSaveBeforeLoadToastExpiresAtMs_ = 0;
    char autoSaveBeforeLoadToastText_[24] = {};
    static constexpr size_t kLoadSaveListCapacity = 16;
    SetRevisionCatalog::SetBrowserListEntry loadSaveListEntries_[kLoadSaveListCapacity] = {};
    size_t loadSaveListCount_ = 0;
    SetRevisionCatalog::RevisionBrowserListEntry loadSaveRevisionListEntries_[kLoadSaveListCapacity] =
        {};
    size_t loadSaveRevisionListCount_ = 0;
    uint16_t loadSaveRevisionListSetId_ = 0;
    uint8_t loadSaveListSelection_ = 0;
    uint8_t loadSaveListScrollOffset_ = 0;
    bool loadSaveListCacheValid_ = false;
    bool loadSaveRevisionListCacheValid_ = false;
    bool loadSaveModeWasActive_ = false;
    bool workspaceDisplayRefreshPending_ = false;

    void applyWorkspaceDisplayRefreshPending(uint32_t currentTick);

    struct LoadSaveDetailCacheKey {
        SetBrowserOverlayPolicy::Mode mode = SetBrowserOverlayPolicy::Mode::Root;
        uint8_t listSelection = 0;
        uint16_t drilledSetId = 0;
        char setFolderName[16] = {};
    };
    LoadSaveDetailCacheKey loadSaveDetailCacheKey_{};
    LoadSaveWorkspaceDetailParams loadSaveDetailCache_{};
    bool loadSaveDetailCacheValid_ = false;
    bool loadSaveDetailCacheOk_ = false;

    static constexpr float PULSE_SPEED = 1.0f; // Pulses per second (slowed by 40%)
    // Track status rendering
    void drawTrackStatus(uint8_t selectedTrack, uint32_t currentMillis);
    // Piano roll rendering
    void drawPianoRoll(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot, const DisplayNoteVec& notes);
    void drawOverviewStrip(uint32_t fullLoopLength, uint32_t loopOriginTick, uint32_t windowStart,
                           uint32_t windowLength, uint32_t playheadTick, int minPitch, int maxPitch,
                           const DisplayNoteVec& notes, int y0, int y1,
                           const VisualBarVec* barBandMask = nullptr);
    /// RC-G: fold newly appended capture-preview notes into `overviewCaptureBandMask_`.
    /// Returns the mask when it can stand in for a full-loop note scan, else nullptr.
    const VisualBarVec* updateOverviewCaptureDensity(const Track& track, const Loop& loop,
                                                     uint8_t displaySlot, uint32_t loopLength);
    bool shouldAutoFollowDetailedWindow(const Track& track, uint32_t loopLength) const;
    // Info area rendering
    void drawInfoArea(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot, uint32_t nowMs);
    void refreshLoadSaveListCache();
    void refreshLoadSaveRevisionHistoryCache(uint16_t setId, bool forceCatalogRead = false);
    void ensureLoadSaveRevisionListCache(uint16_t setId, bool forceCatalogRead = false);
    void invalidateLoadSaveRevisionListCache();
    uint16_t resolveFocusedRootSetId() const;
    uint16_t resolveFocusedRevisionId() const;
    void adjustLoadSaveListSelectionInDrillMode(int delta);
    void invalidateLoadSaveDetailCache();
    bool resolveLoadSaveWorkspaceDetail(LoadSaveWorkspaceDetailParams& out);
    void refreshAutoSaveBeforeLoadToast(uint32_t nowMs);
    void drawLoadSaveView(uint32_t nowMs);
    void drawLoadSaveDirtyPromptView(uint32_t nowMs);
    void drawLoadSaveMinimalLoadingView(uint32_t nowMs);
    void drawLoadSaveRevisionHistoryView(uint32_t nowMs, uint16_t setId);
    void drawLoadSaveLoopPickView(uint16_t setId);
    void drawLoadSaveWorkspaceDetail(int detailX, const LoadSaveWorkspaceDetailParams& params);
    void drawLoadSaveTrackFilledBar(int x, int y, int barWidth, int barHeight, uint8_t filledSlots,
                                    uint8_t maxSlots, uint8_t brightness);
    void drawLoadSaveDetailDateColumn(uint32_t timestampUnix);
    void drawLoadSaveDetailMetricAtColon(int colonX, int y, const char* label, const char* value,
                                         int labelCharCount, bool showUnsavedMarker = false);
    void drawLoadSaveDetailMetricLeft(int detailX, int y, const char* label, const char* value);
    void drawLoadSaveDetailMetricRight(int y, const char* label, const char* value,
                                       int labelCharCount, bool showUnsavedMarker = false);
    void drawAutoSaveBeforeLoadToast(int detailX, uint32_t nowMs);
    void drawSidebar(Track& selectedTrack, uint8_t displaySlot);
    void drawSaveStatusIndicator(uint32_t nowMs, int textRight);
    void drawPersistenceStatusDots(int startX, int dotY, const DeferredSaveDisplayStatus& status);
    void drawLoadSaveRowLoadStatusDots(int labelLeftX, int labelCharCount, int rowY, uint32_t nowMs,
                                       uint16_t setId, uint16_t revisionId);
    SidebarMode resolveSidebarMode(const Track& selectedTrack, uint8_t displaySlot) const;
    const char* sidebarModeLabel(SidebarMode mode) const;
    MidiOutput resolveMidiOutput() const;
    const char* midiOutputLabel(MidiOutput out) const;
    // Note info rendering
    void drawNoteInfo(uint32_t currentTick, Track& selectedTrack, uint8_t displaySlot, const DisplayNoteVec& notes);
    void drawInfoField(const char* label, const char* value, int x, int y, bool highlight, uint8_t defaultBrightness);
}; extern DisplayManager displayManager;