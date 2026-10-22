//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

/// **Slice 3.15e/2 — who owns `ready` (the next adoption opportunity).**
///
/// Three owners can owe a window on one slot: the **NOTE_EDIT preview** (an explicit session-local presentation override),
/// the **live overdub overlay** (temporary continuity reader for the active overdub generation), and the **committed
/// successor** (ordinary convergence, the authoritative baseline). They share one `ready` role
/// (`RuntimePlaybackViewConfig::kViewCount == 3`: current / ready / build), so exactly one of them may own the next
/// adoption opportunity, and every other one must be left with a **named outcome**.
///
/// This resolver is **pure**: it reads claims and returns outcomes. It stores no window, no queue and no demand record —
/// a losing owner keeps its own existing obligation and re-asks on its own turn, which is why contention cannot invent a
/// fourth state. Its classes are ordered by the *contract*, never by which lane finished first:
///
/// 1. `Preview`  — the user is explicitly editing; the session's presentation override owns the boundary.
/// 2. `Overlay`  — a live overdub is sounding; its window already carries the committed base (slice 1), so it is also this
///                 slot's convergence while it is owed.
/// 3. `Committed`— ordinary convergence; it defers to the two above and is subsumed by the overlay's base meanwhile.
///
/// Identity always outranks class: a claim whose currency no longer matches its owner's live state (a stale overlay
/// capture generation, a stale preview revision, a superseded committed root) is refused **before** priority is consulted,
/// and a stale claim that already holds `ready` asks for that role to be retired rather than adopted.
namespace PlaybackDemandArbitration {

enum class Class : uint8_t { Preview = 0, Overlay = 1, Committed = 2 };
constexpr size_t kClassCount = 3;

enum class Outcome : uint8_t {
  /// Nothing was owed by this owner: it takes no part in this arbitration.
  NotClaimed,
  /// This owner holds the next adoption opportunity.
  OwnsNextOpportunity,
  /// A valid demand that lost on class order. It stays owed to its own owner and re-asks on its own turn.
  DeferredToHigherPriority,
  /// The claim's currency no longer matches its owner's live state. Refused regardless of class; the owner must
  /// re-derive before it may own a boundary again.
  RefusedStaleIdentity,
  /// The winner cannot be served because no role is free and the room is held by a claim that may not be superseded.
  /// The compatibility route (the live-capture echo) stays authoritative for this slot; callers report this name.
  RefusedNoRoleCapacity,
  /// Two owners claimed the **same** `ready` role. At most one may hold it; the loser is refused here and its candidate
  /// must be retired. This is a named inconsistency, never a silently resolved race.
  RefusedRoleConflict,
};

struct Claim {
  Class demandClass = Class::Committed;
  /// The owner has work it must place (a demand, not merely a served window).
  bool owed = false;
  /// The claim's currency still matches the owner's live state: the active overdub capture generation for `Overlay`, the
  /// live session's preview revision for `Preview`, the current committed source identity for `Committed`.
  bool identityCurrent = false;
  /// This owner already holds the complete `ready` candidate.
  bool readyComplete = false;
  /// Owner-specific currency, carried only so a caller can report *why* an identity lost. Never compared across classes.
  uint64_t identityToken = 0;
};

struct Resolution {
  /// Exactly one class owns the boundary when `hasOwner` is true; `NotClaimed` when nothing was owed at all.
  Class owner = Class::Preview;
  bool hasOwner = false;
  Outcome outcomes[kClassCount] = {Outcome::NotClaimed, Outcome::NotClaimed, Outcome::NotClaimed};
  /// The `ready` role holds a **valid** complete candidate of a lower-priority class than the winner: retire it, and name
  /// the retirement as superseded by the winner. It is a role decision, not a content decision.
  bool supersedeReady = false;
  /// The `ready` role holds a stale candidate (`identityCurrent == false`): retire it before any build.
  bool retireStaleReady = false;
  /// The boundary has no owner this turn (nothing owed, or the winner is refused) and the compatibility route carries the
  /// slot. Named so a caller never reports silence as success.
  bool compatibilityAuthoritative = false;
  /// More than one owner claimed the `ready` role: a named inconsistency (the loser is `RefusedRoleConflict`).
  bool roleConflict = false;

