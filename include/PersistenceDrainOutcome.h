//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet C / A3.4 of `deferred-slot-restore-liveness`: the blocking drain's **three-way result** and the
// caller rule that reads it.
//
// `StorageManager::saveState` returned only `bool`, and every `false` was treated as failure at both call
// sites, so a named *temporary* wait could not coexist with "the operation reported success". The drain
// therefore distinguishes **completed**, **temporarily blocked** (a named wait it could not clear within
// its budget) and **failed** (a real failure), and this header is the single place that decides what a
// caller does with each.
//
// The decision is a pure function so it is native-testable: the real drain cannot link natively
// (see `REVIEW.md` A1.7), but the *contract* the callers implement can be pinned exactly.

#pragma once

#include <cstdint>

/// The blocking drain's outcome. `Completed` is the only outcome that licenses a destructive operation.
enum class SyncDrainOutcome : uint8_t {
  Completed = 0,
  /// A named wait the drain could not clear within its budget. Not success, and not a real failure: the
  /// owed save stays owed and the scheduler retries it.
  TemporarilyBlocked = 1,
  /// A real failure.
  Failed = 2,
};

/// What a caller that performs a destructive operation only after a drain does with the outcome.
enum class SyncDrainCallerAction : uint8_t {
  /// Proceed with the destructive operation.
  Proceed = 0,
  /// Refuse the operation, report the named reason, and leave the target untouched.
  RefuseBlocked = 1,
  /// Report the failure with its reason; the operation does not proceed.
  ReportFailure = 2,
};

/// **A3.4 rule:** only `Completed` proceeds. A `TemporarilyBlocked` drain is an explicit **refusal** —
/// never silent success, never a slot reset, and the owed save stays owed. A `Failed` drain is reported
/// as a failure, which stays distinguishable from the blocked wait.
inline SyncDrainCallerAction resolveDrainCallerAction(SyncDrainOutcome outcome) {
  switch (outcome) {
    case SyncDrainOutcome::Completed:
      return SyncDrainCallerAction::Proceed;
    case SyncDrainOutcome::TemporarilyBlocked:
      return SyncDrainCallerAction::RefuseBlocked;
    case SyncDrainOutcome::Failed:
      return SyncDrainCallerAction::ReportFailure;
  }
  return SyncDrainCallerAction::ReportFailure;
}

/// True when the caller must leave its target (a slot, for CLEAR) untouched: neither a blocked wait nor a
/// failure licenses the mutation.
inline bool drainOutcomeLeavesTargetUntouched(SyncDrainOutcome outcome) {
  return resolveDrainCallerAction(outcome) != SyncDrainCallerAction::Proceed;
}
