//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Cold-path display helpers shared across DisplayManager translation units.

#include "DisplayManagerInternal.h"

#include "Loop.h"
#include "LoopContentResolution.h"
#include "StorageManager.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/DebugSessionCapture.h"
#include "VisualCache.h"
#include <algorithm>
#include <Arduino.h>

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#else
#define DISP_COLD_MEM
#endif

namespace DisplayManagerInternal {

DMAMEM SessionMidiEventVec liveDisplayEventBuffer;

bool shouldAvoidFullVisualRebuild(const Loop& loop, uint32_t loopLength) {
    // Long-loop visual-cache rebuild policy only — not a display-window predicate.
    // Transient SD/undo/save pressure must not skip short-loop ensureVisualCacheBuilt;
    // after invalidate that left the OLED on an empty roll after the first good paint.
    return loop.shouldAvoidFullVisualRebuild(loopLength);
}

bool visualCacheCoversFollowWindow(const Loop& loop, uint32_t windowStart, uint32_t windowLength,
                                   uint32_t loopLength) {
    if (loop.visualCache.notes.empty()) {
        return false;
    }
    uint32_t gatherStart = 0;
    uint32_t gatherLength = 0;
    const uint32_t padBars =
        kWindowedGatherMarginBars > DisplayWindowUtils::kFollowReadyLookaheadBars
            ? kWindowedGatherMarginBars
            : static_cast<uint8_t>(DisplayWindowUtils::kFollowReadyLookaheadBars);
    DisplayWindowUtils::expandWindowWithMargin(windowStart, windowLength, loopLength,
                                               padBars * Config::TICKS_PER_BAR, gatherStart,
                                               gatherLength);
    return visualCacheCoversWindow(loop.visualCacheDirty, loop.visualCache.dirtyBars, gatherStart,
                                   gatherLength, loopLength, Config::TICKS_PER_BAR);
}

bool shouldDeferHeavyDisplayRebuild() {
    // Undo hydrate / deferred save can steal the SD bus for long stretches — soft-defer
    // full visual cache builds. Do NOT key off SlotLoadSession: LoadLoopJob can
    // stay active across many main-loop turns and made OLED stutter (session_20260718_174018).
    // Long loops already use the windowed / stale-while-revalidate path while PLAYING.
    return StorageManager::hasPendingUndoSnapshotHydrate() ||
           StorageManager::hasDeferredSaveWork();
}

DISP_COLD_MEM void rebuildDisplayNotesInWindow(Loop& mutLoop, const Loop& loop, uint32_t loopLength,
                                               uint32_t windowStart, uint32_t windowLength,
                                               SessionMidiEventVec& eventBuffer,
                                               NoteUtils::DisplayNoteVec& outNotes,
                                               bool includeActiveCapture,
                                               DisplayBaseGatherTrace* trace) {
    const bool traced = trace != nullptr;
    const uint32_t totalStartUs = traced ? micros() : 0;
    eventBuffer.clear();
    const bool mergeLiveCapture = includeActiveCapture && loop.captureActive();
    if (!mergeLiveCapture &&
        mutLoop.tryCopyCurrentSnapshotDisplayNotesInWindow(outNotes, windowStart, windowLength)) {
        if (traced) {
            trace->path = "root";
            trace->rootUs = micros() - totalStartUs;
            trace->totalUs = trace->rootUs;
            trace->notes = static_cast<uint32_t>(outNotes.size());
            trace->windowStart = windowStart;
            trace->windowLength = windowLength;
        }
        return;
    }
    Loop::SnapshotWindowGatherPath snapshotPath = Loop::SnapshotWindowGatherPath::None;
    bool usedSnapshot =
        mergeLiveCapture
            ? mutLoop.tryGatherPublishedSnapshotEventsInWindowWithCapture(
                  eventBuffer, windowStart, windowLength, &snapshotPath)
            : mutLoop.tryGatherPublishedSnapshotEventsInWindow(eventBuffer, windowStart,
                                                               windowLength, &snapshotPath);
    if (snapshotPath == Loop::SnapshotWindowGatherPath::Overlay) {
        eventBuffer.clear();
        usedSnapshot = false;
    }
    if (usedSnapshot && !eventBuffer.empty()) {
        const uint32_t phaseStartUs = traced ? micros() : 0;
        const NoteUtils::DisplayNoteVec reconstructed =
            NoteUtils::reconstructDisplayNotes(eventBuffer, loopLength, false, false);
        outNotes.assign(reconstructed.begin(), reconstructed.end());
        if (traced) {
            trace->path = "snap";
            trace->snapshotUs = micros() - phaseStartUs;
            trace->events = static_cast<uint32_t>(eventBuffer.size());
        }
    } else if (loop.hasCommittedPasses()) {
        const uint32_t phaseStartUs = traced ? micros() : 0;
        LoopContentResolution::resolveDisplayNotes(loop.passes, loopLength, outNotes);
        if (traced) {
            trace->path = "passes";
            trace->passesUs = micros() - phaseStartUs;
        }
    } else {
        outNotes.clear();
        if (traced) {
            trace->path = "empty";
        }
    }
    const uint32_t wrapStartUs = traced ? micros() : 0;
    mutLoop.appendOverdubPassDisplayNotes(outNotes);
    if (traced) {
        trace->wrapUs = micros() - wrapStartUs;
    }
    const uint32_t filterStartUs = traced ? micros() : 0;
    // Keep complete note geometry in both physical halves of a circular window;
    // filtering may exclude a row, but must not clip or re-pair its endpoints.
    DisplayWindowUtils::filterDisplayNotesByWindowInclusionInPlace(
        outNotes, windowStart, windowLength, loopLength);
    if (traced) {
        trace->filterUs = micros() - filterStartUs;
        trace->totalUs = micros() - totalStartUs;
        trace->notes = static_cast<uint32_t>(outNotes.size());
        if (trace->events == 0) {
            trace->events = static_cast<uint32_t>(eventBuffer.size());
        }
        trace->windowStart = windowStart;
        trace->windowLength = windowLength;
    }
}

DISP_COLD_MEM void emitDisplayBaseGatherTrace(const DisplayBaseGatherTrace& trace,
                                              uint8_t displaySlot) {
#if defined(SESSION_CAPTURE)
    // One row per over-budget gather, not per frame. Tier-A through the `DIAG,disp_` family.
    constexpr uint32_t kTraceFloorUs = 5000u;
    if (trace.totalUs < kTraceFloorUs) {
        return;
    }
    char line[224];
    snprintf_P(line, sizeof(line),
               PSTR("#CAP,%lu,DIAG,disp_base,s=%u,site=%s,path=%s,us=%lu,root=%lu,snap=%lu,"
                    "passes=%lu,wrap=%lu,filter=%lu,ev=%lu,notes=%lu,win=%lu,%lu"),
               static_cast<unsigned long>(micros()), static_cast<unsigned>(displaySlot),
               trace.site != nullptr ? trace.site : "-", trace.path != nullptr ? trace.path : "-",
               static_cast<unsigned long>(trace.totalUs),
               static_cast<unsigned long>(trace.rootUs),
               static_cast<unsigned long>(trace.snapshotUs),
               static_cast<unsigned long>(trace.passesUs),
               static_cast<unsigned long>(trace.wrapUs),
               static_cast<unsigned long>(trace.filterUs),
               static_cast<unsigned long>(trace.events),
               static_cast<unsigned long>(trace.notes),
               static_cast<unsigned long>(trace.windowStart),
               static_cast<unsigned long>(trace.windowLength));
    DebugSessionCapture::appendCaptureTextLine(line);
#else
    (void)trace;
    (void)displaySlot;
#endif
}

uint8_t resolveTrackIndex(const Track& track) {
    for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
        if (&trackManager.getTrack(i) == &track) {
            return i;
        }
    }
    return 255;
}

}  // namespace DisplayManagerInternal
