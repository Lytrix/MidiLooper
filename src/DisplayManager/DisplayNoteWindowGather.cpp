//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "LoopSnapshot.h"
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

DISP_COLD_MEM void DisplayManagerInternal::filterNotesToFollowWindow(
    const NoteUtils::DisplayNoteVec& source, uint32_t windowStart, uint32_t windowLength,
    uint32_t loopLength, NoteUtils::DisplayNoteVec& outNotes, uint32_t& gatherStart,
    uint32_t& gatherLength) {
    const uint32_t padBars =
        kWindowedGatherMarginBars > DisplayWindowUtils::kFollowReadyLookaheadBars
            ? kWindowedGatherMarginBars
            : static_cast<uint8_t>(DisplayWindowUtils::kFollowReadyLookaheadBars);
    const uint32_t marginTicks = static_cast<uint32_t>(padBars) * Config::TICKS_PER_BAR;
    DisplayWindowUtils::expandWindowWithMargin(windowStart, windowLength, loopLength, marginTicks,
                                               gatherStart, gatherLength);
    outNotes = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(source, gatherStart,
                                                                       gatherLength, loopLength);
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

    // Prefer filtering visualCache over gather+reconstruct when capture is idle and the
    // cache covers the paint neighborhood. A nonempty dirty prefix must not win — after
    // record stop the first idle slice left 20 notes and later bars never painted
    // (session_20260825_235954).
    if (!loop.captureActive() &&
        visualCacheCoversFollowWindow(loop, windowStart, windowLength, loopLength)) {
        const bool visualCacheHit =
            liveWindowGatherValid_ && displaySlot == livePlaybackDisplaySlot_ &&
            trackIndex == livePlaybackDisplayTrack_ &&
            liveMergePlaybackRevision_ == loop.playbackRevision &&
            liveMergeCaptureRevision_ == loop.captureDisplayRevision &&
            liveWindowVisualCacheRevision_ == loop.visualCache.revision &&
            liveWindowGatherLoopLength_ == loopLength &&
            DisplayWindowUtils::paintWindowInsideGather(windowStart, windowLength,
                                                        liveWindowGatherStart_,
                                                        liveWindowGatherLength_);
        if (visualCacheHit) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            return liveDisplayNotes;
        }
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        uint32_t gatherStart = 0;
        uint32_t gatherLength = 0;
        filterNotesToFollowWindow(loop.visualCache.notes, windowStart, windowLength, loopLength,
                                  liveDisplayNotes, gatherStart, gatherLength);
        liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
        liveMergePlaybackRevision_ = loop.playbackRevision;
        liveMergeCaptureRevision_ = loop.captureDisplayRevision;
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
        liveWindowGatherStart_ = gatherStart;
        liveWindowGatherLength_ = gatherLength;
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
        liveWindowGatherLoopLength_ == loopLength &&
        DisplayWindowUtils::paintWindowInsideGather(windowStart, windowLength,
                                                    liveWindowGatherStart_,
                                                    liveWindowGatherLength_);
    if (cacheHit) {
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        return liveDisplayNotes;
    }

    const uint32_t displayBuildStartUs = micros();
    DIAG_COUNTER_INC(DisplayFullRebuild);
    const uint32_t marginTicks =
        static_cast<uint32_t>(kWindowedGatherMarginBars) * Config::TICKS_PER_BAR;
    uint32_t gatherStart = 0;
    uint32_t gatherLength = 0;
    DisplayWindowUtils::expandWindowWithMargin(windowStart, windowLength, loopLength, marginTicks,
                                               gatherStart, gatherLength);
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

namespace {

NoteUtils::DisplayNote displayNoteFromSnapshotNote(const LoopSnapshotNote& note) {
    NoteUtils::DisplayNote out{};
    out.noteId = note.noteId;
    out.note = note.pitch;
    out.velocity = note.velocity;
    out.startTick = note.startTick;
    out.endTick = note.endTick;
    return out;
}

void appendDisplayNoteUniqueById(NoteUtils::DisplayNoteVec& out, const NoteUtils::DisplayNote& note) {
    if (note.noteId != kInvalidNoteId) {
        for (const auto& existing : out) {
            if (existing.noteId == note.noteId) {
                return;
            }
        }
    }
    out.push_back(note);
}

bool previewNoteIntersectsBar(const NoteUtils::DisplayNote& note, uint32_t bar,
                              uint32_t loopLength) {
    const uint32_t barStart = bar * Config::TICKS_PER_BAR;
    return DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, barStart,
                                                    Config::TICKS_PER_BAR, loopLength);
}

bool barIsLiveCapture(const Loop& loop, uint32_t bar, uint32_t playheadBar, uint32_t loopLength) {
    if (!loop.captureActive()) {
        return false;
    }
    if (bar == playheadBar) {
        return true;
    }
    const VisualBarVec& dirtyBars = loop.capturePreview.dirtyBars;
    if (bar < dirtyBars.size() && dirtyBars[bar] != 0) {
        return true;
    }
    for (const NoteUtils::DisplayNote& note : loop.capturePreview.notes) {
        if (previewNoteIntersectsBar(note, bar, loopLength)) {
            return true;
        }
    }
    return false;
}

void appendSnapshotNotesForBar(const LoopSnapshot& snapshot, uint32_t bar, uint8_t pitchLow,
                               uint8_t pitchHigh, NoteUtils::DisplayNoteVec& out) {
    const uint8_t lastPitch =
        pitchHigh < kLoopSnapshotPitchCount ? pitchHigh
                                            : static_cast<uint8_t>(kLoopSnapshotPitchCount - 1);
    for (uint16_t pitch = pitchLow; pitch <= lastPitch; ++pitch) {
        const auto membership = snapshot.noteMembership(static_cast<uint8_t>(pitch), bar);
        forEachSnapshotMembershipReference(
            membership.get(), [&out](const LoopSnapshotNoteRef& reference) {
                if (!reference) {
                    return;
                }
                appendDisplayNoteUniqueById(out, displayNoteFromSnapshotNote(*reference));
            });
    }
}

void appendNotesIntersectingBar(const NoteUtils::DisplayNoteVec& source, uint32_t bar,
                                uint32_t loopLength, NoteUtils::DisplayNoteVec& out) {
    for (const NoteUtils::DisplayNote& note : source) {
        if (previewNoteIntersectsBar(note, bar, loopLength)) {
            appendDisplayNoteUniqueById(out, note);
        }
    }
}

}  // namespace

