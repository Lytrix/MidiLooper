//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopSnapshot.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

#if defined(SESSION_CAPTURE)
#include "Utils/DebugSessionCapture.h"
#include <cstdio>
#endif

#if defined(__IMXRT1062__)
#define LOOP_SNAPSHOT_COLD_MEM __attribute__((noinline, section(".flashmem")))
#else
#define LOOP_SNAPSHOT_COLD_MEM
#endif

#if defined(SESSION_CAPTURE) || !defined(ARDUINO)
#define SNAPSHOT_ALLOC_COUNTERS 1
#else
#define SNAPSHOT_ALLOC_COUNTERS 0
#endif

namespace {
/// Canonical order inside one event-membership page: empty slots last, then canonical order, then
/// event id. Owned once so every bar-apply walk sorts a page identically.
bool eventMembershipPageLess(const LoopSnapshotEventRef& left, const LoopSnapshotEventRef& right) {
  if (!left || !right) {
    return static_cast<bool>(left) && !right;
  }
  if (left->canonicalOrder != right->canonicalOrder) {
    return left->canonicalOrder < right->canonicalOrder;
  }
  return left->eventId < right->eventId;
}


struct SnapshotSlab {
  uint32_t liveCount = 0;
  uint32_t used = 0;
  /// Live-slab registry links. They sit in the slab's own 32 KB header, so the census costs
  /// no RAM1, and the list is doubly linked so retiring a slab stays O(1) on the MIDI edge.
  SnapshotSlab* nextLive = nullptr;
  SnapshotSlab* prevLive = nullptr;
};

constexpr size_t kSnapshotSlabAlign = 8;
constexpr size_t kSnapshotSlabPrefixBytes = sizeof(SnapshotSlab*);
constexpr size_t kSnapshotSlabHeaderBytes =
    (sizeof(SnapshotSlab) + (kSnapshotSlabAlign - 1)) & ~(kSnapshotSlabAlign - 1);
constexpr size_t kSnapshotSlabPayloadCapacity = kSnapshotSlabBytes - kSnapshotSlabHeaderBytes;

#if SNAPSHOT_ALLOC_COUNTERS
uint32_t g_snapshotAllocNodes = 0;
uint32_t g_snapshotAllocBytes = 0;
uint32_t g_snapshotAllocSlabs = 0;
#endif

SnapshotSlab* g_currentSnapshotSlab = nullptr;
/// Head of the live-slab registry the census walks. Maintained unconditionally (two pointer
/// writes per open/free) so the diagnostic works in builds without `SNAPSHOT_ALLOC_COUNTERS`.
SnapshotSlab* g_liveSlabHead = nullptr;
uint32_t g_liveSlabCount = 0;

LOOP_SNAPSHOT_COLD_MEM size_t alignSnapshotSlabOffset(size_t offset) {
  return (offset + (kSnapshotSlabAlign - 1)) & ~(kSnapshotSlabAlign - 1);
}

LOOP_SNAPSHOT_COLD_MEM uint8_t* snapshotSlabPayload(SnapshotSlab* slab) {
  return reinterpret_cast<uint8_t*>(slab) + alignSnapshotSlabOffset(sizeof(SnapshotSlab));
}

LOOP_SNAPSHOT_COLD_MEM size_t snapshotSlabPayloadBytes() {
  return kSnapshotSlabBytes - alignSnapshotSlabOffset(sizeof(SnapshotSlab));
}

LOOP_SNAPSHOT_COLD_MEM void* snapshotSlabMalloc(size_t bytes) {
#if EXTMEM_AVAILABLE
  return extmem_malloc(bytes);
#else
  return std::malloc(bytes);
#endif
}

LOOP_SNAPSHOT_COLD_MEM void snapshotSlabFree(void* pointer) {
#if EXTMEM_AVAILABLE
  extmem_free(pointer);
#else
  std::free(pointer);
#endif
}

LOOP_SNAPSHOT_COLD_MEM void registerLiveSlab(SnapshotSlab* slab) {
  slab->prevLive = nullptr;
  slab->nextLive = g_liveSlabHead;
  if (g_liveSlabHead != nullptr) {
    g_liveSlabHead->prevLive = slab;
  }
  g_liveSlabHead = slab;
  ++g_liveSlabCount;
}

LOOP_SNAPSHOT_COLD_MEM void unregisterLiveSlab(SnapshotSlab* slab) {
  if (slab->prevLive != nullptr) {
    slab->prevLive->nextLive = slab->nextLive;
  } else if (g_liveSlabHead == slab) {
    g_liveSlabHead = slab->nextLive;
  } else {
    // Never registered (or already unregistered): the registry must not lose a count it
    // does not own.
    return;
  }
  if (slab->nextLive != nullptr) {
    slab->nextLive->prevLive = slab->prevLive;
  }
  slab->nextLive = nullptr;
  slab->prevLive = nullptr;
  if (g_liveSlabCount > 0) {
    --g_liveSlabCount;
  }
}

LOOP_SNAPSHOT_COLD_MEM SnapshotSlab* snapshotSlabOpen() {
  void* memory = snapshotSlabMalloc(kSnapshotSlabBytes);
  if (memory == nullptr) {
    emitSnapshotAllocFailure("slab_open", kSnapshotSlabBytes);
    return nullptr;
  }
  auto* slab = new (memory) SnapshotSlab();
  g_currentSnapshotSlab = slab;
  registerLiveSlab(slab);
#if SNAPSHOT_ALLOC_COUNTERS
  ++g_snapshotAllocSlabs;
#endif
  return slab;
}

LOOP_SNAPSHOT_COLD_MEM bool snapshotSlabHasRoom(const SnapshotSlab* slab, size_t bytes) {
  if (slab == nullptr) {
    return false;
  }
  const size_t userOffset =
      alignSnapshotSlabOffset(static_cast<size_t>(slab->used) + kSnapshotSlabPrefixBytes);
  return userOffset + bytes <= snapshotSlabPayloadBytes();
}

LOOP_SNAPSHOT_COLD_MEM bool snapshotSlabEnsureCapacity(size_t bytes) {
  if (bytes > snapshotSlabPayloadBytes()) {
    emitSnapshotAllocFailure("slab_too_large", bytes);
    return false;
  }
  if (snapshotSlabHasRoom(g_currentSnapshotSlab, bytes)) {
    return true;
  }
  return snapshotSlabOpen() != nullptr;
}

LOOP_SNAPSHOT_COLD_MEM void* snapshotSlabAllocate(size_t bytes) {
  if (bytes > snapshotSlabPayloadBytes()) {
    return nullptr;
  }
  if (!snapshotSlabHasRoom(g_currentSnapshotSlab, bytes) && snapshotSlabOpen() == nullptr) {
    return nullptr;
  }
  SnapshotSlab* slab = g_currentSnapshotSlab;
  const size_t userOffset =
      alignSnapshotSlabOffset(static_cast<size_t>(slab->used) + kSnapshotSlabPrefixBytes);
  uint8_t* user = snapshotSlabPayload(slab) + userOffset;
  *reinterpret_cast<SnapshotSlab**>(user - kSnapshotSlabPrefixBytes) = slab;
  slab->used = static_cast<uint32_t>(userOffset + bytes);
  ++slab->liveCount;
  return user;
}

LOOP_SNAPSHOT_COLD_MEM void snapshotSlabDeallocate(void* pointer) {
  if (pointer == nullptr) {
    return;
  }
  auto* slab = *reinterpret_cast<SnapshotSlab**>(static_cast<uint8_t*>(pointer) -
                                                 kSnapshotSlabPrefixBytes);
  if (slab->liveCount == 0) {
    return;
  }
  --slab->liveCount;
  if (slab->liveCount > 0) {
    return;
  }
  if (g_currentSnapshotSlab == slab) {
    g_currentSnapshotSlab = nullptr;
  }
  unregisterLiveSlab(slab);
  slab->~SnapshotSlab();
  snapshotSlabFree(slab);
}

#if SNAPSHOT_ALLOC_COUNTERS
LOOP_SNAPSHOT_COLD_MEM void snapshotAllocRecord(size_t objectBytes) {
  ++g_snapshotAllocNodes;
  g_snapshotAllocBytes += static_cast<uint32_t>(objectBytes);
}
#endif

template <typename Type>
class SnapshotSlabAllocator {
 public:
  using value_type = Type;

