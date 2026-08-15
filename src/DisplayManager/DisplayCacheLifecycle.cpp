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
    // Capture-layer rows and playhead tails must not become post-stop display authority.
    // Keep only the committed prefix (may be empty after live record); committed resolve
    // replaces it through the bounded window path.
    const size_t committedBaseNoteCount = liveDisplayCacheCommittedNoteCount_;
    const uint8_t trackIndex = resolveTrackIndex(track);
    invalidateLiveDisplayCache(true);
    const Loop& loop = track.getLoop(displaySlot);
    if (!loop.visualCacheDirty && !loop.visualCache.notes.empty()) {
        liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
    } else {
        liveDisplayNotes.resize(DisplayWindowUtils::clampPreservedDisplayNoteCount(
            liveDisplayNotes.size(), committedBaseNoteCount));
    }
    if (!liveDisplayNotes.empty()) {
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
    } else {
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
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

void DisplayManager::refreshViewportAfterOverdubStop(Track& track, uint8_t displaySlot,
                                                     uint32_t storagePhaseTickInLoop) {
    // RC5a: overdub stop must not use record-stop clamp. Composed frame is
    // committed display + capturePreview; stripping the suffix causes the record-only flash.
    // Drop temporary playhead-tail rows only, then promote the capture suffix into the
    // committed-prefix bookkeeping so PLAYING resolve treats the frame as committed.
    // RC5c: adopt that composed DisplayNote vector into visualCache — no gather/reconstruct.
    const size_t preserveCount = DisplayWindowUtils::preservedOverdubStopDisplayNoteCount(
        liveDisplayNotes.size(), liveDisplayCacheBaseNoteCount_);
    const uint8_t trackIndex = resolveTrackIndex(track);
    Loop& loop = track.getLoop(displaySlot);
    const uint32_t playbackRevision = loop.playbackRevision;
    const uint16_t captureDisplayRevision = loop.captureDisplayRevision;
    invalidateLiveDisplayCache(true);
    if (preserveCount < liveDisplayNotes.size()) {
        liveDisplayNotes.resize(preserveCount);
    }
    liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
    liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
    liveMergePlaybackRevision_ = playbackRevision;
    liveMergeCaptureRevision_ = captureDisplayRevision;
    const uint32_t loopLength =
        resolveDisplayLoopLength(track, displaySlot, clockManager.getCurrentTick());
    if (!shouldAvoidFullVisualRebuild(loop, loopLength)) {
        loop.rebuildVisualCacheFromPasses();
        liveDisplayCacheCommittedNoteCount_ = loop.visualCache.notes.size();
        liveDisplayCacheBaseNoteCount_ = loop.visualCache.notes.size();
        liveMergePlaybackRevision_ = playbackRevision;
        liveMergeCaptureRevision_ = captureDisplayRevision;
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
        liveWindowVisualCacheRevision_ = loop.visualCache.revision;
    } else if (!loop.visualCache.notes.empty()) {
        // 6B: keep the loop-wide cache. Affected bars are already marked at commit.
        // adopt_partial would replace notes with the viewport and dirty the rest
        // (session_20260815_185931: 2403 → 496 notes, 117 bars dirty).
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
        liveWindowVisualCacheRevision_ = loop.visualCache.revision;
    } else if (!liveDisplayNotes.empty()) {
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
        // RC5c: adopt composed display notes without claiming the whole loop is built.
        // RC-E: clearing dirtyBars and visualCacheDirty here left only the 16-bar window in
        // cache and blocked idle backfill (session_20260812_162230).
        loop.adoptComposedDisplayNotesFromViewport(liveDisplayNotes);
        liveWindowVisualCacheRevision_ = loop.visualCache.revision;
    } else {
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
    }
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

void DisplayManager::invalidateLiveDisplayCache(bool preserveDisplayNotes) {
    liveDisplayCacheEventCount = static_cast<size_t>(-1);
    liveDisplayCacheCommittedNoteCount_ = 0;
    liveDisplayCacheCaptureNoteCount_ = 0;
    liveDisplayCacheCaptureChangeCount_ = 0;
    liveDisplayCacheBaseNoteCount_ = 0;
    liveDisplayCacheCaptureReplacementRevision_ = UINT32_MAX;
    liveDisplayCacheCapturePreviewRevision_ = UINT32_MAX;
    liveDisplayCacheCaptureRevision = 0;
    liveDisplayCacheLoopLength = 0;
    liveDisplayCacheSlot = 255;
    liveDisplayCacheTrackState = NUM_TRACK_STATES;
    liveDisplayCacheOpenNotes.clear();
    if (!preserveDisplayNotes) {
        liveDisplayNotes.clear();
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
    }
    liveDisplayEventBuffer.clear();
    liveMergePlaybackRevision_ = UINT32_MAX;
    liveMergeCaptureRevision_ = 0;
    liveWindowGatherStart_ = 0;
    liveWindowGatherLength_ = 0;
    liveWindowGatherLoopLength_ = 0;
    liveWindowGatherValid_ = false;
    liveDisplayCommittedFromWindowGather_ = false;
    liveWindowVisualCacheRevision_ = UINT32_MAX;
    editManager.invalidateProjectedNoteEditDisplayCache();
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
    trackManager.forceMidiLedUpdate(currentTick);
}
