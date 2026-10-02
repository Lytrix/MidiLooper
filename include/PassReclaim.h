//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "GlobalUndoStack.h"
#include "Globals.h"
#include "LoopPasses.h"
#include "StorageLoopIo.h"
#include "Utils/InternalHeapFirstAllocator.h"

struct SlotPassReferences {
  std::vector<PassId, InternalHeapFirstAllocator<PassId>> capturePassIds;
  std::vector<EditPassId, InternalHeapFirstAllocator<EditPassId>> editPassIds;

  void pinCapturePass(PassId id) {
    if (id == kInvalidPassId) {
      return;
    }
    for (PassId pinned : capturePassIds) {
      if (pinned == id) {
        return;
      }
    }
    capturePassIds.push_back(id);
  }

  void pinEditPass(EditPassId id) {
    if (id == kInvalidEditPassId) {
      return;
    }
    for (EditPassId pinned : editPassIds) {
      if (pinned == id) {
        return;
      }
    }
    editPassIds.push_back(id);
  }

  bool referencesCapturePass(PassId id) const {
    for (PassId pinned : capturePassIds) {
      if (pinned == id) {
        return true;
      }
    }
    return false;
  }

  bool referencesEditPass(EditPassId id) const {
    for (EditPassId pinned : editPassIds) {
      if (pinned == id) {
        return true;
      }
    }
    return false;
  }
};

struct PassReferenceSet {
  SlotPassReferences slots[Config::MAX_LOOPS_PER_TRACK];
};

void pinPassesFromLoopPasses(const LoopPasses& passes, SlotPassReferences& refs);
void collectReferencedPasses(const GlobalUndoStack& stack, PassReferenceSet& out);

bool overUndoMemoryPressure(const GlobalUndoStack& stack);

/// windowed-playback-generation task 3.6 / DEC-108 items 4, 12(a) and 13(a): the **source roles**. A retained
/// revision is admitted in a named role rather than counted as "one revision per slot" (item 13(a) replaced that
/// clause): `Serving` is the revision a window serves, `Candidate` the newer revision being prepared, and
/// `Retiring` a source that has stopped serving and awaits the one release predicate. The role vocabulary is what
/// keeps "a still-needed source is never discarded to satisfy a count" checkable.
enum class RetainedSourceRole : uint8_t { None, Serving, Candidate, Retiring };

/// A **retained source revision**: the immutable description of a revision's pass membership — an identity
/// (`revision`, `generation`), **descriptor protection** (`pins`, the set the pass-level reclamation consumer
/// reads) and **actual body ownership** (`ownedChunks`, the store references this holder owns, so the direct
/// reset/replacement path's `releaseChunkRefs` cannot free or silently alias them). The ownership is the spec's
/// own clause ("with actual body ownership"): it is what makes the eleven `Loop::resetPassTimeline` call sites safe
/// without a veto in any of them -- they release the loop's reference, and this holder keeps its own until the one
/// release predicate drops it.
struct RetainedPlaybackSource {
  uint32_t revision = 0;
  uint32_t generation = 0;
  RetainedSourceRole role = RetainedSourceRole::None;
  SlotPassReferences pins;
  CommittedChunkIdList ownedChunks;

  bool isRetained() const { return role != RetainedSourceRole::None; }

  void clear() {
    revision = 0;
    generation = 0;
    role = RetainedSourceRole::None;
    pins = SlotPassReferences{};
    ownedChunks.clear();
  }
};

/// The bounded role set: one serving source plus one candidate/retiring source covers the lane's own sequencing
/// (a single build at a time means a third source cannot be needed), and an admission that would exceed it is
/// refused **by name** rather than discarding a source that is still needed (item 13(a)).
constexpr size_t kMaxRetainedPlaybackSources = 2;

/// Trim global undo entries under depth/pressure rails. Prefers dropping entries before
/// cursor (undo history) so the redo branch at cursor..end stays intact when cursor > 0.
size_t trimGlobalUndoStackForMemory(GlobalUndoStack& stack);

struct PassReclaimStats {
  uint16_t passesReclaimed = 0;
  uint16_t chunksReleased = 0;
  uint16_t chunksFreeBefore = 0;
  uint16_t chunksFreeAfter = 0;
  uint32_t durationUs = 0;
};

/// Feedback trimming of an exclusively owned, unpublished stack. The enclosing
/// Track transaction must hide it from readers/mutators and cross its no-fail
/// point before begin: retirement cannot be rolled back. No pass reclaim occurs
/// here; the transaction schedules it after prepared canonical proofs are retired.
class GlobalUndoStackTrimCursor {
 public:
  bool begin(GlobalUndoStack& privateStack);
  bool step(size_t maximumUnits);
  bool complete() const { return stack_ == nullptr; }
  size_t trimmedCount() const { return trimmed_; }
  size_t completedUnits() const { return units_; }
 private:
  GlobalUndoStack* stack_ = nullptr;
  UndoEntryReleaseCursor release_;
  size_t shift_ = 0;
  size_t trimmed_ = 0;
  size_t units_ = 0;
  uint8_t phase_ = 0;
};
