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

namespace {

uint32_t clampOpenNoteCloseTick(uint32_t closeTick, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    if (closeTick >= loopLength) {
        return loopLength - 1;
    }
    return closeTick;
}

template <typename Alloc>
bool findPreferredWrapHeadOffTick(const std::vector<MidiEvent, Alloc>& midiEvents,
                                  const NoteUtils::OpenNoteOn& open, uint32_t loopLength,
                                  uint32_t& headOffTickOut) {
    uint8_t channel = 0;
    bool channelKnown = false;
    for (const MidiEvent& evt : midiEvents) {
        if (evt.isNoteOn() && evt.data.noteData.note == open.note && evt.tick == open.tick) {
            channel = evt.channel;
            channelKnown = true;
            break;
        }
    }
    for (const MidiEvent& evt : midiEvents) {
        if (!evt.isNoteOff() || evt.data.noteData.note != open.note) {
            continue;
        }
        if (channelKnown && evt.channel != channel) {
            continue;
        }
        uint32_t headOffTick = evt.tick;
        if (headOffTick >= loopLength) {
            headOffTick %= loopLength;
        }
        if (headOffTick >= loopLength - 1) {
            continue;
        }
        if (NoteUtils::isPreferredWrapTailForHeadOff(open.tick, headOffTick, midiEvents, open.note,
                                                     channelKnown ? channel : evt.channel,
                                                     loopLength)) {
            headOffTickOut = headOffTick;
            return true;
        }
    }
    return false;
}

bool updateDisplayNoteEndFrom(DisplayNoteVec& notes, size_t regionStart, uint8_t pitch,
                              uint32_t startTick, uint32_t endTick) {
    for (size_t i = regionStart; i < notes.size(); ++i) {
        if (notes[i].note == pitch && notes[i].startTick == startTick) {
            notes[i].endTick = endTick;
            return true;
        }
    }
    return false;
}

int findCaptureTailTargetIndex(const DisplayNoteVec& notes, size_t regionStart,
                               const NoteUtils::DisplayNote& previewNote) {
    if (regionStart >= notes.size()) {
        return -1;
    }

    if (previewNote.noteId != kInvalidNoteId) {
        for (size_t i = regionStart; i < notes.size(); ++i) {
            if (notes[i].noteId == previewNote.noteId) {
                return static_cast<int>(i);
            }
        }
    }

    int openHoldMatch = -1;
    int invalidIdMatch = -1;
    int anyMatch = -1;
    for (size_t i = regionStart; i < notes.size(); ++i) {
        if (notes[i].note != previewNote.note || notes[i].startTick != previewNote.startTick) {
            continue;
        }
        if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(notes[i].startTick,
                                                             notes[i].endTick)) {
            openHoldMatch = static_cast<int>(i);
            break;
        }
        if (invalidIdMatch < 0 && notes[i].noteId == kInvalidNoteId) {
            invalidIdMatch = static_cast<int>(i);
        }
        if (anyMatch < 0) {
            anyMatch = static_cast<int>(i);
        }
    }

    if (openHoldMatch >= 0) {
        return openHoldMatch;
    }
    if (invalidIdMatch >= 0) {
        return invalidIdMatch;
    }
    return anyMatch;
}

void appendWrapHeldOpenNoteDisplay(DisplayNoteVec& notes, size_t regionStart,
                                   const NoteUtils::OpenNoteOn& open, uint32_t tailEnd,
                                   const NoteUtils::WrapHeadSegment& headSegment) {
    if (!updateDisplayNoteEndFrom(notes, regionStart, open.note, open.tick, tailEnd)) {
        DisplayNote tailSeg;
        tailSeg.note = open.note;
        tailSeg.velocity = open.velocity;
        tailSeg.startTick = open.tick;
        tailSeg.endTick = tailEnd;
        notes.push_back(tailSeg);
    }

    if (!headSegment.visible) {
        return;
    }
    DisplayNote headSeg;
    headSeg.note = open.note;
    headSeg.velocity = open.velocity;
    headSeg.startTick = headSegment.startTick;
    headSeg.endTick = headSegment.endTickInclusive;
    notes.push_back(headSeg);
}

NoteUtils::WrapHeadSegment resolveWrapOpenHeadSegment(uint32_t loopLength,
                                                      const NoteUtils::OpenNoteOn& open,
                                                      uint32_t closeTick, bool hasCommittedHeadOff,
                                                      uint32_t headOffTick) {
    if (hasCommittedHeadOff) {
        return NoteUtils::resolveWrapHeadSegment(
            loopLength, headOffTick, NoteUtils::WrapHeadSegmentContext::CommittedHeadOff);
    }
    if (closeTick < open.tick) {
        return NoteUtils::resolveWrapHeadSegment(loopLength, closeTick,
                                                 NoteUtils::WrapHeadSegmentContext::LivePlayhead,
                                                 768, open.tick);
    }
    return {};
}

bool shouldSplitLiveWrapOpenNoteDisplay(const NoteUtils::OpenNoteOn& open, uint32_t loopLength,
                                        uint32_t closeTick, bool extendHeldNotesToPlayhead,
                                        bool isWrapHeld) {
    return isWrapHeld ||
           NoteUtils::isLiveWrapHeadContinuationDisplay(open.tick, closeTick, loopLength,
                                                        extendHeldNotesToPlayhead);
}

}  // namespace

