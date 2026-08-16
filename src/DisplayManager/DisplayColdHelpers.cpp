//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Cold-path display helpers shared across DisplayManager translation units.

#include "DisplayManagerInternal.h"

#include "Loop.h"
#include "StorageManager.h"
#include "Track.h"
#include "TrackManager.h"
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
    // Long-loop policy only. Transient SD/undo/save pressure must not skip short-loop
    // ensureVisualCacheBuilt — syncDetailedPaintWindow cannot run below the 16-bar threshold,
    // and after invalidate that left the OLED on an empty roll after the first good paint.
    return loop.shouldAvoidFullVisualRebuild(loopLength);
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
                                               bool includeActiveCapture) {
    eventBuffer.clear();
    if (includeActiveCapture && loop.captureActive()) {
        mutLoop.gatherCommittedEventsInWindowWithCapture(eventBuffer, windowStart, windowLength);
    } else {
        mutLoop.gatherCommittedEventsInWindow(eventBuffer, windowStart, windowLength);
    }
    if (!eventBuffer.empty()) {
        const NoteUtils::DisplayNoteVec reconstructed =
            NoteUtils::reconstructDisplayNotes(eventBuffer, loopLength, false);
        outNotes.assign(reconstructed.begin(), reconstructed.end());
    } else {
        outNotes.clear();
    }
    mutLoop.appendOverdubPassDisplayNotes(outNotes);
    const uint32_t windowEnd = windowStart + windowLength;
    outNotes.erase(std::remove_if(outNotes.begin(), outNotes.end(),
                                  [windowStart, windowEnd](const NoteUtils::DisplayNote& note) {
                                    return note.endTick < windowStart ||
                                           note.startTick >= windowEnd;
                                  }),
                   outNotes.end());
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
