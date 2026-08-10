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

DISP_CAPTURE_MEM const DisplayNoteVec& DisplayManager::resolveDisplayNotesCommitted(const Track& track, uint8_t displaySlot, uint32_t currentTick) {
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    if (loopLength == 0 || (!loop.hasCommittedPasses() && !loop.captureActive())) {
        liveDisplayNotes.clear();
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
        liveWindowGatherValid_ = false;
        return liveDisplayNotes;
    }

    const uint8_t trackIndex = resolveTrackIndex(track);
    if (displaySlot != livePlaybackDisplaySlot_ || trackIndex != livePlaybackDisplayTrack_) {
        // Slot or track context changed — drop cached notes (never paint another loop's notes).
        liveDisplayNotes.clear();
        liveDisplayEventBuffer.clear();
        liveMergePlaybackRevision_ = UINT32_MAX;
        liveMergeCaptureRevision_ = 0;
        liveWindowGatherValid_ = false;
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
    }

    if (editManager.isNoteEditActive()) {
        // Active note-edit session is always for the focused display slot; session store owns notes.
        invalidateLiveDisplayCache();
        const uint32_t editLoopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
        if (editLoopLength > 0) {
            liveDisplayNotes = editManager.projectedNoteEditDisplayNotes(track);
        }
        return liveDisplayNotes;
    }

    Loop& mutLoop = const_cast<Loop&>(loop);
    const bool deferVisualRebuild =
        (track.isPlaying() || track.isStoppedRecording()) && !track.isOverdubbing() &&
        !(track.isRecording() && !track.isPlaying());
    const bool avoidFullVisualRebuild = shouldAvoidFullVisualRebuild(loop, loopLength);
    const bool deferHeavyDisplayRebuild = shouldDeferHeavyDisplayRebuild();
    // Architectural gate: never ensureVisualCacheBuilt when avoidFullVisualRebuild is true
    // (long loops). Transient boot/undo/save pressure uses cached notes or windowed path.
    if (!deferVisualRebuild && !avoidFullVisualRebuild && !deferHeavyDisplayRebuild) {
        mutLoop.ensureVisualCacheBuilt();
    } else if (!deferVisualRebuild && !avoidFullVisualRebuild && deferHeavyDisplayRebuild &&
               loop.visualCacheDirty && loop.visualCache.notes.empty()) {
        // Short loop + first paint under restore/undo pressure: still build once so frame1
        // is not the only good frame (session_20260717_234050).
        mutLoop.ensureVisualCacheBuilt();
    }
    const bool needsLiveMergeForDisplay =
        loop.captureActive() || track.isRecording() || track.isOverdubbing();
    if (!needsLiveMergeForDisplay) {
        // Stale-while-revalidate: when PLAYING defers rebuild, show last visual cache until idle
        // maintenance refreshes it — never return stale notes after invalidate (dirty cache).
        if (!avoidFullVisualRebuild && (!loop.visualCacheDirty || deferVisualRebuild) &&
            !loop.visualCache.notes.empty()) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
            livePlaybackDisplaySlot_ = displaySlot;
            livePlaybackDisplayTrack_ = trackIndex;
            return liveDisplayNotes;
        }
        // Prefer last live frame while deferred restore/undo is still draining.
        if (deferHeavyDisplayRebuild && !liveDisplayNotes.empty() &&
            displaySlot == livePlaybackDisplaySlot_ && trackIndex == livePlaybackDisplayTrack_) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            return liveDisplayNotes;
        }
        // Windowed reconstruction for long loops / deferred full rebuild.
        // Use the same paint window as drawPianoRoll (detailedWindowStartTick_), not a
        // separate centered playhead window — mismatch blanks the roll until a slot switch.
        if (avoidFullVisualRebuild ||
            loopLength > DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR) {
            uint32_t windowStart = 0;
            uint32_t windowLength = 0;
            uint8_t windowBars = 0;
            if (syncDetailedPaintWindow(track, displaySlot, currentTick, loopLength, windowStart,
                                        windowLength, windowBars)) {
                return resolveWindowedDisplayNotes(track, mutLoop, loop, displaySlot, loopLength,
                                                   windowStart, windowLength);
            }
        }
        if (!liveDisplayNotes.empty() && displaySlot == livePlaybackDisplaySlot_ &&
            trackIndex == livePlaybackDisplayTrack_) {
            return liveDisplayNotes;
        }
    }

    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (loopLength > boundedThreshold || avoidFullVisualRebuild) {
        uint32_t windowStart = 0;
        uint32_t windowLength = 0;
        uint8_t windowBars = 0;
        if (syncDetailedPaintWindow(track, displaySlot, currentTick, loopLength, windowStart,
                                    windowLength, windowBars)) {
            return resolveWindowedDisplayNotes(track, mutLoop, loop, displaySlot, loopLength,
                                               windowStart, windowLength);
        }
    }

    if (loop.visualCacheDirty || liveDisplayEventBuffer.empty() ||
        liveMergePlaybackRevision_ != loop.playbackRevision ||
        liveMergeCaptureRevision_ != loop.captureDisplayRevision) {
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayFullRebuild);
        liveDisplayEventBuffer.clear();
        if (loop.captureActive()) {
            mutLoop.gatherCommittedEventsWithCapture(liveDisplayEventBuffer);
        } else {
            mutLoop.gatherCommittedEvents(liveDisplayEventBuffer);
        }
        liveMergePlaybackRevision_ = loop.playbackRevision;
        liveMergeCaptureRevision_ = loop.captureDisplayRevision;
        DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    }

    // Prefer visualCache when fresh — after invalidation fall through to merged events.
    if (!loop.visualCacheDirty && !loop.visualCache.notes.empty()) {
        liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
    } else if (!liveDisplayEventBuffer.empty()) {
        const NoteUtils::DisplayNoteVec reconstructed =
            NoteUtils::reconstructDisplayNotes(liveDisplayEventBuffer, loopLength, false);
        liveDisplayNotes.assign(reconstructed.begin(), reconstructed.end());
    } else {
        liveDisplayNotes.clear();
    }

    if (!liveDisplayEventBuffer.empty()) {
        const std::vector<NoteUtils::OpenNoteOn> openNotes =
            NoteUtils::findOpenNoteOns(liveDisplayEventBuffer, loopLength);
        if (!openNotes.empty()) {
            const uint32_t playheadCloseTick =
                resolvePlayheadInLoop(track, displaySlot, currentTick);
            applyLiveOpenTails(openNotes, liveDisplayEventBuffer, loopLength, playheadCloseTick,
                               liveDisplayNotes, false);
        }
    }
    livePlaybackDisplaySlot_ = displaySlot;
    livePlaybackDisplayTrack_ = trackIndex;
    return liveDisplayNotes;
}
