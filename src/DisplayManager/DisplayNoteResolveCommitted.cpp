//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "LoopContentResolution.h"
#include "TrackStateMachine.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/SlotFocusDisplay.h"
#include <Arduino.h>
#include <algorithm>
#include <cstdio>

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#define DISP_CAPTURE_MEM FLASHMEM
#else
#define DISP_COLD_MEM
#define DISP_CAPTURE_MEM
#endif

#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define DISP_PAINT_DIAG_FMT(literal) PSTR(literal)
#else
#define DISP_PAINT_DIAG_FMT(literal) literal
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
        // `000013`: other tracks STOPPED first and cleared compose 141. Adopt onto
        // the outgoing loop before dropping the vector.
        if (livePlaybackDisplayTrack_ != 255 && livePlaybackDisplaySlot_ != 255 &&
            livePlaybackDisplayTrack_ < trackManager.getTrackCount() &&
            livePlaybackDisplaySlot_ < Config::MAX_LOOPS_PER_TRACK &&
            !liveDisplayNotes.empty()) {
            Loop& previousLoop = trackManager.getTrack(livePlaybackDisplayTrack_)
                                     .getLoop(livePlaybackDisplaySlot_);
            adoptComposedDisplayNotesIfAuthoritative(previousLoop);
        }
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
        const bool preservedHandoffAuthority =
            !liveDisplayNotes.empty() && displaySlot == livePlaybackDisplaySlot_ &&
            trackIndex == livePlaybackDisplayTrack_;
        const bool incrementalCommittedDisplay =
            DisplayWindowUtils::preferIncrementalCommittedDisplay(deferVisualRebuild,
                                                                  track.isStopped());
        if (loop.hasCommittedPasses()) {
            if (const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
                snapshot && loop.publishedSnapshotIsCurrent()) {
                DIAG_COUNTER_INC(DisplayIncrementalUpdate);
                LoopContentResolution::resolveDisplayNotes(*snapshot, liveDisplayNotes);
                liveMergePlaybackRevision_ = loop.playbackRevision;
                liveMergeCaptureRevision_ = loop.captureDisplayRevision;
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                livePlaybackDisplaySlot_ = displaySlot;
                livePlaybackDisplayTrack_ = trackIndex;
                liveWindowGatherValid_ = false;
                return liveDisplayNotes;
            }
            if (DisplayWindowUtils::shouldHoldOverdubStopHandoffUntilCurrentSnapshot(
                    incrementalCommittedDisplay, preservedHandoffAuthority, loop.visualCacheDirty,
                    loop.publishedSnapshotIsCurrent(), loop.pendingSnapshotSessionExitQueued())) {
                DIAG_COUNTER_INC(DisplayIncrementalUpdate);
                livePlaybackDisplaySlot_ = displaySlot;
                livePlaybackDisplayTrack_ = trackIndex;
                liveMergePlaybackRevision_ = loop.playbackRevision;
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                if (!liveWindowGatherValid_) {
                    liveWindowGatherValid_ = true;
                    liveWindowGatherStart_ = 0;
                    liveWindowGatherLength_ = loopLength;
                    liveWindowGatherLoopLength_ = loopLength;
                }
                return liveDisplayNotes;
            }
            if (track.isStopped()) {
                DIAG_COUNTER_INC(DisplayIncrementalUpdate);
                LoopContentResolution::resolveDisplayNotes(loop.passes, loopLength,
                                                           liveDisplayNotes);
                liveMergePlaybackRevision_ = loop.playbackRevision;
                liveMergeCaptureRevision_ = loop.captureDisplayRevision;
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                livePlaybackDisplaySlot_ = displaySlot;
                livePlaybackDisplayTrack_ = trackIndex;
                liveWindowGatherValid_ = false;
                return liveDisplayNotes;
            }
        }
        const bool visualCacheAuthoritative =
            DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(
                loop.visualCacheDirty, !loop.visualCache.notes.empty());
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
        // RC5a: dirty visualCache does not yet include the just-committed capture suffix.
        // Keep the composed handoff. Do not snapshot-gather on ODUB,stop,display
        // (`003459`: display 97-154 ms). Do not in-place filter — draw already
        // window-filters a copy; shrinking the store dropped last bars (`001907`).
        if (DisplayWindowUtils::preferPreservedOverdubStopHandoff(
                incrementalCommittedDisplay, preservedHandoffAuthority, loop.visualCacheDirty)) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            livePlaybackDisplaySlot_ = displaySlot;
            livePlaybackDisplayTrack_ = trackIndex;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            if (!liveWindowGatherValid_) {
                liveWindowGatherValid_ = true;
                liveWindowGatherStart_ = 0;
                liveWindowGatherLength_ = loopLength;
                liveWindowGatherLoopLength_ = loopLength;
            }
            return liveDisplayNotes;
        }
        if (DisplayWindowUtils::shouldHoldDirtyVisualCacheUntilIdle(
                loop.visualCacheDirty, !loop.visualCache.notes.empty(), incrementalCommittedDisplay,
                preservedHandoffAuthority)) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
            liveMergePlaybackRevision_ = loop.playbackRevision;
            livePlaybackDisplaySlot_ = displaySlot;
            livePlaybackDisplayTrack_ = trackIndex;
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            return liveDisplayNotes;
        }
        // Window filter is allowed from a non-empty visualCache even while globally dirty, so
        // auto-follow can keep the next bar ready. When the window covers the loop the
        // filter keeps the same identity set as a full-cache assign. Do not snapshot-gather
        // here when a composed live frame exists (`011829` wrap-then-stop).
        if (incrementalCommittedDisplay && !loop.visualCache.notes.empty() &&
            liveDisplayNotes.empty()) {
            uint32_t windowStart = 0;
            uint32_t windowLength = 0;
            uint8_t windowBars = 0;
            if (syncDetailedPaintWindow(track, displaySlot, currentTick, loopLength, windowStart,
                                        windowLength, windowBars)) {
                return resolveWindowedDisplayNotes(track, mutLoop, loop, displaySlot, loopLength,
                                                   windowStart, windowLength);
            }
            if (visualCacheAuthoritative) {
                DIAG_COUNTER_INC(DisplayIncrementalUpdate);
                liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
                liveMergePlaybackRevision_ = loop.playbackRevision;
                liveWindowGatherValid_ = false;
                livePlaybackDisplaySlot_ = displaySlot;
                livePlaybackDisplayTrack_ = trackIndex;
                return liveDisplayNotes;
            }
        }
        // RC5a/RC5b/RC5f: revision-matched preserved frame stays authority when the
        // visualCache cannot supply a window filter (empty notes).
        if (incrementalCommittedDisplay && preservedHandoffAuthority &&
            loop.visualCache.notes.empty()) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            return liveDisplayNotes;
        }
        // Detailed window before any preserved live-frame fallback. Deferred save must
        // not keep capture-suffix / playhead-tail rows as authority
        // (session_20260811_013056: 1872 visualCache + 1011 capturePreview).
        // When STOPPED/PLAYING already had incremental authority above, dirty-cache gather here
        // is recovery only (no preserved frame / cold context).
        {
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

    {
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
        Loop::SnapshotWindowGatherPath snapshotPath = Loop::SnapshotWindowGatherPath::None;
        bool usedSnapshot =
            loop.captureActive()
                ? mutLoop.tryGatherPublishedSnapshotEventsInWindowWithCapture(
                      liveDisplayEventBuffer, 0, loopLength, &snapshotPath)
                : mutLoop.tryGatherPublishedSnapshotEventsInWindow(liveDisplayEventBuffer, 0,
                                                                   loopLength, &snapshotPath);
        if (snapshotPath == Loop::SnapshotWindowGatherPath::Overlay) {
            liveDisplayEventBuffer.clear();
            usedSnapshot = false;
        }
        const bool holdUntilCurrentSnapshot = loop.pendingSnapshotSessionExitQueued() &&
                                              !loop.publishedSnapshotIsCurrent();
        if (!usedSnapshot && !holdUntilCurrentSnapshot) {
            if (loop.captureActive()) {
                mutLoop.gatherCommittedEventsWithCapture(liveDisplayEventBuffer);
            } else {
                mutLoop.gatherCommittedEvents(liveDisplayEventBuffer);
            }
        }
#if defined(SESSION_CAPTURE)
        {
            const char* source = usedSnapshot ? "Snapshot" : "Fallback";
            const char* path =
                snapshotPath == Loop::SnapshotWindowGatherPath::Overlay ? "OverlayRejected"
                                                                        : source;
            char line[192];
            snprintf(
                line, sizeof(line),
                DISP_PAINT_DIAG_FMT(
                    "#CAP,%lu,DISP,paint_src,%s,src,%s,root_rev,%lu,playback_rev,%lu"),
                static_cast<unsigned long>(micros()), path, source,
                static_cast<unsigned long>(loop.publishedSnapshotRevision()),
                static_cast<unsigned long>(loop.playbackRevision));
            DebugSessionCapture::appendCaptureTextLine(line);
        }
#endif
        liveMergePlaybackRevision_ = loop.playbackRevision;
        liveMergeCaptureRevision_ = loop.captureDisplayRevision;
        DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    }

    // Prefer visualCache when fresh — after invalidation fall through to
    // per-pass paired notes. Do not reconstruct the merged event stream
    // (`211440` D# Offs).
    if (!loop.visualCacheDirty && !loop.visualCache.notes.empty() &&
        !(loop.pendingSnapshotSessionExitQueued() && !loop.publishedSnapshotIsCurrent())) {
        liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
    } else if (loop.captureActive() && !liveDisplayEventBuffer.empty()) {
        const NoteUtils::DisplayNoteVec reconstructed =
            NoteUtils::reconstructDisplayNotes(liveDisplayEventBuffer, loopLength, false, false);
        liveDisplayNotes.assign(reconstructed.begin(), reconstructed.end());
        mutLoop.appendOverdubPassDisplayNotes(liveDisplayNotes);
    } else if (loop.hasCommittedPasses() && !(loop.pendingSnapshotSessionExitQueued() &&
                                             !loop.publishedSnapshotIsCurrent())) {
        LoopContentResolution::resolveDisplayNotes(loop.passes, loopLength, liveDisplayNotes);
        mutLoop.appendOverdubPassDisplayNotes(liveDisplayNotes);
    } else if (!(loop.pendingSnapshotSessionExitQueued() && !loop.publishedSnapshotIsCurrent())) {
        liveDisplayNotes.clear();
    }

    if (loop.captureActive() && !liveDisplayEventBuffer.empty()) {
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
