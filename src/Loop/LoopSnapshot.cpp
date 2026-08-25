//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopSnapshot.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <memory>
#include <utility>

#if defined(__IMXRT1062__)
#define LOOP_SNAPSHOT_COLD_MEM __attribute__((noinline, section(".flashmem")))
#else
#define LOOP_SNAPSHOT_COLD_MEM
#endif

namespace {

template <typename Type, typename... Arguments>
LOOP_SNAPSHOT_COLD_MEM std::shared_ptr<Type> allocateSnapshotShared(
    Arguments&&... arguments) {
  return std::allocate_shared<Type>(ExternalMemoryFirstAllocator<Type>{},
                                    std::forward<Arguments>(arguments)...);
}

template <typename Type, typename... Arguments>
LOOP_SNAPSHOT_COLD_MEM std::shared_ptr<const Type> allocateConstSnapshotShared(
    Arguments&&... arguments) {
  return allocateSnapshotShared<Type>(
      std::forward<Arguments>(arguments)...);
}

template <typename Reference>
struct MutableMembershipEdit {
  SnapshotBarPageDirectory<Reference>* directory = nullptr;
  uint32_t barIndex = 0;
  std::shared_ptr<SnapshotMembership<Reference>> membership;
};

template <typename Reference>
struct MutablePageEdit {
  SnapshotBarPageDirectory<Reference>* directory = nullptr;
  size_t pageIndex = 0;
  std::shared_ptr<SnapshotBarPage<Reference>> page;
};

template <typename Reference>
LOOP_SNAPSHOT_COLD_MEM size_t pageIndexForBar(uint32_t barIndex) {
  return static_cast<size_t>(barIndex / kLoopSnapshotBarsPerPage);
}

template <typename Reference>
LOOP_SNAPSHOT_COLD_MEM size_t pageOffsetForBar(uint32_t barIndex) {
  return static_cast<size_t>(barIndex % kLoopSnapshotBarsPerPage);
}

template <typename Reference>
LOOP_SNAPSHOT_COLD_MEM SnapshotMembershipRef<Reference> membershipForBar(
    const SnapshotBarPageDirectory<Reference>& directory, uint32_t barIndex) {
  const size_t pageIndex = pageIndexForBar<Reference>(barIndex);
  if (pageIndex >= directory.pages.size() || !directory.pages[pageIndex]) {
    return {};
  }
  return directory.pages[pageIndex]->memberships[pageOffsetForBar<Reference>(barIndex)];
}

template <typename Reference>
LOOP_SNAPSHOT_COLD_MEM
std::shared_ptr<SnapshotMembership<Reference>> mutableMembershipForBar(
    SnapshotBarPageDirectory<Reference>& directory,
    std::vector<MutablePageEdit<Reference>>& pageEdits,
    std::vector<MutableMembershipEdit<Reference>>& membershipEdits,
    uint32_t barIndex) {
  for (auto& edit : membershipEdits) {
    if (edit.directory == &directory && edit.barIndex == barIndex) {
      return edit.membership;
    }
  }

  const size_t pageIndex = pageIndexForBar<Reference>(barIndex);
  std::shared_ptr<SnapshotBarPage<Reference>> page;
  for (auto& edit : pageEdits) {
    if (edit.directory == &directory && edit.pageIndex == pageIndex) {
      page = edit.page;
      break;
    }
  }

  if (!page) {
    page = allocateSnapshotShared<SnapshotBarPage<Reference>>();
    if (pageIndex < directory.pages.size() && directory.pages[pageIndex]) {
      *page = *directory.pages[pageIndex];
    }
    if (directory.pages.size() <= pageIndex) {
      directory.pages.resize(pageIndex + 1);
    }
    directory.pages[pageIndex] = page;
    pageEdits.push_back({&directory, pageIndex, page});
  }

  const size_t pageOffset = pageOffsetForBar<Reference>(barIndex);
  auto membership = allocateSnapshotShared<SnapshotMembership<Reference>>();
  if (page->memberships[pageOffset]) {
    membership->references = page->memberships[pageOffset]->references;
  }
  page->memberships[pageOffset] = membership;
  membershipEdits.push_back({&directory, barIndex, membership});
  return membership;
}

LOOP_SNAPSHOT_COLD_MEM void appendBarRange(std::vector<uint32_t>& bars,
                                           uint32_t startTick,
                                           uint32_t endTick,
                                           uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return;
  }
  startTick = std::min(startTick, loopLengthTicks);
  endTick = std::min(endTick, loopLengthTicks);
  if (startTick >= endTick) {
    return;
  }