namespace DisplayManagerInternal {

void applyCapturePlayheadTails(const CapturePreview& preview, uint32_t loopLength,
                               uint32_t closeTick, size_t captureRegionStart,
                               DisplayNoteVec& notes, bool allowWrapContinuation) {
    const uint32_t clampedCloseTick = clampOpenNoteCloseTick(closeTick, loopLength);

    for (const uint32_t previewNoteIndex : preview.openNoteIndices) {
        if (previewNoteIndex >= preview.notes.size() ||
            previewNoteIndex >= preview.noteStates.size()) {
            continue;
        }
        const NoteUtils::DisplayNote& previewNote = preview.notes[previewNoteIndex];
        const CapturePreviewNoteState& state = preview.noteStates[previewNoteIndex];
        if (!state.open) {
            continue;
        }
        const NoteUtils::OpenNoteOn open{
            previewNote.note, previewNote.velocity, previewNote.startTick};
        // Growing live record has no loop wrap: closeTick < noteOn is playhead catch-up, not
        // wrap-head continuation (false head from tick 0 until NoteOff).
        const bool wrapHeldForSplit = allowWrapContinuation && state.wrapHeld;
        if (allowWrapContinuation &&
            shouldSplitLiveWrapOpenNoteDisplay(open, loopLength, clampedCloseTick, true,
                                               wrapHeldForSplit)) {
            uint32_t tailEnd = loopLength - 1;
            if (clampedCloseTick >= open.tick) {
                tailEnd = std::min(clampedCloseTick, loopLength - 1);
            }

            const NoteUtils::WrapHeadSegment head = resolveWrapOpenHeadSegment(
                loopLength, open, clampedCloseTick, state.hasPreferredHeadOff,
                state.preferredHeadOffTick);
            appendWrapHeldOpenNoteDisplay(notes, captureRegionStart, open, tailEnd, head);
            continue;
        }

        uint32_t noteStartTick = open.tick;
        uint32_t playheadEndTick = std::max(noteStartTick, clampedCloseTick);
        DisplayWindowUtils::clampNonWrapDisplayNoteBarTicks(noteStartTick, playheadEndTick,
                                                            loopLength);
        // Same pitch+start siblings can coexist after wrap/overlap transforms.
        // Tail growth must follow the live capture row identity, not whichever
        // closed sibling appears first in the suffix (`142336` frame drop).
        const int targetIndex =
            findCaptureTailTargetIndex(notes, captureRegionStart, previewNote);
        if (targetIndex >= 0) {
            notes[static_cast<size_t>(targetIndex)].startTick = noteStartTick;
            notes[static_cast<size_t>(targetIndex)].endTick = playheadEndTick;
        } else {
            DisplayNote liveNote;
            liveNote.noteId = previewNote.noteId;
            liveNote.note = open.note;
            liveNote.velocity = open.velocity;
            liveNote.startTick = noteStartTick;
            liveNote.endTick = playheadEndTick;
            notes.push_back(liveNote);
        }
    }
}

void applyLiveOpenTails(const std::vector<NoteUtils::OpenNoteOn>& openNotes,
                        const SessionMidiEventVec& midiEvents, uint32_t loopLength,
                        uint32_t closeTick, DisplayNoteVec& notes, bool extendHeldNotesToPlayhead) {
    const uint32_t clampedCloseTick = clampOpenNoteCloseTick(closeTick, loopLength);

    for (const auto& open : openNotes) {
        const bool isWrapHeld = NoteUtils::isWrapHeldOpenNote(midiEvents, open, loopLength);
        if (shouldSplitLiveWrapOpenNoteDisplay(open, loopLength, clampedCloseTick,
                                               extendHeldNotesToPlayhead, isWrapHeld)) {
            uint32_t tailEnd = loopLength - 1;
            if (extendHeldNotesToPlayhead && clampedCloseTick >= open.tick) {
                tailEnd = std::min(clampedCloseTick, loopLength - 1);
            }

            NoteUtils::WrapHeadSegment head;
            if (extendHeldNotesToPlayhead) {
                uint32_t headOffTick = 0;
                const bool hasCommittedHead =
                    isWrapHeld &&
                    findPreferredWrapHeadOffTick(midiEvents, open, loopLength, headOffTick);
                head = resolveWrapOpenHeadSegment(loopLength, open, clampedCloseTick,
                                                  hasCommittedHead, headOffTick);
            } else if (isWrapHeld) {
                uint32_t headOffTick = 0;
                if (findPreferredWrapHeadOffTick(midiEvents, open, loopLength, headOffTick)) {
                    head = NoteUtils::resolveWrapHeadSegment(
                        loopLength, headOffTick,
                        NoteUtils::WrapHeadSegmentContext::CommittedHeadOff);
                }
            }
            appendWrapHeldOpenNoteDisplay(notes, 0, open, tailEnd, head);
            continue;
        }

        if (!extendHeldNotesToPlayhead) {
            continue;
        }

        const uint32_t playheadEndTick = std::max(open.tick, clampedCloseTick);
        bool found = false;
        for (auto& note : notes) {
            if (note.note == open.note && note.startTick == open.tick) {
                note.endTick = playheadEndTick;
                found = true;
                break;
            }
        }
        if (!found) {
            DisplayNote liveNote;
            liveNote.note = open.note;
            liveNote.velocity = open.velocity;
            liveNote.startTick = open.tick;
            liveNote.endTick = playheadEndTick;
            notes.push_back(liveNote);
        }
    }
}

}  // namespace DisplayManagerInternal

