//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet C / A3.4a of `deferred-slot-restore-liveness`: the **single-owner blocking-drain guard**.
//
// A blocking drain services MIDI while it waits, and MIDI service is a callback boundary, not a passive
// delay: a controller message reaches `MidiButtonProcessor` and executes a `MidiButtonActions` entry
// point immediately. Without one owner, a second CLEAR would re-enter the drain and overwrite the outer
// drain's budget/progress/result, or reset the very slot the outer caller is draining for.
//
// Therefore: the detailed drain acquires `PersistenceDrainScope` **before** it changes shared
// save/budget/progress/result state, and the scope releases it on every exit (Completed, Blocked, Failed).
// A nested detailed drain call sees the held guard and returns `Blocked(drain_in_progress)` immediately
// (the bool wrapper returns false) without touching the outer state. Destructive/storage callback entry
// points consult `isPersistenceDrainInProgress()` **before** any side effect and refuse with that name.
//
// This header is deliberately standalone (no project includes), so both the persistence owners and the
// MIDI callback owners can include it without an ordering constraint.

#pragma once

namespace StorageManagerInternal {

/// The named reason a nested or conflicting operation is refused with. One literal, one meaning, so the
/// drain log and the callback log cannot disagree.
constexpr const char* kDrainInProgressRefusalReason = "drain_in_progress";

/// True while a blocking persistence drain owns the shared storage state.
bool isPersistenceDrainInProgress();

/// Scoped ownership of the blocking drain. The **only** writer of the state, so acquire and release
/// cannot drift: construction reports whether this call took ownership, and destruction releases it only
/// when it did.
class PersistenceDrainScope {
public:
    PersistenceDrainScope();
    ~PersistenceDrainScope();
    PersistenceDrainScope(const PersistenceDrainScope&) = delete;
    PersistenceDrainScope& operator=(const PersistenceDrainScope&) = delete;

    /// False when another scope already owns the drain: this scope holds nothing and its destructor must
    /// not release the outer owner's guard.
    bool acquired() const { return acquired_; }

private:
    bool acquired_ = false;
};

}  // namespace StorageManagerInternal