  const uint32_t firstBar = startTick / Config::TICKS_PER_BAR;
  const uint32_t lastBar = (endTick - 1) / Config::TICKS_PER_BAR;
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    bars.push_back(bar);
  }
}

LOOP_SNAPSHOT_COLD_MEM void appendNoteBars(const LoopSnapshotNote& note,
                                           std::vector<uint32_t>& bars) {
  if (note.isWrap()) {
    appendBarRange(bars, note.startTick, note.loopLengthTicks,
                   note.loopLengthTicks);
    appendBarRange(bars, 0, note.endTick, note.loopLengthTicks);
    return;
  }
  appendBarRange(bars, note.startTick, note.endTick, note.loopLengthTicks);
}

LOOP_SNAPSHOT_COLD_MEM void sortUniqueBars(std::vector<uint32_t>& bars) {
  std::sort(bars.begin(), bars.end());
  bars.erase(std::unique(bars.begin(), bars.end()), bars.end());
}

LOOP_SNAPSHOT_COLD_MEM NoteId referenceId(
    const LoopSnapshotNoteRef& reference) {
  return reference ? reference->noteId : kInvalidNoteId;
}

LOOP_SNAPSHOT_COLD_MEM EventId referenceId(
    const LoopSnapshotEventRef& reference) {
  return reference ? reference->eventId : kInvalidEventId;
}

template <typename Reference>
LOOP_SNAPSHOT_COLD_MEM void removeReference(
    SnapshotMembership<Reference>& membership, const Reference& target) {
  const auto targetId = referenceId(target);
  auto& references = membership.references;
  references.erase(
      std::remove_if(references.begin(), references.end(),
                     [targetId](const Reference& reference) {
                       return referenceId(reference) == targetId;
                     }),
      references.end());
}

LOOP_SNAPSHOT_COLD_MEM void addNoteReference(
    SnapshotMembership<LoopSnapshotNoteRef>& membership,
    const LoopSnapshotNoteRef& reference) {
  removeReference(membership, reference);
  membership.references.push_back(reference);
}

LOOP_SNAPSHOT_COLD_MEM void addEventReference(
    SnapshotMembership<LoopSnapshotEventRef>& membership,
    const LoopSnapshotEventRef& reference) {
  removeReference(membership, reference);
  membership.references.push_back(reference);
  std::sort(membership.references.begin(), membership.references.end(),
            [](const LoopSnapshotEventRef& left,
               const LoopSnapshotEventRef& right) {
              if (left->canonicalOrder != right->canonicalOrder) {
                return left->canonicalOrder < right->canonicalOrder;
              }
              return left->eventId < right->eventId;
            });
}

LOOP_SNAPSHOT_COLD_MEM uint32_t ticksUntilBarReached(uint32_t phaseTick,
                                                     uint32_t barIndex,
                                                     uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return 0;
  }
  const uint32_t phase = phaseTick % loopLengthTicks;
  const uint32_t barStart = barIndex * Config::TICKS_PER_BAR;
  if (barStart >= loopLengthTicks) {
    return loopLengthTicks;
  }
  const uint32_t barEnd =
      std::min(barStart + Config::TICKS_PER_BAR, loopLengthTicks);
  if (phase >= barStart && phase < barEnd) {
    return 0;
  }
  if (phase < barStart) {
    return barStart - phase;
  }
  return (loopLengthTicks - phase) + barStart;
}

