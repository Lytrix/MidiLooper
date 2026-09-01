//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "Loop.h"
#include "LoopSnapshot.h"
#include "PendingNoteChange.h"
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
    if (track.isJamming() || displaySlot >= kDisplaySlotCount) {
        outWindowStart = 0;
        outWindowLength = loopLength;
        outWindowBars = DisplayWindowUtils::kMaxDetailedWindowBars;
        return false;
    }
    const uint8_t requestedBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                                    DisplayWindowUtils::kMaxDetailedWindowBars);
    const uint32_t horizonLength =
        static_cast<uint32_t>(requestedBars) * Config::TICKS_PER_BAR;
    const bool autoFollow = !DisplayWindowUtils::windowCoversLoop(horizonLength, loopLength) &&
                            shouldAutoFollowDetailedWindow(track, loopLength);
    const uint32_t playhead = autoFollow ? resolvePlayheadInLoop(track, displaySlot, currentTick)
                                         : 0;
    if (!DisplayWindowUtils::resolveDetailedPaintWindow(
            loopLength, requestedBars, detailedWindowStartTick_[displaySlot], playhead, autoFollow,
            outWindowStart, outWindowLength, outWindowBars)) {
        outWindowStart = 0;
        outWindowLength = loopLength;
        outWindowBars = requestedBars;
        return false;
    }
    detailedWindowStartTick_[displaySlot] = outWindowStart;
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

uint32_t fingerprintPendingNoteChanges(const PendingNoteChangeVec& changes) {
    uint32_t generation = static_cast<uint32_t>(changes.size());
    for (const PendingNoteChange& change : changes) {
        generation ^= static_cast<uint32_t>(change.noteId) + 0x9e3779b9u + (generation << 6) +
                      (generation >> 2);
        generation ^= static_cast<uint32_t>(change.kind);
        generation ^= change.startTick;
        generation ^= change.endTick;
    }
    return generation;
}

void gatherDrawSourceBars(const Loop& loop, uint32_t firstBar, uint32_t lastBar,
                          uint32_t playheadBar, uint8_t pitchLow, uint8_t pitchHigh,
                          NoteUtils::DisplayNoteVec& out) {
    const uint32_t loopLength = loop.loopLengthTicks;
    if (loopLength == 0 || firstBar > lastBar) {
        return;
    }
    const bool snapshotCurrent = loop.publishedSnapshotIsCurrent();
    const ImmutableLoopSnapshotRef snapshot = snapshotCurrent ? loop.publishedSnapshot() : nullptr;
    const bool captureActive = loop.captureActive();
    const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (snapshotCurrent && snapshot) {
            appendSnapshotNotesForBar(*snapshot, bar, pitchLow, resolvedPitchHigh, out);
        } else {
            appendNotesIntersectingBar(loop.visualCache.notes, bar, loopLength, out);
        }
    }
    if (loop.hasSealedCapturePreview() && !loop.sealedCapturePreviewIsCovered()) {
        for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
            for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
                if (!previewNoteIntersectsBar(sealedRow, bar, loopLength)) {
                    continue;
                }
                DisplayWindowUtils::upsertDisplayNoteAtPitchStart(out, sealedRow);
                break;
            }
        }
    }
    if (captureActive) {
        for (const NoteUtils::DisplayNote& previewNote : loop.capturePreview.notes) {
            bool intersectsWindow = false;
            for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
                if (previewNoteIntersectsBar(previewNote, bar, loopLength)) {
                    intersectsWindow = true;
                    break;
                }
            }
            if (!intersectsWindow) {
                continue;
            }
            if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(previewNote.startTick,
                                                                 previewNote.endTick)) {
                appendDisplayNoteUniqueById(out, previewNote);
            } else {
                DisplayWindowUtils::upsertDisplayNoteAtPitchStart(out, previewNote);
            }
        }
    }
    (void)playheadBar;
}

void applyPendingDrawSourceTransforms(const Loop& loop, NoteUtils::DisplayNoteVec& notes,
                                      size_t committedDisplayEnd) {
    if (loop.hasPendingNoteChanges() || loop.hasSealedCompanionOverlay()) {
        loop.applyPendingNoteChangesToDisplayNotes(notes, committedDisplayEnd);
    }
}

