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
#include <algorithm>

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
        // STOPPED track/slot switch must not wipe visualCache.notes (161804 / 165107).
        // Boot already skipped this wipe. PLAYING still clears so a live preview
        // cannot keep another slot's note list as authority.
        // Queued dest is not getActiveLoop(); idle visual-cache slices never
        // service it. Leave that cache in place (222539).
        const bool queuedDestination =
            playbackActive && (slot == newSlot) && (newSlot != activeSlot);
        if (queuedDestination) {
            return;
        }
        if (playbackActive && (slot == newSlot || slot == activeSlot)) {
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
        // Stopped: derived/playback caches only. Loop::invalidateCaches does not
        // clear visualCache.notes; slot display above already marked stale.
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
    if (track.isJamming() || displaySlot >= kDisplaySlotCount) {
        return;
    }
    const uint8_t requestedBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                                    DisplayWindowUtils::kMaxDetailedWindowBars);
    uint32_t windowStart = 0;
    uint32_t windowLength = 0;
    uint8_t windowBars = 0;
    if (!DisplayWindowUtils::resolveDetailedPaintWindow(
            loopLength, requestedBars, detailedWindowStartTick_[displaySlot],
            storagePhaseTickInLoop, true, windowStart, windowLength, windowBars)) {
        return;
    }
    if (DisplayWindowUtils::windowCoversLoop(windowLength, loopLength)) {
        detailedWindowStartTick_[displaySlot] = 0;
        return;
    }
    detailedWindowStartTick_[displaySlot] = windowStart;
}

void DisplayManager::syncLiveDisplayComposeCounts() {
    DisplayWindowUtils::syncLiveDisplayComposeCounts(
        liveDisplayNotes.size(), liveDisplayCacheCommittedNoteCount_,
        liveDisplayCacheCaptureNoteCount_, liveDisplayCacheBaseNoteCount_);
}

void DisplayManager::resizeLiveDisplayNotesNoGrow(size_t requestedCount) {
    liveDisplayNotes.resize(DisplayWindowUtils::clampDisplayNoteResizeCount(
        liveDisplayNotes.size(), requestedCount));
}

