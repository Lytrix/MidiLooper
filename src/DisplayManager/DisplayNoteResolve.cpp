//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Display note resolve: tick/length helpers, windowed gather, mode dispatch, capture (Phase 2).

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "TrackStateMachine.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteMovementWrap.h"
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

DISP_CAPTURE_MEM const DisplayNoteVec& DisplayManager::resolveDisplayNotes(const Track& track,
                                                                            uint8_t displaySlot,
                                                                            uint32_t currentTick) {
    if (track.isJamming()) {
        const auto& cachedNotes = track.getCachedNotes();
        liveDisplayNotes.assign(cachedNotes.begin(), cachedNotes.end());
        return liveDisplayNotes;
    }

    if (isLiveRecordingDisplay(track, displaySlot)) {
        return resolveDisplayNotesLiveCapture(track, displaySlot, currentTick);
    }

    // NOTE_EDIT: session store (editAware) is the live edit buffer; filter for Hidden / inner overlap.
    if (editManager.getEditSessionType() == EditSessionType::Note) {
        const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
        if (editManager.isNoteEditActive() && loopLength > 0) {
            liveDisplayNotes = editManager.projectedNoteEditDisplayNotes(track);
        } else {
            // NOTE_EDIT mode, session idle: never use getCachedNotes() (active loop). Preview
            // focus can differ from activeLoopIndex while another slot is playing.
            liveDisplayNotes.clear();
        }
        if (liveDisplayNotes.empty() && loopLength > 0 && !editManager.isNoteEditActive()) {
            const Loop& loop = track.getLoop(displaySlot);
            if (loop.hasCommittedPasses() || loop.captureActive()) {
                Loop& mutLoop = const_cast<Loop&>(loop);
                if (!shouldAvoidFullVisualRebuild(loop, loopLength)) {
                    mutLoop.ensureVisualCacheBuilt();
                    liveDisplayNotes.assign(loop.visualCache.notes.begin(),
                                            loop.visualCache.notes.end());
                } else {
                    uint32_t windowStart = 0;
                    uint32_t windowLength = 0;
                    uint8_t windowBars = 0;
                    if (syncDetailedPaintWindow(track, displaySlot, currentTick, loopLength,
                                                windowStart, windowLength, windowBars)) {
                        return resolveWindowedDisplayNotes(track, mutLoop, loop, displaySlot,
                                                           loopLength, windowStart, windowLength);
                    }
                }
            }
        }
        return liveDisplayNotes;
    }

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

DISP_CAPTURE_MEM void DisplayManager::emitDisplayCaptureSnapshot(const Track& track, uint8_t displaySlot,
                                                uint32_t currentTick,
                                                const DisplayNoteVec& frameNotes) {
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLen = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const size_t bufferEventsExpr =
        (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size() : frameNotes.size();
    const uint32_t boundedThreshold =
        DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
    if (!track.isJamming() && loopLen > boundedThreshold && displaySlot < kDisplaySlotCount) {
        const uint8_t windowBars =
            std::min<uint8_t>(detailedWindowBars_[displaySlot],
                              DisplayWindowUtils::kMaxDetailedWindowBars);
        const uint32_t windowStart = detailedWindowStartTick_[displaySlot];
        const uint32_t windowLength = static_cast<uint32_t>(windowBars) * Config::TICKS_PER_BAR;
        const DisplayNoteVec windowNotes = DisplayWindowUtils::filterDisplayNotesToWindow(
            frameNotes, windowStart, windowLength, loopLen);
        SC_DISP_WINDOW(displaySlot, TrackStateMachine::toString(track.getState()), loopLen,
                       bufferEventsExpr, loop.visualCache.notes.size(), frameNotes.size(),
                       bufferEventsExpr, loop.hasCommittedPasses() ? 1 : 0, windowStart, windowBars,
                       windowNotes.size());
        return;
    }

    SC_DISP(displaySlot, TrackStateMachine::toString(track.getState()), loopLen,
            bufferEventsExpr, loop.visualCache.notes.size(), frameNotes.size(),
            bufferEventsExpr, loop.hasCommittedPasses() ? 1 : 0);
}

DISP_CAPTURE_MEM void DisplayManager::emitDisplayCaptureSnapshot(const Track& track, uint8_t displaySlot,
                                                uint32_t currentTick) {
    const DisplayNoteVec& frameNotes = resolveDisplayNotes(track, displaySlot, currentTick);
    emitDisplayCaptureSnapshot(track, displaySlot, currentTick, frameNotes);
}

DISP_CAPTURE_MEM void DisplayManager::maybeEmitDisplayCaptureOnChange(const Track& track, uint8_t displaySlot,
                                                     uint32_t currentTick,
                                                     const DisplayNoteVec& frameNotes) {
    static size_t lastFrameNotes = static_cast<size_t>(-1);
    static uint8_t lastSlot = 255;
    static TrackState lastState = NUM_TRACK_STATES;
    static uint32_t lastLoopLen = 0;
    static size_t lastSourceEventCount = static_cast<size_t>(-1);

    const Loop& loop = track.getLoop(displaySlot);
    const TrackState state = track.getState();
    const uint32_t loopLen = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const size_t frameNoteCount = frameNotes.size();
    const size_t sourceEventCount =
        (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size() : frameNoteCount;

    const bool changed = frameNoteCount != lastFrameNotes || displaySlot != lastSlot ||
                         state != lastState || loopLen != lastLoopLen ||
                         sourceEventCount != lastSourceEventCount;
    const bool regression =
        loopLen > 0 && loop.hasCommittedPasses() && frameNoteCount == 0 && sourceEventCount > 0;

    if (!changed && !regression) {
        return;
    }

    if (regression) {
        static uint32_t lastRegressionCaptureMs = 0;
        const uint32_t nowMs = millis();
        if (nowMs - lastRegressionCaptureMs < 500U) {
            return;
        }
        lastRegressionCaptureMs = nowMs;
    }

    lastFrameNotes = frameNoteCount;
    lastSlot = displaySlot;
    lastState = state;
    lastLoopLen = loopLen;
    lastSourceEventCount = sourceEventCount;
    emitDisplayCaptureSnapshot(track, displaySlot, currentTick, frameNotes);
}
