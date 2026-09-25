//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef WORKING_SET_LEASE_H
#define WORKING_SET_LEASE_H

#include <cstdint>

/**
 * @brief Generation-qualified working-set identity for `runtime-working-set-ownership` S3.
 *
 * The runtime working set is owned per (slot, generation, role), never by the slot alone:
 * `TrackPlaybackRuntime::runtimeBySlot_` has no generation, so a late cleanup cannot be told
 * from a live owner (invariant 6 / S4.2) and a release has no identity to reject against
 * (S4.1). This header is the vocabulary those stages share. It allocates nothing, owns no
 * resource, and is dependency-free so the host tests drive the production contract.
 *
 * S3.1 scope: identity, composable roles and a declared pool with a per-class census.
 * Release/recycle/window sizing are S4/S4f/phase F and are deliberately absent.
 */
enum class WorkingSetRole : uint8_t {
  None = 0,
  /// Playback window: the term multiplied by N; must stay small and length-independent.
  PlaybackWindow = 1u << 0,
  /// Record/overdub ingress and staging: **1 per track**.
  LiveCapture = 1u << 1,
  /// Accepted stop's finalization job: **per admitted interval** (DEC-078).
  DeferredFinalization = 1u << 2,
  /// Edit / undo / redo / display: **1**.
  Interactive = 1u << 3,
};

