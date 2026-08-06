//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Display cache invalidation and workspace refresh hooks (Phase 6).

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "ClockManager.h"
#include "Globals.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/SlotFocusDisplay.h"

using namespace DisplayManagerInternal;

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
