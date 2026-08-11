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

        const uint32_t playheadEndTick = std::max(open.tick, clampedCloseTick);
        if (!updateDisplayNoteEndFrom(notes, captureRegionStart, open.note, open.tick,
                                      playheadEndTick)) {
            DisplayNote liveNote;
            liveNote.note = open.note;
            liveNote.velocity = open.velocity;
            liveNote.startTick = open.tick;
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

    auto rebuildCommittedLayer = [&]() {
        if (track.isOverdubbing()) {
            // Only a fully built visualCache may back the committed layer. A partial idle
            // backfill (PLAYING neighborhood slices) is sparse and shows as gaps in the roll.
            if (!loop.visualCacheDirty && !loop.visualCache.notes.empty()) {
                liveDisplayNotes.assign(loop.visualCache.notes.begin(),
                                        loop.visualCache.notes.end());
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                liveWindowGatherValid_ = false;
                liveDisplayCommittedFromWindowGather_ = false;
            } else if (loop.hasCommittedPasses()) {
                if (shouldAvoidFullVisualRebuild(loop, liveLoopLength)) {
                    // Full committed span — drawPianoRoll owns rolling-window filter (RC4g).
                    // Narrow window gather revealed chunks gradually (165148); stale dirty
                    // visualCache hid the fresh record pass (170314).
                    rebuildDisplayNotesInWindow(mutLoop, loop, liveLoopLength, 0, liveLoopLength,
                                                liveDisplayEventBuffer, liveDisplayNotes, false);
                    liveWindowGatherValid_ = false;
                    liveDisplayCommittedFromWindowGather_ = false;
                    liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                } else {
                    mutLoop.ensureVisualCacheBuilt();
                    liveDisplayNotes.assign(loop.visualCache.notes.begin(),
                                            loop.visualCache.notes.end());
                    liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                    liveWindowGatherValid_ = false;
                    liveDisplayCommittedFromWindowGather_ = false;
                }
            } else {
                liveDisplayNotes.clear();
                liveDisplayCacheCommittedNoteCount_ = 0;
                liveWindowGatherValid_ = false;
                liveDisplayCommittedFromWindowGather_ = false;
            }
            liveMergePlaybackRevision_ = loop.playbackRevision;
            return;
        }

        liveDisplayNotes.clear();
        liveDisplayCacheCommittedNoteCount_ = 0;
        liveWindowGatherValid_ = false;
        liveDisplayCommittedFromWindowGather_ = false;
    };

    auto replaceCaptureLayer = [&]() {
        liveDisplayNotes.resize(liveDisplayCacheCommittedNoteCount_);
        liveDisplayNotes.insert(liveDisplayNotes.end(), loop.capturePreview.notes.begin(),
                                loop.capturePreview.notes.end());
        liveDisplayCacheCaptureNoteCount_ = loop.capturePreview.notes.size();
        liveDisplayCacheCaptureChangeCount_ = loop.capturePreview.changedNoteIndices.size();
        liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
        liveDisplayCacheCaptureReplacementRevision_ =
            loop.capturePreview.replacementRevision;
        liveDisplayCacheCapturePreviewRevision_ = loop.capturePreview.revision;
    };

    auto synchronizeCaptureLayer = [&]() {
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
            replaceCaptureLayer();
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
    };

    uint32_t paintWindowStart = 0;
    uint32_t paintWindowLength = 0;
    uint8_t paintWindowBars = 0;
    const bool havePaintWindow =
        liveLoopLength > DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR &&
        displaySlot < kDisplaySlotCount &&
        syncDetailedPaintWindow(track, displaySlot, currentTick, liveLoopLength, paintWindowStart,
                                paintWindowLength, paintWindowBars);
    // Dirty visualCache uses window gather for the committed layer. Auto-follow moves the paint
    // window without bumping playbackRevision — rebuild when the gather no longer covers it
    // (session_20260811_034230: notes stuck in first ~18 bars until overdub stop).
    const bool committedWindowStale =
        track.isOverdubbing() && havePaintWindow && loop.visualCacheDirty &&
        liveWindowGatherValid_ &&
        !DisplayWindowUtils::paintWindowInsideGather(paintWindowStart, paintWindowLength,
                                                     liveWindowGatherStart_,
                                                     liveWindowGatherLength_);

    const bool committedLayerPromoteToFullVisualCache =
        track.isOverdubbing() &&
        DisplayWindowUtils::shouldPromoteOverdubCommittedToFullVisualCache(
            true, loop.visualCacheDirty, !loop.visualCache.notes.empty(),
            liveDisplayCommittedFromWindowGather_);

    const bool committedLayerChanged =
        cacheCold || contextChanged || loopLengthChanged || committedWindowStale ||
        committedLayerPromoteToFullVisualCache ||
        (track.isOverdubbing() && liveMergePlaybackRevision_ != loop.playbackRevision);
    const bool captureLayerChanged =
        cacheCold || contextChanged || eventsShrunk || captureRevisionChanged ||
        capturePreviewChanged;

    const uint32_t composeStartUs = micros();
    if (committedLayerChanged) {
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayFullRebuild);
        rebuildCommittedLayer();
        replaceCaptureLayer();
        DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    } else if (captureLayerChanged) {
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        synchronizeCaptureLayer();
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

    if (track.isRecording() || track.isOverdubbing()) {
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
            // Growing RECORD has no sealed loop length — never infer wrap-head to tick 0.
            const bool allowWrapContinuation =
                !(track.isRecording() && !track.isPlaying());
            applyCapturePlayheadTails(loop.capturePreview, liveLoopLength, playheadCloseTick,
                                      committedDisplayEnd, liveDisplayNotes,
                                      allowWrapContinuation);
        }
        DIAG_TIMING_RECORD(DisplayCaptureTails, micros() - tailsStartUs);
    }

    const uint32_t resolveElapsedUs = micros() - resolveStartUs;
    DIAG_TIMING_RECORD(DisplayResolveLiveCapture, resolveElapsedUs);
    if (resolveElapsedUs > Diagnostics::kDisplayResolveBudgetMicros) {
        DIAG_COUNTER_INC(DisplayResolveOverBudgetCount);
    }

    return liveDisplayNotes;
}