DISP_CAPTURE_MEM const DisplayNoteVec& DisplayManager::resolveDisplayNotesByDrawSource(
    const Track& track, uint8_t displaySlot, uint32_t currentTick, uint8_t pitchLow,
    uint8_t pitchHigh) {
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    liveDisplayNotes.clear();
    if (loopLength == 0) {
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
        return liveDisplayNotes;
    }

    uint32_t windowStart = 0;
    uint32_t windowLength = loopLength;
    uint8_t windowBars = 0;
    syncDetailedPaintWindow(track, displaySlot, currentTick, loopLength, windowStart, windowLength,
                            windowBars);
    if (windowLength == 0) {
        windowStart = 0;
        windowLength = loopLength;
    }

    const uint32_t playheadTick = resolvePlayheadInLoop(track, displaySlot, currentTick);
    const uint32_t playheadBar =
        Config::TICKS_PER_BAR > 0 ? playheadTick / Config::TICKS_PER_BAR : 0;
    const uint32_t firstBar =
        Config::TICKS_PER_BAR > 0 ? windowStart / Config::TICKS_PER_BAR : 0;
    const uint32_t lastTick = windowStart + windowLength > 0 ? windowStart + windowLength - 1 : 0;
    const uint32_t lastBar = Config::TICKS_PER_BAR > 0 ? lastTick / Config::TICKS_PER_BAR : 0;
    const bool snapshotCurrent = loop.publishedSnapshotIsCurrent();
    const ImmutableLoopSnapshotRef snapshot = snapshotCurrent ? loop.publishedSnapshot() : nullptr;
    const bool captureActive = loop.captureActive();
    const uint8_t resolvedPitchLow = pitchLow;
    const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;

    const uint32_t gatherStartUs = micros();
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (snapshotCurrent && snapshot) {
            appendSnapshotNotesForBar(*snapshot, bar, resolvedPitchLow, resolvedPitchHigh,
                                      liveDisplayNotes);
        } else {
            appendNotesIntersectingBar(loop.visualCache.notes, bar, loopLength, liveDisplayNotes);
        }
    }
    // Seal handoff (150650): between the seal and the moment the selected committed
    // representation carries the pass, these rows are the only place the sealed geometry exists.
    // They are committed content, so they join the committed prefix and pending note changes
    // still apply to them.
    if (loop.hasSealedCapturePreview() && !loop.sealedCapturePreviewIsCovered()) {
        for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
            for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
                if (!previewNoteIntersectsBar(sealedRow, bar, loopLength)) {
                    continue;
                }
                DisplayWindowUtils::upsertDisplayNoteAtPitchStart(liveDisplayNotes, sealedRow);
                break;
            }
        }
    }
    const size_t committedDisplayEnd = liveDisplayNotes.size();
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        const bool liveBar = barIsLiveCapture(loop, bar, playheadBar, loopLength);
        const DisplayWindowUtils::DrawSourcePath path = DisplayWindowUtils::selectDrawSourceForBar(
            captureActive, liveBar, snapshotCurrent);
        if (path == DisplayWindowUtils::DrawSourcePath::Capture) {
            appendNotesIntersectingBar(loop.capturePreview.notes, bar, loopLength, liveDisplayNotes);
        }
    }
    DIAG_TIMING_RECORD(DisplayBuild, micros() - gatherStartUs);

    if (loop.hasPendingNoteChanges() || loop.sealedCompanionOverlayRequired()) {
        loop.applyPendingNoteChangesToDisplayNotes(liveDisplayNotes, committedDisplayEnd);
    }

    const bool applyLiveCaptureTails = DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
        track.isRecording(), track.isOverdubbing(), false, false,
        !loop.capturePreview.openNoteIndices.empty());
    const DisplayNoteVec* paintedNotes = &liveDisplayNotes;
    if (applyLiveCaptureTails) {
        liveDisplayPendingPaintNotes_ = liveDisplayNotes;
        const uint32_t playheadCloseTick =
            resolvePlayheadInLoop(track, displaySlot, currentTick);
        const bool allowWrapContinuation =
            !track.isOverdubbing() && !(track.isRecording() && !track.isPlaying());
        applyCapturePlayheadTails(loop.capturePreview, loopLength, playheadCloseTick,
                                  committedDisplayEnd, liveDisplayPendingPaintNotes_,
                                  allowWrapContinuation);
        paintedNotes = &liveDisplayPendingPaintNotes_;
    }

    liveDisplayCacheCommittedNoteCount_ = committedDisplayEnd;
    liveDisplayCacheBaseNoteCount_ = paintedNotes->size();
    livePlaybackDisplaySlot_ = displaySlot;
    livePlaybackDisplayTrack_ = resolveTrackIndex(track);
    liveMergePlaybackRevision_ = loop.playbackRevision;
    liveMergeCaptureRevision_ = loop.captureDisplayRevision;
    return *paintedNotes;
}
