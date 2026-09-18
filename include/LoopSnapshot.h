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

/// DEC-073 publication coverage. A published or pending snapshot root records the committed-pass
/// revision its content was **enumerated and constructed from** (`coveredPassRevision`).
/// `Loop::publishedSnapshotIsCurrent()` requires that coverage to equal the loop's current
/// committed-pass revision (`Loop::committedPassRevision()`), so a root can never become current
/// merely because a later revision was reached after it was built.
/// `kSnapshotCoverageUnset` means no coverage was recorded: such a root is conservatively not
/// current. `kSnapshotCoverageCurrent` is the explicit "derived from the current state in this same
/// turn" stamp used by synchronous publication and the native test hook.
inline constexpr uint32_t kSnapshotCoverageUnset = 0xFFFFFFFFu;
inline constexpr uint32_t kSnapshotCoverageCurrent = 0xFFFFFFFEu;

constexpr uint16_t kLoopSnapshotPitchCount = 128;
constexpr uint16_t kLoopSnapshotBarsPerPage = 32;
constexpr uint16_t kSnapshotMembershipPageCapacity = 16;
/// Explicit bar-membership design envelope (DEC-047 amendment 2026-09-16).
/// The maximum reference count representable in one bar, applied equally to the
/// event bar index and to each note pitch/bar list. The page-count limit is
/// derived from it rather than being an accidental side effect of the page
/// constant. 4096 refs is 16x the prior 256-ref (16-page) ceiling and ~15.7x
/// the observed dense device bar (260 refs); pages stay lazily allocated, so
/// the envelope costs nothing until content uses it. A bar can never
/// legitimately need the whole-store theoretical maximum.
constexpr size_t kSnapshotMembershipMaxRefsPerBar = 4096;
constexpr size_t kSnapshotMembershipMaxPages =
    (kSnapshotMembershipMaxRefsPerBar + kSnapshotMembershipPageCapacity - 1) /
    kSnapshotMembershipPageCapacity;
static_assert(kSnapshotMembershipMaxPages <= UINT16_MAX,
              "bar membership page count must fit the uint16_t page counters");
constexpr uint32_t kSnapshotSlabBytes = 32 * 1024;

void resetSnapshotAllocCounters();
uint32_t snapshotAllocNodeCount();
uint32_t snapshotAllocByteCount();
uint32_t snapshotAllocSlabCount();
void emitSnapshotAllocCounters();
void emitSnapshotAllocFailure(const char* site, size_t bytes);

/// Live-slab census, for attributing the arena's *slope* rather than its peak.
///
/// `snapshotAllocByteCount()`/`snapshotAllocSlabCount()` are cumulative: they only ever
/// increment, so a rising `snap=` in a `[Memory] ext` line is not by itself evidence of
/// retention. Deciding whether the arena creeps because slabs are genuinely retained or
/// because a retiring generation strands mostly-empty slabs needs the two quantities the
/// cumulative counters cannot express: how many slabs are live, and how much of each live
/// slab is unreachable. This is O(live slabs) and diagnostic-only -- the allocator's free
/// authority stays `SnapshotSlab::liveCount`, so reading this can never change a decision.
struct SnapshotSlabCensus {
  /// Slabs currently held by the arena (opened and not yet returned to the pool).
  uint32_t liveSlabs = 0;
  /// `liveSlabs * kSnapshotSlabBytes`.
  uint32_t liveSlabBytes = 0;
  /// Sum of each live slab's bump high-water, i.e. the bytes any node ever used.
  uint32_t usedBytes = 0;
  /// `liveSlabBytes - usedBytes`: payload inside live slabs that no node ever used.
  uint32_t unusedBytes = 0;
  /// Live slabs holding at most one node -- the pinning signature. A slab here is retained
  /// by a single node while its remaining payload is unreachable.
  uint32_t slabsAtMostOneNode = 0;
  /// Live slabs holding 2..4 nodes.
  uint32_t slabsTwoToFourNodes = 0;
  /// Live slabs with >4 nodes whose payload is at least three quarters used.
  uint32_t slabsMostlyFull = 0;
  /// The remainder: >4 nodes, less than three quarters used.
  uint32_t slabsPartiallyUsed = 0;
};

SnapshotSlabCensus snapshotSlabCensus();

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

// Child ownership slots are mutable solely for final-reference release.
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

enum class SnapshotChangeType : uint8_t {
  Add,
  Replace,
  Remove,
};

/// Existing semantic owners provide the previous reference and the settled next
/// value. Snapshot construction does not recompute overlap or pairing.
struct SnapshotNoteChange {
  SnapshotChangeType kind = SnapshotChangeType::Add;
  LoopSnapshotNoteRef previous;
  LoopSnapshotNote next;
};

/// Existing event-stream order is carried into the indexed event view.
struct SnapshotEventChange {
  SnapshotChangeType kind = SnapshotChangeType::Add;
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
  /// Owner-local final-reference release; drain before destroying during active
  /// transport. No semantic reader or weak-reference resurrection may acquire the
  /// released root. Shared descendants remain untouched. Fixed depth, no allocation.
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

/// Why a `LoopSnapshotConstruction` stopped without completing. The publisher
/// uses this to distinguish a transient/allocation failure (retry the exact
/// fragment) from a persistent representability failure (keep the previous
/// root, block publication, emit a diagnostic). DEC-047 amendment 2026-09-16.
enum class LoopSnapshotFailureReason : uint8_t {
  None = 0,
  /// A bar reached `kSnapshotMembershipMaxRefsPerBar`; the committed content
  /// is not representable under the current envelope.
  MembershipCapacity,
  /// A snapshot page/object allocation failed.
  Allocation,
  /// Native-only fault injection (partial-apply retry fixture).
  Injected,
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
  LoopSnapshotFailureReason failureReason() const { return failureReason_; }
  size_t completedUnitCount() const { return completedUnits_; }
  size_t totalChangeCount() const { return changes_.notes.size() + changes_.events.size(); }

#if defined(PIO_UNIT_TEST_NATIVE)
  /// Native-only fault injection: fail once `units` construction units have
  /// been applied, so a partial-apply-then-retry transaction is testable.
  /// `kNoFailureInjection` disables it. The reason selects transient (retry) or
  /// representability (park) failure behavior.
  static constexpr size_t kNoFailureInjection = static_cast<size_t>(-1);
  void failAfterUnitsForTest(
      size_t units, LoopSnapshotFailureReason reason = LoopSnapshotFailureReason::Injected) {
    failAfterUnits_ = units;
    injectedReason_ = reason;
  }
#endif

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
  size_t failAfterUnits_ = static_cast<size_t>(-1);
  bool failed_ = false;
  LoopSnapshotFailureReason failureReason_ = LoopSnapshotFailureReason::None;
#if defined(PIO_UNIT_TEST_NATIVE)
  LoopSnapshotFailureReason injectedReason_ = LoopSnapshotFailureReason::Injected;
#endif
  mutable std::shared_ptr<const LoopSnapshot> frozenCandidate_;
};
