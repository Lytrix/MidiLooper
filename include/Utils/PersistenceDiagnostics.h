//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

namespace PersistenceDiagnostics {

/// Mark first deferred-save request while work remains outstanding (dirty-save age proxy).
void onDeferredSaveRequested();

/// Deferred writer returned without dispatching because capture/overdub is active.
void onTransportGateBlock();

/// Deferred writer deferred dispatch because internal heap is below the safety floor.
void onHeapFloorBlock();

/// Slice loop exited with deferred work remaining because the time budget was exhausted.
void onBudgetBlock();

/// One deferred-save finite-state-machine sub-step completed.
void onSliceCompleted(uint32_t sliceLatencyUs);

/// Emit pool-pressure warning when free chunks approach reserve (rate-limited).
void maybeEmitPoolPressureWarning();

/**
 * Periodic persistence diagnostic line when capture is active or deferred save is pending.
 * Phase 0: queue depth proxies deferred-save backlog (chunk queue arrives in Phase 2).
 */
void maybeEmitPeriodic(bool captureOrTransportActive, bool savePending, bool saveInProgress,
                       bool sdIoActive);

#if !defined(SESSION_CAPTURE)
inline void onDeferredSaveRequested() {}
inline void onTransportGateBlock() {}
inline void onHeapFloorBlock() {}
inline void onBudgetBlock() {}
inline void onSliceCompleted(uint32_t) {}
inline void maybeEmitPoolPressureWarning() {}
inline void maybeEmitPeriodic(bool, bool, bool, bool) {}
#endif

}  // namespace PersistenceDiagnostics