LOOP_SNAPSHOT_COLD_MEM void sortBarsBySoonestPlayhead(std::vector<uint32_t>& bars,
                                                      uint32_t phaseTick,
                                                      uint32_t loopLengthTicks) {
  std::sort(bars.begin(), bars.end(),
            [phaseTick, loopLengthTicks](uint32_t left, uint32_t right) {
              const uint32_t leftDistance =
                  ticksUntilBarReached(phaseTick, left, loopLengthTicks);
              const uint32_t rightDistance =
                  ticksUntilBarReached(phaseTick, right, loopLengthTicks);
              if (leftDistance != rightDistance) {
                return leftDistance < rightDistance;
              }
              return left < right;
            });
}

}  // namespace

LOOP_COLD_MEM void LoopSnapshotNote::appendCanonicalSpans(
    CanonicalNoteSpanVec& out) const {
  auto appendSpan = [this, &out](uint32_t startTick, uint32_t endTick,
                                 bool splitHeadTail) {
    if (startTick >= endTick) {
      return;
    }
    CanonicalNoteSpan span{};
    span.noteId = noteId;
    span.interval.start = static_cast<int32_t>(startTick);
    span.interval.end = static_cast<int32_t>(endTick);
    span.pitch = pitch;
    span.velocity = velocity;
    span.isOpen = false;
    span.splitHeadTail = splitHeadTail;
    out.push_back(span);
  };

  if (isWrap()) {
    appendSpan(startTick, loopLengthTicks, true);
    appendSpan(0, endTick, true);
    return;
  }
  appendSpan(startTick, endTick, false);
}

LOOP_COLD_MEM LoopSnapshot::LoopSnapshot(LoopId loopId,
                                         uint32_t loopLengthTicks,
                                         uint32_t loopStartTick,
                                         uint32_t nextNoteId,
                                         uint32_t nextEventId)
    : loopId_(loopId),
      loopLengthTicks_(loopLengthTicks),
      loopStartTick_(loopStartTick),
      nextNoteId_(nextNoteId),
      nextEventId_(nextEventId),
      spanRoot_(allocateConstSnapshotShared<SnapshotSpanRoot>()),
      eventRoot_(allocateConstSnapshotShared<SnapshotEventRoot>()) {}

LOOP_COLD_MEM ImmutableLoopSnapshotRef LoopSnapshot::createEmpty(
    LoopId loopId, uint32_t loopLengthTicks, uint32_t loopStartTick,
    uint32_t nextNoteId, uint32_t nextEventId) {
  return allocateConstSnapshotShared<LoopSnapshot>(
      loopId, loopLengthTicks, loopStartTick, nextNoteId, nextEventId);
}

LOOP_COLD_MEM SnapshotMembershipRef<LoopSnapshotNoteRef>
LoopSnapshot::noteMembership(
    uint8_t pitch, uint32_t barIndex) const {
  if (!spanRoot_ || pitch >= kLoopSnapshotPitchCount) {
    return {};
  }
  const auto& pitchDirectory = spanRoot_->pitches[pitch];
  if (!pitchDirectory) {
    return {};
  }
  return membershipForBar<LoopSnapshotNoteRef>(pitchDirectory->pages, barIndex);
}

LOOP_COLD_MEM SnapshotMembershipRef<LoopSnapshotEventRef>
LoopSnapshot::eventMembership(
    uint32_t barIndex) const {
  if (!eventRoot_) {
    return {};
  }
  return membershipForBar<LoopSnapshotEventRef>(eventRoot_->pages, barIndex);
}

LOOP_COLD_MEM LoopSnapshotNoteRef LoopSnapshot::findNote(NoteId noteId,
                                                        uint8_t pitchHint) const {
  if (noteId == kInvalidNoteId || !spanRoot_) {
    return {};
  }
  auto searchPitch = [this, noteId](uint8_t pitch) -> LoopSnapshotNoteRef {
    if (pitch >= kLoopSnapshotPitchCount) {
      return {};
    }
    const auto& pitchDirectory = spanRoot_->pitches[pitch];
    if (!pitchDirectory) {
      return {};
    }
    for (const auto& page : pitchDirectory->pages.pages) {
      if (!page) {
        continue;
      }
      for (const auto& membership : page->memberships) {
        if (!membership) {
          continue;
        }
        for (const auto& reference : membership->references) {
          if (reference && reference->noteId == noteId) {
            return reference;
          }
        }
      }
    }
    return {};
  };

  if (auto found = searchPitch(pitchHint)) {
    return found;
  }
  for (uint16_t pitch = 0; pitch < kLoopSnapshotPitchCount; ++pitch) {
    if (pitch == pitchHint) {
      continue;
    }
    if (auto found = searchPitch(static_cast<uint8_t>(pitch))) {
      return found;
    }
  }
  return {};
}