  Outcome outcomeFor(Class c) const { return outcomes[static_cast<size_t>(c)]; }
};

namespace detail {
inline constexpr Class kClassOrder[kClassCount] = {Class::Preview, Class::Overlay, Class::Committed};

inline size_t indexOf(Class c) { return static_cast<size_t>(c); }

inline const Claim* claimFor(const Claim (&claims)[kClassCount], Class c) {
  for (size_t i = 0; i < kClassCount; ++i) {
    if (claims[i].demandClass == c) return &claims[i];
  }
  return nullptr;
}

inline bool valid(const Claim* claim) { return claim != nullptr && claim->owed && claim->identityCurrent; }
}  // namespace detail

/// Resolve one slot's three claims. Deterministic: the result is a pure function of the claims and reads the **class** each
/// claim declares (never its array position), so reordering the caller's claims cannot change the winner.
inline Resolution resolve(const Claim (&claims)[kClassCount]) {
  Resolution resolution;
  bool anyOwed = false;

  // 1. Identity first: a stale claim never owns a boundary, whatever its class, and a stale `ready` must be retired.
  for (const Class c : detail::kClassOrder) {
    const Claim* claim = detail::claimFor(claims, c);
    if (claim == nullptr || !claim->owed) {
      resolution.outcomes[detail::indexOf(c)] = Outcome::NotClaimed;
      continue;
    }
    anyOwed = true;
    if (!claim->identityCurrent) {
      resolution.outcomes[detail::indexOf(c)] = Outcome::RefusedStaleIdentity;
      if (claim->readyComplete) resolution.retireStaleReady = true;
    }
  }

  // 2. Class order among the valid claims: the first valid class is the only possible owner.
  bool hasOwner = false;
  Class owner = Class::Preview;
  for (const Class c : detail::kClassOrder) {
    if (!detail::valid(detail::claimFor(claims, c))) continue;
    owner = c;
    hasOwner = true;
    break;
  }
  if (!hasOwner) {
    // Nothing valid was owed. If something *was* owed (stale only), the compatibility route carries the slot and the
    // caller must report it rather than imply success.
    resolution.compatibilityAuthoritative = anyOwed;
    return resolution;
  }
  resolution.hasOwner = true;
  resolution.owner = owner;
  resolution.outcomes[detail::indexOf(owner)] = Outcome::OwnsNextOpportunity;
  for (const Class c : detail::kClassOrder) {
    if (c == owner) continue;
    if (detail::valid(detail::claimFor(claims, c))) {
      resolution.outcomes[detail::indexOf(c)] = Outcome::DeferredToHigherPriority;
    }
  }

  // 3. The `ready` role: at most one owner may hold it.
  size_t readyHolders = 0;
  Class readyOwner = owner;
  for (const Class c : detail::kClassOrder) {
    const Claim* claim = detail::claimFor(claims, c);
    // A stale candidate is retired as stale (step 1); it neither supersedes nor conflicts.
    if (claim == nullptr || !claim->owed || !claim->readyComplete || !claim->identityCurrent) continue;
    ++readyHolders;
    if (readyHolders == 1) readyOwner = c;
  }
  if (readyHolders > 1) {
    resolution.roleConflict = true;
    for (const Class c : detail::kClassOrder) {
      if (c == owner) continue;
      const Claim* claim = detail::claimFor(claims, c);
      if (claim != nullptr && claim->owed && claim->readyComplete && claim->identityCurrent) {
        // The loser of a role conflict is refused **by name**, not deferred: two candidates cannot share one boundary.
        resolution.outcomes[detail::indexOf(c)] = Outcome::RefusedRoleConflict;
        resolution.supersedeReady = true;
      }
    }
  } else if (readyHolders == 1 && readyOwner != owner) {
    // One valid candidate of a lower-priority class than the winner: retire its role, named as superseded.
    resolution.supersedeReady = true;
  }
  return resolution;
}

/// Convenience: the capacity refusal is decided by the caller that owns the roles, but it is always **named** and never
/// silent. With `kViewCount == 3` an owner that is neither the current `ready` holder nor able to free the role reports
/// this instead of building into nowhere.
inline Resolution refuseForRoleCapacity(const Claim (&claims)[kClassCount]) {
  Resolution resolution = resolve(claims);
  if (!resolution.hasOwner) return resolution;
  for (size_t i = 0; i < kClassCount; ++i) {
    if (resolution.outcomes[i] != Outcome::OwnsNextOpportunity) continue;
    resolution.outcomes[i] = Outcome::RefusedNoRoleCapacity;
  }
  resolution.hasOwner = false;
  resolution.supersedeReady = false;
  resolution.retireStaleReady = false;
  resolution.compatibilityAuthoritative = true;
  return resolution;
}

}  // namespace PlaybackDemandArbitration