namespace {

DISP_CAPTURE_MEM bool gatherWindowHasDirtyBar(const Loop& loop, uint32_t liveLoopLength,
                                              uint32_t gatherStart, uint32_t gatherLength) {
    if (!loop.visualCacheDirty) {
        return false;
    }
    if (loop.visualCache.dirtyBars.empty() || liveLoopLength == 0 || gatherLength == 0) {
        return true;
    }
    const uint32_t ticksPerBar = Config::TICKS_PER_BAR;
    const uint32_t totalBars = static_cast<uint32_t>(loop.visualCache.dirtyBars.size());
    if (gatherLength >= liveLoopLength) {
        for (uint8_t flag : loop.visualCache.dirtyBars) {
            if (flag != 0) {
                return true;
            }
        }
        return false;
    }
    const uint32_t start = IntervalProjection::tickPhaseInLoop(gatherStart, 0, liveLoopLength);
    for (uint32_t offset = 0; offset < gatherLength; offset += ticksPerBar) {
        const uint32_t tick =
            IntervalProjection::tickPhaseInLoop(start + offset, 0, liveLoopLength);
        const uint32_t bar = visualBarForTick(tick, ticksPerBar);
        if (bar < totalBars && loop.visualCache.dirtyBars[bar] != 0) {
            return true;
        }
    }
    const uint32_t lastTick =
        IntervalProjection::tickPhaseInLoop(start + gatherLength - 1, 0, liveLoopLength);
    const uint32_t lastBar = visualBarForTick(lastTick, ticksPerBar);
    return lastBar < totalBars && loop.visualCache.dirtyBars[lastBar] != 0;
}

}  // namespace