void DisplayManager::adoptComposedDisplayNotesIfAuthoritative(Loop& loop) {
    if (liveDisplayNotes.empty()) {
        return;
    }
    const size_t paintCommittedEnd =
        liveDisplayPaintCommittedEnd_ != static_cast<size_t>(-1)
            ? liveDisplayPaintCommittedEnd_
            : liveDisplayCacheCommittedNoteCount_;
    liveDisplayPaintCommittedEnd_ = static_cast<size_t>(-1);
    uint32_t loopBars = 0;
    if (Config::TICKS_PER_BAR > 0 && loop.loopLengthTicks > 0) {
        loopBars = (loop.loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
    }
    const size_t originalSize = liveDisplayNotes.size();
    const size_t suffixCount =
        paintCommittedEnd < originalSize ? (originalSize - paintCommittedEnd) : 0;
    loop.applyPendingNoteChangesToDisplayNotes(liveDisplayNotes, paintCommittedEnd);
    const size_t prefixEndAfterHide = liveDisplayNotes.size() >= suffixCount
                                          ? (liveDisplayNotes.size() - suffixCount)
                                          : liveDisplayNotes.size();
    DisplayWindowUtils::keepShortestEndAtPitchStartOnCommittedPrefix(liveDisplayNotes,
                                                                     prefixEndAfterHide);
    syncLiveDisplayComposeCounts();
    loop.applyPendingNoteChangesToDisplayNotes(loop.visualCache.notes, loop.visualCache.notes.size());
    DisplayWindowUtils::keepShortestEndAtPitchStart(loop.visualCache.notes);
    if (!DisplayWindowUtils::shouldAdoptComposedNotesOverVisualCache(
            liveDisplayNotes.size(), loop.visualCache.notes.size(), loopBars)) {
        return;
    }
    loop.adoptComposedDisplayNotesFromViewport(liveDisplayNotes);
}

void DisplayManager::refreshViewportAfterOverdubStop(Track& track, uint8_t displaySlot,
                                                     uint32_t storagePhaseTickInLoop) {
    // RC5a: overdub stop must not use record-stop clamp. Composed frame is
    // committed display + capturePreview; stripping the suffix causes the record-only flash.
    // Drop temporary playhead-tail rows only (in-place currentTick ends never land on
    // liveDisplayNotes — paint-only tails). Then promote the capture suffix into the
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
        resizeLiveDisplayNotesNoGrow(preserveCount);
    }
    liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
    liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
    liveMergePlaybackRevision_ = playbackRevision;
    liveMergeCaptureRevision_ = captureDisplayRevision;
    const uint32_t loopLength =
        resolveDisplayLoopLength(track, displaySlot, clockManager.getCurrentTick());
    // Do not rebuildVisualCacheFromPasses / ensureVisualCacheBuilt here — overdub
    // stop is MIDI-sensitive. Idle rebuildVisualCacheIdleSlice fills dirty bars.
    // Keep the loop-wide list when present (020910/021959 stale paint was
    // adopt_partial, not this keep). Empty cache may adopt the composed frame.
    if (!liveDisplayNotes.empty()) {
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
        // 6B: a larger long-loop cache stays. Short-loop compose that is larger
        // than cache is adopted (`000013` 141>95). Empty cache still adopts (RC5c).
        adoptComposedDisplayNotesIfAuthoritative(loop);
        liveWindowVisualCacheRevision_ = loop.visualCache.revision;
    } else if (!loop.visualCache.notes.empty()) {
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
        liveWindowVisualCacheRevision_ = loop.visualCache.revision;
    } else {
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
    }
    if (track.isJamming() || displaySlot >= kDisplaySlotCount) {
        return;
    }
    const uint8_t requestedBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                                    DisplayWindowUtils::kMaxDetailedWindowBars);
    uint32_t windowStart = 0;
    uint32_t windowLength = 0;
    uint8_t windowBars = 0;
    if (!DisplayWindowUtils::resolveDetailedPaintWindow(
            loopLength, requestedBars, detailedWindowStartTick_[displaySlot],
            storagePhaseTickInLoop, true, windowStart, windowLength, windowBars)) {
        return;
    }
    if (DisplayWindowUtils::windowCoversLoop(windowLength, loopLength)) {
        detailedWindowStartTick_[displaySlot] = 0;
        return;
    }
    detailedWindowStartTick_[displaySlot] = windowStart;
}

void DisplayManager::invalidateLiveDisplayCache(bool preserveDisplayNotes) {
    const bool frameInProgress =
        DisplayWindowUtils::displayFramePhaseIsPending(pendingPaintPhase_);
    if (frameInProgress) {
        DisplayWindowUtils::noteDisplayFrameInvalidate(pendingPaintPhase_,
                                                       paintInvalidateCoalesced_);
        if (!preserveDisplayNotes) {
            paintDeferredClearNotes_ = true;
        }
    }
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
    if (!preserveDisplayNotes &&
        DisplayWindowUtils::shouldClearResolvedPaintNotesNow(pendingPaintPhase_)) {
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
    liveDrawSourceKey_ = {};
    liveCommittedLayerHeldForDirtyCache_ = false;
    liveOverdubSourceViewNoteCount_ = static_cast<size_t>(-1);
    liveDisplayPaintCommittedEnd_ = static_cast<size_t>(-1);
    editManager.invalidateProjectedNoteEditDisplayCache();
}

void DisplayManager::invalidateNoteEditDisplayCache() {
    editManager.invalidateProjectedNoteEditDisplayCache();
    if (DisplayWindowUtils::displayFramePhaseIsPending(pendingPaintPhase_)) {
        DisplayWindowUtils::noteDisplayFrameInvalidate(pendingPaintPhase_,
                                                       paintInvalidateCoalesced_);
        paintDeferredClearNotes_ = true;
        return;
    }
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
