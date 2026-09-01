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
#include <cstdio>

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#define DISP_CAPTURE_MEM FLASHMEM
#else
#define DISP_COLD_MEM
#define DISP_CAPTURE_MEM
#endif

using namespace DisplayManagerInternal;

DISP_CAPTURE_MEM const DisplayNoteVec& DisplayManager::resolveDisplayNotes(const Track& track,
                                                                            uint8_t displaySlot,
                                                                            uint32_t currentTick) {
    if (track.isJamming()) {
        const auto& cachedNotes = track.getCachedNotes();
        liveDisplayNotes.assign(cachedNotes.begin(), cachedNotes.end());
        return liveDisplayNotes;
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
                uint32_t windowStart = 0;
                uint32_t windowLength = 0;
                uint8_t windowBars = 0;
                if (syncDetailedPaintWindow(track, displaySlot, currentTick, loopLength,
                                            windowStart, windowLength, windowBars)) {
                    return resolveWindowedDisplayNotes(track, mutLoop, loop, displaySlot,
                                                       loopLength, windowStart, windowLength);
                }
                // Slice 4d: do not ensureVisualCacheBuilt. Session type can be
                // Note before openNoteEditSession sets active (overdub-stop paint).
                // Idle owns rebuild. Consume existing notes (stale or empty).
                liveDisplayNotes.assign(loop.visualCache.notes.begin(),
                                        loop.visualCache.notes.end());
            }
        }
        return liveDisplayNotes;
    }
    return resolveDisplayNotesByDrawSource(track, displaySlot, currentTick);
}