LOOP_COLD_MEM void LoopSnapshot::appendCanonicalSpans(
    const LoopSnapshotNoteRef& note, CanonicalNoteSpanVec& out) const {
  if (note && note->loopId == loopId_) {
    note->appendCanonicalSpans(out);
  }
}

struct LoopSnapshotNoteBarApply {
  SnapshotNoteChange change;
  std::vector<uint32_t> remainingBars;
  std::vector<uint32_t> oldBars;
  std::vector<uint32_t> nextBars;
  std::shared_ptr<SnapshotSpanRoot> nextRoot;
  std::vector<std::pair<uint8_t, std::shared_ptr<SnapshotPitchDirectory>>>
      pitchEdits;
  std::vector<MutablePageEdit<LoopSnapshotNoteRef>> pageEdits;
  std::vector<MutableMembershipEdit<LoopSnapshotNoteRef>> membershipEdits;
  LoopSnapshotNoteRef nextReference;

  std::shared_ptr<SnapshotPitchDirectory> mutablePitchDirectory(uint8_t pitch) {
    for (auto& edit : pitchEdits) {
      if (edit.first == pitch) {
        return edit.second;
      }
    }
    auto directory = allocateSnapshotShared<SnapshotPitchDirectory>();
    if (nextRoot->pitches[pitch]) {
      *directory = *nextRoot->pitches[pitch];
    }
    nextRoot->pitches[pitch] = directory;
    pitchEdits.push_back({pitch, directory});
    return directory;
  }

  bool begin(LoopSnapshot& snapshot, const SnapshotNoteChange& incoming) {
    const bool hasPrevious = static_cast<bool>(incoming.previous);
    if (incoming.kind == SnapshotChangeKind::Add && hasPrevious) {
      return false;
    }
    if (incoming.kind != SnapshotChangeKind::Add && !hasPrevious) {
      return false;
    }
    if (incoming.previous && incoming.previous->loopId != snapshot.loopId_) {
      return false;
    }
    if (incoming.kind != SnapshotChangeKind::Remove &&
        (incoming.next.loopId != snapshot.loopId_ ||
         incoming.next.noteId == kInvalidNoteId ||
         incoming.next.pitch >= kLoopSnapshotPitchCount ||
         incoming.next.loopLengthTicks != snapshot.loopLengthTicks_)) {
      return false;
    }
    if (incoming.previous &&
        (incoming.previous->noteId == kInvalidNoteId ||
         (incoming.kind == SnapshotChangeKind::Replace &&
          incoming.previous->noteId != incoming.next.noteId))) {
      return false;
    }

    change = incoming;
    nextRoot = allocateSnapshotShared<SnapshotSpanRoot>(
        snapshot.spanRoot_ ? *snapshot.spanRoot_ : SnapshotSpanRoot{});
    if (incoming.previous) {
      appendNoteBars(*incoming.previous, oldBars);
    }
    if (incoming.kind != SnapshotChangeKind::Remove) {
      appendNoteBars(incoming.next, nextBars);
      nextReference = allocateConstSnapshotShared<LoopSnapshotNote>(incoming.next);
    }
    sortUniqueBars(oldBars);
    sortUniqueBars(nextBars);
    remainingBars = oldBars;
    remainingBars.insert(remainingBars.end(), nextBars.begin(), nextBars.end());
    sortUniqueBars(remainingBars);
    return true;
  }

