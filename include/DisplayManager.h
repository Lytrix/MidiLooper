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
    void update();
    void clearDisplayBuffer();
    /// Full-screen boot status (splash / SD load); pushes frame to OLED immediately.
    void drawBootStatusMessage(const char* text);
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

    /// Center the bounded detailed piano-roll window on the current playhead (long-loop loops only).
    void centerDetailedWindowOnPlayhead(Track& track, uint8_t displaySlot, uint32_t currentTick);

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
    void invalidateLiveDisplayCache();
    void invalidateNoteEditDisplayCache();
    DisplayNoteVec liveDisplayNotes;
    std::vector<NoteUtils::OpenNoteOn> liveDisplayCacheOpenNotes;
    size_t liveDisplayCacheEventCount = static_cast<size_t>(-1);
    uint16_t liveDisplayCacheCaptureRevision = 0;
    uint32_t liveDisplayCacheLoopLength = 0;
    uint8_t liveDisplayCacheSlot = 255;
    TrackState liveDisplayCacheTrackState = NUM_TRACK_STATES;
    uint8_t noteEditDisplayCacheSlot_ = 255;
    uint32_t noteEditDisplayCachePreviewRevision_ = UINT32_MAX;
    uint32_t noteEditDisplayCacheLoopLength_ = 0;
    size_t noteEditDisplayCacheOverlapCount_ = static_cast<size_t>(-1);

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
                           const DisplayNoteVec& notes, int y0, int y1);
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