DISP_CAPTURE_MEM void DisplayManager::emitDisplayCaptureSnapshot(const Track& track, uint8_t displaySlot,
                                                uint32_t currentTick,
                                                const DisplayNoteVec& frameNotes) {
#if defined(SESSION_CAPTURE) && SESSION_CAPTURE_DRAW_SRC
    logDrawSourceComposeDifferential(track, displaySlot, currentTick, frameNotes);
#endif
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLen = resolveDisplayLoopLength(track, displaySlot, currentTick);
    const size_t bufferEventsExpr =
        (track.isRecording() && !track.isPlaying()) ? loop.capture.store.size() : frameNotes.size();
#if defined(SESSION_CAPTURE)
    if (track.isStopped()) {
        queueStoppedFrameNoteDump(frameNotes, loop.loopLengthTicks, loop.loopStartTick);
    }
#endif
    uint32_t windowStart = 0;
    uint32_t windowLength = 0;
    uint8_t windowBars = 0;
    if (syncDetailedPaintWindow(track, displaySlot, currentTick, loopLen, windowStart,
                                windowLength, windowBars)) {
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

#if defined(SESSION_CAPTURE)
DISP_CAPTURE_MEM void DisplayManager::queueStoppedFrameNoteDump(const DisplayNoteVec& frameNotes,
                                                                uint32_t loopLength,
                                                                uint32_t loopStartTick) {
    if (!stoppedFrameNoteDumpArmed_ || frameNotes.empty()) {
        return;
    }
    stoppedFrameNoteDumpArmed_ = false;
    stoppedFrameNoteDump_ = frameNotes;
    stoppedFrameNoteDumpCursor_ = 0;
    stoppedFrameNoteDumpLoopLength_ = loopLength;
    stoppedFrameNoteDumpLoopStart_ = loopStartTick;
    stoppedFrameNoteDumpHeaderEmitted_ = false;
}

DISP_CAPTURE_MEM size_t DisplayManager::emitStoppedFrameNoteDump(size_t maxLines) {
    if (stoppedFrameNoteDump_.empty()) {
        return 0;
    }
    if (!stoppedFrameNoteDumpHeaderEmitted_) {
        char header[128];
        snprintf(header, sizeof(header),
                 "#CAP,%lu,DIAG,dnte,src,frame,count,%u",
                 static_cast<unsigned long>(micros()),
                 static_cast<unsigned>(stoppedFrameNoteDump_.size()));
        DebugSessionCapture::appendCaptureTextLine(header);
        stoppedFrameNoteDumpHeaderEmitted_ = true;
    }
    size_t emitted = 0;
    while (stoppedFrameNoteDumpCursor_ < stoppedFrameNoteDump_.size() && emitted < maxLines) {
        const DisplayNote& note = stoppedFrameNoteDump_[stoppedFrameNoteDumpCursor_];
        const uint32_t displayStart = IntervalProjection::noteRelativeTick(
            note.startTick, stoppedFrameNoteDumpLoopStart_, stoppedFrameNoteDumpLoopLength_);
        uint32_t length = 0;
        if (stoppedFrameNoteDumpLoopLength_ > 0 && note.endTick >= note.startTick) {
            const uint32_t endExclusive =
                (note.endTick == stoppedFrameNoteDumpLoopLength_ - 1) ? stoppedFrameNoteDumpLoopLength_
                                                                      : note.endTick;
            length = endExclusive > note.startTick ? (endExclusive - note.startTick) : 0;
        }
        SC_DNTE(note.note, note.startTick, displayStart, length,
                static_cast<int>(stoppedFrameNoteDumpCursor_));
        ++stoppedFrameNoteDumpCursor_;
        ++emitted;
    }
    if (stoppedFrameNoteDumpCursor_ >= stoppedFrameNoteDump_.size()) {
        stoppedFrameNoteDump_.clear();
        stoppedFrameNoteDumpCursor_ = 0;
    }
    return emitted;
}
#endif

#if defined(SESSION_CAPTURE) && SESSION_CAPTURE_DRAW_SRC
DISP_CAPTURE_MEM void DisplayManager::logDrawSourceComposeDifferential(
    const Track& track, uint8_t displaySlot, uint32_t currentTick,
    const DisplayNoteVec& drawnNotes) {
    (void)currentTick;
    if (track.isJamming() || editManager.getEditSessionType() == EditSessionType::Note) {
        return;
    }
    const Loop& loop = track.getLoop(displaySlot);
    if (!track.isOverdubbing() && !loop.captureActive()) {
        return;
    }

    static uint32_t sLastDiffLogMs = 0;
    const uint32_t nowMs = millis();
    if (sLastDiffLogMs != 0 && (nowMs - sLastDiffLogMs) < 250u) {
        return;
    }
    sLastDiffLogMs = nowMs;

    auto findByNoteId = [](const DisplayNoteVec& notes, NoteId noteId) -> const DisplayNote* {
        if (noteId == kInvalidNoteId) {
            return nullptr;
        }
        for (const DisplayNote& note : notes) {
            if (note.noteId == noteId) {
                return &note;
            }
        }
        return nullptr;
    };
    auto findByPitchStart = [](const DisplayNoteVec& notes, uint8_t pitch,
                               uint32_t startTick) -> const DisplayNote* {
        for (const DisplayNote& note : notes) {
            if (note.note == pitch && note.startTick == startTick) {
                return &note;
            }
        }
        return nullptr;
    };
    auto emitDiff = [](uint32_t bar, NoteId noteId, const DisplayNote* oldNote,
                       const DisplayNote* newNote) {
        char line[192];
        snprintf(line, sizeof(line),
                 "#CAP,%lu,DIAG,draw_src,diff,bar,%u,id,%u,old,%u,%u,%u,new,%u,%u,%u",
                 static_cast<unsigned long>(micros()), static_cast<unsigned>(bar),
                 static_cast<unsigned>(noteId),
                 oldNote != nullptr ? static_cast<unsigned>(oldNote->note) : 0u,
                 oldNote != nullptr ? static_cast<unsigned>(oldNote->startTick) : 0u,
                 oldNote != nullptr ? static_cast<unsigned>(oldNote->endTick) : 0u,
                 newNote != nullptr ? static_cast<unsigned>(newNote->note) : 0u,
                 newNote != nullptr ? static_cast<unsigned>(newNote->startTick) : 0u,
                 newNote != nullptr ? static_cast<unsigned>(newNote->endTick) : 0u);
        DebugSessionCapture::appendCaptureTextLine(line);
    };

    uint32_t mismatchCount = 0;
    uint32_t hiddenCount = 0;
    uint32_t missedPreviewCount = 0;
    uint32_t logged = 0;
    constexpr uint32_t kMaxLoggedDiffs = 8;
    const uint32_t ticksPerBar = Config::TICKS_PER_BAR;

    for (const DisplayNote& visual : loop.visualCache.notes) {
        if (visual.noteId == kInvalidNoteId) {
            continue;
        }
        const DisplayNote* byId = findByNoteId(drawnNotes, visual.noteId);
        if (byId != nullptr && byId->note == visual.note && byId->startTick == visual.startTick &&
            byId->endTick == visual.endTick) {
            continue;
        }
        const DisplayNote* byPitchStart = (byId == nullptr)
                                              ? findByPitchStart(drawnNotes, visual.note,
                                                                 visual.startTick)
                                              : byId;
        if (byPitchStart != nullptr && byPitchStart->note == visual.note &&
            byPitchStart->startTick == visual.startTick &&
            byPitchStart->endTick == visual.endTick) {
            continue;
        }
        ++mismatchCount;
        if (byPitchStart == nullptr) {
            ++hiddenCount;
        }
        if (logged < kMaxLoggedDiffs) {
            const uint32_t bar = ticksPerBar > 0 ? visual.startTick / ticksPerBar : 0;
            emitDiff(bar, visual.noteId, &visual, byPitchStart);
            ++logged;
        }
    }

    for (const DisplayNote& preview : loop.capturePreview.notes) {
        if (preview.noteId == kInvalidNoteId) {
            continue;
        }
        if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(preview.startTick, preview.endTick)) {
            continue;
        }
        const DisplayNote* byId = findByNoteId(drawnNotes, preview.noteId);
        const DisplayNote* byPitchStart =
            (byId != nullptr) ? byId : findByPitchStart(drawnNotes, preview.note, preview.startTick);
        if (byPitchStart != nullptr && byPitchStart->note == preview.note &&
            byPitchStart->startTick == preview.startTick &&
            byPitchStart->endTick == preview.endTick) {
            continue;
        }
        ++mismatchCount;
        ++missedPreviewCount;
        if (logged < kMaxLoggedDiffs) {
            const uint32_t bar = ticksPerBar > 0 ? preview.startTick / ticksPerBar : 0;
            emitDiff(bar, preview.noteId, findByPitchStart(loop.visualCache.notes, preview.note,
                                                           preview.startTick),
                     &preview);
            ++logged;
        }
    }

    char layers[192];
    snprintf(layers, sizeof(layers),
             "#CAP,%lu,DIAG,draw_src,layers,visual,%u,preview,%u,sealed,%u,snap,%u,frame,%u,"
             "hidden,%u,missed_preview,%u",
             static_cast<unsigned long>(micros()),
             static_cast<unsigned>(loop.visualCache.notes.size()),
             static_cast<unsigned>(loop.capturePreview.notes.size()),
             static_cast<unsigned>(loop.sealedCapturePreviewNotes().size()),
             loop.publishedSnapshotIsCurrent() ? 1u : 0u,
             static_cast<unsigned>(drawnNotes.size()), static_cast<unsigned>(hiddenCount),
             static_cast<unsigned>(missedPreviewCount));
    DebugSessionCapture::appendCaptureTextLine(layers);

    if (mismatchCount > 0) {
        char summary[128];
        snprintf(summary, sizeof(summary),
                 "#CAP,%lu,DIAG,draw_src,diff,count,%u,old,%u,new,%u",
                 static_cast<unsigned long>(micros()), static_cast<unsigned>(mismatchCount),
                 static_cast<unsigned>(loop.visualCache.notes.size()),
                 static_cast<unsigned>(drawnNotes.size()));
        DebugSessionCapture::appendCaptureTextLine(summary);
    }
}
#endif