DisplayWindowUtils::DrawSourceGatherKey makeDrawSourceGatherKey(
    const Track& track, const Loop& loop, uint8_t displaySlot, uint32_t windowStart,
    uint32_t windowLength, uint32_t playheadBar, uint8_t pitchLow, uint8_t pitchHigh) {
    DisplayWindowUtils::DrawSourceGatherKey key;
    key.slot = displaySlot;
    key.track = 255;
    key.pitchLow = pitchLow;
    key.pitchHigh = pitchHigh;
    key.windowStart = windowStart;
    key.windowLength = windowLength;
    key.playbackRevision = loop.playbackRevision;
    key.captureDisplayRevision = loop.captureDisplayRevision;
    key.capturePreviewRevision = loop.capturePreview.revision;
    key.visualCacheRevision = loop.visualCache.revision;
    key.publishedSnapshotRevision = loop.publishedSnapshotRevision();
    if (loop.hasSealedCapturePreview() && !loop.sealedCapturePreviewIsCovered()) {
        key.sealedPreviewRevision = loop.playbackRevision;
        const size_t sealedCount = loop.sealedCapturePreviewNotes().size();
        key.sealedPreviewCount =
            static_cast<uint16_t>(sealedCount > 0xFFFFu ? 0xFFFFu : sealedCount);
    } else {
        key.sealedPreviewRevision = 0;
        key.sealedPreviewCount = 0;
    }
    key.pendingChangeGeneration = fingerprintPendingNoteChanges(loop.pendingNoteChanges());
    key.playheadBar = playheadBar;
    key.snapshotCurrent = loop.publishedSnapshotIsCurrent() ? 1 : 0;
    key.sealedCompanionOverlay = loop.hasSealedCompanionOverlay() ? 1 : 0;
    key.valid = 1;
    (void)track;
    return key;
}

}  // namespace

DISP_CAPTURE_MEM const DisplayNoteVec& DisplayManager::resolveDisplayNotesByDrawSource(
    const Track& track, uint8_t displaySlot, uint32_t currentTick, uint8_t pitchLow,
    uint8_t pitchHigh) {
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    if (loopLength == 0) {
        liveDisplayNotes.clear();
        liveDrawSourceKey_ = {};
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
    const uint8_t resolvedPitchLow = pitchLow;
    const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
    const uint8_t trackIndex = resolveTrackIndex(track);

    DisplayWindowUtils::DrawSourceGatherKey currentKey = makeDrawSourceGatherKey(
        track, loop, displaySlot, windowStart, windowLength, playheadBar, resolvedPitchLow,
        resolvedPitchHigh);
    currentKey.track = trackIndex;

    const DisplayWindowUtils::DrawSourceReuseKind reuseKind =
        DisplayWindowUtils::selectDrawSourceReuseKind(liveDrawSourceKey_, currentKey,
                                                      Config::TICKS_PER_BAR);

    size_t committedDisplayEnd = liveDisplayCacheCommittedNoteCount_;
    if (reuseKind == DisplayWindowUtils::DrawSourceReuseKind::Hit) {
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
    } else {
        DisplayNoteVec candidate;
        const uint32_t gatherStartUs = micros();
        if (reuseKind == DisplayWindowUtils::DrawSourceReuseKind::SlideOneBar &&
            !liveDisplayNotes.empty()) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            candidate = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(
                liveDisplayNotes, windowStart, windowLength, loopLength);
            const uint32_t enteringBar =
                windowStart > liveDrawSourceKey_.windowStart ? lastBar : firstBar;
            gatherDrawSourceBars(loop, enteringBar, enteringBar, playheadBar, resolvedPitchLow,
                                 resolvedPitchHigh, candidate);
            committedDisplayEnd = candidate.size();
            applyPendingDrawSourceTransforms(loop, candidate, committedDisplayEnd);
        } else {
            DIAG_COUNTER_INC(DisplayFullRebuild);
            gatherDrawSourceBars(loop, firstBar, lastBar, playheadBar, resolvedPitchLow,
                                 resolvedPitchHigh, candidate);
            committedDisplayEnd = candidate.size();
            applyPendingDrawSourceTransforms(loop, candidate, committedDisplayEnd);
        }
        DIAG_TIMING_RECORD(DisplayBuild, micros() - gatherStartUs);
        liveDisplayNotes.swap(candidate);
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
    livePlaybackDisplayTrack_ = trackIndex;
    liveMergePlaybackRevision_ = loop.playbackRevision;
    liveMergeCaptureRevision_ = loop.captureDisplayRevision;
    liveDrawSourceKey_ = currentKey;
    return *paintedNotes;
}
