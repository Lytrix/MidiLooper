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

/// OpenSpec 5.12 / D8: `ownerPass` is the interval's job-owned staged receipt when a deferred
/// job holds one; nullptr means "the Loop's own producer-local pending pass".
TRACK_INTERNAL_MEM StopPathStorageStats collectStopPathStorageStats(const Loop& loop,
                                                                    bool includeCaptureBuffer = true,
                                                                    const PendingCapturePass* ownerPass = nullptr);

TRACK_INTERNAL_MEM const char* commitResultLabel(CommitResult result);

TRACK_INTERNAL_MEM void logRecordStopStage(const Loop& loop, uint32_t stopStartUs, const char* stage,
                                           uint32_t stageDurationUs, uint32_t heapBefore,
                                           uint32_t heapAfter, const char* outcome,
                                           const StopPathStorageStats* cachedStats = nullptr);

TRACK_INTERNAL_MEM void logOverdubStopStage(const Loop& loop, uint32_t stopStartUs, const char* stage,
                                            uint32_t stageDurationUs, uint32_t heapBefore,
                                            uint32_t heapAfter, const char* outcome,
                                            const StopPathStorageStats* cachedStats = nullptr,
                                            const PendingCapturePass* ownerPass = nullptr);

TRACK_INTERNAL_MEM void emitOverdubStopDisplaySnapshot(Track& track, uint8_t displaySlot,
                                                       uint32_t currentTick);

TRACK_INTERNAL_MEM void logMemoryAfterOverdubStop(uint32_t overdubNoteOns, const Loop& loop);

TRACK_INTERNAL_MEM uint8_t resolveTrackIndexForPersistence(const Track& track);
/// S1.3 save/restore-survival: the single production expression that turns a recorded slot into the
/// persistence target. Both record-stop persist sites and the native regression use it, so a future
/// edit cannot resolve the target differently from the identity the take was pinned with.
struct RecordStopPersistTarget {
  uint8_t trackIndex = 0;
  uint8_t slotIndex = 0;
};
TRACK_INTERNAL_MEM RecordStopPersistTarget resolveRecordStopPersistTarget(const Track& track,
                                                                          uint8_t recordedSlotIndex);
/// S1.3 save/restore-survival: the take identity for the record-stop slot-identity diagnostic.
/// Packed into one 32-bit word (take id in the low 24 bits, the take's start slot in the high byte)
/// held in FLASH-resident storage, deliberately **not** a `Track` member: `Track` is the eight-way
/// DTCM array and carries a pinned `sizeof` guard (S3.0). Transport is single, so one active take.
TRACK_INTERNAL_MEM void noteRecordStartSlotForIdentity(uint8_t slotIndex);
TRACK_INTERNAL_MEM uint32_t recordStartSlotIdentityPacked();

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

TRACK_INTERNAL_MEM void requestActiveLoopSlotPersist(Track& track, uint8_t slotIndex);

TRACK_INTERNAL_MEM void requestActiveLoopSlotPersistAndSaveState(Track& track, uint8_t slotIndex,
                                                                 uint32_t admissionHeap);

TRACK_INTERNAL_MEM void resetActiveLoopAfterEmptyCapture(Loop& loop);

struct MergedPlaybackStreamCtx {
  const PlaybackOrderVec* order = nullptr;
  const SessionMidiEventVec* merged = nullptr;
};

/// Immutable, fixed-capacity equivalent of the legacy merged stream. The
/// active MIDI tick may read it only after the owner has published a complete
/// full-loop view with the exact source identity.
struct FixedPlaybackStreamCtx {
  const FixedPlaybackEventStorage* storage = nullptr;
  /// **Task 3.15e slice 3 (C5): optional per-row provenance.** When set, the adapter records the pass origin of the row it
  /// is about to return (`passOrigins[storage->order[cursor]].pass`) in `lastOrigin`, **before** the emission callback runs.
  /// The overlay route uses it to apply the occupy ledger only to committed-origin rows (`221334`/`224719`); every other
  /// fixed route leaves it null and pays one null check per row. No second traversal, no allocation.
  const PlaybackPassEventOrigin* passOrigins = nullptr;
  /// The origin of the row most recently returned by `fixedPlaybackStreamEventAt`; read by the emission callback.
  uint16_t lastOrigin = 0;
  /// Task 3.15e slice 3: the live window's readable frontier (`[0, readableCount)`; zero = expose every row).
  uint16_t readableCount = 0;
};

TRACK_INTERNAL_MEM ProjectionContext makePlaybackContext(const Track& track, const Loop& loop,
                                                         uint32_t currentTick);