  bool applySoonestBar(LoopSnapshot& snapshot, uint32_t playheadPhaseTick) {
    if (!remainingBars.empty()) {
      sortBarsBySoonestPlayhead(remainingBars, playheadPhaseTick,
                                snapshot.loopLengthTicks_);
      const uint32_t barIndex = remainingBars.front();
      remainingBars.erase(remainingBars.begin());
      const bool oldMember =
          std::binary_search(oldBars.begin(), oldBars.end(), barIndex);
      const bool nextMember =
          std::binary_search(nextBars.begin(), nextBars.end(), barIndex);
      const uint8_t oldPitch =
          change.previous ? change.previous->pitch : change.next.pitch;
      const uint8_t nextPitch = change.next.pitch;
      if (oldMember) {
        auto directory = mutablePitchDirectory(oldPitch);
        auto membership = mutableMembershipForBar<LoopSnapshotNoteRef>(
            directory->pages, pageEdits, membershipEdits, barIndex);
        removeReference(*membership, change.previous);
      }
      if (nextMember) {
        auto directory = mutablePitchDirectory(nextPitch);
        auto membership = mutableMembershipForBar<LoopSnapshotNoteRef>(
            directory->pages, pageEdits, membershipEdits, barIndex);
        addNoteReference(*membership, nextReference);
      }
      snapshot.spanRoot_ = nextRoot;
    }
    if (!remainingBars.empty()) {
      return true;
    }
    snapshot.spanRoot_ = std::move(nextRoot);
    if (change.kind != SnapshotChangeKind::Remove &&
        change.next.noteId >= snapshot.nextNoteId_) {
      snapshot.nextNoteId_ = change.next.noteId + 1;
    }
    return true;
  }
};

struct LoopSnapshotEventBarApply {
  SnapshotEventChange change;
  std::vector<uint32_t> remainingBars;
  uint32_t oldBar = 0;
  uint32_t nextBar = 0;
  bool removeOld = false;
  bool addNext = false;
  std::shared_ptr<SnapshotEventRoot> nextRoot;
  std::vector<MutablePageEdit<LoopSnapshotEventRef>> pageEdits;
  std::vector<MutableMembershipEdit<LoopSnapshotEventRef>> membershipEdits;
  LoopSnapshotEventRef nextReference;

  bool begin(LoopSnapshot& snapshot, const SnapshotEventChange& incoming) {
    const bool hasPrevious = static_cast<bool>(incoming.previous);
    if (incoming.kind == SnapshotChangeKind::Add && hasPrevious) {
      return false;
    }
    if (incoming.kind != SnapshotChangeKind::Add && !hasPrevious) {
      return false;
    }
    if (incoming.previous && incoming.previous->loopId != snapshot.loopId_) {
      return false;
    }
    if (incoming.kind != SnapshotChangeKind::Remove &&
        (incoming.next.loopId != snapshot.loopId_ ||
         incoming.next.eventId == kInvalidEventId ||
         incoming.next.value.tick >= snapshot.loopLengthTicks_)) {
      return false;
    }
    if (incoming.previous &&
        (incoming.previous->eventId == kInvalidEventId ||
         (incoming.kind == SnapshotChangeKind::Replace &&
          incoming.previous->eventId != incoming.next.eventId))) {
      return false;
    }

    change = incoming;
    oldBar = incoming.previous
                 ? incoming.previous->value.tick / Config::TICKS_PER_BAR
                 : 0;
    nextBar = incoming.kind == SnapshotChangeKind::Remove
                  ? oldBar
                  : incoming.next.value.tick / Config::TICKS_PER_BAR;
    removeOld = static_cast<bool>(incoming.previous);
    addNext = incoming.kind != SnapshotChangeKind::Remove;
    remainingBars.push_back(oldBar);
    if (nextBar != oldBar) {
      remainingBars.push_back(nextBar);
    }
    sortUniqueBars(remainingBars);
    nextRoot = allocateSnapshotShared<SnapshotEventRoot>(
        snapshot.eventRoot_ ? *snapshot.eventRoot_ : SnapshotEventRoot{});
    if (addNext) {
      nextReference = allocateConstSnapshotShared<LoopSnapshotEvent>(incoming.next);
    }
    return true;
  }

