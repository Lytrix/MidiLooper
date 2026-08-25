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

struct LoopSnapshotNote {
  LoopId loopId = kInvalidLoopId;
  NoteId noteId = kInvalidNoteId;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint32_t loopLengthTicks = 0;

  bool isWrap() const {
    return loopLengthTicks > 0 && endTick < startTick;
  }

  void appendCanonicalSpans(CanonicalNoteSpanVec& out) const;
};

struct LoopSnapshotEvent {
  LoopId loopId = kInvalidLoopId;
  EventId eventId = kInvalidEventId;
  uint32_t canonicalOrder = 0;
  MidiEvent value{};
};

using LoopSnapshotNoteRef = std::shared_ptr<const LoopSnapshotNote>;
using LoopSnapshotEventRef = std::shared_ptr<const LoopSnapshotEvent>;

template <typename Reference>
using SnapshotReferenceVec =
    std::vector<Reference, ExternalMemoryFirstAllocator<Reference>>;

template <typename Reference>
struct SnapshotMembership {
  SnapshotReferenceVec<Reference> references;
};

template <typename Reference>
using SnapshotMembershipRef = std::shared_ptr<const SnapshotMembership<Reference>>;

template <typename Reference>
struct SnapshotBarPage {
  std::array<SnapshotMembershipRef<Reference>, kLoopSnapshotBarsPerPage>
      memberships{};
};

template <typename Reference>
using SnapshotBarPageRef = std::shared_ptr<const SnapshotBarPage<Reference>>;

template <typename Reference>
struct SnapshotBarPageDirectory {
  std::vector<SnapshotBarPageRef<Reference>,
              ExternalMemoryFirstAllocator<SnapshotBarPageRef<Reference>>>
      pages;
};

struct SnapshotPitchDirectory {
  SnapshotBarPageDirectory<LoopSnapshotNoteRef> pages;
};

using SnapshotPitchDirectoryRef = std::shared_ptr<const SnapshotPitchDirectory>;

struct SnapshotSpanRoot {
  std::array<SnapshotPitchDirectoryRef, kLoopSnapshotPitchCount> pitches{};
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
using ImmutableLoopSnapshotRef = std::shared_ptr<const LoopSnapshot>;

/// Complete immutable effective-state root after construction is frozen.
class LoopSnapshot {
 public:
  LoopSnapshot() = default;
  LoopSnapshot(LoopId loopId, uint32_t loopLengthTicks, uint32_t loopStartTick,
               uint32_t nextNoteId, uint32_t nextEventId);

  static ImmutableLoopSnapshotRef createEmpty(LoopId loopId, uint32_t loopLengthTicks,
                                              uint32_t loopStartTick, uint32_t nextNoteId,
                                              uint32_t nextEventId);

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

  void appendCanonicalSpans(const LoopSnapshotNoteRef& note,
                            CanonicalNoteSpanVec& out) const;

 private:
  friend class LoopSnapshotConstruction;

  bool applyNoteChange(const SnapshotNoteChange& change);
  bool applyEventChange(const SnapshotEventChange& change);

  LoopId loopId_ = kInvalidLoopId;
  uint32_t loopLengthTicks_ = 0;
  uint32_t loopStartTick_ = 0;
  uint32_t nextNoteId_ = 1;
  uint32_t nextEventId_ = 1;
  std::shared_ptr<const SnapshotSpanRoot> spanRoot_;
  std::shared_ptr<const SnapshotEventRoot> eventRoot_;
};

/// Candidate construction with persistent progress. Publication is owned by
/// `Loop` at session-exit and NOTE_EDIT close only.
class LoopSnapshotConstruction {
 public:
  LoopSnapshotConstruction(std::shared_ptr<const LoopSnapshot> parent,
                            const LoopSnapshotChangeSet& changes);

  bool step(size_t maximumChanges);
  bool complete() const { return nextChangeIndex_ == totalChangeCount(); }
  bool failed() const { return failed_; }
  size_t completedChangeCount() const { return nextChangeIndex_; }
  size_t totalChangeCount() const { return changes_.notes.size() + changes_.events.size(); }

  const LoopSnapshot& candidate() const { return candidate_; }
  std::shared_ptr<const LoopSnapshot> freeze() const;

 private:
  std::shared_ptr<const LoopSnapshot> parent_;
  LoopSnapshot candidate_;
  LoopSnapshotChangeSet changes_;
  size_t nextChangeIndex_ = 0;
  bool failed_ = false;
  mutable std::shared_ptr<const LoopSnapshot> frozenCandidate_;
};