DISP_CAPTURE_MEM void DisplayManager::rebuildOverdubCommittedDisplayLayer(
    const Track& track, Loop& mutLoop, const Loop& loop, uint8_t displaySlot, uint32_t currentTick,
    uint32_t liveLoopLength, bool havePaintWindow, uint32_t paintWindowStart,
    uint32_t paintWindowLength) {
    const uint32_t maxCommittedGatherLength =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;

    if (track.isOverdubbing()) {
        // Live overdub paint is MIDI-sensitive. Do not snapshot-gather every
        // rebuild (session_20260826_001048 midi_gap 31–113 ms). Keep the PLAYING
        // handoff only while the paint window stays inside the last gather.
        // Follow exit or wrap invalidate snapshot-gathers once
        // (session_20260826_001907). Source-view is consume ownership and must
        // not replace a nonempty committed layer.
        const bool visualCacheGrewPastHold =
            DisplayWindowUtils::shouldAssignGrownVisualCacheOverOverdubHold(
                loop.visualCache.notes.size(), liveDisplayCacheCommittedNoteCount_,
                liveDisplayNotes.size());
        if (visualCacheGrewPastHold && !loop.visualCache.notes.empty()) {
            // 201556: wrap left visualCache at 126 then idle/append grew it to 206 while
            // the held layer stayed 126. Assign the grown cache; do not hold.
            if (havePaintWindow) {
                uint32_t gatherStart = 0;
                uint32_t gatherLength = 0;
                DIAG_COUNTER_INC(DisplayCommittedWindowFilter);
                filterNotesToFollowWindow(loop.visualCache.notes, paintWindowStart,
                                          paintWindowLength, liveLoopLength, liveDisplayNotes,
                                          gatherStart, gatherLength);
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = true;
                liveWindowGatherStart_ = gatherStart;
                liveWindowGatherLength_ = gatherLength;
                liveWindowGatherLoopLength_ = liveLoopLength;
            } else {
                DIAG_COUNTER_INC(DisplayCommittedFullAssign);
                liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = false;
            }
            loop.applyPendingNoteChangesToDisplayNotes(liveDisplayNotes, liveDisplayNotes.size());
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            liveDisplayCommittedFromWindowGather_ = false;
            liveCommittedLayerHeldForDirtyCache_ = false;
            liveOverdubSourceViewNoteCount_ =
                loop.hasOverdubSourceView() ? loop.overdubSourceViewNotes().size() : 0;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }
        const bool wrapRevisionChanged = liveMergePlaybackRevision_ != UINT32_MAX &&
                                         liveMergePlaybackRevision_ != loop.playbackRevision;
        if (DisplayWindowUtils::shouldHoldComposedNotesOnOverdubWrapRevision(
                wrapRevisionChanged, loop.visualCacheDirty, !liveDisplayNotes.empty(),
                visualCacheGrewPastHold)) {
            // 231953: wrap first paint 109→65 when resize(committedCount) kept only
            // the pre-wrap record prefix. Sealed capture is not in visualCache (1d).
            liveDisplayPaintCommittedEnd_ = liveDisplayCacheCommittedNoteCount_;
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            liveDisplayCacheCaptureNoteCount_ = 0;
            liveDisplayCacheCaptureChangeCount_ = 0;
            liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
            liveCommittedLayerHeldForDirtyCache_ = true;
            liveOverdubSourceViewNoteCount_ =
                loop.hasOverdubSourceView() ? loop.overdubSourceViewNotes().size() : 0;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            adoptComposedDisplayNotesIfAuthoritative(mutLoop);
            return;
        }
        if (havePaintWindow &&
            visualCacheCoversFollowWindow(loop, paintWindowStart, paintWindowLength,
                                          liveLoopLength)) {
            uint32_t gatherStart = 0;
            uint32_t gatherLength = 0;
            DIAG_COUNTER_INC(DisplayCommittedWindowFilter);
            filterNotesToFollowWindow(loop.visualCache.notes, paintWindowStart, paintWindowLength,
                                      liveLoopLength, liveDisplayNotes, gatherStart, gatherLength);
            loop.applyPendingNoteChangesToDisplayNotes(liveDisplayNotes, liveDisplayNotes.size());
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            liveWindowGatherValid_ = true;
            liveWindowGatherStart_ = gatherStart;
            liveWindowGatherLength_ = gatherLength;
            liveWindowGatherLoopLength_ = liveLoopLength;
            liveDisplayCommittedFromWindowGather_ = false;
            liveCommittedLayerHeldForDirtyCache_ = false;
            liveOverdubSourceViewNoteCount_ =
                loop.hasOverdubSourceView() ? loop.overdubSourceViewNotes().size() : 0;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }
        const bool canHoldCommittedLayer =
            liveDisplayCacheCommittedNoteCount_ > 0 &&
            liveDisplayCacheCommittedNoteCount_ <= liveDisplayNotes.size();
        const bool paintWindowInsideGather =
            liveWindowGatherValid_ &&
            DisplayWindowUtils::paintWindowInsideGather(paintWindowStart, paintWindowLength,
                                                        liveWindowGatherStart_,
                                                        liveWindowGatherLength_);
        if (DisplayWindowUtils::shouldHoldOverdubCommittedHandoff(
                canHoldCommittedLayer, havePaintWindow, liveWindowGatherValid_,
                paintWindowInsideGather)) {
            syncLiveDisplayComposeCounts();
            resizeLiveDisplayNotesNoGrow(liveDisplayCacheCommittedNoteCount_);
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            liveCommittedLayerHeldForDirtyCache_ = true;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }
        if (DisplayWindowUtils::shouldHoldDirtyOverdubLayerBeforeWindowGather(
                loop.visualCacheDirty, canHoldCommittedLayer, visualCacheGrewPastHold)) {
            syncLiveDisplayComposeCounts();
            resizeLiveDisplayNotesNoGrow(liveDisplayCacheCommittedNoteCount_);
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            liveCommittedLayerHeldForDirtyCache_ = true;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }
        if (havePaintWindow && loop.publishedSnapshot() &&
            (liveDisplayNotes.empty() || !paintWindowInsideGather)) {
            // A failed visual-cache coverage check must use the bounded published
            // snapshot window gather; a nonempty cache is not evidence of coverage.
            uint32_t gatherStart = 0;
            uint32_t gatherLength = 0;
            const uint32_t marginTicks =
                static_cast<uint32_t>(kWindowedGatherMarginBars) * Config::TICKS_PER_BAR;
            DisplayWindowUtils::expandWindowWithMargin(paintWindowStart, paintWindowLength,
                                                       liveLoopLength, marginTicks, gatherStart,
                                                       gatherLength);
            DIAG_COUNTER_INC(DisplayCaptureFullGather);
            rebuildDisplayNotesInWindow(mutLoop, loop, liveLoopLength, gatherStart, gatherLength,
                                        liveDisplayEventBuffer, liveDisplayNotes, false);
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            liveWindowGatherValid_ = true;
            liveWindowGatherStart_ = gatherStart;
            liveWindowGatherLength_ = gatherLength;
            liveWindowGatherLoopLength_ = liveLoopLength;
            liveDisplayCommittedFromWindowGather_ = true;
            liveCommittedLayerHeldForDirtyCache_ = false;
            liveOverdubSourceViewNoteCount_ =
                loop.hasOverdubSourceView() ? loop.overdubSourceViewNotes().size() : 0;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }
        if (canHoldCommittedLayer && !visualCacheGrewPastHold) {
            syncLiveDisplayComposeCounts();
            resizeLiveDisplayNotesNoGrow(liveDisplayCacheCommittedNoteCount_);
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            liveCommittedLayerHeldForDirtyCache_ = true;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }
        if (loop.hasOverdubSourceView() && !loop.overdubSourceViewNotes().empty()) {
            if (havePaintWindow) {
                uint32_t gatherStart = 0;
                uint32_t gatherLength = 0;
                DIAG_COUNTER_INC(DisplayCommittedWindowFilter);
                filterNotesToFollowWindow(loop.overdubSourceViewNotes(), paintWindowStart,
                                          paintWindowLength, liveLoopLength, liveDisplayNotes,
                                          gatherStart, gatherLength);
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = true;
                liveWindowGatherStart_ = gatherStart;
                liveWindowGatherLength_ = gatherLength;
                liveWindowGatherLoopLength_ = liveLoopLength;
            } else {
                DIAG_COUNTER_INC(DisplayCommittedFullAssign);
                liveDisplayNotes.assign(loop.overdubSourceViewNotes().begin(),
                                        loop.overdubSourceViewNotes().end());
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = false;
            }
            liveDisplayCommittedFromWindowGather_ = false;
            liveCommittedLayerHeldForDirtyCache_ = false;
            liveOverdubSourceViewNoteCount_ = loop.overdubSourceViewNotes().size();
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }
        // Cache recovery belongs to idle work. Gathering the committed window here costs
        // ~27.9 ms per frame for the whole post-commit dirty window and starves the capture
        // layer, so played notes stop landing (172405: 30 gathers, 70 of 241 frames over
        // budget). Hold the layer we already have; processDeferredIdleMaintenance makes the
        // cache clean in ~0.6-2.3 s and the clean branch then rebuilds it.
        if (loop.visualCacheDirty && canHoldCommittedLayer) {
            syncLiveDisplayComposeCounts();
            resizeLiveDisplayNotesNoGrow(liveDisplayCacheCommittedNoteCount_);
            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            liveCommittedLayerHeldForDirtyCache_ = true;
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }
        if (!loop.visualCacheDirty && !loop.visualCache.notes.empty()) {
            if (havePaintWindow) {
                uint32_t gatherStart = 0;
                uint32_t gatherLength = 0;
                DIAG_COUNTER_INC(DisplayCommittedWindowFilter);
                filterNotesToFollowWindow(loop.visualCache.notes, paintWindowStart,
                                          paintWindowLength, liveLoopLength, liveDisplayNotes,
                                          gatherStart, gatherLength);
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = true;
                liveWindowGatherStart_ = gatherStart;
                liveWindowGatherLength_ = gatherLength;
                liveWindowGatherLoopLength_ = liveLoopLength;
                liveDisplayCommittedFromWindowGather_ = false;
                liveCommittedLayerHeldForDirtyCache_ = false;
            } else {
                DIAG_COUNTER_INC(DisplayCommittedFullAssign);
                liveDisplayNotes.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = false;
                liveDisplayCommittedFromWindowGather_ = false;
                liveCommittedLayerHeldForDirtyCache_ = false;
            }
        } else if (loop.hasCommittedPasses()) {
            uint32_t gatherStart = 0;
            uint32_t gatherLength = liveLoopLength;
            if (havePaintWindow) {
                gatherStart = paintWindowStart;
                gatherLength = paintWindowLength;
            } else if (liveLoopLength > maxCommittedGatherLength) {
                const uint32_t playhead = resolvePlayheadInLoop(track, displaySlot, currentTick);
                gatherLength = maxCommittedGatherLength;
                gatherStart = DisplayWindowUtils::resolveCenteredWindowStart(playhead, gatherLength,
                                                                             liveLoopLength);
            }
            if (!loop.visualCache.notes.empty() &&
                !gatherWindowHasDirtyBar(loop, liveLoopLength, gatherStart, gatherLength)) {
                DIAG_COUNTER_INC(DisplayCommittedWindowFilter);
                liveDisplayNotes = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(
                    loop.visualCache.notes, gatherStart, gatherLength, liveLoopLength);
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = true;
                liveWindowGatherStart_ = gatherStart;
                liveWindowGatherLength_ = gatherLength;
                liveWindowGatherLoopLength_ = liveLoopLength;
                liveDisplayCommittedFromWindowGather_ = false;
            } else {
                DIAG_COUNTER_INC(DisplayCaptureFullGather);
                const uint32_t gatherStartUs = micros();
                rebuildDisplayNotesInWindow(mutLoop, loop, liveLoopLength, gatherStart, gatherLength,
                                            liveDisplayEventBuffer, liveDisplayNotes, false);
                DIAG_TIMING_RECORD(DisplayCaptureGather, micros() - gatherStartUs);
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = true;
                liveWindowGatherStart_ = gatherStart;
                liveWindowGatherLength_ = gatherLength;
                liveWindowGatherLoopLength_ = liveLoopLength;
                liveDisplayCommittedFromWindowGather_ = true;
            }
        } else {
            liveDisplayNotes.clear();
            liveDisplayCacheCommittedNoteCount_ = 0;
            liveWindowGatherValid_ = false;
            liveDisplayCommittedFromWindowGather_ = false;
        }
        liveCommittedLayerHeldForDirtyCache_ = false;
        liveMergePlaybackRevision_ = loop.playbackRevision;
        return;
    }

    liveDisplayNotes.clear();
    liveDisplayCacheCommittedNoteCount_ = 0;
    liveWindowGatherValid_ = false;
    liveDisplayCommittedFromWindowGather_ = false;
}

