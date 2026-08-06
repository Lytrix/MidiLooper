//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Display note resolve: tick/length helpers and windowed gather (Phase 2a).

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/SlotFocusDisplay.h"
#include <Arduino.h>
#include <algorithm>

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#else
#define DISP_COLD_MEM
#endif

using namespace DisplayManagerInternal;

bool DisplayManager::isLiveRecordingDisplay(const Track& track, uint8_t displaySlot) const {
    return !track.isJamming() && (track.isRecording() || track.isOverdubbing()) &&
           track.getRecordingFocusSlot() == displaySlot;
}

uint32_t DisplayManager::resolveDisplayLoopLength(const Track& track, uint8_t displaySlot,
                                                  uint32_t currentTick) const {
    if (track.isJamming()) {
        return track.getLoopLength();
    }

    const uint32_t loopLength = track.getLoopLengthForSlot(displaySlot);
    if (loopLength > 0) {
        return loopLength;
    }

    if (!isLiveRecordingDisplay(track, displaySlot)) {
        return 0;
    }

    const Loop& loop = track.getLoop(displaySlot);
    if (currentTick <= loop.startLoopTick) {
        return 1;
    }
    return currentTick - loop.startLoopTick;
}

uint32_t DisplayManager::resolveDisplayTick(const Track& track, uint8_t displaySlot,
                                            uint32_t currentTick) const {
    if (!isLiveRecordingDisplay(track, displaySlot)) {
        return currentTick;
    }

    const Loop& loop = track.getLoop(displaySlot);
    if (currentTick <= loop.startLoopTick) {
        return 0;
    }
    return currentTick - loop.startLoopTick;
}

uint32_t DisplayManager::resolveLoopOriginTick(const Track& track, uint8_t displaySlot) const {
    if (track.isJamming()) {
        return track.getLoopStartTick();
    }
    return track.getLoopStartTickForSlot(displaySlot);
}

uint32_t DisplayManager::resolvePlayheadInLoop(const Track& track, uint8_t displaySlot,
                                               uint32_t currentTick) const {
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    if (loopLength == 0) {
        return 0;
    }

    const uint32_t displayTick = resolveDisplayTick(track, displaySlot, currentTick);
    if (isLiveRecordingDisplay(track, displaySlot)) {
        if (track.isRecording() && !track.isPlaying()) {
            // Growing capture length equals displayTick; modulo would always yield 0.
            if (loopLength == 0 || displayTick == 0) {
                return 0;
            }
            return std::min(displayTick, loopLength) - 1;
        }
        return displayTick % loopLength;
    }

    const Loop& dispLoop = track.getLoop(displaySlot);
    const uint32_t loopOrigin = resolveLoopOriginTick(track, displaySlot);
    const bool transportActive = track.isPlaying() || track.isOverdubbing();
    if (isPreviewPlayheadPending(displaySlot, track.getActiveLoopIndex(), transportActive)) {
        // Preview playhead parks at the queued loop's launch bracket (display phase 0).
        return 0;
    }
    const uint32_t tickInLoopStorage = resolvePlayheadStoragePhase(
        currentTick, track.getProjectionCycleStartTick(), loopLength, displaySlot,
        track.getActiveLoopIndex(), transportActive, displayTick, dispLoop.startLoopTick);
    return IntervalProjection::noteRelativeTick(tickInLoopStorage, loopOrigin, loopLength);
}

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
    const bool cacheHit =
        liveWindowGatherValid_ && displaySlot == livePlaybackDisplaySlot_ &&
        trackIndex == livePlaybackDisplayTrack_ &&
        liveMergePlaybackRevision_ == loop.playbackRevision &&
        liveMergeCaptureRevision_ == loop.captureDisplayRevision &&
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
                                liveDisplayEventBuffer, liveDisplayNotes);
    liveMergePlaybackRevision_ = loop.playbackRevision;
    liveMergeCaptureRevision_ = loop.captureDisplayRevision;
    livePlaybackDisplaySlot_ = displaySlot;
    livePlaybackDisplayTrack_ = trackIndex;
    liveWindowGatherStart_ = gatherStart;
    liveWindowGatherLength_ = gatherLength;
    liveWindowGatherLoopLength_ = loopLength;
    liveWindowGatherValid_ = true;
    DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    return liveDisplayNotes;
}
