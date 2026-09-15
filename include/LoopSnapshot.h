//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Globals.h"
#include "LoopPasses.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Utils/IntervalProjection.h"

/// Stable loop-scoped identity for a non-note event object.
using EventId = uint32_t;
constexpr EventId kInvalidEventId = 0;

constexpr uint16_t kLoopSnapshotPitchCount = 128;
constexpr uint16_t kLoopSnapshotBarsPerPage = 32;
constexpr uint16_t kSnapshotMembershipPageCapacity = 16;
constexpr uint16_t kSnapshotMembershipMaxPages = 16;
constexpr uint32_t kSnapshotSlabBytes = 32 * 1024;

void resetSnapshotAllocCounters();
uint32_t snapshotAllocNodeCount();
uint32_t snapshotAllocByteCount();
uint32_t snapshotAllocSlabCount();
void emitSnapshotAllocCounters();
void emitSnapshotAllocFailure(const char* site, size_t bytes);

/// Loaded adopt/restore publication on `PendingSnapshotPublishBox`.
/// Session-exit work stays `None`.
enum class LoadedSnapshotHydrationPhase : uint8_t {
  None = 0,
  Queued = 1,
  Constructing = 2,
};
constexpr uint16_t kSnapshotWrapJournalMaxChanges = 32;
constexpr uint16_t kSnapshotWrapJournalMaxAffectedBars = 32;
constexpr uint16_t kSnapshotWrapJournalMaxMembershipEntries = 64;
constexpr uint16_t kSnapshotWrapJournalMaxBytes = 4096;

struct LoopSnapshotNote {
  LoopId loopId = kInvalidLoopId;
  NoteId noteId = kInvalidNoteId;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  uint32_t startTick = 0;
  uint32_t endTick = 0;

  bool isWrap(uint32_t loopLengthTicks) const {
    return loopLengthTicks > 0 && endTick < startTick;
  }

  void appendCanonicalSpans(uint32_t loopLengthTicks, CanonicalNoteSpanVec& out) const;
};

struct LoopSnapshotEvent {
  LoopId loopId = kInvalidLoopId;
  EventId eventId = kInvalidEventId;
  uint32_t canonicalOrder = 0;
  MidiEvent value{};
};

using LoopSnapshotNoteRef = std::shared_ptr<const LoopSnapshotNote>;
using LoopSnapshotEventRef = std::shared_ptr<const LoopSnapshotEvent>;

// Child ownership slots are mutable solely for final-reference retirement.
// Published/shared nodes remain immutable; release only clears exclusive nodes.
template <typename Reference>
struct SnapshotMembership {
  mutable std::array<Reference, kSnapshotMembershipPageCapacity> references{};
  uint8_t count = 0;
  mutable std::shared_ptr<const SnapshotMembership<Reference>> next;
};

template <typename Reference>
inline size_t snapshotMembershipCount(
    const SnapshotMembership<Reference>* membership) {
  size_t total = 0;
  while (membership != nullptr) {
    total += membership->count;
    membership = membership->next.get();
  }
  return total;
}

template <typename Reference, typename Visitor>
inline void forEachSnapshotMembershipReference(
    const SnapshotMembership<Reference>* membership, Visitor&& visitor) {
  while (membership != nullptr) {
    for (uint8_t index = 0; index < membership->count; ++index) {
      visitor(membership->references[index]);
    }
    membership = membership->next.get();
  }
}

template <typename Reference>
using SnapshotMembershipRef = std::shared_ptr<const SnapshotMembership<Reference>>;

template <typename Reference>
struct SnapshotBarPage {
  mutable std::array<SnapshotMembershipRef<Reference>, kLoopSnapshotBarsPerPage>
      memberships{};
};

template <typename Reference>
using SnapshotBarPageRef = std::shared_ptr<const SnapshotBarPage<Reference>>;

template <typename Reference>
struct SnapshotBarPageDirectory {
  mutable std::vector<SnapshotBarPageRef<Reference>,
              ExternalMemoryFirstAllocator<SnapshotBarPageRef<Reference>>>
      pages;
};

struct SnapshotPitchDirectory {
  SnapshotBarPageDirectory<LoopSnapshotNoteRef> pages;
};

using SnapshotPitchDirectoryRef = std::shared_ptr<const SnapshotPitchDirectory>;

struct SnapshotSpanRoot {
  mutable std::array<SnapshotPitchDirectoryRef, kLoopSnapshotPitchCount> pitches{};
};

struct SnapshotEventRoot {
  SnapshotBarPageDirectory<LoopSnapshotEventRef> pages;
};

enum class SnapshotChangeKind : uint8_t {
  Add,
  Replace,
  Remove,
};

/// Existing semantic owners provide the previous reference and the settled next
/// value. Snapshot construction does not recompute overlap or pairing.
struct SnapshotNoteChange {
  SnapshotChangeKind kind = SnapshotChangeKind::Add;
  LoopSnapshotNoteRef previous;
  LoopSnapshotNote next;
};

/// Existing event-stream order is carried into the indexed event view.
struct SnapshotEventChange {
  SnapshotChangeKind kind = SnapshotChangeKind::Add;
  LoopSnapshotEventRef previous;
  LoopSnapshotEvent next;
};

struct LoopSnapshotChangeSet {
  std::vector<SnapshotNoteChange> notes;
  std::vector<SnapshotEventChange> events;
};

class LoopSnapshot;
class LoopSnapshotConstruction;
struct LoopSnapshotNoteBarApply;
struct LoopSnapshotEventBarApply;
using ImmutableLoopSnapshotRef = std::shared_ptr<const LoopSnapshot>;