  template <typename Other>
  struct rebind {
    using other = SnapshotSlabAllocator<Other>;
  };

  SnapshotSlabAllocator() noexcept = default;

  template <typename Other>
  SnapshotSlabAllocator(const SnapshotSlabAllocator<Other>&) noexcept {}

  Type* allocate(std::size_t count) {
    // DEC-073 RC-PUB-3: this is a *dead* branch and it must stay one. Every snapshot allocation
    // goes through `allocateSnapshotShared`, which pre-reserves `128 + align(sizeof(Type))` on the
    // current slab before calling here, and `std::allocate_shared` then asks for exactly one
    // object whose control block is ~16-24 B of that ~128 B allowance. So room is already
    // guaranteed. The failure this would report is therefore a programming error, not memory
    // pressure: the pressure case is `snapshotSlabOpen` failing inside `snapshotSlabAllocate`,
    // which `allocateSnapshotShared` observes *before* reaching here and reports non-fatally by
    // returning an empty ref -- the publisher then records
    // `LoopSnapshotFailureReason::Allocation` and parks (DEC-073 RC-PUB-2).
    //
    // It cannot be converted to a null return: the device builds `-fno-exceptions`, which removes
    // the standard allocator failure channel, and a custom `shared_ptr` deleter would move every
    // control block onto the internal heap, which cannot hold one per snapshot node (~450 KB at
    // the observed 14k-node peaks). The abort therefore stays as the invariant check, tagged
    // distinctly (`slab_allocate`) from the reachable pressure site (`slab_open`).
    void* pointer = snapshotSlabAllocate(count * sizeof(Type));
    if (pointer == nullptr) {
      emitSnapshotAllocFailure("slab_allocate", count * sizeof(Type));
#if defined(ARDUINO)
      abort();
#else
      throw std::bad_alloc();
#endif
    }
    return static_cast<Type*>(pointer);
  }

  void deallocate(Type* pointer, std::size_t /*count*/) noexcept {
    snapshotSlabDeallocate(pointer);
  }

  template <typename Other>
  bool operator==(const SnapshotSlabAllocator<Other>&) const noexcept {
    return true;
  }

