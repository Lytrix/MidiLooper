//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "Loop.h"
#include "LoopPasses.h"
#include "Track.h"
#include "Utils/TrackMem.h"

#if defined(__IMXRT1062__)
#define TRACK_INTERNAL_MEM TRACK_COLD_MEM
#else
#define TRACK_INTERNAL_MEM
#endif

struct StopPathStorageStats {
  size_t eventCount = 0;
  size_t chunkRefCount = 0;
};

TRACK_INTERNAL_MEM bool shouldRestoreCommittedOverlapOnOverdubStop(const Loop& loop, uint8_t note,
                                                                   uint32_t pendingOnPhaseTick,
                                                                   uint32_t closePhaseTick);

TRACK_INTERNAL_MEM StopPathStorageStats collectStopPathStorageStats(const Loop& loop,
                                                                    bool includeCaptureBuffer = true);

TRACK_INTERNAL_MEM const char* commitResultLabel(CommitResult result);

TRACK_INTERNAL_MEM void logRecordStopStage(const Loop& loop, uint32_t stopStartUs, const char* stage,
                                           uint32_t stageDurationUs, uint32_t heapBefore,
                                           uint32_t heapAfter, const char* outcome,
                                           const StopPathStorageStats* cachedStats = nullptr);

TRACK_INTERNAL_MEM void logOverdubStopStage(const Loop& loop, uint32_t stopStartUs, const char* stage,
                                            uint32_t stageDurationUs, uint32_t heapBefore,
                                            uint32_t heapAfter, const char* outcome,
                                            const StopPathStorageStats* cachedStats = nullptr);

TRACK_INTERNAL_MEM void emitOverdubStopDisplaySnapshot(Track& track, uint8_t displaySlot,
                                                       uint32_t currentTick);

TRACK_INTERNAL_MEM void logMemoryAfterOverdubStop(uint32_t overdubNoteOns, const Loop& loop);

TRACK_INTERNAL_MEM uint8_t resolveTrackIndexForPersistence(const Track& track);

#if defined(SESSION_CAPTURE)
TRACK_INTERNAL_MEM void logOverdubCaptureCoordinate(const Track& track, uint32_t absTick,
                                                    uint32_t storageTick, uint8_t channel,
                                                    uint8_t note);
#endif
