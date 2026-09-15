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
// Stateless implementation of Track's handoff, kept independent of Arduino
// so native tests execute the same joint preflight and publication body.
namespace TrackPreparedStopCommit {
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
    const uint32_t& generation, PreparedCapturePassTransfer& transferred);
bool commitStoppedWithUndo(Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, PreparedCapturePassTransfer& transferred,
    GlobalUndoStack& stack, TrackUndoPublication& undo);
bool commitWithUndo(Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, GlobalUndoStack& stack, TrackUndoPublication& undo);
bool commit(Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred);
}