  template <typename Other>
  bool operator!=(const SnapshotSlabAllocator<Other>&) const noexcept {
    return false;
  }
};

template <typename Type, typename... Arguments>
LOOP_SNAPSHOT_COLD_MEM std::shared_ptr<Type> allocateSnapshotShared(
    Arguments&&... arguments) {
  constexpr size_t estimate =
      128 + ((sizeof(Type) + (kSnapshotSlabAlign - 1)) & ~(kSnapshotSlabAlign - 1));
  if (!snapshotSlabEnsureCapacity(estimate)) {
    return {};
  }
#if !defined(ARDUINO)
  try {
    auto allocated = std::allocate_shared<Type>(SnapshotSlabAllocator<Type>{},
                                                std::forward<Arguments>(arguments)...);
#if SNAPSHOT_ALLOC_COUNTERS
    snapshotAllocRecord(sizeof(Type));
#endif
    return allocated;
  } catch (const std::bad_alloc&) {
    return {};
  }
#else
  auto allocated = std::allocate_shared<Type>(SnapshotSlabAllocator<Type>{},
                                              std::forward<Arguments>(arguments)...);
#if SNAPSHOT_ALLOC_COUNTERS
  snapshotAllocRecord(sizeof(Type));
#endif
  return allocated;
#endif
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
  uint16_t pageCount = 0;
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
struct SnapshotMembershipCopyProgress {
  SnapshotMembershipRef<Reference> source;
  std::shared_ptr<SnapshotMembership<Reference>> head;
  std::shared_ptr<SnapshotMembership<Reference>> tail;
  uint16_t pageCount = 0;
  bool sourceWasEmpty = false;
  bool complete = false;

  void begin(const SnapshotMembershipRef<Reference>& input) {
    source = input;
    head.reset();
    tail.reset();
    pageCount = 0;
    sourceWasEmpty = !input;
    complete = false;
  }

  bool copyNextPage() {
    if (complete) {
      return true;
    }
    if (source) {
      if (pageCount >= kSnapshotMembershipMaxPages) {
        return false;
      }
      auto copy = allocateSnapshotShared<SnapshotMembership<Reference>>();
      if (!copy) {
        return false;
      }
      copy->references = source->references;
      copy->count = source->count;
      if (!head) {
        head = copy;
      } else {
        tail->next = copy;
      }
      tail = copy;
      source = source->next;
      ++pageCount;
      return true;
    }
    if (!head) {
      head = allocateSnapshotShared<SnapshotMembership<Reference>>();
      if (!head) {
        return false;
      }
      tail = head;
      pageCount = 1;
    }
    complete = true;
    return true;
  }
};

template <typename Reference>
LOOP_SNAPSHOT_COLD_MEM std::shared_ptr<SnapshotBarPage<Reference>> mutableBarPageForBar(
    SnapshotBarPageDirectory<Reference>& directory,
    std::vector<MutablePageEdit<Reference>>& pageEdits,
    uint32_t barIndex) {
  const size_t pageIndex = pageIndexForBar<Reference>(barIndex);
  for (auto& edit : pageEdits) {
    if (edit.directory == &directory && edit.pageIndex == pageIndex) {
      return edit.page;
    }
  }
  auto page = allocateSnapshotShared<SnapshotBarPage<Reference>>();
  if (!page) {
    return {};
  }
  if (pageIndex < directory.pages.size() && directory.pages[pageIndex]) {
    *page = *directory.pages[pageIndex];
  }
  if (directory.pages.size() <= pageIndex) {
    directory.pages.resize(pageIndex + 1);
  }
  directory.pages[pageIndex] = page;
  pageEdits.push_back({&directory, pageIndex, page});
  return page;
}

struct SnapshotNoteBarCursor {
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t nextTick = 0;
  uint8_t segment = 0;
  bool active = false;

  void begin(uint32_t rangeStart, uint32_t rangeEnd, uint32_t lengthTicks) {
    startTick = rangeStart;
    endTick = rangeEnd;
    loopLengthTicks = lengthTicks;
    nextTick = 0;
    segment = 0;
    active = false;
    if (lengthTicks == 0) {
      return;
    }
    startTick = std::min(startTick, lengthTicks);
    endTick = std::min(endTick, lengthTicks);
    if (endTick < startTick) {
      nextTick = startTick;
      active = startTick < lengthTicks || endTick > 0;
      return;
    }
    if (startTick >= endTick) {
      return;
    }
    nextTick = startTick;
    active = true;
  }

  void beginNote(const LoopSnapshotNote& note, uint32_t lengthTicks) {
    begin(note.startTick, note.endTick, lengthTicks);
  }

  bool takeNextBar(uint32_t& barIndex) {
    if (!active || loopLengthTicks == 0) {
      active = false;
      return false;
    }
    if (endTick < startTick) {
      if (segment == 0) {
        if (nextTick >= loopLengthTicks) {
          segment = 1;
          nextTick = 0;
        }
      }
      if (segment == 1 && nextTick >= endTick) {
        active = false;
        return false;
      }
    } else if (nextTick >= endTick) {
      active = false;
      return false;
    }

    barIndex = nextTick / Config::TICKS_PER_BAR;
    const uint32_t barEnd = (barIndex + 1) * Config::TICKS_PER_BAR;
    nextTick = barEnd;
    if (endTick < startTick) {
      if (segment == 0 && nextTick >= loopLengthTicks) {
        if (endTick == 0) {
          active = false;
        }
      } else if (segment == 1 && nextTick >= endTick) {
        active = false;
      }
    } else if (nextTick >= endTick) {
      active = false;
    }
    return true;
  }
};

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
LOOP_SNAPSHOT_COLD_MEM bool removeReferenceFromPage(
    SnapshotMembership<Reference>& page, const Reference& target) {
  const auto targetId = referenceId(target);
  uint8_t write = 0;
  bool removed = false;
  for (uint8_t read = 0; read < page.count; ++read) {
    if (!removed && referenceId(page.references[read]) == targetId) {
      removed = true;
      continue;
    }
    if (write != read) {
      page.references[write] = page.references[read];
    }
    ++write;
  }
  if (removed) {
    if (write < page.count) {
      page.references[write] = {};
    }
    page.count = write;
  }
  return removed;
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
    uint32_t loopLengthTicks, CanonicalNoteSpanVec& out) const {
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

  if (isWrap(loopLengthTicks)) {
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

LOOP_COLD_MEM LoopSnapshot::LoopSnapshot(const LoopSnapshot& other,
                                         uint32_t loopLengthTicks)
    : loopId_(other.loopId_),
      loopLengthTicks_(loopLengthTicks),
      loopStartTick_(other.loopStartTick_),
      nextNoteId_(other.nextNoteId_),
      nextEventId_(other.nextEventId_),
      spanRoot_(other.spanRoot_),
      eventRoot_(other.eventRoot_) {}

LOOP_COLD_MEM ImmutableLoopSnapshotRef LoopSnapshot::createEmpty(
    LoopId loopId, uint32_t loopLengthTicks, uint32_t loopStartTick,
    uint32_t nextNoteId, uint32_t nextEventId) {
  return allocateConstSnapshotShared<LoopSnapshot>(
      loopId, loopLengthTicks, loopStartTick, nextNoteId, nextEventId);
}

LOOP_COLD_MEM ImmutableLoopSnapshotRef LoopSnapshot::withLoopLengthTicks(
    uint32_t loopLengthTicks) const {
  return allocateConstSnapshotShared<LoopSnapshot>(*this, loopLengthTicks);
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

LOOP_COLD_MEM LoopSnapshotNoteRef LoopSnapshot::findNoteAtPitch(NoteId noteId,
                                                               uint8_t pitch) const {
  if (noteId == kInvalidNoteId || !spanRoot_ || pitch >= kLoopSnapshotPitchCount) {
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
      LoopSnapshotNoteRef found;
      forEachSnapshotMembershipReference(
          membership.get(), [noteId, &found](const LoopSnapshotNoteRef& reference) {
            if (!found && reference && reference->noteId == noteId) {
              found = reference;
            }
          });
      if (found) {
        return found;
      }
    }
  }
  return {};
}

LOOP_COLD_MEM LoopSnapshotNoteRef LoopSnapshot::findNote(NoteId noteId,
                                                        uint8_t pitchHint) const {
  if (auto found = findNoteAtPitch(noteId, pitchHint)) {
    return found;
  }
  if (noteId == kInvalidNoteId || !spanRoot_) {
    return {};
  }
  for (uint16_t pitch = 0; pitch < kLoopSnapshotPitchCount; ++pitch) {
    if (pitch == pitchHint) {
      continue;
    }
    if (auto found = findNoteAtPitch(noteId, static_cast<uint8_t>(pitch))) {
      return found;
    }
  }
  return {};
}

LOOP_COLD_MEM void LoopSnapshot::appendCanonicalSpans(
    const LoopSnapshotNoteRef& note, CanonicalNoteSpanVec& out) const {
  if (note && note->loopId == loopId_) {
    note->appendCanonicalSpans(loopLengthTicks_, out);
  }
}

struct LoopSnapshotNoteBarApply {
  enum class BarStage : uint8_t {
    Select,
    CopyMembership,
    RemoveReference,
    AppendReference,
    Complete,
  };

  SnapshotNoteChange change;
  SnapshotNoteBarCursor oldCursor;
  SnapshotNoteBarCursor nextCursor;
  std::shared_ptr<SnapshotSpanRoot> nextRoot;
  std::vector<std::pair<uint8_t, std::shared_ptr<SnapshotPitchDirectory>>>
      pitchEdits;
  std::vector<MutablePageEdit<LoopSnapshotNoteRef>> pageEdits;
  std::vector<MutableMembershipEdit<LoopSnapshotNoteRef>> membershipEdits;
  LoopSnapshotNoteRef nextReference;
  SnapshotMembershipCopyProgress<LoopSnapshotNoteRef> membershipCopy;
  std::shared_ptr<SnapshotMembership<LoopSnapshotNoteRef>> currentMembership;
  std::shared_ptr<SnapshotBarPage<LoopSnapshotNoteRef>> currentPage;
  SnapshotMembership<LoopSnapshotNoteRef>* mutationPage = nullptr;
  SnapshotMembership<LoopSnapshotNoteRef>* appendPage = nullptr;
  SnapshotBarPageDirectory<LoopSnapshotNoteRef>* currentDirectory = nullptr;
  uint16_t currentPageCount = 0;
  uint32_t currentBarIndex = 0;
  bool currentBarActive = false;
  bool currentBarRemovesReference = false;
  bool currentBarAppendsReference = false;
  bool currentBarRemovesPreviousReference = false;
  /// True when this apply failed because the bar hit
  /// `kSnapshotMembershipMaxRefsPerBar` rather than an allocation failure.
  bool pageCapacityExceeded = false;
  BarStage barStage = BarStage::Select;

  bool done() const {
    return !oldCursor.active && !nextCursor.active && !currentBarActive;
  }

  std::shared_ptr<SnapshotPitchDirectory> mutablePitchDirectory(uint8_t pitch) {
    for (auto& edit : pitchEdits) {
      if (edit.first == pitch) {
        return edit.second;
      }
    }
    auto directory = allocateSnapshotShared<SnapshotPitchDirectory>();
    if (!directory) {
      return {};
    }
    if (nextRoot->pitches[pitch]) {
      *directory = *nextRoot->pitches[pitch];
    }
    nextRoot->pitches[pitch] = directory;
    pitchEdits.push_back({pitch, directory});
    return directory;
  }

  bool begin(LoopSnapshot& snapshot, const SnapshotNoteChange& incoming,
             std::shared_ptr<SnapshotSpanRoot>& constructionSpanRoot) {
    const bool hasPrevious = static_cast<bool>(incoming.previous);
    if (incoming.kind == SnapshotChangeType::Add && hasPrevious) {
      return false;
    }
    if (incoming.kind != SnapshotChangeType::Add && !hasPrevious) {
      return false;
    }
    if (incoming.previous && incoming.previous->loopId != snapshot.loopId_) {
      return false;
    }
    if (incoming.kind != SnapshotChangeType::Remove &&
        (incoming.next.loopId != snapshot.loopId_ ||
         incoming.next.noteId == kInvalidNoteId ||
         incoming.next.pitch >= kLoopSnapshotPitchCount)) {
      return false;
    }
    if (incoming.previous &&
        (incoming.previous->noteId == kInvalidNoteId ||
         (incoming.kind == SnapshotChangeType::Replace &&
          incoming.previous->noteId != incoming.next.noteId))) {
      return false;
    }

    change = incoming;
    if (constructionSpanRoot) {
      nextRoot = constructionSpanRoot;
    } else {
      nextRoot = allocateSnapshotShared<SnapshotSpanRoot>(
          snapshot.spanRoot_ ? *snapshot.spanRoot_ : SnapshotSpanRoot{});
      if (!nextRoot) {
        return false;
      }
      constructionSpanRoot = nextRoot;
    }
    if (incoming.previous) {
      oldCursor.beginNote(*incoming.previous, snapshot.loopLengthTicks_);
    }
    if (incoming.kind != SnapshotChangeType::Remove) {
      nextCursor.beginNote(incoming.next, snapshot.loopLengthTicks_);
      nextReference = allocateConstSnapshotShared<LoopSnapshotNote>(incoming.next);
      if (!nextReference) {
        return false;
      }
    }
    return true;
  }

  bool beginCurrentBar(uint32_t barIndex, bool removeReferenceValue,
                       bool appendReferenceValue, bool removesPreviousReference) {
    currentBarIndex = barIndex;
    currentBarRemovesReference = removeReferenceValue;
    currentBarAppendsReference = appendReferenceValue;
    currentBarRemovesPreviousReference = removesPreviousReference;
    const uint8_t pitch =
        removesPreviousReference
            ? (change.previous ? change.previous->pitch : change.next.pitch)
            : change.next.pitch;
    auto directory = mutablePitchDirectory(pitch);
    if (!directory) {
      return false;
    }
    currentDirectory = &directory->pages;
    currentPage = mutableBarPageForBar(
        *currentDirectory, pageEdits, currentBarIndex);
    if (!currentPage) {
      return false;
    }
    const size_t pageOffset = pageOffsetForBar<LoopSnapshotNoteRef>(currentBarIndex);
    for (const auto& edit : membershipEdits) {
      if (edit.directory == currentDirectory && edit.barIndex == currentBarIndex) {
        currentMembership = edit.membership;
        currentPageCount = edit.pageCount;
        mutationPage = currentMembership.get();
        appendPage = currentMembership.get();
        barStage = currentBarRemovesReference ? BarStage::RemoveReference
                                              : BarStage::AppendReference;
        return true;
      }
    }
    membershipCopy.begin(currentPage->memberships[pageOffset]);
    barStage = BarStage::CopyMembership;
    return true;
  }

  void installCopiedMembership() {
    currentMembership = membershipCopy.head;
    currentPageCount = membershipCopy.pageCount;
    const size_t pageOffset = pageOffsetForBar<LoopSnapshotNoteRef>(currentBarIndex);
    currentPage->memberships[pageOffset] = currentMembership;
    membershipEdits.push_back(
        {currentDirectory, currentBarIndex, currentMembership, currentPageCount});
    mutationPage = currentMembership.get();
    appendPage = membershipCopy.tail.get();
  }

  bool appendCurrentReference(LoopSnapshot& snapshot) {
    if (appendPage == nullptr) {
      return false;
    }
    if (appendPage->count == kSnapshotMembershipPageCapacity) {
      if (currentPageCount >= kSnapshotMembershipMaxPages) {
        pageCapacityExceeded = true;
        return false;
      }
      auto nextPage = allocateSnapshotShared<SnapshotMembership<LoopSnapshotNoteRef>>();
      if (!nextPage) {
        return false;
      }
      appendPage->next = nextPage;
      appendPage = nextPage.get();
      ++currentPageCount;
    }
    appendPage->references[appendPage->count++] = nextReference;
    return finishCurrentBar(snapshot);
  }

  bool finishCurrentBar(LoopSnapshot& snapshot) {
    snapshot.spanRoot_ = nextRoot;
    currentBarActive = false;
    currentMembership.reset();
    currentPage.reset();
    mutationPage = nullptr;
    appendPage = nullptr;
    currentDirectory = nullptr;
    currentPageCount = 0;
    currentBarRemovesPreviousReference = false;
    barStage = BarStage::Select;
    if (done() && change.kind != SnapshotChangeType::Remove &&
        change.next.noteId >= snapshot.nextNoteId_) {
      snapshot.nextNoteId_ = change.next.noteId + 1;
    }
    return true;
  }

  // Note-side twin of the event path's `applySoonestBar` below. This one applies bars in ascending
  // source order and does not consult the playhead; ordering them by playhead would change the
  // membership order the snapshot publishes, so it is a timing-model change deferred to its own
  // change with its own evidence (`runtime-code-tidyup` OD-3). The parameter stays in the signature
  // to match the twin and is left unnamed here so the intent is explicit.
  bool applySoonestBar(LoopSnapshot& snapshot, uint32_t /*playheadPhaseTick*/) {
    if (!currentBarActive) {
      uint32_t barIndex = 0;
      if (oldCursor.takeNextBar(barIndex)) {
        currentBarActive = true;
        if (!beginCurrentBar(barIndex, true, false, true)) {
          return false;
        }
        return true;
      }
      if (nextCursor.takeNextBar(barIndex)) {
        currentBarActive = true;
        if (!beginCurrentBar(barIndex, change.kind != SnapshotChangeType::Remove, true,
                             false)) {
          return false;
        }
        if (barStage == BarStage::CopyMembership && membershipCopy.sourceWasEmpty) {
          if (!membershipCopy.copyNextPage()) {
            return false;
          }
          installCopiedMembership();
          return appendCurrentReference(snapshot);
        }
        return true;
      }
      return true;
    }

    if (barStage == BarStage::CopyMembership) {
      if (!membershipCopy.copyNextPage()) {
        return false;
      }
      if (!membershipCopy.complete) {
        return true;
      }
      installCopiedMembership();
      if (membershipCopy.sourceWasEmpty && currentBarAppendsReference) {
        return appendCurrentReference(snapshot);
      }
      barStage = currentBarRemovesReference ? BarStage::RemoveReference
                                            : BarStage::AppendReference;
      return true;
    }

    if (barStage == BarStage::RemoveReference) {
      if (mutationPage != nullptr) {
        SnapshotMembership<LoopSnapshotNoteRef>* page = mutationPage;
        mutationPage = const_cast<SnapshotMembership<LoopSnapshotNoteRef>*>(
            page->next.get());
        appendPage = page;
        const LoopSnapshotNoteRef& target =
            currentBarRemovesPreviousReference ? change.previous : nextReference;
        if (removeReferenceFromPage(*page, target)) {
          if (!currentBarAppendsReference) {
            barStage = BarStage::Complete;
          }
        }
        return true;
      }
      barStage = currentBarAppendsReference ? BarStage::AppendReference
                                            : BarStage::Complete;
      return true;
    }

    if (barStage == BarStage::AppendReference) {
      return appendCurrentReference(snapshot);
    }

    return finishCurrentBar(snapshot);
  }
};

struct LoopSnapshotEventBarApply {
  enum class BarStage : uint8_t {
    Select,
    CopyMembership,
    RemoveReference,
    AppendReference,
    SortMembership,
    Complete,
  };

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
  SnapshotMembershipCopyProgress<LoopSnapshotEventRef> membershipCopy;
  std::shared_ptr<SnapshotMembership<LoopSnapshotEventRef>> currentMembership;
  std::shared_ptr<SnapshotBarPage<LoopSnapshotEventRef>> currentPage;
  SnapshotMembership<LoopSnapshotEventRef>* mutationPage = nullptr;
  SnapshotMembership<LoopSnapshotEventRef>* appendPage = nullptr;
  SnapshotMembership<LoopSnapshotEventRef>* sortPage = nullptr;
  SnapshotBarPageDirectory<LoopSnapshotEventRef>* currentDirectory = nullptr;
  uint16_t currentPageCount = 0;
  uint32_t currentBarIndex = 0;
  bool currentBarActive = false;
  bool currentBarRemovesReference = false;
  bool currentBarAppendsReference = false;
  bool currentBarRemovesPreviousReference = false;
  /// True when this apply failed because the bar hit
  /// `kSnapshotMembershipMaxRefsPerBar` rather than an allocation failure.
  bool pageCapacityExceeded = false;
  BarStage barStage = BarStage::Select;

  bool done() const { return remainingBars.empty() && !currentBarActive; }

  bool begin(LoopSnapshot& snapshot, const SnapshotEventChange& incoming,
             std::shared_ptr<SnapshotEventRoot>& constructionEventRoot) {
    const bool hasPrevious = static_cast<bool>(incoming.previous);
    if (incoming.kind == SnapshotChangeType::Add && hasPrevious) {
      return false;
    }
    if (incoming.kind != SnapshotChangeType::Add && !hasPrevious) {
      return false;
    }
    if (incoming.previous && incoming.previous->loopId != snapshot.loopId_) {
      return false;
    }
    if (incoming.kind != SnapshotChangeType::Remove &&
        (incoming.next.loopId != snapshot.loopId_ ||
         incoming.next.eventId == kInvalidEventId ||
         incoming.next.value.tick >= snapshot.loopLengthTicks_)) {
      return false;
    }
    if (incoming.previous &&
        (incoming.previous->eventId == kInvalidEventId ||
         (incoming.kind == SnapshotChangeType::Replace &&
          incoming.previous->eventId != incoming.next.eventId))) {
      return false;
    }

    change = incoming;
    oldBar = incoming.previous
                 ? incoming.previous->value.tick / Config::TICKS_PER_BAR
                 : 0;
    nextBar = incoming.kind == SnapshotChangeType::Remove
                  ? oldBar
                  : incoming.next.value.tick / Config::TICKS_PER_BAR;
    removeOld = static_cast<bool>(incoming.previous);
    addNext = incoming.kind != SnapshotChangeType::Remove;
    if (removeOld) {
      remainingBars.push_back(oldBar);
    }
    if (addNext && (!removeOld || nextBar != oldBar)) {
      remainingBars.push_back(nextBar);
    }
    sortUniqueBars(remainingBars);
    if (constructionEventRoot) {
      nextRoot = constructionEventRoot;
    } else {
      nextRoot = allocateSnapshotShared<SnapshotEventRoot>(
          snapshot.eventRoot_ ? *snapshot.eventRoot_ : SnapshotEventRoot{});
      if (!nextRoot) {
        return false;
      }
      constructionEventRoot = nextRoot;
    }
    if (addNext) {
      nextReference = allocateConstSnapshotShared<LoopSnapshotEvent>(incoming.next);
      if (!nextReference) {
        return false;
      }
    }
    return true;
  }

  bool beginCurrentBar(uint32_t barIndex, bool removeReferenceValue,
                       bool appendReferenceValue, bool removesPreviousReference) {
    currentBarIndex = barIndex;
    currentBarRemovesReference = removeReferenceValue;
    currentBarAppendsReference = appendReferenceValue;
    currentBarRemovesPreviousReference = removesPreviousReference;
    currentDirectory = &nextRoot->pages;
    currentPage = mutableBarPageForBar(
        *currentDirectory, pageEdits, currentBarIndex);
    if (!currentPage) {
      return false;
    }
    const size_t pageOffset = pageOffsetForBar<LoopSnapshotEventRef>(currentBarIndex);
    for (const auto& edit : membershipEdits) {
      if (edit.directory == currentDirectory && edit.barIndex == currentBarIndex) {
        currentMembership = edit.membership;
        currentPageCount = edit.pageCount;
        mutationPage = currentMembership.get();
        appendPage = currentMembership.get();
        barStage = currentBarRemovesReference ? BarStage::RemoveReference
                                              : BarStage::AppendReference;
        return true;
      }
    }
    membershipCopy.begin(currentPage->memberships[pageOffset]);
    barStage = BarStage::CopyMembership;
    return true;
  }

  void installCopiedMembership() {
    currentMembership = membershipCopy.head;
    currentPageCount = membershipCopy.pageCount;
    const size_t pageOffset = pageOffsetForBar<LoopSnapshotEventRef>(currentBarIndex);
    currentPage->memberships[pageOffset] = currentMembership;
    membershipEdits.push_back(
        {currentDirectory, currentBarIndex, currentMembership, currentPageCount});
    mutationPage = currentMembership.get();
    appendPage = membershipCopy.tail.get();
  }

  bool appendCurrentReference(LoopSnapshot& snapshot) {
    if (appendPage == nullptr) {
      return false;
    }
    if (appendPage->count == kSnapshotMembershipPageCapacity) {
      if (currentPageCount >= kSnapshotMembershipMaxPages) {
        pageCapacityExceeded = true;
        return false;
      }
      auto nextPage = allocateSnapshotShared<SnapshotMembership<LoopSnapshotEventRef>>();
      if (!nextPage) {
        return false;
      }
      appendPage->next = nextPage;
      appendPage = nextPage.get();
      ++currentPageCount;
    }
    appendPage->references[appendPage->count++] = nextReference;
    sortPage = currentMembership.get();
    if (membershipCopy.sourceWasEmpty) {
      auto* page = sortPage;
      std::sort(page->references.begin(), page->references.begin() + page->count,
                eventMembershipPageLess);
      return finishCurrentBar(snapshot);
    }
    barStage = BarStage::SortMembership;
    return true;
  }

  bool finishCurrentBar(LoopSnapshot& snapshot) {
    snapshot.eventRoot_ = nextRoot;
    currentBarActive = false;
    currentMembership.reset();
    currentPage.reset();
    mutationPage = nullptr;
    appendPage = nullptr;
    sortPage = nullptr;
    currentDirectory = nullptr;
    currentPageCount = 0;
    currentBarRemovesPreviousReference = false;
    barStage = BarStage::Select;
    if (done() && change.kind != SnapshotChangeType::Remove &&
        change.next.eventId >= snapshot.nextEventId_) {
      snapshot.nextEventId_ = change.next.eventId + 1;
    }
    return true;
  }

  bool applySoonestBar(LoopSnapshot& snapshot, uint32_t playheadPhaseTick) {
    if (!currentBarActive) {
      if (!remainingBars.empty()) {
        sortBarsBySoonestPlayhead(remainingBars, playheadPhaseTick,
                                  snapshot.loopLengthTicks_);
        currentBarIndex = remainingBars.front();
        remainingBars.erase(remainingBars.begin());
        currentBarActive = true;
        const bool removes = removeOld && currentBarIndex == oldBar;
        const bool appends = addNext && currentBarIndex == nextBar;
        if (!beginCurrentBar(currentBarIndex, removes || appends, appends, removes)) {
          return false;
        }
        if (barStage == BarStage::CopyMembership && membershipCopy.sourceWasEmpty && appends) {
          if (!membershipCopy.copyNextPage()) {
            return false;
          }
          installCopiedMembership();
          return appendCurrentReference(snapshot);
        }
        return true;
      }
      return true;
    }

    if (barStage == BarStage::CopyMembership) {
      if (!membershipCopy.copyNextPage()) {
        return false;
      }
      if (!membershipCopy.complete) {
        return true;
      }
      installCopiedMembership();
      if (membershipCopy.sourceWasEmpty && currentBarAppendsReference) {
        return appendCurrentReference(snapshot);
      }
      barStage = currentBarRemovesReference ? BarStage::RemoveReference
                                            : BarStage::AppendReference;
      return true;
    }

    if (barStage == BarStage::RemoveReference) {
      if (mutationPage != nullptr) {
        SnapshotMembership<LoopSnapshotEventRef>* page = mutationPage;
        mutationPage = const_cast<SnapshotMembership<LoopSnapshotEventRef>*>(
            page->next.get());
        appendPage = page;
        const LoopSnapshotEventRef& target =
            currentBarRemovesPreviousReference ? change.previous : nextReference;
        if (removeReferenceFromPage(*page, target)) {
          if (!currentBarAppendsReference) {
            barStage = BarStage::Complete;
          }
        }
        return true;
      }
      barStage = currentBarAppendsReference ? BarStage::AppendReference
                                            : BarStage::Complete;
      return true;
    }

    if (barStage == BarStage::AppendReference) {
      return appendCurrentReference(snapshot);
    }

    if (barStage == BarStage::SortMembership) {
      if (sortPage == nullptr) {
        return finishCurrentBar(snapshot);
      }
      auto* page = sortPage;
      sortPage = const_cast<SnapshotMembership<LoopSnapshotEventRef>*>(
          page->next.get());
      std::sort(page->references.begin(), page->references.begin() + page->count,
                eventMembershipPageLess);
      return true;
    }

    if (barStage == BarStage::Complete) {
      return finishCurrentBar(snapshot);
    }

    if (!remainingBars.empty()) {
      return true;
    }
    return finishCurrentBar(snapshot);
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

LOOP_COLD_MEM void LoopSnapshotConstruction::handoffLoopLengthTicks(
    uint32_t loopLengthTicks) {
  candidate_.loopLengthTicks_ = loopLengthTicks;
}

LOOP_COLD_MEM bool LoopSnapshotConstruction::applyNextBar(
    uint32_t playheadPhaseTick) {
  if (noteApply_) {
    if (!noteApply_->applySoonestBar(candidate_, playheadPhaseTick)) {
      failureReason_ = noteApply_->pageCapacityExceeded
                           ? LoopSnapshotFailureReason::MembershipCapacity
                           : LoopSnapshotFailureReason::Allocation;
      return false;
    }
    if (noteApply_->done()) {
      noteApply_.reset();
      ++nextChangeIndex_;
    }
    return true;
  }
  if (eventApply_) {
    if (!eventApply_->applySoonestBar(candidate_, playheadPhaseTick)) {
      failureReason_ = eventApply_->pageCapacityExceeded
                           ? LoopSnapshotFailureReason::MembershipCapacity
                           : LoopSnapshotFailureReason::Allocation;
      return false;
    }
    if (eventApply_->done()) {
      eventApply_.reset();
      ++nextChangeIndex_;
    }
    return true;
  }
  if (nextChangeIndex_ < changes_.notes.size()) {
    SnapshotNoteChange incoming = changes_.notes[nextChangeIndex_];
    if ((incoming.kind == SnapshotChangeType::Remove ||
         incoming.kind == SnapshotChangeType::Replace) &&
        !incoming.previous) {
      incoming.previous = candidate_.findNote(incoming.next.noteId, incoming.next.pitch);
      if (!incoming.previous) {
        ++nextChangeIndex_;
        return nextChangeIndex_ < totalChangeCount() ? applyNextBar(playheadPhaseTick)
                                                     : true;
      }
    }
    noteApply_ = std::make_unique<LoopSnapshotNoteBarApply>();
    if (!noteApply_->begin(candidate_, incoming, constructionSpanRoot_)) {
      noteApply_.reset();
      failureReason_ = LoopSnapshotFailureReason::Allocation;
      return false;
    }
    return applyNextBar(playheadPhaseTick);
  }
  const size_t eventIndex = nextChangeIndex_ - changes_.notes.size();
  eventApply_ = std::make_unique<LoopSnapshotEventBarApply>();
  if (!eventApply_->begin(candidate_, changes_.events[eventIndex],
                         constructionEventRoot_)) {
    eventApply_.reset();
    failureReason_ = LoopSnapshotFailureReason::Allocation;
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
#if defined(PIO_UNIT_TEST_NATIVE)
    // Inject a partial-apply failure: units already applied stay in
    // `candidate_`, but nothing is frozen, so the publisher must discard this
    // candidate and retain authoritative progress (DEC-047 transaction).
    if (failAfterUnits_ != kNoFailureInjection && completedUnits_ >= failAfterUnits_) {
      failureReason_ = injectedReason_;
      failed_ = true;
      return false;
    }
#endif
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
    emitSnapshotAllocCounters();
  }
  return frozenCandidate_;
}

void resetSnapshotAllocCounters() {
#if SNAPSHOT_ALLOC_COUNTERS
  g_snapshotAllocNodes = 0;
  g_snapshotAllocBytes = 0;
  g_snapshotAllocSlabs = 0;
#endif
}

uint32_t snapshotAllocNodeCount() {
#if SNAPSHOT_ALLOC_COUNTERS
  return g_snapshotAllocNodes;
#else
  return 0;
#endif
}

uint32_t snapshotAllocByteCount() {
#if SNAPSHOT_ALLOC_COUNTERS
  return g_snapshotAllocBytes;
#else
  return 0;
#endif
}

uint32_t snapshotAllocSlabCount() {
#if SNAPSHOT_ALLOC_COUNTERS
  return g_snapshotAllocSlabs;
#else
  return 0;
#endif
}

SnapshotSlabCensus snapshotSlabCensus() {
  SnapshotSlabCensus census{};
  for (SnapshotSlab* slab = g_liveSlabHead; slab != nullptr; slab = slab->nextLive) {
    ++census.liveSlabs;
    census.liveSlabBytes += static_cast<uint32_t>(kSnapshotSlabBytes);
    const uint32_t used = slab->used <= kSnapshotSlabPayloadCapacity
                              ? slab->used
                              : static_cast<uint32_t>(kSnapshotSlabPayloadCapacity);
    census.usedBytes += used;
    if (slab->liveCount <= 1) {
      ++census.slabsAtMostOneNode;
    } else if (slab->liveCount <= 4) {
      ++census.slabsTwoToFourNodes;
    } else if (static_cast<size_t>(used) * 4 >= kSnapshotSlabPayloadCapacity * 3) {
      ++census.slabsMostlyFull;
    } else {
      ++census.slabsPartiallyUsed;
    }
  }
  census.unusedBytes =
      census.liveSlabBytes >= census.usedBytes ? census.liveSlabBytes - census.usedBytes : 0;
  return census;
}

void emitSnapshotAllocCounters() {
#if defined(SESSION_CAPTURE)
  const SnapshotSlabCensus census = snapshotSlabCensus();
  char line[240];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,snapshot_alloc,nodes=%lu,bytes=%lu,slabs=%lu,live=%lu,"
           "live_kb=%lu,used_kb=%lu,unused_kb=%lu,pin1=%lu,pin24=%lu,full=%lu,part=%lu",
           static_cast<unsigned long>(micros()),
           static_cast<unsigned long>(snapshotAllocNodeCount()),
           static_cast<unsigned long>(snapshotAllocByteCount()),
           static_cast<unsigned long>(snapshotAllocSlabCount()),
           static_cast<unsigned long>(census.liveSlabs),
           static_cast<unsigned long>(census.liveSlabBytes / 1024),
           static_cast<unsigned long>(census.usedBytes / 1024),
           static_cast<unsigned long>(census.unusedBytes / 1024),
           static_cast<unsigned long>(census.slabsAtMostOneNode),
           static_cast<unsigned long>(census.slabsTwoToFourNodes),
           static_cast<unsigned long>(census.slabsMostlyFull),
           static_cast<unsigned long>(census.slabsPartiallyUsed));
  DebugSessionCapture::appendCaptureTextLine(line);
#else
  (void)0;
#endif
}

LOOP_SNAPSHOT_COLD_MEM void emitSnapshotAllocFailure(const char* site, size_t bytes) {
#if defined(SESSION_CAPTURE)
  char line[160];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,snapshot_slab_fail,site=%s,bytes=%lu,nodes=%lu,slabs=%lu",
           static_cast<unsigned long>(micros()), site != nullptr ? site : "unknown",
           static_cast<unsigned long>(bytes),
           static_cast<unsigned long>(snapshotAllocNodeCount()),
           static_cast<unsigned long>(snapshotAllocSlabCount()));
  DebugSessionCapture::appendCaptureTextLine(line);
#else
  (void)site;
  (void)bytes;
#endif
}

namespace {
// At most span -> pitch -> page -> membership -> note (five levels).
// Linked membership tails are moved sideways, never recursively destroyed.
using SnapshotReleasePositions = std::array<size_t, 5>;
template <typename Type> struct SnapshotReleaseChildren;
template <typename Type>
LOOP_SNAPSHOT_COLD_MEM bool releaseSnapshotReference(
    std::shared_ptr<const Type>& value, SnapshotReleasePositions& positions,
    size_t depth) {
  if (!value || value.use_count() > 1 ||
      SnapshotReleaseChildren<Type>::step(value, positions, depth)) {
    value.reset();
    positions[depth] = 0;
    return true;
  }
  return false;
}

template <typename Type> struct SnapshotReleaseChildren {
  static bool step(std::shared_ptr<const Type>&, SnapshotReleasePositions&, size_t) {
    return true;  // Scalar note/event object; no child ownership.
  }
};

template <typename Reference, size_t Count>
LOOP_SNAPSHOT_COLD_MEM bool releaseSnapshotArray(
    std::array<Reference, Count>& children, SnapshotReleasePositions& positions,
    size_t depth) {
  size_t& index = positions[depth];
  if (index == Count) return true;
  if (releaseSnapshotReference(children[index], positions, depth + 1)) ++index;
  return false;
}

template <typename Reference>
struct SnapshotReleaseChildren<SnapshotMembership<Reference>> {
  static LOOP_SNAPSHOT_COLD_MEM bool step(
      SnapshotMembershipRef<Reference>& value,
      SnapshotReleasePositions& positions, size_t depth) {
    if (!releaseSnapshotArray(value->references, positions, depth)) return false;
    auto next = std::move(value->next);
    value = std::move(next);  // Old node contains only empty ownership slots.
    positions[depth] = 0;
    return false;
  }
};
template <typename Reference>
struct SnapshotReleaseChildren<SnapshotBarPage<Reference>> {
  static LOOP_SNAPSHOT_COLD_MEM bool step(
      SnapshotBarPageRef<Reference>& value,
      SnapshotReleasePositions& positions, size_t depth) {
    return releaseSnapshotArray(value->memberships, positions, depth);
  }
};
template <> struct SnapshotReleaseChildren<SnapshotPitchDirectory> {
  static LOOP_SNAPSHOT_COLD_MEM bool step(
      SnapshotPitchDirectoryRef& value, SnapshotReleasePositions& positions,
      size_t depth) {
    // The directory wrapper is logically immutable; only its mutable pages move.
    auto& pages = value->pages.pages;
    if (pages.empty()) return true;
    if (releaseSnapshotReference(pages.back(), positions, depth + 1)) pages.pop_back();
    return false;
  }
};
template <> struct SnapshotReleaseChildren<SnapshotSpanRoot> {
  static LOOP_SNAPSHOT_COLD_MEM bool step(
      std::shared_ptr<const SnapshotSpanRoot>& value,
      SnapshotReleasePositions& positions, size_t depth) {
    return releaseSnapshotArray(value->pitches, positions, depth);
  }
};
template <> struct SnapshotReleaseChildren<SnapshotEventRoot> {
  static LOOP_SNAPSHOT_COLD_MEM bool step(
      std::shared_ptr<const SnapshotEventRoot>& value,
      SnapshotReleasePositions& positions, size_t depth) {
    auto& pages = value->pages.pages;
    if (pages.empty()) return true;
    if (releaseSnapshotReference(pages.back(), positions, depth + 1)) pages.pop_back();
    return false;
  }
};
}  // namespace

LOOP_SNAPSHOT_COLD_MEM bool LoopSnapshot::beginRelease(
    ReleaseCursor& cursor, ImmutableLoopSnapshotRef& root) {
  if (!cursor.complete()) return false;
  cursor.positions_.fill(0);
  cursor.phase_ = 0;
  cursor.units_ = 0;
  cursor.root_ = std::move(root);
  return true;
}

LOOP_SNAPSHOT_COLD_MEM bool LoopSnapshot::stepRelease(
    ReleaseCursor& cursor, size_t maximumUnits) {
  for (size_t unit = 0; unit < maximumUnits && cursor.root_; ++unit) {
    ++cursor.units_;
    if (cursor.root_.use_count() > 1) {
      cursor.root_.reset();
    } else if (cursor.phase_ == 0) {
      if (releaseSnapshotReference(cursor.root_->spanRoot_, cursor.positions_, 0)) {
        ++cursor.phase_;
      }
    } else if (cursor.phase_ == 1) {
      if (releaseSnapshotReference(cursor.root_->eventRoot_, cursor.positions_, 0)) {
        ++cursor.phase_;
      }
    } else {
      cursor.root_.reset();  // Both child roots are empty.
    }
  }
  return cursor.complete();
}