/// Roles are composable: one slot may hold PlaybackWindow | LiveCapture at once.
constexpr WorkingSetRole operator|(WorkingSetRole a, WorkingSetRole b) {
  return static_cast<WorkingSetRole>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr WorkingSetRole operator&(WorkingSetRole a, WorkingSetRole b) {
  return static_cast<WorkingSetRole>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr WorkingSetRole& operator|=(WorkingSetRole& a, WorkingSetRole b) {
  a = a | b;
  return a;
}
constexpr bool hasRole(WorkingSetRole set, WorkingSetRole role) {
  return (set & role) != WorkingSetRole::None;
}

/// A lease is identified by slot + generation + role. Generations are per slot runtime, so a
/// released-and-reused slot cannot be reached by the previous holder's late cleanup.
struct WorkingSetLease {
  uint8_t slot = 0;
  uint16_t generation = 0;
  WorkingSetRole role = WorkingSetRole::None;

  constexpr bool valid() const { return role != WorkingSetRole::None; }
};

constexpr bool sameWorkingSetIdentity(const WorkingSetLease& a, const WorkingSetLease& b) {
  return a.slot == b.slot && a.generation == b.generation;
}

enum class WorkingSetOutcome : uint8_t {
  Acquired = 0,
  /// Release: the identity was held and is now retired.
  Released,
  /// The lease names no role, or names a role this pool does not declare.
  InvalidRole,
  /// The declared cap for every requested role class is already held.
  CapReached,
  /// Release: nothing held this exact identity.
  NotHeld,
  /// Release: the slot is held, but by a different generation (the rejected no-op, S4.2).
  StaleGeneration,
};

/// Declared caps for the measured active envelope (design.md role classes). Device facts:
/// N is 8 today (8 tracks x 1 slot) and must not preclude 16/32/48/64.
struct WorkingSetLeaseCaps {
  uint8_t playbackWindow = 8;
  uint8_t liveCapturePerTrack = 1;
  uint8_t deferredFinalization = 1;
  uint8_t interactive = 1;
};

constexpr WorkingSetRole kAllWorkingSetRoles = WorkingSetRole::PlaybackWindow |
                                               WorkingSetRole::LiveCapture |
                                               WorkingSetRole::DeferredFinalization |
                                               WorkingSetRole::Interactive;

/// True when every bit names a declared role class (a composite set is valid).
constexpr bool isDeclaredRoleSet(WorkingSetRole role) {
  const uint8_t declared = static_cast<uint8_t>(kAllWorkingSetRoles);
  const uint8_t value = static_cast<uint8_t>(role);
  return value != 0 && (value & static_cast<uint8_t>(~declared)) == 0;
}

inline uint8_t workingSetCapFor(const WorkingSetLeaseCaps& caps, WorkingSetRole role) {
  switch (role) {
    case WorkingSetRole::PlaybackWindow: return caps.playbackWindow;
    case WorkingSetRole::LiveCapture: return caps.liveCapturePerTrack;
    case WorkingSetRole::DeferredFinalization: return caps.deferredFinalization;
    case WorkingSetRole::Interactive: return caps.interactive;
    default: return 0;
  }
}

/**
 * @brief Fixed-capacity lease bookkeeping. Holds counts, never resources.
 *
 * Acquire/release are O(1) and allocate nothing, so they are safe on the action edge; a
 * cap breach is *reported* (`CapReached`), never resolved by growing.
 */
class WorkingSetLeasePool {
 public:
  static constexpr uint8_t kMaxLeases = 32;

  explicit WorkingSetLeasePool(const WorkingSetLeaseCaps& caps = WorkingSetLeaseCaps{})
      : caps_(caps) {}

  WorkingSetOutcome acquire(const WorkingSetLease& lease) {
    if (!isDeclaredRoleSet(lease.role)) {
      return WorkingSetOutcome::InvalidRole;
    }
    if (count_ >= kMaxLeases || !hasCapacityFor(lease.role)) {
      return WorkingSetOutcome::CapReached;
    }
    leases_[count_++] = lease;
    return WorkingSetOutcome::Acquired;
  }

  /// Total release: removes every role held at this slot + generation.
  uint8_t releaseAllRoles(const WorkingSetLease& identity) {
    uint8_t released = 0;
    for (uint8_t i = 0; i < count_;) {
      if (sameWorkingSetIdentity(leases_[i], identity)) {
        leases_[i] = leases_[--count_];
        ++released;
      } else {
        ++i;
      }
    }
    return released;
  }

  /// Release by exact identity. A generation mismatch is the reported no-op of S4.2.
  WorkingSetOutcome release(const WorkingSetLease& lease) {
    for (uint8_t i = 0; i < count_; ++i) {
      if (leases_[i].slot != lease.slot) {
        continue;
      }
      if (leases_[i].generation != lease.generation) {
        return WorkingSetOutcome::StaleGeneration;
      }
      if (leases_[i].role == lease.role) {
        leases_[i] = leases_[--count_];
        return WorkingSetOutcome::Released;
      }
    }
    return WorkingSetOutcome::NotHeld;
  }

  bool isHeld(const WorkingSetLease& lease) const {
    for (uint8_t i = 0; i < count_; ++i) {
      if (leases_[i].slot == lease.slot && leases_[i].generation == lease.generation &&
          hasRole(leases_[i].role, lease.role)) {
        return true;
      }
    }
    return false;
  }

  /// Roles currently held at this slot regardless of generation (diagnostic/census use).
  WorkingSetRole rolesHeldAtSlot(uint8_t slot) const {
    WorkingSetRole roles = WorkingSetRole::None;
    for (uint8_t i = 0; i < count_; ++i) {
      if (leases_[i].slot == slot) {
        roles |= leases_[i].role;
      }
    }
    return roles;
  }

  uint8_t holderCount(WorkingSetRole role) const {
    // Distinct (slot, generation) identities holding this class: a composite lease is one
    // holder of each class it names, not one per lease record.
    uint16_t seen[kMaxLeases] = {};
    uint8_t identities = 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      if (!hasRole(leases_[i].role, role)) {
        continue;
      }
      const uint16_t key = static_cast<uint16_t>(leases_[i].slot) | (leases_[i].generation << 8);
      bool known = false;
      for (uint8_t k = 0; k < identities; ++k) {
        if (seen[k] == key) {
          known = true;
          break;
        }
      }
      if (!known) {
        seen[identities++] = key;
        ++n;
      }
    }
    return n;
  }

  uint8_t totalHolders() const { return count_; }

  const WorkingSetLeaseCaps& caps() const { return caps_; }

 private:
  bool hasCapacityFor(WorkingSetRole role) const {
    // A leased role set may name several classes; every named class must have room.
    for (uint8_t bit = 0; bit < 4; ++bit) {
      const WorkingSetRole single = static_cast<WorkingSetRole>(1u << bit);
      if (hasRole(role, single) && holderCount(single) >= workingSetCapFor(caps_, single)) {
        return false;
      }
    }
    return true;
  }

  WorkingSetLeaseCaps caps_{};
  WorkingSetLease leases_[kMaxLeases]{};
  uint8_t count_ = 0;
};

#endif  // WORKING_SET_LEASE_H
