//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef WORKING_SET_LEASE_H
#define WORKING_SET_LEASE_H

#include <array>
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
 * @brief Lease bookkeeping for one slot's working set (one `LoopPlaybackRuntime`).
 *
 * A slot runtime *is* the working set, so it carries exactly one lease identity. Putting this on
 * the per-slot runtime rather than on the per-track container is deliberate: the per-track
 * container is a measured by-value member of an 8-way `Track` array, so a byte there costs eight,
 * while `LoopPlaybackRuntime` is externally allocated.
 */
class WorkingSetLeaseSlot {
 public:
  WorkingSetOutcome acquire(WorkingSetRole role) {
    if (!isDeclaredRoleSet(role)) {
      return WorkingSetOutcome::InvalidRole;
    }
    roles_ = roles_ | role;
    return WorkingSetOutcome::Acquired;
  }

  /// Release the exact identity. A generation mismatch is the reported no-op of S4.2.
  WorkingSetOutcome release(uint16_t generation, WorkingSetRole role) {
    // Generation first: after a total release the slot is empty *and* its generation moved on, so
    // an empty check first would misreport the previous holder's late release as simply NotHeld.
    if (generation_ != generation) {
      return WorkingSetOutcome::StaleGeneration;
    }
    if (roles_ == WorkingSetRole::None) {
      return WorkingSetOutcome::NotHeld;
    }
    if (!hasRole(roles_, role)) {
      return WorkingSetOutcome::NotHeld;
    }
    roles_ = roles_ & static_cast<WorkingSetRole>(~static_cast<uint8_t>(role));
    return WorkingSetOutcome::Released;
  }

  /// The slot runtime left the active set: clear every role and bump the generation so a late
  /// release carrying the previous generation is rejected (S4.2).
  WorkingSetOutcome releaseAll(uint16_t generation) {
    if (generation_ != generation) {
      return WorkingSetOutcome::StaleGeneration;
    }
    if (roles_ == WorkingSetRole::None) {
      return WorkingSetOutcome::NotHeld;
    }
    roles_ = WorkingSetRole::None;
    ++generation_;
    return WorkingSetOutcome::Released;
  }

  bool isHeld(uint16_t generation, WorkingSetRole role) const {
    return generation_ == generation && hasRole(roles_, role);
  }
  WorkingSetRole roles() const { return roles_; }
  uint16_t generation() const { return generation_; }

 private:
  WorkingSetRole roles_ = WorkingSetRole::None;
  uint16_t generation_ = 0;
};

/**
 * @brief Fixed, slot-indexed lease bookkeeping for one `TrackPlaybackRuntime`.
 *
 * The runtime authorises working sets by slot index and its per-track container is a measured
 * member of an 8-way `Track` array, so this variant is sized by `MAX_LOOPS_PER_TRACK` instead of
 * carrying a record array: two bytes per slot (generation + role set), 16 B today. It exposes the
 * same identity and outcome vocabulary as the pool; `acquire`/`release` allocate nothing, and a
 * cap breach or generation mismatch is reported rather than resolved.
 *
 * The generation is the slot runtime generation: it stays 0 until S4.1 releases and reuses a slot,
 * which is also when S4.2 needs it to reject a late cleanup.
 */
template <uint8_t MaxSlots = 8>
class WorkingSetLeaseTable {
 public:
  static constexpr uint8_t kMaxSlots = MaxSlots;

  struct SlotState {
    uint8_t generation = 0;
    WorkingSetRole roles = WorkingSetRole::None;
  };

  WorkingSetLeaseTable() = default;

  WorkingSetOutcome acquire(uint8_t slot, WorkingSetRole role) {
    if (slot >= kMaxSlots) {
      return WorkingSetOutcome::InvalidRole;
    }
    if (!isDeclaredRoleSet(role)) {
      return WorkingSetOutcome::InvalidRole;
    }
    SlotState& state = slots_[slot];
    const WorkingSetRole combined = state.roles | role;
    if (!hasCapacityFor(slot, combined)) {
      return WorkingSetOutcome::CapReached;
    }
    state.roles = combined;
    return WorkingSetOutcome::Acquired;
  }

  /// Release the exact identity. A generation mismatch is the reported no-op of S4.2.
  WorkingSetOutcome release(uint8_t slot, uint16_t generation, WorkingSetRole role) {
    if (slot >= kMaxSlots) {
      return WorkingSetOutcome::NotHeld;
    }
    SlotState& state = slots_[slot];
    if (state.roles == WorkingSetRole::None) {
      return WorkingSetOutcome::NotHeld;
    }
    if (state.generation != generation) {
      return WorkingSetOutcome::StaleGeneration;
    }
    if (!hasRole(state.roles, role)) {
      return WorkingSetOutcome::NotHeld;
    }
    state.roles = state.roles & static_cast<WorkingSetRole>(~static_cast<uint8_t>(role));
    return WorkingSetOutcome::Released;
  }

  /// Total release at this slot: the slot leaves the active set. Bumps the runtime generation so a
  /// later release carrying the previous generation is rejected (S4.2).
  WorkingSetOutcome releaseAllRoles(uint8_t slot, uint16_t generation) {
    if (slot >= kMaxSlots) {
      return WorkingSetOutcome::NotHeld;
    }
    SlotState& state = slots_[slot];
    if (state.roles == WorkingSetRole::None) {
      return WorkingSetOutcome::NotHeld;
    }
    if (state.generation != generation) {
      return WorkingSetOutcome::StaleGeneration;
    }
    state.roles = WorkingSetRole::None;
    ++state.generation;
    return WorkingSetOutcome::Released;
  }

  bool isHeld(uint8_t slot, uint16_t generation, WorkingSetRole role) const {
    if (slot >= kMaxSlots) {
      return false;
    }
    const SlotState& state = slots_[slot];
    return state.generation == generation && hasRole(state.roles, role);
  }

  WorkingSetRole rolesHeldAtSlot(uint8_t slot) const {
    return slot < kMaxSlots ? slots_[slot].roles : WorkingSetRole::None;
  }

  uint16_t generationAtSlot(uint8_t slot) const {
    return slot < kMaxSlots ? slots_[slot].generation : 0;
  }

  uint8_t holderCount(WorkingSetRole role) const {
    uint8_t n = 0;
    for (uint8_t slot = 0; slot < kMaxSlots; ++slot) {
      if (hasRole(slots_[slot].roles, role)) {
        ++n;
      }
    }
    return n;
  }

  uint8_t totalHolders() const {
    uint8_t n = 0;
    for (uint8_t slot = 0; slot < kMaxSlots; ++slot) {
      if (slots_[slot].roles != WorkingSetRole::None) {
        ++n;
      }
    }
    return n;
  }

 private:
  bool hasCapacityFor(uint8_t slot, WorkingSetRole role) const {
    for (uint8_t bit = 0; bit < 4; ++bit) {
      const WorkingSetRole single = static_cast<WorkingSetRole>(1u << bit);
      if (!hasRole(role, single)) {
        continue;
      }
      const uint8_t cap = workingSetCapFor(caps_, single);
      if (cap == 0) {
        return false;
      }
      // Count distinct slot identities holding the class, excluding this slot's existing hold.
      uint8_t held = 0;
      for (uint8_t other = 0; other < kMaxSlots; ++other) {
        if (other != slot && hasRole(slots_[other].roles, single)) {
          ++held;
        }
      }
      if (held >= cap) {
        return false;
      }
    }
    return true;
  }

  WorkingSetLeaseCaps caps_{};
  std::array<SlotState, kMaxSlots> slots_{};
};

#endif  // WORKING_SET_LEASE_H