  bool applySoonestBar(LoopSnapshot& snapshot, uint32_t playheadPhaseTick) {
    if (!remainingBars.empty()) {
      sortBarsBySoonestPlayhead(remainingBars, playheadPhaseTick,
                                snapshot.loopLengthTicks_);
      const uint32_t barIndex = remainingBars.front();
      remainingBars.erase(remainingBars.begin());
      auto membership = mutableMembershipForBar<LoopSnapshotEventRef>(
          nextRoot->pages, pageEdits, membershipEdits, barIndex);
      if (removeOld && barIndex == oldBar) {
        removeReference(*membership, change.previous);
      }
      if (addNext && barIndex == nextBar) {
        addEventReference(*membership, nextReference);
      }
      snapshot.eventRoot_ = nextRoot;
    }
    if (!remainingBars.empty()) {
      return true;
    }
    snapshot.eventRoot_ = std::move(nextRoot);
    if (change.kind != SnapshotChangeKind::Remove &&
        change.next.eventId >= snapshot.nextEventId_) {
      snapshot.nextEventId_ = change.next.eventId + 1;
    }
    return true;
  }
};

LOOP_COLD_MEM LoopSnapshotConstruction::LoopSnapshotConstruction(
    std::shared_ptr<const LoopSnapshot> parent,
    const LoopSnapshotChangeSet& changes)
    : parent_(std::move(parent)),
      candidate_(parent_ ? *parent_ : LoopSnapshot{}),
      changes_(changes) {
  failed_ = !parent_;
}

LOOP_COLD_MEM LoopSnapshotConstruction::~LoopSnapshotConstruction() = default;

LOOP_COLD_MEM bool LoopSnapshotConstruction::applyNextBar(
    uint32_t playheadPhaseTick) {
  if (noteApply_) {
    if (!noteApply_->applySoonestBar(candidate_, playheadPhaseTick)) {
      return false;
    }
    if (noteApply_->remainingBars.empty()) {
      noteApply_.reset();
      ++nextChangeIndex_;
    }
    return true;
  }
  if (eventApply_) {
    if (!eventApply_->applySoonestBar(candidate_, playheadPhaseTick)) {
      return false;
    }
    if (eventApply_->remainingBars.empty()) {
      eventApply_.reset();
      ++nextChangeIndex_;
    }
    return true;
  }
  if (nextChangeIndex_ < changes_.notes.size()) {
    noteApply_ = std::make_unique<LoopSnapshotNoteBarApply>();
    if (!noteApply_->begin(candidate_, changes_.notes[nextChangeIndex_])) {
      noteApply_.reset();
      return false;
    }
    return applyNextBar(playheadPhaseTick);
  }
  const size_t eventIndex = nextChangeIndex_ - changes_.notes.size();
  eventApply_ = std::make_unique<LoopSnapshotEventBarApply>();
  if (!eventApply_->begin(candidate_, changes_.events[eventIndex])) {
    eventApply_.reset();
    return false;
  }
  return applyNextBar(playheadPhaseTick);
}

LOOP_COLD_MEM bool LoopSnapshotConstruction::step(size_t maximumUnits,
                                                 uint32_t playheadPhaseTick) {
  if (failed_ || complete()) {
    return complete() && !failed_;
  }
  if (maximumUnits == 0) {
    return false;
  }

  size_t processed = 0;
  while (processed < maximumUnits && !complete()) {
    if (!applyNextBar(playheadPhaseTick)) {
      failed_ = true;
      return false;
    }
    ++completedUnits_;
    ++processed;
  }
  return !failed_;
}

LOOP_COLD_MEM std::shared_ptr<const LoopSnapshot>
LoopSnapshotConstruction::freeze() const {
  if (failed_ || !complete()) {
    return {};
  }
  if (!frozenCandidate_) {
    frozenCandidate_ = allocateConstSnapshotShared<LoopSnapshot>(candidate_);
  }
  return frozenCandidate_;
}
