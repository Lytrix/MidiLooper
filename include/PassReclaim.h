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

/// Task 3.6c (DEC-108 item 14(c)): the **interval index over pinned bodies**. One bit per bar records whether any
/// pinned body carries content in that bar, so an interval query can **skip** the bars -- and therefore the chunks --
/// that cannot intersect. That skip is the whole point: it is what makes lookup interval-directed instead of a scan
/// whose cost grows with unrelated loop content.
///
/// It is a **descriptor**, never an event copy: `TickIndex`'s whole-history copy storage is deliberately **not**
/// inherited, and `CommittedEventRange` stays the body-reading primitive. Its ownership is the retained source's
/// (built at admission, replaced by a single assignment, refused when its revision is not the one asked for), its
/// resident memory is bounded by `kRetainedIndexMaxBars` (the bar ceiling; beyond it the index **refuses by name**, so
/// the caller must account the complete scan in admission rather than present it as window-bounded), and its lifetime
/// is the source revision it was built for. Updates are a **rebuild per admitted revision**, not a dual-range patch:
/// that covers both the old and the new affected ranges by construction and can never modify an index a reader holds.
constexpr uint32_t kRetainedIndexMaxBars = 512;
constexpr size_t kRetainedIndexMaxWords = kRetainedIndexMaxBars / 32;

struct RetainedIntervalIndex {
  uint32_t revision = 0;
  uint32_t generation = 0;
  uint32_t bars = 0;          ///< bars indexed at build time (the loop's bar count then)
  uint16_t spansIndexed = 0;  ///< pinned body spans folded in -- reported, not guessed
  bool published = false;     ///< false until a build has published one
  std::vector<uint32_t, InternalHeapFirstAllocator<uint32_t>> bodyWords;

  bool coversBar(uint32_t bar) const {
    const size_t word = bar / 32u;
    return word < bodyWords.size() && ((bodyWords[word] >> (bar % 32u)) & 1u) != 0;
  }
  void clear() {
    revision = 0;
    generation = 0;
    bars = 0;
    spansIndexed = 0;
    published = false;
    bodyWords.clear();
  }
};
