//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Utils/RecordPreparation.h"

namespace RecordPreparation {

/// One externally allocated preparation role, one per track. The worker binds it
/// to a single job identity and drives bounded grains; the binding token is a
/// stable member of the role, so a rebind (or a replaced job) can never let a
/// stale cursor publish into a successor's job. The workspace is the union
/// scratch role, so this owns one lane array, not two.
class Role {
 public:
  /// The identity a binding belongs to. Deliberately not the active or selected
  /// slot: a successor on another slot must not consume this obligation.
  struct Key {
    uint8_t slot = 0;
    uint32_t loopId = 0;
    uint64_t interval = 0;
    uint32_t generation = 0;
  };

  bool bound() const { return bound_; }
  const Key& key() const { return key_; }

  bool matches(const Key& key) const {
    return bound_ && key_.slot == key.slot && key_.loopId == key.loopId &&
           key_.interval == key.interval && key_.generation == key.generation;
  }

  /// Bind and start one preparation. Preflight (private dense output, credits,
  /// valid identity source) must already hold; this performs no content-sized
  /// work. A failed bind leaves the role unbound.
  bool begin(const Key& key, LoopEventStore& output, const Geometry& geometry,
             LoopEventStore::ChunkReservation& reservation, const NoteIdSource& ids) {
    cursor_.discard();
    key_ = key;
    generationToken_ = key.generation;
    bound_ = cursor_.begin(output, workspace_, generationToken_, geometry, &reservation, ids);
    return bound_;
  }

  Step step(size_t quantum) { return cursor_.step(quantum); }

  bool complete() const { return cursor_.complete(); }
  bool denied() const { return cursor_.denied(); }
  DenyReason denyReason() const { return cursor_.denyReason(); }
  size_t lastStepUnits() const { return cursor_.lastStepUnits(); }
  size_t syntheticOffs() const { return cursor_.syntheticOffs(); }
  size_t pairsRemoved() const { return cursor_.pairsRemoved(); }
  size_t assignedIds() const { return cursor_.assignedIds(); }

  void discard() {
    cursor_.discard();
    bound_ = false;
  }

 private:
  RecordPreparationCursor cursor_{};
  Workspace workspace_{};
  Key key_{};
  uint64_t generationToken_ = 0;
  bool bound_ = false;
};

}  // namespace RecordPreparation
