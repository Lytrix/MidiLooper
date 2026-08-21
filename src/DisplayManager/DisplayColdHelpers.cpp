//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Cold-path display helpers shared across DisplayManager translation units.

#include "DisplayManagerInternal.h"

#include "Loop.h"
#include "StorageManager.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include <algorithm>
#include <Arduino.h>

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#else
#define DISP_COLD_MEM
#endif

// Keep diagnostic format strings out of DTCM .rodata — same 4 KB RAM1 alignment edge as the
// RT_TAG emit tags in RuntimeTimingTelemetry.
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define DISP_DIAG_FMT(literal) PSTR(literal)
#else
#define DISP_DIAG_FMT(literal) literal
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
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    // Sub-phase attribution for the silent windowed committed rebuild. Threshold-gated:
    // the bounded visualCache-filter path in resolveWindowedDisplayNotes never reaches
    // here, so a crossing is already the anomaly (session_20260821_100726: 22.4s
    // disp_res_cmt emitted zero lines).
    constexpr uint32_t kWindowRebuildReportUs = 10000u;
    const uint32_t rebuildStartUs = micros();
    uint32_t phaseStartUs = rebuildStartUs;
    uint32_t gatherUs = 0;
    uint32_t reconstructUs = 0;
    uint32_t appendUs = 0;
#endif
    eventBuffer.clear();
    if (includeActiveCapture && loop.captureActive()) {
        mutLoop.gatherCommittedEventsInWindowWithCapture(eventBuffer, windowStart, windowLength);
    } else {
        mutLoop.gatherCommittedEventsInWindow(eventBuffer, windowStart, windowLength);
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    gatherUs = micros() - phaseStartUs;
    phaseStartUs = micros();
#endif
    if (!eventBuffer.empty()) {
        const NoteUtils::DisplayNoteVec reconstructed =
            NoteUtils::reconstructDisplayNotes(eventBuffer, loopLength, false);
        outNotes.assign(reconstructed.begin(), reconstructed.end());
    } else {
        outNotes.clear();
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    reconstructUs = micros() - phaseStartUs;
    phaseStartUs = micros();
#endif
    mutLoop.appendOverdubPassDisplayNotes(outNotes);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    appendUs = micros() - phaseStartUs;
    phaseStartUs = micros();
#endif
    const uint32_t windowEnd = windowStart + windowLength;
    outNotes.erase(std::remove_if(outNotes.begin(), outNotes.end(),
                                  [windowStart, windowEnd](const NoteUtils::DisplayNote& note) {
                                    return note.endTick < windowStart ||
                                           note.startTick >= windowEnd;
                                  }),
                   outNotes.end());
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    const uint32_t filterUs = micros() - phaseStartUs;
    const uint32_t rebuildUs = micros() - rebuildStartUs;
    if (rebuildUs >= kWindowRebuildReportUs) {
        char line[224];
        snprintf(line, sizeof(line),
                 DISP_DIAG_FMT("#CAP,%lu,DIAG,disp_win,us=%lu,gather=%lu,recon=%lu,append=%lu,"
                               "filter=%lu,ev=%u,notes=%u,ws=%lu,wl=%lu"),
                 static_cast<unsigned long>(micros()), static_cast<unsigned long>(rebuildUs),
                 static_cast<unsigned long>(gatherUs), static_cast<unsigned long>(reconstructUs),
                 static_cast<unsigned long>(appendUs), static_cast<unsigned long>(filterUs),
                 static_cast<unsigned>(eventBuffer.size()),
                 static_cast<unsigned>(outNotes.size()),
                 static_cast<unsigned long>(windowStart),
                 static_cast<unsigned long>(windowLength));
        DebugSessionCapture::appendCaptureTextLine(line);
    }
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
