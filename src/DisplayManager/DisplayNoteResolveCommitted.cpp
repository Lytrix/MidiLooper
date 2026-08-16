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
    // Do not ensureVisualCacheBuilt on this paint path. PLAYING already defers;
    // STOPPED uses incremental/handoff. Idle rebuildVisualCacheIdleSlice /
    // STOPPED idle ensure owns the rebuild. Last-resort gather below is a
    // later slice.
    const bool needsLiveMergeForDisplay =
        loop.captureActive() || track.isRecording() || track.isOverdubbing();
    if (!needsLiveMergeForDisplay) {
        const bool visualCacheAuthoritative =
            DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(
                loop.visualCacheDirty, !loop.visualCache.notes.empty());
        const bool preservedHandoffAuthority =
            !liveDisplayNotes.empty() && displaySlot == livePlaybackDisplaySlot_ &&
            trackIndex == livePlaybackDisplayTrack_ &&
            liveMergePlaybackRevision_ == loop.playbackRevision;
        // RC5f: STOPPED must use the same incremental paths as deferred PLAYING — otherwise
        // transport/play stop falls into dirty-cache window gather (174742 ~0.9s DFRAME gap).
        const bool incrementalCommittedDisplay =
            DisplayWindowUtils::preferIncrementalCommittedDisplay(deferVisualRebuild,
                                                                  track.isStopped());
        // RC5b: only a clean visualCache may authorize committed display after a revision bump.
        // Dirty/stale visualCache must not overwrite the RC5a overdub-stop composed frame.
        if (visualCacheAuthoritative && !avoidFullVisualRebuild) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
            liveMergePlaybackRevision_ = loop.playbackRevision;
            livePlaybackDisplaySlot_ = displaySlot;
            livePlaybackDisplayTrack_ = trackIndex;
            return liveDisplayNotes;
        }
        // Clean cache: prefer window filter (RC5d/RC5f) over full-vector assign every frame.
        // Dirty cache falls through to preserved handoff.
        if (incrementalCommittedDisplay && visualCacheAuthoritative) {
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
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
            liveMergePlaybackRevision_ = loop.playbackRevision;
            liveWindowGatherValid_ = false;
            livePlaybackDisplaySlot_ = displaySlot;
            livePlaybackDisplayTrack_ = trackIndex;
            return liveDisplayNotes;
        }
        // RC5a/RC5b/RC5f: revision-matched preserved frame stays authority while visualCache is dirty.
        if (incrementalCommittedDisplay && preservedHandoffAuthority) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            return liveDisplayNotes;
        }
        // Long loops: bounded committed window before any preserved live-frame fallback.
        // Deferred save must not keep capture-suffix / playhead-tail rows as authority
        // (session_20260811_013056: 1872 visualCache + 1011 capturePreview).
        // When STOPPED/PLAYING already had incremental authority above, dirty-cache gather here
        // is recovery only (no preserved frame / cold context).
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
        // Retain last live frame only when canonical cache/window data cannot be produced.
        if (deferHeavyDisplayRebuild && !liveDisplayNotes.empty() &&
            displaySlot == livePlaybackDisplaySlot_ && trackIndex == livePlaybackDisplayTrack_) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            return liveDisplayNotes;
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
        mutLoop.appendOverdubPassDisplayNotes(liveDisplayNotes);
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
