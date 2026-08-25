//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopSnapshot.h"
#include "Utils/LoopMem.h"

#include <algorithm>
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

LOOP_COLD_MEM bool LoopSnapshot::applyNoteChange(
    const SnapshotNoteChange& change) {
  const bool hasPrevious = static_cast<bool>(change.previous);
  if (change.kind == SnapshotChangeKind::Add && hasPrevious) {
    return false;
  }
  if (change.kind != SnapshotChangeKind::Add && !hasPrevious) {
    return false;
  }
  if (change.previous && change.previous->loopId != loopId_) {
    return false;
  }
  if (change.kind != SnapshotChangeKind::Remove &&
      (change.next.loopId != loopId_ ||
       change.next.noteId == kInvalidNoteId ||
       change.next.pitch >= kLoopSnapshotPitchCount ||
       change.next.loopLengthTicks != loopLengthTicks_)) {
    return false;
  }
  if (change.previous &&
      (change.previous->noteId == kInvalidNoteId ||
       (change.kind == SnapshotChangeKind::Replace &&
        change.previous->noteId != change.next.noteId))) {
    return false;
  }

  auto nextRoot = allocateSnapshotShared<SnapshotSpanRoot>(
      spanRoot_ ? *spanRoot_ : SnapshotSpanRoot{});
  std::vector<uint32_t> oldBars;
  std::vector<uint32_t> nextBars;
  if (change.previous) {
    appendNoteBars(*change.previous, oldBars);
  }
  if (change.kind != SnapshotChangeKind::Remove) {
    appendNoteBars(change.next, nextBars);
  }
  sortUniqueBars(oldBars);
  sortUniqueBars(nextBars);
  std::vector<uint32_t> dirtyBars = oldBars;
  dirtyBars.insert(dirtyBars.end(), nextBars.begin(), nextBars.end());
  sortUniqueBars(dirtyBars);

  std::vector<std::pair<uint8_t, std::shared_ptr<SnapshotPitchDirectory>>>
      pitchEdits;
  std::vector<MutablePageEdit<LoopSnapshotNoteRef>> pageEdits;
  std::vector<MutableMembershipEdit<LoopSnapshotNoteRef>> membershipEdits;
  auto mutablePitchDirectory =
      [&nextRoot, &pitchEdits](uint8_t pitch)
      -> std::shared_ptr<SnapshotPitchDirectory> {
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
  };

  LoopSnapshotNoteRef nextReference;
  if (change.kind != SnapshotChangeKind::Remove) {
    nextReference = allocateConstSnapshotShared<LoopSnapshotNote>(change.next);
  }

  for (const uint32_t barIndex : dirtyBars) {
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
      LoopSnapshotNoteRef previousReference = change.previous;
      removeReference(*membership, previousReference);
    }
    if (nextMember) {
      auto directory = mutablePitchDirectory(nextPitch);
      auto membership = mutableMembershipForBar<LoopSnapshotNoteRef>(
          directory->pages, pageEdits, membershipEdits, barIndex);
      addNoteReference(*membership, nextReference);
    }
  }

  spanRoot_ = std::move(nextRoot);
  if (change.kind != SnapshotChangeKind::Remove &&
      change.next.noteId >= nextNoteId_) {
    nextNoteId_ = change.next.noteId + 1;
  }
  return true;
}

LOOP_COLD_MEM bool LoopSnapshot::applyEventChange(
    const SnapshotEventChange& change) {
  const bool hasPrevious = static_cast<bool>(change.previous);
  if (change.kind == SnapshotChangeKind::Add && hasPrevious) {
    return false;
  }
  if (change.kind != SnapshotChangeKind::Add && !hasPrevious) {
    return false;
  }
  if (change.previous && change.previous->loopId != loopId_) {
    return false;
  }
  if (change.kind != SnapshotChangeKind::Remove &&
      (change.next.loopId != loopId_ ||
       change.next.eventId == kInvalidEventId ||
       change.next.value.tick >= loopLengthTicks_)) {
    return false;
  }
  if (change.previous &&
      (change.previous->eventId == kInvalidEventId ||
       (change.kind == SnapshotChangeKind::Replace &&
        change.previous->eventId != change.next.eventId))) {
    return false;
  }

  const uint32_t oldBar =
      change.previous ? change.previous->value.tick / Config::TICKS_PER_BAR : 0;
  const uint32_t nextBar =
      change.kind == SnapshotChangeKind::Remove
          ? oldBar
          : change.next.value.tick / Config::TICKS_PER_BAR;
  auto nextRoot = allocateSnapshotShared<SnapshotEventRoot>(
      eventRoot_ ? *eventRoot_ : SnapshotEventRoot{});
  std::vector<MutablePageEdit<LoopSnapshotEventRef>> pageEdits;
  std::vector<MutableMembershipEdit<LoopSnapshotEventRef>> membershipEdits;

  LoopSnapshotEventRef nextReference;
  if (change.kind != SnapshotChangeKind::Remove) {
    nextReference = allocateConstSnapshotShared<LoopSnapshotEvent>(change.next);
  }
  const bool sameBar = oldBar == nextBar;
  auto applyBarChange = [&](uint32_t barIndex, bool removeOld, bool addNext) {
    auto membership = mutableMembershipForBar<LoopSnapshotEventRef>(
        nextRoot->pages, pageEdits, membershipEdits, barIndex);
    if (removeOld) {
      removeReference(*membership, change.previous);
    }
    if (addNext) {
      addEventReference(*membership, nextReference);
    }
  };

  if (change.previous) {
    applyBarChange(oldBar, true,
                   change.kind != SnapshotChangeKind::Remove && sameBar);
  }
  if (change.kind != SnapshotChangeKind::Remove &&
      (!change.previous || !sameBar)) {
    applyBarChange(nextBar, false, true);
  }

  eventRoot_ = std::move(nextRoot);
  if (change.kind != SnapshotChangeKind::Remove &&
      change.next.eventId >= nextEventId_) {
    nextEventId_ = change.next.eventId + 1;
  }
  return true;
}

LOOP_COLD_MEM LoopSnapshotConstruction::LoopSnapshotConstruction(
    std::shared_ptr<const LoopSnapshot> parent,
    const LoopSnapshotChangeSet& changes)
    : parent_(std::move(parent)),
      candidate_(parent_ ? *parent_ : LoopSnapshot{}),
      changes_(changes) {
  failed_ = !parent_;
}

LOOP_COLD_MEM bool LoopSnapshotConstruction::step(size_t maximumChanges) {
  if (failed_ || complete()) {
    return complete() && !failed_;
  }
  if (maximumChanges == 0) {
    return false;
  }

  size_t processed = 0;
  while (processed < maximumChanges && !complete()) {
    bool applied = false;
    if (nextChangeIndex_ < changes_.notes.size()) {
      applied = candidate_.applyNoteChange(changes_.notes[nextChangeIndex_]);
    } else {
      const size_t eventIndex =
          nextChangeIndex_ - changes_.notes.size();
      applied = candidate_.applyEventChange(changes_.events[eventIndex]);
    }
    if (!applied) {
      failed_ = true;
      return false;
    }
    ++nextChangeIndex_;
    ++processed;
  }
  return complete();
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