struct SnapshotWrapJournalFragment {
  ImmutableLoopSnapshotRef parent;
  LoopSnapshotChangeSet changes;
  PassId wrapPassId = kInvalidPassId;
  /// `playbackRevision` the completed candidate represents, stamped by
  /// `Loop::stampWrapSnapshotJournalRevision` once the wrap seal is done. Zero
  /// means unstamped: the candidate is not selectable until session exit.
  uint32_t targetRevision = 0;
  uint8_t fragmentIndex = 0;
  uint8_t fragmentCount = 1;
};

/// Complete immutable effective-state root after construction is frozen.
class LoopSnapshot {
 public:
  /// Owner-local final-reference retirement; drain before destroying during active
  /// transport. No semantic reader or weak-reference resurrection may acquire the
  /// retired root. Shared descendants remain untouched. Fixed depth, no allocation.
  class ReleaseCursor {
   public:
    ReleaseCursor() = default;
    ReleaseCursor(const ReleaseCursor&) = delete;
    ReleaseCursor& operator=(const ReleaseCursor&) = delete;
    bool complete() const { return !root_; }
    size_t completedUnits() const { return units_; }
   private:
    friend class LoopSnapshot;
    ImmutableLoopSnapshotRef root_;
    std::array<size_t, 5> positions_{};
    uint8_t phase_ = 0;
    size_t units_ = 0;
  };
  /// Consumes the caller's reference only when the cursor is idle.
  static bool beginRelease(ReleaseCursor& cursor, ImmutableLoopSnapshotRef& root);
  /// Each unit visits one child ownership slot or releases one emptied node.
  /// Destruction visits at most 128 empty fixed-array slots, never a variable
  /// directory or linked membership tail. Zero quantum performs no work.
  static bool stepRelease(ReleaseCursor& cursor, size_t maximumUnits);

  LoopSnapshot() = default;
  LoopSnapshot(LoopId loopId, uint32_t loopLengthTicks, uint32_t loopStartTick,
               uint32_t nextNoteId, uint32_t nextEventId);
  LoopSnapshot(const LoopSnapshot& other, uint32_t loopLengthTicks);

  static ImmutableLoopSnapshotRef createEmpty(LoopId loopId, uint32_t loopLengthTicks,
                                              uint32_t loopStartTick, uint32_t nextNoteId,
                                              uint32_t nextEventId);
  ImmutableLoopSnapshotRef withLoopLengthTicks(uint32_t loopLengthTicks) const;

  LoopId loopId() const { return loopId_; }
  uint32_t loopLengthTicks() const { return loopLengthTicks_; }
  uint32_t loopStartTick() const { return loopStartTick_; }
  uint32_t nextNoteId() const { return nextNoteId_; }
  uint32_t nextEventId() const { return nextEventId_; }

  SnapshotMembershipRef<LoopSnapshotNoteRef> noteMembership(
      uint8_t pitch, uint32_t barIndex) const;
  SnapshotMembershipRef<LoopSnapshotEventRef> eventMembership(
      uint32_t barIndex) const;
  LoopSnapshotNoteRef findNote(NoteId noteId, uint8_t pitchHint = 0) const;
  /// One-pitch identity lookup. Does not walk other pitches on a miss.
  LoopSnapshotNoteRef findNoteAtPitch(NoteId noteId, uint8_t pitch) const;

  void appendCanonicalSpans(const LoopSnapshotNoteRef& note,
                            CanonicalNoteSpanVec& out) const;

 private:
  friend class LoopSnapshotConstruction;
  friend struct LoopSnapshotNoteBarApply;
  friend struct LoopSnapshotEventBarApply;

  LoopId loopId_ = kInvalidLoopId;
  uint32_t loopLengthTicks_ = 0;
  uint32_t loopStartTick_ = 0;
  uint32_t nextNoteId_ = 1;
  uint32_t nextEventId_ = 1;
  mutable std::shared_ptr<const SnapshotSpanRoot> spanRoot_;
  mutable std::shared_ptr<const SnapshotEventRoot> eventRoot_;
};

/// Candidate construction with persistent bar-cursor progress. Publication is
/// owned by `Loop` at session-exit, wrap-journal idle, and NOTE_EDIT close.
class LoopSnapshotConstruction {
 public:
  LoopSnapshotConstruction(std::shared_ptr<const LoopSnapshot> parent,
                            const LoopSnapshotChangeSet& changes);
  ~LoopSnapshotConstruction();

  bool step(size_t maximumUnits, uint32_t playheadPhaseTick = 0);
  bool complete() const {
    return nextChangeIndex_ == totalChangeCount() && !noteApply_ && !eventApply_;
  }
  bool failed() const { return failed_; }
  size_t completedChangeCount() const { return nextChangeIndex_; }
  size_t completedUnitCount() const { return completedUnits_; }
  size_t totalChangeCount() const { return changes_.notes.size() + changes_.events.size(); }

  const LoopSnapshot& candidate() const { return candidate_; }
  const LoopSnapshotChangeSet& changes() const { return changes_; }
  void handoffLoopLengthTicks(uint32_t loopLengthTicks);
  std::shared_ptr<const LoopSnapshot> freeze() const;

 private:
  bool applyNextBar(uint32_t playheadPhaseTick);

  std::shared_ptr<const LoopSnapshot> parent_;
  LoopSnapshot candidate_;
  LoopSnapshotChangeSet changes_;
  std::shared_ptr<SnapshotSpanRoot> constructionSpanRoot_;
  std::shared_ptr<SnapshotEventRoot> constructionEventRoot_;
  std::unique_ptr<LoopSnapshotNoteBarApply> noteApply_;
  std::unique_ptr<LoopSnapshotEventBarApply> eventApply_;
  size_t nextChangeIndex_ = 0;
  size_t completedUnits_ = 0;
  bool failed_ = false;
  mutable std::shared_ptr<const LoopSnapshot> frozenCandidate_;
};