DISP_CAPTURE_MEM const DisplayNoteVec& DisplayManager::resolveDisplayNotesLiveCapture(
    const Track& track, uint8_t displaySlot, uint32_t currentTick) {
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t liveLoopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    if (liveLoopLength == 0) {
        invalidateLiveDisplayCache();
        liveDisplayNotes.clear();
        return liveDisplayNotes;
    }

    const uint32_t resolveStartUs = micros();

    const TrackState liveTrackState = track.isRecording() ? TRACK_RECORDING : TRACK_OVERDUBBING;
    const size_t eventCount =
        (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size()
                                                    : loop.displayEventCountHint();
    const bool cacheCold = liveDisplayCacheEventCount == static_cast<size_t>(-1);
    const bool contextChanged = displaySlot != liveDisplayCacheSlot ||
                                liveTrackState != liveDisplayCacheTrackState;
    const bool eventsShrunk = !cacheCold && eventCount < liveDisplayCacheEventCount;
    const bool eventsAdded = !cacheCold && eventCount > liveDisplayCacheEventCount;
    const bool expandingLiveRecordLength = track.isRecording() && !track.isPlaying();
    const bool loopLengthChanged = !cacheCold && liveLoopLength != liveDisplayCacheLoopLength &&
                                   !expandingLiveRecordLength;
    const bool captureRevisionChanged =
        !cacheCold && loop.captureDisplayRevision != liveDisplayCacheCaptureRevision;
    const bool capturePreviewChanged =
        !cacheCold && loop.capturePreviewView().revision != liveDisplayCacheCapturePreviewRevision_;
    if (eventsAdded) {
        DIAG_COUNTER_INC(DisplayCaptureEventsAdded);
    }
    Loop& mutLoop = const_cast<Loop&>(loop);

    uint32_t paintWindowStart = 0;
    uint32_t paintWindowLength = 0;
    uint8_t paintWindowBars = 0;
    const bool havePaintWindow =
        syncDetailedPaintWindow(track, displaySlot, currentTick, liveLoopLength, paintWindowStart,
                                paintWindowLength, paintWindowBars);

    auto budgetExceeded = [&]() {
        return (micros() - resolveStartUs) > Diagnostics::kDisplayResolveBudgetMicros;
    };

    auto replaceCaptureLayer = [&]() {
        const uint32_t replaceStartUs = micros();
#if defined(SESSION_CAPTURE)
        // Rate-limited Tier-A DIAG when full capture-layer copy runs on a large preview
        // (second-overdub lag investigation).
        constexpr size_t kReplaceCaptureLayerLogThreshold = 256;
        if (loop.capturePreviewView().notes.size() >= kReplaceCaptureLayerLogThreshold) {
            static uint32_t sLastReplaceCaptureLogMs = 0;
            const uint32_t nowMs = millis();
            if (sLastReplaceCaptureLogMs == 0 || (nowMs - sLastReplaceCaptureLogMs) >= 5000u) {
                sLastReplaceCaptureLogMs = nowMs;
                DebugSessionCapture::architectureTimingMax(
                    "replaceCaptureLayer",
                    static_cast<uint32_t>(loop.capturePreviewView().notes.size()));
            }
        }
#endif
        syncLiveDisplayComposeCounts();
        resizeLiveDisplayNotesNoGrow(liveDisplayCacheCommittedNoteCount_);
        liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
        liveDisplayNotes.insert(liveDisplayNotes.end(), loop.capturePreviewView().notes.begin(),
                                loop.capturePreviewView().notes.end());
        liveDisplayCacheCaptureNoteCount_ = loop.capturePreviewView().notes.size();
        liveDisplayCacheCaptureChangeCount_ = loop.capturePreviewView().changedNoteIndices.size();
        liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
        liveDisplayCacheCaptureReplacementRevision_ =
            loop.capturePreviewView().replacementRevision;
        liveDisplayCacheCapturePreviewRevision_ = loop.capturePreviewView().revision;
        DIAG_TIMING_RECORD(DisplayCaptureReplace, micros() - replaceStartUs);
    };

    auto trimToComposeCounts = [&]() {
        syncLiveDisplayComposeCounts();
        const size_t composeEnd =
            liveDisplayCacheCommittedNoteCount_ + liveDisplayCacheCaptureNoteCount_;
        resizeLiveDisplayNotesNoGrow(composeEnd);
        liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
    };

    auto synchronizeCaptureLayer = [&]() {
        const uint32_t syncStartUs = micros();
        const bool captureMirrorInvalid =
            liveDisplayCacheCaptureReplacementRevision_ !=
                loop.capturePreviewView().replacementRevision ||
            liveDisplayCacheCaptureNoteCount_ > loop.capturePreviewView().notes.size() ||
            liveDisplayCacheCaptureChangeCount_ >
                loop.capturePreviewView().changedNoteIndices.size() ||
            liveDisplayCacheCommittedNoteCount_ + liveDisplayCacheCaptureNoteCount_ >
                liveDisplayCacheBaseNoteCount_ ||
            liveDisplayCacheBaseNoteCount_ > liveDisplayNotes.size();
        if (captureMirrorInvalid) {
            // Nests DisplayCaptureReplace inside this sample.
            replaceCaptureLayer();
            DIAG_TIMING_RECORD(DisplayCaptureSync, micros() - syncStartUs);
            return;
        }

        resizeLiveDisplayNotesNoGrow(liveDisplayCacheBaseNoteCount_);
        for (size_t changeIndex = liveDisplayCacheCaptureChangeCount_;
             changeIndex < loop.capturePreviewView().changedNoteIndices.size(); ++changeIndex) {
            const size_t previewNoteIndex =
                loop.capturePreviewView().changedNoteIndices[changeIndex];
            if (previewNoteIndex >= liveDisplayCacheCaptureNoteCount_ ||
                previewNoteIndex >= loop.capturePreviewView().notes.size()) {
                continue;
            }
            liveDisplayNotes[liveDisplayCacheCommittedNoteCount_ + previewNoteIndex] =
                loop.capturePreviewView().notes[previewNoteIndex];
        }
        liveDisplayNotes.insert(
            liveDisplayNotes.end(),
            loop.capturePreviewView().notes.begin() +
                static_cast<std::ptrdiff_t>(liveDisplayCacheCaptureNoteCount_),
            loop.capturePreviewView().notes.end());
        liveDisplayCacheCaptureNoteCount_ = loop.capturePreviewView().notes.size();
        liveDisplayCacheCaptureChangeCount_ = loop.capturePreviewView().changedNoteIndices.size();
        liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
        liveDisplayCacheCapturePreviewRevision_ = loop.capturePreviewView().revision;
        DIAG_TIMING_RECORD(DisplayCaptureSync, micros() - syncStartUs);
    };

    // Auto-follow moves the paint window without bumping playbackRevision. Rebuild the
    // committed layer when the recorded gather no longer covers the paint window. A non-empty
    // visualCache can refresh by filter even while globally dirty — do not freeze on the
    // previous 16-bar source-view frame.
    const bool committedWindowStale =
        track.isOverdubbing() && havePaintWindow && liveWindowGatherValid_ &&
        !DisplayWindowUtils::paintWindowInsideGather(paintWindowStart, paintWindowLength,
                                                     liveWindowGatherStart_,
                                                     liveWindowGatherLength_) &&
        !loop.visualCache.notes.empty();

    // Idle finished recovering the cache while the committed layer was held — rebuild once now
    // that the clean branch is affordable.
    const bool committedLayerCleanCacheReady =
        track.isOverdubbing() && liveCommittedLayerHeldForDirtyCache_ && !loop.visualCacheDirty &&
        !loop.visualCache.notes.empty();
    const bool committedLayerVisualCacheGrew =
        track.isOverdubbing() &&
        DisplayWindowUtils::shouldAssignGrownVisualCacheOverOverdubHold(
            loop.visualCache.notes.size(), liveDisplayCacheCommittedNoteCount_,
            liveDisplayNotes.size());

    const bool committedLayerPromoteToFullVisualCache =
        track.isOverdubbing() && !loop.hasOverdubSourceView() &&
        DisplayWindowUtils::shouldPromoteOverdubCommittedToFullVisualCache(
            true, loop.visualCacheDirty, !loop.visualCache.notes.empty(),
            liveDisplayCommittedFromWindowGather_);

    const bool committedLayerChanged =
        cacheCold || contextChanged || loopLengthChanged || committedWindowStale ||
        committedLayerPromoteToFullVisualCache || committedLayerCleanCacheReady ||
        committedLayerVisualCacheGrew ||
        (track.isOverdubbing() && liveMergePlaybackRevision_ != loop.playbackRevision) ||
        (track.isOverdubbing() && loop.hasOverdubSourceView() &&
         liveOverdubSourceViewNoteCount_ != loop.overdubSourceViewNotes().size());
    const bool captureLayerChanged =
        cacheCold || contextChanged || eventsShrunk || captureRevisionChanged ||
        capturePreviewChanged;

    const uint32_t composeStartUs = micros();
    // Safety: if already over budget and we have a prior valid frame, skip heavy compose
    // and reuse last valid liveDisplayNotes (never fall back to full-loop rebuild).
    const bool reuseLastValidFrame =
        !cacheCold && budgetExceeded() && liveDisplayCacheCommittedNoteCount_ +
                                                  liveDisplayCacheCaptureNoteCount_ >
                                              0;

    if (reuseLastValidFrame && committedWindowStale) {
        // Over-budget compose must still advance the committed window.
        rebuildOverdubCommittedDisplayLayer(track, mutLoop, loop, displaySlot, currentTick,
                                            liveLoopLength, havePaintWindow, paintWindowStart,
                                            paintWindowLength);
        if (track.isOverdubbing()) {
            synchronizeCaptureLayer();
        } else {
            trimToComposeCounts();
        }
        DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
    } else if (reuseLastValidFrame) {
        if (captureLayerChanged && track.isOverdubbing()) {
            synchronizeCaptureLayer();
        }
    } else if (committedLayerChanged) {
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        // Timed here rather than inside the member: it returns early on the overdub path.
        rebuildOverdubCommittedDisplayLayer(track, mutLoop, loop, displaySlot, currentTick,
                                            liveLoopLength, havePaintWindow, paintWindowStart,
                                            paintWindowLength);
        DIAG_TIMING_RECORD(DisplayCommittedRebuild, micros() - displayBuildStartUs);
        if (!budgetExceeded()) {
            replaceCaptureLayer();
        } else if (track.isOverdubbing()) {
            // 104129 / 111343: over-budget committed rebuild must still mirror capture.
            synchronizeCaptureLayer();
            DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
        } else {
            trimToComposeCounts();
            DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
        }
        DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    } else if (captureLayerChanged) {
        if (!budgetExceeded()) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            synchronizeCaptureLayer();
        } else if (track.isOverdubbing()) {
            // 104129: mirror capture on over-budget overdub frames without full compose.
            synchronizeCaptureLayer();
            DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
        } else {
            DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
        }
    } else if (liveDisplayCacheBaseNoteCount_ <= liveDisplayNotes.size()) {
        resizeLiveDisplayNotesNoGrow(liveDisplayCacheBaseNoteCount_);
    }
    DIAG_TIMING_RECORD(DisplayCaptureCompose, micros() - composeStartUs);

    liveDisplayCacheSlot = displaySlot;
    liveDisplayCacheTrackState = liveTrackState;
    liveDisplayCacheLoopLength = liveLoopLength;
    liveDisplayCacheEventCount = eventCount;
    liveDisplayCacheCaptureRevision = loop.captureDisplayRevision;
    livePlaybackDisplaySlot_ = displaySlot;
    livePlaybackDisplayTrack_ = resolveTrackIndex(track);
    const size_t committedDisplayEnd = liveDisplayCacheCommittedNoteCount_;
    if (track.isOverdubbing()) {
        size_t recoverEnd = committedDisplayEnd;
        if (loop.hasPendingNoteChanges()) {
            recoverMissingPendingShortenSources(loop, liveDisplayNotes, recoverEnd);
        }
        if (loop.hasSealedCompanionOverlay()) {
            recoverMissingSealedShortenSources(loop, liveDisplayNotes, recoverEnd);
        }
    }
    const bool applyLiveCaptureTails = DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
        track.isRecording(), track.isOverdubbing(), reuseLastValidFrame, budgetExceeded(),
        !loop.capturePreviewView().openNoteIndices.empty());
    const bool haveOpenPlayheadTails = applyLiveCaptureTails;
    const bool havePendingPaint =
        track.isOverdubbing() &&
        (loop.hasPendingNoteChanges() || loop.hasSealedCompanionOverlay());

    // Stage 1j: compose base stays at capture-preview lengths (open and closed).
    // Playhead tails write the paint copy only.
    const size_t previewCount = loop.capturePreviewView().notes.size();
    for (size_t previewNoteIndex = 0; previewNoteIndex < previewCount; ++previewNoteIndex) {
        const size_t displayNoteIndex = committedDisplayEnd + previewNoteIndex;
        if (displayNoteIndex < liveDisplayCacheBaseNoteCount_ &&
            displayNoteIndex < liveDisplayNotes.size()) {
            liveDisplayNotes[displayNoteIndex] = loop.capturePreviewView().notes[previewNoteIndex];
        }
    }

    const DisplayNoteVec* paintedNotes = &liveDisplayNotes;
    if (haveOpenPlayheadTails || havePendingPaint) {
        liveDisplayPendingPaintNotes_ = liveDisplayNotes;
        if (havePendingPaint) {
            // Pending transforms must run before tail growth. Otherwise a closed
            // committed row at the same pitch+start can absorb the incoming open
            // hold update and then be removed by Hide/Shorten on the same frame
            // (`004710`: long incoming note appears only after NoteOff).
            loop.applyPendingNoteChangesToDisplayNotes(liveDisplayPendingPaintNotes_,
                                                       committedDisplayEnd);
            emitPaintedPendingShortenDiagnostics(loop, liveDisplayNotes,
                                                 liveDisplayPendingPaintNotes_);
        }
        if (haveOpenPlayheadTails) {
            const uint32_t tailsStartUs = micros();
            const uint32_t playheadCloseTick =
                resolvePlayheadInLoop(track, displaySlot, currentTick);
            // Growing RECORD has no sealed loop wrap. OVERDUBBING+PLAYING enables
            // wrap continuation via shouldAllowCapturePlayheadWrapContinuation
            // (`175831` pitch 18 @2095 → head@6).
            const bool allowWrapContinuation =
                DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
                    track.isRecording(), track.isAudiblePlaybackActive(),
                    track.isOverdubbing());
            applyCapturePlayheadTails(loop.capturePreviewView(), liveLoopLength, playheadCloseTick,
                                      committedDisplayEnd, liveDisplayPendingPaintNotes_,
                                      allowWrapContinuation);
            DIAG_TIMING_RECORD(DisplayCaptureTails, micros() - tailsStartUs);
        }
        paintedNotes = &liveDisplayPendingPaintNotes_;
    }

    const uint32_t resolveElapsedUs = micros() - resolveStartUs;
    DIAG_TIMING_RECORD(DisplayResolveLiveCapture, resolveElapsedUs);
    if (resolveElapsedUs > Diagnostics::kDisplayResolveBudgetMicros) {
        DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
#if defined(SESSION_CAPTURE)
        // Tier-A DIAG so over-budget resolve remains visible during transport flush.
        static uint32_t sLastOverBudgetLogMs = 0;
        static uint32_t sMaxResolveSinceLogUs = 0;
        if (resolveElapsedUs > sMaxResolveSinceLogUs) {
            sMaxResolveSinceLogUs = resolveElapsedUs;
        }
        const uint32_t nowMs = millis();
        if (sLastOverBudgetLogMs == 0 || (nowMs - sLastOverBudgetLogMs) >= 5000u) {
            sLastOverBudgetLogMs = nowMs;
            DebugSessionCapture::architectureTimingMax("DisplayResolveLiveCapture",
                                                       sMaxResolveSinceLogUs);
            sMaxResolveSinceLogUs = 0;
        }
#endif
    }

    return *paintedNotes;
}
