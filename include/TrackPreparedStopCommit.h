// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstdint>
#include <cstddef>
#include "RuntimePlaybackOrderBuilder.h"
class Loop;
class TrackUndoPublication;
struct GlobalUndoStack;
struct LoopPlaybackRuntime;
struct PendingStopAdmission;
struct EmissionCursor;
struct PreparedCapturePassTransfer;
struct PendingCapturePass;
struct CapturePreview;
// Stateless implementation of Track's handoff, kept independent of Arduino
// so native tests execute the same joint preflight and publication body.
namespace TrackPreparedStopCommit {
// Empty post-wrap stops retain the current audible source and need no bridge.
/// OpenSpec 5.12 / D8: `pendingPass` is the interval-owned staged receipt when a deferred job
/// holds one; nullptr means the live producer slot (Loop::pendingCapturePass_).
bool requiresBridgeRefresh(const Loop& loop, const LoopPlaybackRuntime& runtime,
    const PendingStopAdmission& stop, const EmissionCursor& cursor,
    const PendingCapturePass* pendingPass = nullptr);
// Production atomic dispatch. A populated PLAYING transfer must publish its
// bridge jointly; a failed preflight never falls back to canonical-only commit.
bool advanceAtomicTransfer(Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, PendingCapturePass* pendingPass = nullptr);
/// Same atomic dispatch, publishing the prepared protected undo entry in the same
/// turn as the canonical/audible commit. The caller must already have proven
/// `undo.canPublish(stack, loopId, finalPassId)` through the readiness path.
bool advanceAtomicTransferWithUndo(Loop& loop, LoopPlaybackRuntime& runtime,
    PendingStopAdmission& stop, const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, GlobalUndoStack& stack,
    TrackUndoPublication& undo, PendingCapturePass* pendingPass = nullptr);
// Finish canonical session bookkeeping after recycling. Preserve the selected
// audible source, its cursor and ledger; Track owns transport-Stop silence.
// `preview` is the admitted job's owned capture preview, so the deferred seal
// can never consume a successor interval's preview.
// `ownsLiveSession` (DEC-068) says the retiring job still owns the live session; only then may
// it end that session. Its session-exit receipt was captured at its own stop admission and is
// released here; this never builds one from live state, which under reentry is a successor's.
bool completeDeferredCleanup(Loop& loop, LoopPlaybackRuntime& runtime,
    PendingStopAdmission& stop, CapturePreview& preview, bool ownsLiveSession);
/// Approved superseded rule: the recorded causal predecessor is no longer the
/// published root. Commit the accepted capture pass through the plain route and
/// publish no audible bridge, so a stale PLAYING successor can never be selected
/// and content the newer root already carries is not duplicated. The caller has
/// already retired the preparation credits. `undo` (with `stack` as its
/// publishability witness) is published in this same turn when it is ready --
/// including when this turn stages no pass of its own, because the interval's
/// take was already committed by its wraps.
bool commitSupersededCausalBase(Loop& loop, PendingStopAdmission& stop,
    PreparedCapturePassTransfer& transferred, GlobalUndoStack* stack,
    TrackUndoPublication* undo, PendingCapturePass* pendingPass = nullptr);
// One admitted canonical preparation grain; setup and unused credit retirement
// remain separate turns. Complete readiness queries consume no additional credit.
RuntimePlaybackOrderBuildResult stepPreparation(Loop& loop, LoopPlaybackRuntime& runtime,
    PendingStopAdmission& stop, size_t quantum);
// Explicit acknowledgement that undo/snapshot consumers have consumed the
// committed ID receipt. Charges a separate setup turn and ends receipt access.
bool beginHistoryRecycle(Loop& loop, PendingStopAdmission& stop);
// One admitted quantum on the committed owner. Complete queries are free;
// stale Loop state denies after charging the attempted grain.
RuntimePlaybackOrderBuildResult stepHistoryRecycle(Loop& loop, PendingStopAdmission& stop,
    size_t quantum);
bool retargetStoppedRecovery(LoopPlaybackRuntime& runtime, const PendingStopAdmission& stop);
bool commitStopped(Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, PreparedCapturePassTransfer& transferred,
    PendingCapturePass* pendingPass = nullptr);
bool commitStoppedWithUndo(Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, PreparedCapturePassTransfer& transferred,
    GlobalUndoStack& stack, TrackUndoPublication& undo,
    PendingCapturePass* pendingPass = nullptr);
bool commitWithUndo(Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, GlobalUndoStack& stack, TrackUndoPublication& undo,
    PendingCapturePass* pendingPass = nullptr);
bool commit(Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, PendingCapturePass* pendingPass = nullptr);
}
