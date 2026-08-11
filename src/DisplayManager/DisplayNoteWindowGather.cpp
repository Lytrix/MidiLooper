//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "TrackStateMachine.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/SlotFocusDisplay.h"
#include "VisualCache.h"
#include <Arduino.h>
#include <algorithm>

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#define DISP_CAPTURE_MEM FLASHMEM
#else
#define DISP_COLD_MEM
#define DISP_CAPTURE_MEM
#endif

using namespace DisplayManagerInternal;

bool DisplayManager::syncDetailedPaintWindow(const Track& track, uint8_t displaySlot,
                                             uint32_t currentTick, uint32_t loopLength,
                                             uint32_t& outWindowStart, uint32_t& outWindowLength,
                                             uint8_t& outWindowBars) {
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (track.isJamming() || loopLength <= boundedThreshold || displaySlot >= kDisplaySlotCount) {
        outWindowStart = 0;
        outWindowLength = loopLength;
        outWindowBars = DisplayWindowUtils::kMaxDetailedWindowBars;
        return false;
    }
    outWindowBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                      DisplayWindowUtils::kMaxDetailedWindowBars);
    outWindowLength = static_cast<uint32_t>(outWindowBars) * Config::TICKS_PER_BAR;
    if (shouldAutoFollowDetailedWindow(track, loopLength)) {
        const uint32_t playhead = resolvePlayheadInLoop(track, displaySlot, currentTick);
        detailedWindowStartTick_[displaySlot] =
            DisplayWindowUtils::resolveCenteredWindowStart(playhead, outWindowLength, loopLength);
    }
    outWindowStart = detailedWindowStartTick_[displaySlot];
    if (outWindowStart + outWindowLength > loopLength) {
        outWindowStart = loopLength > outWindowLength ? loopLength - outWindowLength : 0;
        detailedWindowStartTick_[displaySlot] = outWindowStart;
    }
    return true;
}

DISP_COLD_MEM const DisplayNoteVec& DisplayManager::resolveWindowedDisplayNotes(
    const Track& track, Loop& mutLoop, const Loop& loop, uint8_t displaySlot, uint32_t loopLength,
    uint32_t windowStart, uint32_t windowLength) {
    const uint8_t trackIndex = resolveTrackIndex(track);

    // Prefer filtering a covered visualCache over gather+reconstruct when capture is idle.
    // session_20260811_030614: long-loop PLAYING filled visualCache while every frame still
    // reconstructed the window — dual work tore the OLED after record stop.
    if (!loop.captureActive() &&
        visualCacheCoversWindow(loop.visualCacheDirty, loop.visualCache.dirtyBars, windowStart,
                                windowLength, loopLength, Config::TICKS_PER_BAR)) {
        const bool visualCacheHit =
            liveWindowGatherValid_ && displaySlot == livePlaybackDisplaySlot_ &&
            trackIndex == livePlaybackDisplayTrack_ &&
            liveMergePlaybackRevision_ == loop.playbackRevision &&
            liveMergeCaptureRevision_ == loop.captureDisplayRevision &&
            liveWindowVisualCacheRevision_ == loop.visualCache.revision &&
            liveWindowGatherLoopLength_ == loopLength &&
            liveWindowGatherStart_ == windowStart && liveWindowGatherLength_ == windowLength;
        if (visualCacheHit) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            return liveDisplayNotes;
        }
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        const DisplayNoteVec filtered = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(
            loop.visualCache.notes, windowStart, windowLength, loopLength);
        liveDisplayNotes.assign(filtered.begin(), filtered.end());
        liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
        liveMergePlaybackRevision_ = loop.playbackRevision;
        liveMergeCaptureRevision_ = loop.captureDisplayRevision;
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
        liveWindowGatherStart_ = windowStart;
        liveWindowGatherLength_ = windowLength;
        liveWindowGatherLoopLength_ = loopLength;
        liveWindowGatherValid_ = true;
        liveWindowVisualCacheRevision_ = loop.visualCache.revision;
        DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
        return liveDisplayNotes;
    }

    const bool cacheHit =
        liveWindowGatherValid_ && displaySlot == livePlaybackDisplaySlot_ &&
        trackIndex == livePlaybackDisplayTrack_ &&
        liveMergePlaybackRevision_ == loop.playbackRevision &&
        liveMergeCaptureRevision_ == loop.captureDisplayRevision &&
        liveWindowVisualCacheRevision_ == UINT32_MAX &&
        liveWindowGatherLoopLength_ == loopLength && windowLength > 0 &&
        liveWindowGatherLength_ > 0 && windowStart >= liveWindowGatherStart_ &&
        (windowStart - liveWindowGatherStart_) + windowLength <= liveWindowGatherLength_;
    if (cacheHit) {
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        return liveDisplayNotes;
    }

    const uint32_t displayBuildStartUs = micros();
    DIAG_COUNTER_INC(DisplayFullRebuild);
    const uint32_t marginTicks =
        static_cast<uint32_t>(kWindowedGatherMarginBars) * Config::TICKS_PER_BAR;
    uint32_t gatherStart = windowStart > marginTicks ? windowStart - marginTicks : 0;
    uint32_t gatherEnd = windowStart + windowLength + marginTicks;
    if (gatherEnd > loopLength) {
        gatherEnd = loopLength;
    }
    if (gatherStart > gatherEnd) {
        gatherStart = 0;
    }
    const uint32_t gatherLength = gatherEnd - gatherStart;
    rebuildDisplayNotesInWindow(mutLoop, loop, loopLength, gatherStart, gatherLength,
                                liveDisplayEventBuffer, liveDisplayNotes, true);
    liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
    liveMergePlaybackRevision_ = loop.playbackRevision;
    liveMergeCaptureRevision_ = loop.captureDisplayRevision;
    livePlaybackDisplaySlot_ = displaySlot;
    livePlaybackDisplayTrack_ = trackIndex;
    liveWindowGatherStart_ = gatherStart;
    liveWindowGatherLength_ = gatherLength;
    liveWindowGatherLoopLength_ = loopLength;
    liveWindowGatherValid_ = true;
    liveWindowVisualCacheRevision_ = UINT32_MAX;
    DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    return liveDisplayNotes;
}
