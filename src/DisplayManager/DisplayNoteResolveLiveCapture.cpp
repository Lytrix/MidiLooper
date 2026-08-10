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

std::vector<NoteUtils::OpenNoteOn> findCaptureOpenNoteOnsFromPreview(const Loop& loop) {
    if (loop.loopLengthTicks == 0) {
        return {};
    }
    std::vector<NoteUtils::OpenNoteOn> opens;
    opens.reserve(loop.capturePreview.notes.size());
    for (const NoteUtils::DisplayNote& note : loop.capturePreview.notes) {
        if (note.endTick != note.startTick) {
            continue;
        }
        opens.push_back(
            NoteUtils::OpenNoteOn{note.note, note.velocity, note.startTick});
    }
    return opens;
}

void copySortedCaptureEvents(const Loop& loop, SessionMidiEventVec& out) {
    if (loop.capture.store.empty()) {
        out.clear();
        return;
    }
    Loop& mutLoop = const_cast<Loop&>(loop);
    mutLoop.ensureCaptureEventsSorted();
    loop.capture.store.copyEventsTo(out);
}

void applyCapturePlayheadTails(const std::vector<NoteUtils::OpenNoteOn>& captureOpens,
                               const SessionMidiEventVec& captureEvents, uint32_t loopLength,
                               uint32_t closeTick, size_t captureRegionStart,
                               DisplayNoteVec& notes) {
    const uint32_t clampedCloseTick = clampOpenNoteCloseTick(closeTick, loopLength);

    for (const auto& open : captureOpens) {
        const bool isWrapHeld =
            NoteUtils::isWrapHeldOpenNote(captureEvents, open, loopLength);
        if (shouldSplitLiveWrapOpenNoteDisplay(open, loopLength, clampedCloseTick, true,
                                               isWrapHeld)) {
            uint32_t tailEnd = loopLength - 1;
            if (clampedCloseTick >= open.tick) {
                tailEnd = std::min(clampedCloseTick, loopLength - 1);
            }

            uint32_t headOffTick = 0;
            const bool hasCommittedHead =
                isWrapHeld &&
                findPreferredWrapHeadOffTick(captureEvents, open, loopLength, headOffTick);
            const NoteUtils::WrapHeadSegment head = resolveWrapOpenHeadSegment(
                loopLength, open, clampedCloseTick, hasCommittedHead, headOffTick);
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

void applyRecordingPreviewOpenTails(DisplayNoteVec& notes, uint32_t loopLength,
                                    uint32_t closeTick) {
    const uint32_t clampedCloseTick = clampOpenNoteCloseTick(closeTick, loopLength);
    for (DisplayNote& note : notes) {
        if (note.endTick != note.startTick) {
            continue;
        }
        note.endTick = std::max(note.startTick, clampedCloseTick);
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
    size_t committedDisplayEnd = 0;
    Loop& mutLoop = const_cast<Loop&>(loop);

    auto rebuildLiveDisplayNotes = [&]() {
        if (track.isOverdubbing()) {
            if (!loop.visualCache.notes.empty()) {
                liveDisplayNotes.assign(loop.visualCache.notes.begin(),
                                        loop.visualCache.notes.end());
                liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
            } else if (loop.hasCommittedPasses()) {
                if (shouldAvoidFullVisualRebuild(loop, liveLoopLength)) {
                    uint32_t windowStart = 0;
                    uint32_t windowLength = 0;
                    uint8_t windowBars = 0;
                    if (syncDetailedPaintWindow(track, displaySlot, currentTick, liveLoopLength,
                                                windowStart, windowLength, windowBars)) {
                        const uint8_t trackIndex = resolveTrackIndex(track);
                        const uint32_t marginTicks =
                            static_cast<uint32_t>(kWindowedGatherMarginBars) * Config::TICKS_PER_BAR;
                        uint32_t gatherStart =
                            windowStart > marginTicks ? windowStart - marginTicks : 0;
                        uint32_t gatherEnd = windowStart + windowLength + marginTicks;
                        if (gatherEnd > liveLoopLength) {
                            gatherEnd = liveLoopLength;
                        }
                        if (gatherStart > gatherEnd) {
                            gatherStart = 0;
                        }
                        const uint32_t gatherLength = gatherEnd - gatherStart;
                        const bool windowCacheHit =
                            liveWindowGatherValid_ && displaySlot == livePlaybackDisplaySlot_ &&
                            trackIndex == livePlaybackDisplayTrack_ &&
                            liveMergePlaybackRevision_ == loop.playbackRevision &&
                            liveMergeCaptureRevision_ == loop.captureDisplayRevision &&
                            liveWindowGatherLoopLength_ == liveLoopLength && gatherLength > 0 &&
                            liveWindowGatherLength_ > 0 && windowStart >= liveWindowGatherStart_ &&
                            (windowStart - liveWindowGatherStart_) + windowLength <=
                                liveWindowGatherLength_ &&
                            liveDisplayCacheCommittedNoteCount_ <= liveDisplayNotes.size();
                        if (windowCacheHit) {
                            liveDisplayNotes.resize(liveDisplayCacheCommittedNoteCount_);
                        } else {
                            rebuildDisplayNotesInWindow(mutLoop, loop, liveLoopLength, gatherStart,
                                                        gatherLength, liveDisplayEventBuffer,
                                                        liveDisplayNotes);
                            liveMergePlaybackRevision_ = loop.playbackRevision;
                            liveMergeCaptureRevision_ = loop.captureDisplayRevision;
                            livePlaybackDisplaySlot_ = displaySlot;
                            livePlaybackDisplayTrack_ = trackIndex;
                            liveWindowGatherStart_ = gatherStart;
                            liveWindowGatherLength_ = gatherLength;
                            liveWindowGatherLoopLength_ = liveLoopLength;
                            liveWindowGatherValid_ = true;
                            liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                        }
                    } else {
                        liveDisplayNotes.clear();
                        liveDisplayCacheCommittedNoteCount_ = 0;
                    }
                } else {
                    mutLoop.ensureVisualCacheBuilt();
                    liveDisplayNotes.assign(loop.visualCache.notes.begin(),
                                            loop.visualCache.notes.end());
                    liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
                }
            } else {
                liveDisplayNotes.clear();
                liveDisplayCacheCommittedNoteCount_ = 0;
            }
            committedDisplayEnd = liveDisplayNotes.size();
            liveDisplayNotes.insert(liveDisplayNotes.end(), loop.capturePreview.notes.begin(),
                                    loop.capturePreview.notes.end());
            return;
        }

        liveDisplayNotes.assign(loop.capturePreview.notes.begin(), loop.capturePreview.notes.end());
        committedDisplayEnd = 0;
        if (liveDisplayNotes.empty() && track.isRecording() && !loop.capture.store.empty()) {
            SessionMidiEventVec captureFlat;
            copySortedCaptureEvents(loop, captureFlat);
            if (!captureFlat.empty()) {
                const NoteUtils::DisplayNoteVec reconstructed =
                    NoteUtils::reconstructDisplayNotes(captureFlat, liveLoopLength, false);
                liveDisplayNotes.assign(reconstructed.begin(), reconstructed.end());
            }
        }
    };

    const bool needsFullLiveRebuild = cacheCold || contextChanged || eventsShrunk || eventsAdded ||
                                      loopLengthChanged || captureRevisionChanged;

    if (liveLoopLength > DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR &&
        displaySlot < kDisplaySlotCount) {
        uint32_t windowStartScratch = 0;
        uint32_t windowLengthScratch = 0;
        uint8_t windowBarsScratch = 0;
        syncDetailedPaintWindow(track, displaySlot, currentTick, liveLoopLength, windowStartScratch,
                                windowLengthScratch, windowBarsScratch);
    }

    if (needsFullLiveRebuild) {
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayFullRebuild);
        if (track.isOverdubbing()) {
            const bool useWindowedCommitLayer =
                loop.visualCache.notes.empty() && loop.hasCommittedPasses() &&
                shouldAvoidFullVisualRebuild(loop, liveLoopLength);
            if (!useWindowedCommitLayer && liveDisplayEventBuffer.empty()) {
                if (loop.captureActive()) {
                    mutLoop.gatherCommittedEventsWithCapture(liveDisplayEventBuffer);
                } else {
                    mutLoop.mergeActiveCapturePasses(liveDisplayEventBuffer);
                }
            }
        } else {
            liveDisplayEventBuffer.clear();
        }
        rebuildLiveDisplayNotes();
        if (track.isOverdubbing()) {
            liveDisplayCacheOpenNotes =
                NoteUtils::findOpenNoteOns(liveDisplayEventBuffer, liveLoopLength);
        } else {
            liveDisplayCacheOpenNotes.clear();
        }
        liveDisplayCacheSlot = displaySlot;
        liveDisplayCacheTrackState = liveTrackState;
        liveDisplayCacheLoopLength = liveLoopLength;
        liveDisplayCacheEventCount = eventCount;
        liveDisplayCacheCaptureRevision = loop.captureDisplayRevision;
        DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    } else {
        liveDisplayCacheLoopLength = liveLoopLength;
        if (track.isRecording() || track.isOverdubbing()) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            rebuildLiveDisplayNotes();
        }
    }

    if (track.isRecording() || track.isOverdubbing()) {
        const uint32_t playheadCloseTick = resolvePlayheadInLoop(track, displaySlot, currentTick);
        if (track.isOverdubbing()) {
            const std::vector<NoteUtils::OpenNoteOn> captureOpens =
                findCaptureOpenNoteOnsFromPreview(loop);
            if (!captureOpens.empty()) {
                SessionMidiEventVec captureEvents;
                copySortedCaptureEvents(loop, captureEvents);
                applyCapturePlayheadTails(captureOpens, captureEvents, liveLoopLength,
                                          playheadCloseTick, committedDisplayEnd, liveDisplayNotes);
            }
        } else {
            applyRecordingPreviewOpenTails(liveDisplayNotes, liveLoopLength, playheadCloseTick);
        }
    }

    return liveDisplayNotes;
}