TRACK_INTERNAL_MEM void reanchorPlaybackIndex(Loop& loop, const SessionMidiEventVec& mergedEvents,
                                              const PlaybackOrderVec& order,
                                              const ProjectionContext& playbackContext);

TRACK_INTERNAL_MEM void reanchorFixedPlaybackIndex(Loop& loop,
                                                   const FixedPlaybackEventStorage& storage,
                                                   const ProjectionContext& playbackContext);

TRACK_INTERNAL_MEM bool ensurePlaybackMergedMidiEventsBuilt(Track& track, Loop& loop,
                                                            LoopPlaybackRuntime& runtime,
                                                            bool allowHeavyBuild,
                                                            uint32_t currentTick,
                                                            bool prepareLaunch = false);

TRACK_INTERNAL_MEM void reconcilePlaybackLedgerAfterFullLoopRebuild(Loop& loop,
                                                                    LoopPlaybackRuntime& runtime,
                                                                    uint32_t currentTick,
                                                                    uint32_t servedThroughPhase);

/// Task 4.5 (the moving closure): the phase the walk has already served on `loop` -- the floor a closure must clear to
/// still be reachable in this pass, expressed in the walk's own terms (`UINT32_MAX` = nothing served yet, or the
/// playhead wrapped, so the whole pass is served again).
TRACK_INTERNAL_MEM uint32_t playbackClosureServeFloor(const Track& track, const Loop& loop,
                                                      uint32_t currentTick);

TRACK_INTERNAL_MEM void rebuildPlaybackOrder(Loop& loop, const SessionMidiEventVec& mergedEvents,
                                             const ProjectionContext& playbackContext);

TRACK_INTERNAL_MEM void reanchorCaptureIndex(Loop& loop);

TRACK_INTERNAL_MEM PlaybackEventStream makeMergedPlaybackStream(MergedPlaybackStreamCtx& ctx);

TRACK_INTERNAL_MEM PlaybackEventStream makeFixedPlaybackStream(FixedPlaybackStreamCtx& ctx);
/// Task 3.15e slice 3: the fixed stream with per-row provenance + the published readable frontier. Opt-in.
TRACK_INTERNAL_MEM PlaybackEventStream makeOverlayPlaybackStream(FixedPlaybackStreamCtx& ctx);
/// Task 3.15e slice 3/C5: the fixed stream with per-row provenance recorded at `eventAt` time. Opt-in; every other fixed
/// route keeps the plain adapter (byte-identical images).
TRACK_INTERNAL_MEM PlaybackEventStream makeOverlayPlaybackStream(FixedPlaybackStreamCtx& ctx);

/// Task 3.7a (I3c-a): **the paged emission adapter's context.** It carries no state of its own beyond the slot it
/// serves -- the position, the staged window and the accounting live in the slot's runtime
/// (`LoopPlaybackRuntime::PagedEmissionState`), because the deferred lane and the walk are two owners of one delivery
/// and neither may hold a private copy of the position.
struct PagedEmissionStreamCtx {
  Track* track = nullptr;
  LoopPlaybackRuntime* runtime = nullptr;
  uint8_t slot = 0;
};

TRACK_INTERNAL_MEM PlaybackEventStream makePagedEmissionStream(PagedEmissionStreamCtx& ctx);

/// Task 2.4 slice 2 cold merged fallback (FLASH-resident): a tick with no consumable pair
/// runs the existing in-place builder once, so the first audible turn is not silent. The
/// clock-path detector counts that build, so this fallback cannot grow silently.
TRACK_INTERNAL_MEM const SessionMidiEventVec* buildMergedPlaybackFallbackCold(
    Track& track, Loop& loop, LoopPlaybackRuntime& runtime, uint32_t currentTick);
/// DEC-241: the tick's route into that fallback. Main-loop tick: the cold build above, unchanged. Internal-clock
/// interrupt tick: consume a current fallback or owe it to `TrackManager::serviceOwedTickPlaybackFallbacks` and return
/// nullptr -- never build.
TRACK_INTERNAL_MEM const SessionMidiEventVec* selectMergedPlaybackFallbackForTick(
    Track& track, Loop& loop, LoopPlaybackRuntime& runtime, uint8_t slotIndex, uint32_t currentTick);

TRACK_INTERNAL_MEM PlaybackEventStream makeCapturePlaybackStream(Loop& loop);

#if defined(SESSION_CAPTURE)
TRACK_INTERNAL_MEM void logOverdubCaptureCoordinate(const Track& track, uint32_t absTick,
                                                    uint32_t storageTick, uint8_t channel,
                                                    uint8_t note);
extern uint32_t g_overdubWrapCommitUs;
extern uint32_t g_overdubWrapSealPendingUs;
extern uint32_t g_overdubWrapBeginCaptureUs;
#endif
