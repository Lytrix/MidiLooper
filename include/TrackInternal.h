//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "Loop.h"
#include "LoopPasses.h"
#include "Track.h"
#include "TrackPlaybackRuntime.h"
#include "Utils/IntervalProjection.h"
#include "Utils/PlaybackCursorAdvance.h"
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
#define TRACK_SC_RECORD_STOP_STAGE(...) logRecordStopStage(__VA_ARGS__)
#define TRACK_SC_OVERDUB_STOP_STAGE(...) logOverdubStopStage(__VA_ARGS__)
#define TRACK_SC_OVERDUB_STOP_MEMORY(...) logMemoryAfterOverdubStop(__VA_ARGS__)
#else
#define TRACK_SC_RECORD_STOP_STAGE(...) ((void)0)
#define TRACK_SC_OVERDUB_STOP_STAGE(...) ((void)0)
#define TRACK_SC_OVERDUB_STOP_MEMORY(...) ((void)0)
#endif

TRACK_INTERNAL_MEM void requestLoopSlotPersist(Track& track, uint8_t slotIndex);

TRACK_INTERNAL_MEM void requestLoopSlotPersistAndSaveState(Track& track, uint8_t slotIndex,
                                                           uint32_t admissionHeap);

TRACK_INTERNAL_MEM void requestActiveLoopSlotPersist(Track& track);

TRACK_INTERNAL_MEM void requestActiveLoopSlotPersistAndSaveState(Track& track,
                                                                 uint32_t admissionHeap);

TRACK_INTERNAL_MEM void resetActiveLoopAfterEmptyCapture(Loop& loop);

struct MergedPlaybackStreamCtx {
  const PlaybackOrderVec* order = nullptr;
  const SessionMidiEventVec* merged = nullptr;
};

TRACK_INTERNAL_MEM ProjectionContext makePlaybackContext(const Track& track, const Loop& loop,
                                                         uint32_t currentTick);

TRACK_INTERNAL_MEM void reanchorPlaybackIndex(Loop& loop, const SessionMidiEventVec& mergedEvents,
                                              const PlaybackOrderVec& order,
                                              const ProjectionContext& playbackContext);

TRACK_INTERNAL_MEM bool ensurePlaybackMergedMidiEventsBuilt(Track& track, Loop& loop,
                                                            LoopPlaybackRuntime& runtime,
                                                            bool allowHeavyBuild,
                                                            uint32_t currentTick);

TRACK_INTERNAL_MEM void reconcilePlaybackLedgerAfterFullLoopRebuild(Loop& loop,
                                                                    LoopPlaybackRuntime& runtime);

TRACK_INTERNAL_MEM void rebuildPlaybackOrder(Loop& loop, const SessionMidiEventVec& mergedEvents,
                                             const ProjectionContext& playbackContext);

TRACK_INTERNAL_MEM void reanchorCaptureIndex(Loop& loop);

TRACK_INTERNAL_MEM PlaybackEventStream makeMergedPlaybackStream(MergedPlaybackStreamCtx& ctx);

TRACK_INTERNAL_MEM PlaybackEventStream makeCapturePlaybackStream(Loop& loop);

#if defined(SESSION_CAPTURE)
TRACK_INTERNAL_MEM void logOverdubCaptureCoordinate(const Track& track, uint32_t absTick,
                                                    uint32_t storageTick, uint8_t channel,
                                                    uint8_t note);
#endif
