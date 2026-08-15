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
                               DisplayNoteVec& notes, bool allowWrapContinuation,
                               bool allowWrapHeadContinuation) {
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

            NoteUtils::WrapHeadSegment head{};
            if (allowWrapHeadContinuation) {
                head = resolveWrapOpenHeadSegment(loopLength, open, clampedCloseTick,
                                                  state.hasPreferredHeadOff,
                                                  state.preferredHeadOffTick);
            }
            appendWrapHeldOpenNoteDisplay(notes, captureRegionStart, open, tailEnd, head);
            continue;
        }

        uint32_t noteStartTick = open.tick;
        uint32_t playheadEndTick = std::max(noteStartTick, clampedCloseTick);
        DisplayWindowUtils::clampNonWrapDisplayNoteBarTicks(noteStartTick, playheadEndTick,
                                                            loopLength);
        bool updated = false;
        for (size_t i = captureRegionStart; i < notes.size(); ++i) {
            if (notes[i].note == open.note && notes[i].startTick == open.tick) {
                notes[i].startTick = noteStartTick;
                notes[i].endTick = playheadEndTick;
                updated = true;
                break;
            }
        }
        if (!updated) {
            DisplayNote liveNote;
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
        !cacheCold && loop.capturePreview.revision != liveDisplayCacheCapturePreviewRevision_;
    if (eventsAdded) {
        DIAG_COUNTER_INC(DisplayCaptureEventsAdded);
    }
    Loop& mutLoop = const_cast<Loop&>(loop);

    uint32_t paintWindowStart = 0;
    uint32_t paintWindowLength = 0;
    uint8_t paintWindowBars = 0;
    const bool havePaintWindow =
        liveLoopLength > DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR &&
        displaySlot < kDisplaySlotCount &&
        syncDetailedPaintWindow(track, displaySlot, currentTick, liveLoopLength, paintWindowStart,
                                paintWindowLength, paintWindowBars);

    // Bounded neighborhood when long-loop paint window is not ready yet (still O(window)).
    const uint32_t kMaxCaptureCommittedBars = DisplayWindowUtils::kMaxDetailedWindowBars;
    const uint32_t maxCommittedGatherLength = kMaxCaptureCommittedBars * Config::TICKS_PER_BAR;

    auto budgetExceeded = [&]() {
        return (micros() - resolveStartUs) > Diagnostics::kDisplayResolveBudgetMicros;
    };

    // RC-C B + post follow-up: never reconstruct 0..loopLength on the hot path.
    // Prefer clean visualCache; when dirty, reuse already-sliced cache notes for clean bars
    // inside the paint/gather window and gather only when that window still has dirty bars.
    // Out-of-window progressive material stays in visualCache (idle + overview).
    auto gatherWindowHasDirtyBar = [&](uint32_t gatherStart, uint32_t gatherLength) -> bool {
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
            const uint32_t tick = IntervalProjection::tickPhaseInLoop(start + offset, 0, liveLoopLength);
            const uint32_t bar = visualBarForTick(tick, ticksPerBar);
            if (bar < totalBars && loop.visualCache.dirtyBars[bar] != 0) {
                return true;
            }
        }
        const uint32_t lastTick =
            IntervalProjection::tickPhaseInLoop(start + gatherLength - 1, 0, liveLoopLength);
        const uint32_t lastBar = visualBarForTick(lastTick, ticksPerBar);
        return lastBar < totalBars && loop.visualCache.dirtyBars[lastBar] != 0;
    };

    auto rebuildCommittedLayer = [&]() {
        if (track.isOverdubbing()) {
            // Cache recovery belongs to idle work. Gathering the committed window here costs
            // ~27.9 ms per frame for the whole post-commit dirty window and starves the capture
            // layer, so played notes stop landing (172405: 30 gathers, 70 of 241 frames over
            // budget). Hold the layer we already have; processDeferredIdleMaintenance makes the
            // cache clean in ~0.6-2.3 s and the clean branch then rebuilds it.
            const bool canHoldCommittedLayer =
                liveDisplayCacheCommittedNoteCount_ > 0 &&
                liveDisplayCacheCommittedNoteCount_ <= liveDisplayNotes.size();
            if (loop.visualCacheDirty && canHoldCommittedLayer) {
                liveDisplayNotes.resize(liveDisplayCacheCommittedNoteCount_);
                liveCommittedLayerHeldForDirtyCache_ = true;
                liveMergePlaybackRevision_ = loop.playbackRevision;
                return;
            }
            if (!loop.visualCacheDirty && !loop.visualCache.notes.empty()) {
                if (havePaintWindow) {
                    // RC-F: filter a window wider than the paint window and record it, so
                    // auto-follow slides inside the gather instead of leaving the committed
                    // prefix pinned to a stale position (165636: wNotes 295 -> 213).
                    const uint32_t marginTicks =
                        static_cast<uint32_t>(kWindowedGatherMarginBars) * Config::TICKS_PER_BAR;
                    const uint32_t gatherStart =
                        paintWindowStart > marginTicks ? paintWindowStart - marginTicks : 0;
                    uint32_t gatherEnd = paintWindowStart + paintWindowLength + marginTicks;
                    if (gatherEnd > liveLoopLength) {
                        gatherEnd = liveLoopLength;
                    }
                    const uint32_t gatherLength =
                        gatherEnd > gatherStart ? gatherEnd - gatherStart : paintWindowLength;
                    DIAG_COUNTER_INC(DisplayCommittedWindowFilter);
                    liveDisplayNotes = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(
                        loop.visualCache.notes, gatherStart, gatherLength, liveLoopLength);
                    liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                    liveWindowGatherValid_ = true;
                    liveWindowGatherStart_ = gatherStart;
                    liveWindowGatherLength_ = gatherLength;
                    liveWindowGatherLoopLength_ = liveLoopLength;
                    liveDisplayCommittedFromWindowGather_ = false;
                    liveCommittedLayerHeldForDirtyCache_ = false;
                } else {
                    DIAG_COUNTER_INC(DisplayCommittedFullAssign);
                    liveDisplayNotes.assign(loop.visualCache.notes.begin(),
                                            loop.visualCache.notes.end());
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
                    const uint32_t playhead =
                        resolvePlayheadInLoop(track, displaySlot, currentTick);
                    gatherLength = maxCommittedGatherLength;
                    gatherStart = DisplayWindowUtils::resolveCenteredWindowStart(
                        playhead, gatherLength, liveLoopLength);
                }
                if (!loop.visualCache.notes.empty() &&
                    !gatherWindowHasDirtyBar(gatherStart, gatherLength)) {
                    // Idle already sliced this window — reuse progressive cache (no gather).
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
                    // Short loops: gatherLength == liveLoopLength ≤ 16 bars (work quantum).
                    DIAG_COUNTER_INC(DisplayCaptureFullGather);
                    const uint32_t gatherStartUs = micros();
                    rebuildDisplayNotesInWindow(mutLoop, loop, liveLoopLength, gatherStart,
                                                gatherLength, liveDisplayEventBuffer,
                                                liveDisplayNotes, false);
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
    };

    auto replaceCaptureLayer = [&]() {
        const uint32_t replaceStartUs = micros();
#if defined(SESSION_CAPTURE)
        // Rate-limited Tier-A DIAG when full capture-layer copy runs on a large preview
        // (second-overdub lag investigation).
        constexpr size_t kReplaceCaptureLayerLogThreshold = 256;
        if (loop.capturePreview.notes.size() >= kReplaceCaptureLayerLogThreshold) {
            static uint32_t sLastReplaceCaptureLogMs = 0;
            const uint32_t nowMs = millis();
            if (sLastReplaceCaptureLogMs == 0 || (nowMs - sLastReplaceCaptureLogMs) >= 5000u) {
                sLastReplaceCaptureLogMs = nowMs;
                DebugSessionCapture::architectureTimingMax(
                    "replaceCaptureLayer",
                    static_cast<uint32_t>(loop.capturePreview.notes.size()));
            }
        }
#endif
        liveDisplayNotes.resize(liveDisplayCacheCommittedNoteCount_);
        liveDisplayNotes.insert(liveDisplayNotes.end(), loop.capturePreview.notes.begin(),
                                loop.capturePreview.notes.end());
        liveDisplayCacheCaptureNoteCount_ = loop.capturePreview.notes.size();
        liveDisplayCacheCaptureChangeCount_ = loop.capturePreview.changedNoteIndices.size();
        liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
        liveDisplayCacheCaptureReplacementRevision_ =
            loop.capturePreview.replacementRevision;
        liveDisplayCacheCapturePreviewRevision_ = loop.capturePreview.revision;
        DIAG_TIMING_RECORD(DisplayCaptureReplace, micros() - replaceStartUs);
    };

    auto synchronizeCaptureLayer = [&]() {
        const uint32_t syncStartUs = micros();
        const bool captureMirrorInvalid =
            liveDisplayCacheCaptureReplacementRevision_ !=
                loop.capturePreview.replacementRevision ||
            liveDisplayCacheCaptureNoteCount_ > loop.capturePreview.notes.size() ||
            liveDisplayCacheCaptureChangeCount_ >
                loop.capturePreview.changedNoteIndices.size() ||
            liveDisplayCacheCommittedNoteCount_ + liveDisplayCacheCaptureNoteCount_ >
                liveDisplayCacheBaseNoteCount_ ||
            liveDisplayCacheBaseNoteCount_ > liveDisplayNotes.size();
        if (captureMirrorInvalid) {
            // Nests DisplayCaptureReplace inside this sample.
            replaceCaptureLayer();
            DIAG_TIMING_RECORD(DisplayCaptureSync, micros() - syncStartUs);
            return;
        }

        liveDisplayNotes.resize(liveDisplayCacheBaseNoteCount_);
        for (size_t changeIndex = liveDisplayCacheCaptureChangeCount_;
             changeIndex < loop.capturePreview.changedNoteIndices.size(); ++changeIndex) {
            const size_t previewNoteIndex =
                loop.capturePreview.changedNoteIndices[changeIndex];
            if (previewNoteIndex >= liveDisplayCacheCaptureNoteCount_ ||
                previewNoteIndex >= loop.capturePreview.notes.size()) {
                continue;
            }
            liveDisplayNotes[liveDisplayCacheCommittedNoteCount_ + previewNoteIndex] =
                loop.capturePreview.notes[previewNoteIndex];
        }
        liveDisplayNotes.insert(
            liveDisplayNotes.end(),
            loop.capturePreview.notes.begin() +
                static_cast<std::ptrdiff_t>(liveDisplayCacheCaptureNoteCount_),
            loop.capturePreview.notes.end());
        liveDisplayCacheCaptureNoteCount_ = loop.capturePreview.notes.size();
        liveDisplayCacheCaptureChangeCount_ = loop.capturePreview.changedNoteIndices.size();
        liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
        liveDisplayCacheCapturePreviewRevision_ = loop.capturePreview.revision;
        DIAG_TIMING_RECORD(DisplayCaptureSync, micros() - syncStartUs);
    };

    // Dirty visualCache uses window gather for the committed layer. Auto-follow moves the paint
    // window without bumping playbackRevision — rebuild when the gather no longer covers it
    // (session_20260811_034230: notes stuck in first ~18 bars until overdub stop).
    // RC-F: not gated on visualCacheDirty being true. Once the RC-E fix lets the cache go clean
    // during overdub, a dirty-only predicate can never fire and the committed layer freezes.
    // It is gated on the cache being clean, because a dirty-cache rebuild would gather.
    const bool committedWindowStale =
        track.isOverdubbing() && havePaintWindow && !loop.visualCacheDirty &&
        liveWindowGatherValid_ &&
        !DisplayWindowUtils::paintWindowInsideGather(paintWindowStart, paintWindowLength,
                                                     liveWindowGatherStart_,
                                                     liveWindowGatherLength_);

    // Idle finished recovering the cache while the committed layer was held — rebuild once now
    // that the clean branch is affordable.
    const bool committedLayerCleanCacheReady =
        track.isOverdubbing() && liveCommittedLayerHeldForDirtyCache_ && !loop.visualCacheDirty &&
        !loop.visualCache.notes.empty();

    const bool committedLayerPromoteToFullVisualCache =
        track.isOverdubbing() &&
        DisplayWindowUtils::shouldPromoteOverdubCommittedToFullVisualCache(
            true, loop.visualCacheDirty, !loop.visualCache.notes.empty(),
            liveDisplayCommittedFromWindowGather_);

    const bool committedLayerChanged =
        cacheCold || contextChanged || loopLengthChanged || committedWindowStale ||
        committedLayerPromoteToFullVisualCache || committedLayerCleanCacheReady ||
        (track.isOverdubbing() && liveMergePlaybackRevision_ != loop.playbackRevision);
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

    if (reuseLastValidFrame) {
        // Keep liveDisplayNotes as last valid frame; unfinished committed/capture work stays
        // pending via visualCacheDirty / capturePreview.revision.
    } else if (committedLayerChanged) {
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        // Timed here rather than inside the lambda: it returns early on the overdub path.
        rebuildCommittedLayer();
        DIAG_TIMING_RECORD(DisplayCommittedRebuild, micros() - displayBuildStartUs);
        if (!budgetExceeded()) {
            replaceCaptureLayer();
        } else {
            // Committed window updated; keep prior capture suffix if mirror still valid.
            if (liveDisplayCacheCommittedNoteCount_ + liveDisplayCacheCaptureNoteCount_ <=
                liveDisplayNotes.size()) {
                liveDisplayNotes.resize(liveDisplayCacheCommittedNoteCount_ +
                                        liveDisplayCacheCaptureNoteCount_);
                liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
            } else {
                replaceCaptureLayer();
            }
            DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
        }
        DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    } else if (captureLayerChanged) {
        if (!budgetExceeded()) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            synchronizeCaptureLayer();
        } else {
            DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
        }
    } else if (liveDisplayCacheBaseNoteCount_ <= liveDisplayNotes.size()) {
        liveDisplayNotes.resize(liveDisplayCacheBaseNoteCount_);
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

    if (!reuseLastValidFrame && (track.isRecording() || track.isOverdubbing()) &&
        !budgetExceeded()) {
        const uint32_t tailsStartUs = micros();
        const uint32_t playheadCloseTick = resolvePlayheadInLoop(track, displaySlot, currentTick);
        for (const uint32_t previewNoteIndex : loop.capturePreview.openNoteIndices) {
            if (previewNoteIndex >= loop.capturePreview.notes.size()) {
                continue;
            }
            const size_t displayNoteIndex = committedDisplayEnd + previewNoteIndex;
            if (displayNoteIndex < liveDisplayCacheBaseNoteCount_) {
                liveDisplayNotes[displayNoteIndex] =
                    loop.capturePreview.notes[previewNoteIndex];
            }
        }
        if (!loop.capturePreview.openNoteIndices.empty()) {
            // Growing RECORD has no sealed loop wrap. Overdub session wrap jumps playhead
            // to S; a held tail ON then looks like wrap-head to tick 0 until NoteOff.
            const bool allowWrapContinuation =
                !(track.isRecording() && !track.isPlaying());
            const bool allowWrapHeadContinuation =
                allowWrapContinuation && !track.isOverdubbing();
            applyCapturePlayheadTails(loop.capturePreview, liveLoopLength, playheadCloseTick,
                                      committedDisplayEnd, liveDisplayNotes,
                                      allowWrapContinuation, allowWrapHeadContinuation);
        }
        DIAG_TIMING_RECORD(DisplayCaptureTails, micros() - tailsStartUs);
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

    return liveDisplayNotes;
}
