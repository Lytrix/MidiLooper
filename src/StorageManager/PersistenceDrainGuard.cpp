//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet C / A3.4a of `deferred-slot-restore-liveness`: the single-owner blocking-drain guard's state.
//
// The guard lives in its own translation unit so it can be linked and verified **without** the blocking
// drain's scheduler closure (`SyncDrainSaveState.cpp` pulls in the persistence admission, budget and
// revision-job owners). That split is not cosmetic: A3.4a's contract is "one owner, acquire before any
// shared mutation, refuse by name before any side effect, release on every exit", and a suite that can
// link the owner can pin that contract directly instead of only through the full drain.
//
// The only writer is `PersistenceDrainScope` (see `include/PersistenceDrainGuard.h`), so acquire and
// release cannot drift and no path can clear a guard it did not take.

#include "PersistenceDrainGuard.h"

// The session aggregate's declaration lives with the rest of the persistence internals. Including that
// header (rather than `StorageSession.h` alone) is what makes this TU compile standalone: the native
// harness happened to have included it first, which hid the missing declaration until the firmware build.
#include "StorageManagerInternal.h"

namespace StorageManagerInternal {

/// The acquire is a test-and-set of one measured byte of session state; the destructor is its single
/// release path.
PersistenceDrainScope::PersistenceDrainScope() {
    if (!storageSession.drainInProgress) {
        storageSession.drainInProgress = true;
        acquired_ = true;
    }
}

PersistenceDrainScope::~PersistenceDrainScope() {
    if (acquired_) {
        storageSession.drainInProgress = false;
    }
}

bool isPersistenceDrainInProgress() {
    return storageSession.drainInProgress;
}

}  // namespace StorageManagerInternal
