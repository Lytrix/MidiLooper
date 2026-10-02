//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackView.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#include "LoopEventStore.h"
#include "Utils/InternalHeapFirstAllocator.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define RUNTIME_PLAYBACK_VIEW_COLD_MEM FLASHMEM
#else
#define RUNTIME_PLAYBACK_VIEW_COLD_MEM
#endif

namespace {

// MIDI lane domain for the held-note closure bitmap. Matches the record stop
// policy's lane space (`LoopStopFinalize::TailRepairWorkspace`).
constexpr size_t kMidiChannelCount = 16;
constexpr size_t kMidiNoteCount = 128;
constexpr size_t kMidiLaneCount = kMidiChannelCount * kMidiNoteCount;
constexpr size_t kMidiLaneWords = RuntimePlaybackView::kOpenLaneWords;
static_assert(kMidiLaneWords * 64 >= kMidiLaneCount, "lane map covers the MIDI domain");

RUNTIME_PLAYBACK_VIEW_COLD_MEM void* allocateExternalMemoryFirst(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
  void* memory = extmem_malloc(bytes);
  return memory != nullptr ? memory : std::malloc(bytes);
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void releaseExternalMemoryFirst(void* memory) {
  if (memory == nullptr) {
    return;
  }
  if (isInExternalMemoryPool(memory)) {
    extmem_free(memory);
  } else {
    std::free(memory);
  }
}

bool ledgerEntryEquals(const ActiveNoteLedger::Entry& a, const ActiveNoteLedger::Entry& b) {  return a.channel == b.channel && a.note == b.note && a.noteId == b.noteId &&
         a.startTick == b.startTick && a.velocity == b.velocity;
}

}  // namespace

bool PlaybackCoverageInterval::contains(uint32_t phase) const {
  if (!isValid()) {
    return false;
  }
  phase %= loopLengthTicks;
  const uint32_t start = startPhase % loopLengthTicks;
  if (lengthTicks == loopLengthTicks) {
    return true;
  }
  const uint32_t end = start + lengthTicks;
  return end <= loopLengthTicks ? phase >= start && phase < end
                                 : phase >= start || phase < end - loopLengthTicks;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void LedgerBoundarySummary::clear() {
  openNotes.fill(ActiveNoteLedger::Entry{});
  count = 0;
  overflowed = false;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void LedgerBoundarySummary::capture(const ActiveNoteLedger& ledger) {
  clear();
  overflowed = ledger.overflowed();
  ledger.forEachActive([this](uint8_t, uint8_t, const ActiveNoteLedger::Entry& entry) {
    if (count < openNotes.size()) {
      openNotes[count++] = entry;
    } else {
      overflowed = true;
    }
  });
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool LedgerBoundarySummary::matches(const ActiveNoteLedger& ledger) const {
  if (overflowed != ledger.overflowed()) {
    return false;
  }
  size_t index = 0;
  bool same = true;
  ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry& entry) {
    if (index >= count || !ledgerEntryEquals(openNotes[index], entry)) {
      same = false;
    }
    ++index;
  });
  return same && index == count;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM FixedPlaybackEventStorage::~FixedPlaybackEventStorage() {
  release();
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool FixedPlaybackEventStorage::prewarm(size_t requestedCapacity) {
  if (isPrewarmed()) {
    return capacity == requestedCapacity;
  }
  MidiEvent* newEvents = static_cast<MidiEvent*>(
      allocateExternalMemoryFirst(requestedCapacity * sizeof(MidiEvent)));
  if (newEvents == nullptr) {
    return false;
  }
  uint16_t* newOrder = static_cast<uint16_t*>(
      allocateExternalMemoryFirst(requestedCapacity * sizeof(uint16_t)));
  if (newOrder == nullptr) {
    releaseExternalMemoryFirst(newEvents);
    return false;
  }
  uint32_t* newPhases = static_cast<uint32_t*>(
      allocateExternalMemoryFirst(requestedCapacity * sizeof(uint32_t)));
  if (newPhases == nullptr) {
    releaseExternalMemoryFirst(newOrder);
    releaseExternalMemoryFirst(newEvents);
    return false;
  }
  uint16_t* newMergeScratch = static_cast<uint16_t*>(
      allocateExternalMemoryFirst(requestedCapacity * sizeof(uint16_t)));
  if (newMergeScratch == nullptr) {
    releaseExternalMemoryFirst(newPhases);
    releaseExternalMemoryFirst(newOrder);
    releaseExternalMemoryFirst(newEvents);
    return false;
  }
  uint8_t* newRetained = static_cast<uint8_t*>(allocateExternalMemoryFirst(requestedCapacity));
  if (newRetained == nullptr) {
    releaseExternalMemoryFirst(newMergeScratch);
    releaseExternalMemoryFirst(newPhases);
    releaseExternalMemoryFirst(newOrder);
    releaseExternalMemoryFirst(newEvents);
    return false;
  }
  for (size_t index = 0; index < requestedCapacity; ++index) {
    new (&newEvents[index]) MidiEvent();
  }
  events = newEvents;
  order = newOrder;
  phases = newPhases;
  mergeScratch = newMergeScratch;
  retained = newRetained;
  capacity = requestedCapacity;
  eventCount = 0;
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void FixedPlaybackEventStorage::release() {
  if (events != nullptr) {
    for (size_t index = 0; index < capacity; ++index) {
      events[index].~MidiEvent();
    }
  }
  releaseExternalMemoryFirst(events);
  releaseExternalMemoryFirst(order);
  releaseExternalMemoryFirst(phases);
  releaseExternalMemoryFirst(mergeScratch);
  releaseExternalMemoryFirst(retained);
  events = nullptr;
  order = nullptr;
  phases = nullptr;
  mergeScratch = nullptr;
  retained = nullptr;
  capacity = 0;
  eventCount = 0;
}

bool FixedPlaybackEventStorage::append(const MidiEvent& event) {
  if (!canAppend()) {
    return false;
  }
  events[eventCount] = event;
  order[eventCount] = static_cast<uint16_t>(eventCount);
  retained[eventCount] = 1;
  ++eventCount;
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM RuntimePlaybackView::~RuntimePlaybackView() { release(); }

RUNTIME_PLAYBACK_VIEW_COLD_MEM void RuntimePlaybackView::release() {
  resetMetadata();
  storage.release();
  releaseExternalMemoryFirst(passOrigins);
  releaseExternalMemoryFirst(canonicalRanks);
  passOrigins = nullptr;
  canonicalRanks = nullptr;
}
RUNTIME_PLAYBACK_VIEW_COLD_MEM bool RuntimePlaybackView::prewarm(size_t eventCapacity) {
  if (!storage.prewarm(eventCapacity)) return false;
  if (passOrigins == nullptr) {
    passOrigins = static_cast<PlaybackPassEventOrigin*>(allocateExternalMemoryFirst(eventCapacity * sizeof(PlaybackPassEventOrigin)));
    if (passOrigins == nullptr) return false;
    for (size_t i = 0; i < eventCapacity; ++i) new (&passOrigins[i]) PlaybackPassEventOrigin{};
  }
  if (!canonicalRanks) canonicalRanks = static_cast<uint16_t*>(allocateExternalMemoryFirst(eventCapacity * sizeof(uint16_t)));
  return canonicalRanks != nullptr;
}

// FLASHMEM: record-stop binding plus its bounded held-note scan. Cold (once per
// take) and RAM1/ITCM is at its bank boundary (see Teensy-RAM1-Bank-Overflow).
RUNTIME_PLAYBACK_VIEW_COLD_MEM bool RuntimePlaybackView::bindRawCoverage(uint32_t loopLengthTicks) {
  if (!rawCoveragePrepared || !complete || !loopLengthTicks ||
      coverage.startPhase >= loopLengthTicks || !coverage.lengthTicks ||
      coverage.lengthTicks > loopLengthTicks - coverage.startPhase) return false;
  coverage.loopLengthTicks = loopLengthTicks;
  rawCoveragePrepared = false;
  // This is now the record-session return window: bound to final geometry, and
  // the only audible source the first take has until the first root publishes
  // (its fill source, the live capture store, is sealed). It must survive the
  // launch's playback-cache resets, so `LoopPlaybackRuntime::reset` preserves it
  // explicitly. A later take releases it.
  firstRecordReturnWindow = true;
  return true;
}

// S4f.19: project a prepared raw bar onto the finalized loop before binding. Cold
// (once per take, in the deferred worker). RAM1/ITCM is at its bank boundary.
RUNTIME_PLAYBACK_VIEW_COLD_MEM bool RuntimePlaybackView::projectRawCoverageToLoopLength(
    uint32_t loopLengthTicks, const uint64_t* openAtStopLanes) {
  if (!rawCoveragePrepared || !complete || loopLengthTicks == 0 || !storage.isPrewarmed() ||
      coverage.startPhase >= loopLengthTicks || storage.eventCount > storage.capacity) {
    return false;
  }
  const size_t count = storage.eventCount;

  // 0. Preflight the held-note closure capacity **before any mutation**. Projection only
  //    drops rows, but closure appends one Off per note left open at the loop end, so a
  //    refusal must be decided here: the worker retries the same window, and a partially
  //    projected window is neither raw nor bound. This non-mutating pass applies the exact
  //    same drop rule and lane logic as steps 1-2, so its count is the count they will use.
  size_t closures = 0;
  {
    std::array<uint64_t, kMidiLaneWords> closed{};
    for (size_t i = count; i-- > 0;) {
      if (!storage.eventRetained(i)) continue;
      MidiEvent event = storage.events[i];
      if (!LoopEventStore::projectRecordStopTick(event, loopLengthTicks)) continue;
      if (!event.isNoteOn() && !event.isNoteOff()) continue;
      if (event.channel == 0 || event.channel > kMidiChannelCount ||
          event.data.noteData.note >= kMidiNoteCount) {
        continue;
      }
      const size_t lane = static_cast<size_t>(event.channel - 1) * kMidiNoteCount +
          event.data.noteData.note;
      const uint64_t bit = static_cast<uint64_t>(1) << (lane & 63u);
      uint64_t& word = closed[lane >> 6];
      if (event.isNoteOff()) {
        word |= bit;
        continue;
      }
      if ((word & bit) != 0) {
        word &= ~bit;
        continue;
      }
      // A lane open at the loop end is closed here only when it was open at the stop.
      // Otherwise its real Off is inside the take, beyond this one-bar window, and
      // closing it here would end the note early.
      if (openAtStopLanes != nullptr && (openAtStopLanes[lane >> 6] & bit) == 0) {
        continue;
      }
      ++closures;
      word |= bit;
    }
  }
  if (closures > storage.capacity - count) {
    return false;  // refused with the window left byte-identical for the retry
  }

  // 1. Record stop tick projection: an overflowing NoteOn is remapped into the loop,
  //    every other overflowing row is dropped. `phases` carries the in-loop tick.
  for (size_t i = 0; i < count; ++i) {
    if (!storage.eventRetained(i)) continue;
    MidiEvent event = storage.events[i];
    if (!LoopEventStore::projectRecordStopTick(event, loopLengthTicks)) {
      storage.discardEvent(i);
      continue;
    }
    storage.events[i] = event;
    storage.phases[i] = event.tick;
  }

  // 2. Close a note left open at the loop end. A reverse pass needs only one 2048-bit
  //    lane map: an Off marks its lane closed, and an unmatched On is held and gets one
  //    synthetic Off at the loop end carrying the On's own identity (the record stop
  //    closure policy). Marking the lane closed also matches an earlier retrigger. Step 0
  //    proved the appended rows fit, so the mutation below cannot fail part-way.
  {
    std::array<uint64_t, kMidiLaneWords> closed{};
    for (size_t i = count; i-- > 0;) {
      if (!storage.eventRetained(i)) continue;
      const MidiEvent& event = storage.events[i];
      if (!event.isNoteOn() && !event.isNoteOff()) continue;
      if (event.channel == 0 || event.channel > kMidiChannelCount ||
          event.data.noteData.note >= kMidiNoteCount) {
        continue;
      }
      const size_t lane = static_cast<size_t>(event.channel - 1) * kMidiNoteCount +
          event.data.noteData.note;
      const uint64_t bit = static_cast<uint64_t>(1) << (lane & 63u);
      uint64_t& word = closed[lane >> 6];
      if (event.isNoteOff()) {
        word |= bit;
        continue;
      }
      if ((word & bit) != 0) {
        word &= ~bit;
        continue;
      }
      if (openAtStopLanes != nullptr && (openAtStopLanes[lane >> 6] & bit) == 0) {
        continue;  // real Off is in the take: the onward window carries it
      }
      MidiEvent terminal = MidiEvent::NoteOff(
          loopLengthTicks - 1, event.channel, event.data.noteData.note, 0);
      terminal.noteId = event.noteId;
      if (!storage.append(terminal)) return false;  // preflighted: unreachable
      storage.phases[storage.eventCount - 1] = terminal.tick;
      word |= bit;
    }
  }

  // 3. Canonical (phase, NoteOff < NoteOn < other) order over the retained rows, stable
  //    by storage index. Insertion sort: bounded by the view role capacity (one bar).
  size_t ordered = 0;
  for (size_t i = 0; i < storage.eventCount; ++i) {
    if (storage.eventRetained(i)) storage.order[ordered++] = static_cast<uint16_t>(i);
  }
  const auto classOf = [](const MidiEvent& event) -> uint8_t {
    return event.isNoteOff() ? 0u : (event.isNoteOn() ? 1u : 2u);
  };
  for (size_t i = 1; i < ordered; ++i) {
    const uint16_t key = storage.order[i];
    size_t j = i;
    while (j > 0) {
      const uint16_t other = storage.order[j - 1];
      const uint32_t keyPhase = storage.phases[key];
      const uint32_t otherPhase = storage.phases[other];
      const bool keyFirst =
          keyPhase != otherPhase
              ? keyPhase < otherPhase
              : (classOf(storage.events[key]) != classOf(storage.events[other])
                     ? classOf(storage.events[key]) < classOf(storage.events[other])
                     : key < other);
      if (!keyFirst) break;
      storage.order[j] = other;
      --j;
    }
    storage.order[j] = key;
  }

  // 4. The bound coverage is the prepared bar intersected with the final loop, so the
  //    existing `bindRawCoverage` preflight accepts it.
  coverage.lengthTicks = std::min<uint32_t>(coverage.lengthTicks,
                                            loopLengthTicks - coverage.startPhase);
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void RuntimePlaybackView::resetMetadata() {
  ++storageGeneration;
  rawCoveragePrepared = false;
  firstRecordReturnWindow = false;
  passOriginsReady = canonicalRanksReady = false; passOriginSource = {};
  storage.clear();
  source = PlaybackViewSourceIdentity{};
  cursorAnchor = EmissionCursor{};
  coverage = PlaybackCoverageInterval{};
  ledgerBoundary.clear();
  sourceFilled = false;
  complete = false;
  published = false;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM FixedPlaybackBridgeStorage::~FixedPlaybackBridgeStorage() {
  release();
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool FixedPlaybackBridgeStorage::prewarm(size_t requestedCapacity) {
  if (records != nullptr) {
    return capacity == requestedCapacity;
  }
  if (requestedCapacity == 0 || requestedCapacity > UINT16_MAX) return false;
  records = static_cast<PlaybackBridgeRecord*>(
      allocateExternalMemoryFirst(requestedCapacity * sizeof(PlaybackBridgeRecord)));
  if (records == nullptr) {
    return false;
  }
  for (size_t index = 0; index < requestedCapacity; ++index) {
    new (&records[index]) PlaybackBridgeRecord();
  }
  capacity = requestedCapacity;
  baseCapacity = RuntimePlaybackViewConfig::kEventsPerView;
  baseOrder = static_cast<uint16_t*>(allocateExternalMemoryFirst(baseCapacity * sizeof(uint16_t)));
  nextCycleBaseOrder = static_cast<uint16_t*>(allocateExternalMemoryFirst(baseCapacity * sizeof(uint16_t)));
  secondCycleBaseOrder = static_cast<uint16_t*>(allocateExternalMemoryFirst(baseCapacity * sizeof(uint16_t)));
  steadyBaseOrder = static_cast<uint16_t*>(allocateExternalMemoryFirst(baseCapacity * sizeof(uint16_t)));
  currentCycleInsertionOrder = static_cast<uint16_t*>(allocateExternalMemoryFirst(requestedCapacity * sizeof(uint16_t)));
  nextCycleInsertionOrder = static_cast<uint16_t*>(allocateExternalMemoryFirst(requestedCapacity * sizeof(uint16_t)));
  secondCycleInsertionOrder = static_cast<uint16_t*>(allocateExternalMemoryFirst(requestedCapacity * sizeof(uint16_t)));
  insertionCycleMask = static_cast<uint8_t*>(allocateExternalMemoryFirst(requestedCapacity));
  baseRetained = static_cast<uint8_t*>(allocateExternalMemoryFirst(baseCapacity));
  canonicalCycleMasks = static_cast<uint8_t*>(allocateExternalMemoryFirst(baseCapacity));
  insertionProvenance = static_cast<uint32_t*>(allocateExternalMemoryFirst(requestedCapacity * sizeof(uint32_t)));
  insertionRanks = static_cast<uint32_t*>(allocateExternalMemoryFirst(requestedCapacity * sizeof(uint32_t)));
  baseRanks = static_cast<uint32_t*>(allocateExternalMemoryFirst(baseCapacity * sizeof(uint32_t)));
  rankedBaseOrder = static_cast<uint16_t*>(allocateExternalMemoryFirst(baseCapacity * sizeof(uint16_t)));
  baseOrderScratch = static_cast<uint16_t*>(allocateExternalMemoryFirst(baseCapacity * sizeof(uint16_t)));
  if (baseOrder == nullptr || nextCycleBaseOrder == nullptr || currentCycleInsertionOrder == nullptr ||
      secondCycleBaseOrder == nullptr || steadyBaseOrder == nullptr ||
      nextCycleInsertionOrder == nullptr || secondCycleInsertionOrder == nullptr ||
      insertionCycleMask == nullptr || baseRetained == nullptr || canonicalCycleMasks == nullptr || insertionProvenance == nullptr ||
      insertionRanks == nullptr || baseRanks == nullptr || rankedBaseOrder == nullptr || baseOrderScratch == nullptr ||
      !insertions.prewarm(requestedCapacity)) {
    release();
    return false;
  }
  resetMetadata();
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void FixedPlaybackBridgeStorage::release() {
  if (records != nullptr) {
    for (size_t index = 0; index < capacity; ++index) {
      records[index].~PlaybackBridgeRecord();
    }
  }
  releaseExternalMemoryFirst(records);
  releaseExternalMemoryFirst(baseOrder);
  releaseExternalMemoryFirst(nextCycleBaseOrder);
  releaseExternalMemoryFirst(secondCycleBaseOrder);
  releaseExternalMemoryFirst(steadyBaseOrder);
  releaseExternalMemoryFirst(currentCycleInsertionOrder);
  releaseExternalMemoryFirst(nextCycleInsertionOrder);
  releaseExternalMemoryFirst(secondCycleInsertionOrder);
  releaseExternalMemoryFirst(insertionCycleMask);
  releaseExternalMemoryFirst(baseRetained);
  releaseExternalMemoryFirst(canonicalCycleMasks);
  releaseExternalMemoryFirst(insertionProvenance);
  releaseExternalMemoryFirst(insertionRanks); releaseExternalMemoryFirst(baseRanks);
  releaseExternalMemoryFirst(rankedBaseOrder); releaseExternalMemoryFirst(baseOrderScratch);
  insertions.release();
  records = nullptr;
  baseOrder = nullptr;
  nextCycleBaseOrder = nullptr;
  secondCycleBaseOrder = nullptr;
  steadyBaseOrder = nullptr;
  currentCycleInsertionOrder = nullptr;
  nextCycleInsertionOrder = nullptr;
  secondCycleInsertionOrder = nullptr;
  insertionCycleMask = nullptr;
  baseRetained = canonicalCycleMasks = nullptr;
  insertionProvenance = insertionRanks = baseRanks = nullptr;
  rankedBaseOrder = baseOrderScratch = nullptr;
  baseCapacity = 0;
  capacity = 0;
  resetMetadata();
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void FixedPlaybackBridgeStorage::resetMetadata() {
  ++storageGeneration;
  recordCount = 0;
  baseEventCount = 0;
  nextCycleBaseEventCount = 0;
  secondCycleBaseEventCount = steadyBaseEventCount = 0;
  currentCycleInsertionCount = nextCycleInsertionCount = secondCycleInsertionCount = steadyInsertionCount = 0;
  canonicalRankCount = 0; canonicalMasksReady = false;
  affectedPitches.fill(0); affectedIdentities.fill(0);
  heldClosureCount = 0; // Retire metadata without clearing the fixed payload array.
  protectsOpenNotes = false;
  hasRetainedClosure = false;
  retainedClosurePhaseLimit = UINT32_MAX;
  preparedAdmissionPhase = 0;
  insertions.clear();
  baseSource = PlaybackViewSourceIdentity{};
  baseStorageGeneration = 0;
  streamPrepared = false;
  source = PlaybackViewSourceIdentity{};
  coverage = PlaybackCoverageInterval{};
  cursorAnchor = EmissionCursor{};
  ledgerBoundary.clear();
  complete = false;
  selected = false;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void FixedPlaybackBridgeStorage::markAffected(const MidiEvent& event) {
  if (!event.isNoteOn() && !event.isNoteOff()) return;
  const unsigned pitch = event.data.noteData.note;
  if (pitch < 128) affectedPitches[pitch / 32] |= uint32_t(1) << (pitch % 32);
  if (event.noteId != kInvalidNoteId) {
    const unsigned key = event.noteId % 128;
    affectedIdentities[key / 32] |= uint32_t(1) << (key % 32);
  }
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool FixedPlaybackBridgeStorage::matchesAffectedLedger(
    const ActiveNoteLedger& ledger) const {
  if (ledger.overflowed() || ledgerBoundary.overflowed) return false;
  const auto relevant = [&](const ActiveNoteLedger::Entry& entry) {
    const unsigned key = entry.noteId % 128;
    return (entry.note < 128 && (affectedPitches[entry.note / 32] & (uint32_t(1) << (entry.note % 32)))) ||
        (entry.noteId != kInvalidNoteId && (affectedIdentities[key / 32] & (uint32_t(1) << (key % 32))));
  };
  size_t index = 0;
  bool same = true;
  ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry& entry) {
    if (!relevant(entry)) return;
    while (index < ledgerBoundary.count && !relevant(ledgerBoundary.openNotes[index])) ++index;
    if (index == ledgerBoundary.count) { same = false; return; }
    const auto& old = ledgerBoundary.openNotes[index++];
    same = same && old.channel == entry.channel && old.note == entry.note &&
        old.noteId == entry.noteId && old.startTick == entry.startTick && old.velocity == entry.velocity;
  });
  while (index < ledgerBoundary.count && !relevant(ledgerBoundary.openNotes[index])) ++index;
  return same && index == ledgerBoundary.count;
}

bool FixedPlaybackBridgeStorage::append(const PlaybackBridgeRecord& record) {
  if (selected || complete || records == nullptr || recordCount >= capacity) {
    return false;
  }
  records[recordCount++] = record;
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool CaptureAdmissionStaging::prewarm() {
  return storage.prewarm(RuntimePlaybackViewConfig::kCaptureStagingEvents);
}

void CaptureAdmissionStaging::reset() {
  storage.clear();
  captureGeneration = 0;
  frozenGeneration = 0;
  frozen = false;
  reservedStagingCredits = 0;
  reservedBridgeCredits = 0;
  reservedStopFinalizationCredits = 0;
  reservations.fill(CaptureNoteReservation{});
}

bool CaptureAdmissionStaging::freeze(uint32_t generation) {
  if (!storage.isPrewarmed() || frozen || generation != captureGeneration) {
    return false;
  }
  frozenGeneration = generation;
  frozen = true;
  return true;
}

size_t CaptureAdmissionStaging::openReservationCount() const {
  size_t count = 0;
  for (const CaptureNoteReservation& reservation : reservations) {
    if (reservation.open) {
      ++count;
    }
  }
  return count;
}

bool CaptureAdmissionStaging::reserveIndependentEvent() {
  if (frozen || !hasCredits(1, 1, 0)) {
    return false;
  }
  ++reservedBridgeCredits;
  return true;
}

void CaptureAdmissionStaging::releaseIndependentEvent() {
  reservedBridgeCredits -= std::min(reservedBridgeCredits, static_cast<size_t>(1));
}

CaptureNoteReservation* CaptureAdmissionStaging::firstOpenReservation() {
  for (CaptureNoteReservation& reservation : reservations) {
    if (reservation.open) {
      return &reservation;
    }
  }
  return nullptr;
}

bool CaptureAdmissionStaging::hasCredits(uint16_t stagingCredits, uint16_t bridgeCredits,
                                         uint16_t stopFinalizationCredits) const {
  const size_t stagingCapacity = storage.capacity >= storage.eventCount
                                     ? storage.capacity - storage.eventCount
                                     : 0;
  const size_t bridgeCapacity = RuntimePlaybackViewConfig::kBridgeCount *
                                RuntimePlaybackViewConfig::kRecordsPerBridge;
  const size_t stopCapacity = RuntimePlaybackViewConfig::kMaxCaptureNoteReservations;
  return stagingCredits <= stagingCapacity - std::min(stagingCapacity, reservedStagingCredits) &&
         bridgeCredits <= bridgeCapacity - std::min(bridgeCapacity, reservedBridgeCredits) &&
         stopFinalizationCredits <=
             stopCapacity - std::min(stopCapacity, reservedStopFinalizationCredits);
}

CaptureNoteReservation* CaptureAdmissionStaging::reserve(
    NoteId noteId, uint8_t channel, uint8_t note, uint16_t stagingCredits, uint16_t bridgeCredits,
    uint16_t stopFinalizationCredits) {
  if (frozen || noteId == kInvalidNoteId ||
      matchingOpenReservation(noteId, channel, note) != nullptr ||
      !hasCredits(stagingCredits, bridgeCredits, stopFinalizationCredits)) {
    return nullptr;
  }
  for (CaptureNoteReservation& reservation : reservations) {
    if (!reservation.open && reservation.noteId == kInvalidNoteId) {
      reservation = CaptureNoteReservation{noteId, channel, note, stagingCredits, bridgeCredits,
                                            stopFinalizationCredits, SIZE_MAX, true};
      reservedStagingCredits += stagingCredits;
      reservedBridgeCredits += bridgeCredits;
      reservedStopFinalizationCredits += stopFinalizationCredits;
      return &reservation;
    }
  }
  return nullptr;
}

CaptureNoteReservation* CaptureAdmissionStaging::matchingOpenReservation(NoteId noteId,
                                                                           uint8_t channel,
                                                                           uint8_t note) {
  for (CaptureNoteReservation& reservation : reservations) {
    if (reservation.open && reservation.noteId == noteId && reservation.channel == channel &&
        reservation.note == note) {
      return &reservation;
    }
  }
  return nullptr;
}

bool CaptureAdmissionStaging::appendContinuationNoteOn(const MidiEvent& event) {
  if (frozen || !event.isNoteOn() || event.noteId == kInvalidNoteId) {
    return false;
  }
  CaptureNoteReservation* reservation =
      reserve(event.noteId, event.channel, event.data.noteData.note, 2, 2, 1);
  if (reservation == nullptr) {
    return false;
  }
  if (storage.append(event)) {
    reservation->noteOnStagingIndex = storage.eventCount - 1;
    return true;
  }
  close(*reservation);
  return false;
}

bool CaptureAdmissionStaging::appendReservedNoteOff(const MidiEvent& event, bool retainPair) {
  if (!event.isNoteOff()) {
    return false;
  }
  CaptureNoteReservation* reservation =
      matchingOpenReservation(event.noteId, event.channel, event.data.noteData.note);
  if (reservation == nullptr || !storage.append(event)) {
    return false;
  }
  if (!retainPair) {
    (void)storage.discardEvent(reservation->noteOnStagingIndex);
    (void)storage.discardEvent(storage.eventCount - 1);
  }
  close(*reservation);
  return true;
}

void CaptureAdmissionStaging::close(CaptureNoteReservation& reservation) {
  reservedStagingCredits -= std::min(reservedStagingCredits,
                                     static_cast<size_t>(reservation.stagingCredits));
  reservedBridgeCredits -= std::min(reservedBridgeCredits,
                                    static_cast<size_t>(reservation.bridgeCredits));
  reservedStopFinalizationCredits -=
      std::min(reservedStopFinalizationCredits,
               static_cast<size_t>(reservation.stopFinalizationCredits));
  reservation = CaptureNoteReservation{};
}

void RuntimePlaybackViewLifecycle::reset() {
  current = 0;
  ready = 1;
  build = 2;
  idle = kNoView;
  retained = kNoView;
}

bool RuntimePlaybackViewLifecycle::retainCurrent(
    std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views) {
  if (current >= views.size() || !views[current].complete ||
      (retained != kNoView && retained != current)) return false;
  retained = current;
  views[current].published = true;
  return true;
}

bool RuntimePlaybackViewLifecycle::publishCompletedBuildAsCurrentWindow(
    std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views) {
  if (build >= views.size() || !views[build].complete) {
    return false;
  }
  // Replacement uses the same atomic swap `handoff` uses: the retired window becomes the next build buffer, so
  // a consumer that read it before this call saw complete content, and no read can straddle the swap. Only a
  // **retained** view is never recycled -- it must stay readable for its own lane.
  const uint8_t retiredCurrent = current;
  if (retiredCurrent < views.size() && retained == retiredCurrent) {
    return false;
  }
  current = build;
  views[current].published = true;
  build = retiredCurrent < views.size() ? retiredCurrent : kNoView;
  if (build != kNoView) {
    views[build].resetMetadata();
  }
  return true;
}

bool RuntimePlaybackViewLifecycle::publishCompletedBuildAsReadySuccessor(
    std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views) {
  if (build >= views.size() || !views[build].complete) {
    return false;
  }
  // One *real* successor at a time: an incomplete shell in the ready role is not one.
  if (ready < views.size() && views[ready].complete) {
    return false;
  }
  const uint8_t shell = ready;
  ready = build;
  build = (shell < views.size() && shell != ready) ? shell : kNoView;
  if (build != kNoView) {
    views[build].resetMetadata();
  }
  return true;
}

bool RuntimePlaybackViewLifecycle::installPreparedCurrent(
    std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views) {
  if (current >= views.size() || ready >= views.size() || current == ready ||
      !views[ready].complete || !views[ready].rawCoveragePrepared ||
      retained != kNoView) {
    return false;
  }
  const uint8_t oldCurrent = current;
  current = ready;
  views[current].published = false;
  retained = current;
  ready = kNoView;
  build = oldCurrent;
  idle = kNoView;
  views[build].resetMetadata();
  return true;
}

bool RuntimePlaybackViewLifecycle::replaceRetainedWithReady(
    std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views) {
  if (retained == kNoView || retained >= views.size() || ready == kNoView ||
      ready >= views.size() || ready == retained || !views[retained].complete ||
      !views[ready].complete || !views[ready].rawCoveragePrepared) {
    return false;
  }
  const uint8_t oldCurrent = retained;
  current = ready;
  retained = current;
  views[current].published = false;
  ready = kNoView;
  build = oldCurrent;
  idle = kNoView;
  views[build].resetMetadata();
  return true;
}

bool RuntimePlaybackViewLifecycle::handoffToRetained(
    std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views) {
  if (retained >= views.size() || !views[retained].complete || views[retained].rawCoveragePrepared) return false;
  current = retained;
  retained = kNoView;
  ready = kNoView;
  build = idle = kNoView;
  for (uint8_t i = 0; i < views.size(); ++i) {
    if (i == current) { views[i].published = true; continue; }
    // Detach at the handoff; the next shared build resets its metadata.
    // Do not clear event or ledger arrays at the return boundary.
    views[i].published = false;
    views[i].complete = false;
    views[i].sourceFilled = false;
    if (build == kNoView) build = i;
    else idle = i;
  }
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool RuntimePlaybackViewLifecycle::handoff(
    std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views, PlaybackViewHandoffMode mode) {
  if (current == kNoView || ready == kNoView || !views[ready].complete ||
      (views[ready].rawCoveragePrepared && mode == PlaybackViewHandoffMode::Audible)) {
    return false;
  }
  const uint8_t retiredCurrent = current;
  views[retiredCurrent].published = retiredCurrent == retained;
  current = ready;
  views[current].published = true;

  // A build that completed before the handoff can immediately become the next
  // successor.  Only then may the retired current view begin a new build.
  if (build != kNoView && views[build].complete &&
      (!views[build].rawCoveragePrepared || mode == PlaybackViewHandoffMode::Preparation)) {
    ready = build;
    build = retiredCurrent == retained ? kNoView : retiredCurrent;
    idle = kNoView;
    if (build != kNoView) views[build].resetMetadata();
    return true;
  }

  // Keep the retired current intact and idle while the successor remains
  // pending.  Reusing it here would require a fourth buffer.
  idle = retiredCurrent == retained ? kNoView : retiredCurrent;
  ready = kNoView;
  // With a retained return window, two views alternate current and successor.
  // Once that successor is consumed, its predecessor can build immediately.
  if (build == kNoView && idle != kNoView) {
    build = idle;
    idle = kNoView;
    views[build].resetMetadata();
  }
  return true;
}

bool RuntimePlaybackViewLifecycle::publishCompletedBuildAsReady(
    std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views, PlaybackViewHandoffMode mode) {
  if (ready != kNoView || build == kNoView ||
      (idle == kNoView && retained == kNoView) || !views[build].complete ||
      (views[build].rawCoveragePrepared && mode == PlaybackViewHandoffMode::Audible)) {
    return false;
  }
  ready = build;
  build = idle;
  idle = kNoView;
  if (build != kNoView) views[build].resetMetadata();
  return true;
}

void RuntimePlaybackBridgeLifecycle::reset() {
  selected = kNoBridge;
  candidate = 0;
}

bool RuntimePlaybackBridgeLifecycle::beginCandidate(
    std::array<FixedPlaybackBridgeStorage, RuntimePlaybackViewConfig::kBridgeCount>& bridges) {
  if (candidate >= bridges.size() || candidate == selected) {
    return false;
  }
  bridges[candidate].resetMetadata();
  return true;
}

bool RuntimePlaybackBridgeLifecycle::selectCandidate(
    std::array<FixedPlaybackBridgeStorage, RuntimePlaybackViewConfig::kBridgeCount>& bridges,
    const EmissionCursor& earliestAffectedCursor) {
  if (candidate >= bridges.size()) {
    return false;
  }
  FixedPlaybackBridgeStorage& next = bridges[candidate];
  if (!next.complete || !next.coverage.contains(earliestAffectedCursor.phase) ||
      next.source.sourceGeneration != earliestAffectedCursor.effectiveSourceGeneration) {
    return false;
  }
  publishCandidate(bridges);
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void RuntimePlaybackBridgeLifecycle::publishCandidate(
    std::array<FixedPlaybackBridgeStorage, RuntimePlaybackViewConfig::kBridgeCount>& bridges) {
  if (selected != kNoBridge) {
    bridges[selected].selected = false;
  }
  selected = candidate;
  bridges[selected].selected = true;
  candidate = selected == 0 ? 1 : 0;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool RuntimePlaybackViewSet::releaseWholeTakeViews() {
  if (!wholeTakeReserved) {
    return true;
  }
  if (bridgeLifecycle.selected != RuntimePlaybackBridgeLifecycle::kNoBridge) {
    // The selected bridge is the authoritative representation right now; releasing its
    // backing would be a use-after-free, so this stays reserved.
    return false;
  }
  for (RuntimePlaybackView& base : bridgeBases) {
    base.release();
  }
  for (FixedPlaybackBridgeStorage& bridge : bridges) {
    bridge.resetMetadata();
    bridge.release();
  }
  bridgeLifecycle.reset();
  wholeTakeReserved = false;
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool RuntimePlaybackViewSet::prewarm(bool reserveWholeTake) {
  if (prewarmed) {
    if (!reserveWholeTake) {
      // Windowed-only request on an already-prewarmed slot: the loop is now known to be long,
      // so the whole-take reservation is dead and must be returned rather than held for the
      // session. A record arm on an empty slot could not know the length; this is where it
      // becomes known.
      return releaseWholeTakeViews();
    }
    if (wholeTakeReserved) {
      return true;
    }
    // Whole-take upgrade for a loop that has since become short. Reachable only from a cold
    // capture-admission boundary (record arm / overdub start), which may allocate.
    for (auto& base : bridgeBases)
      if (!base.prewarm(RuntimePlaybackViewConfig::kEventsPerView)) return false;
    for (FixedPlaybackBridgeStorage& bridge : bridges) {
      if (!bridge.prewarm(RuntimePlaybackViewConfig::kRecordsPerBridge)) return false;
    }
    wholeTakeReserved = true;
    return true;
  }
  for (RuntimePlaybackView& view : views) {
    if (!view.prewarm(RuntimePlaybackViewConfig::kEventsPerView)) {
      for (RuntimePlaybackView& allocated : views) {
        allocated.storage.release();
      }
      return false;
    }
  }
  // RC-MEM-2: the bridges and their immutable bases exist only to publish a whole-take view
  // incrementally, and every full-take publisher requires an exactly full-loop coverage
  // interval. When the owning loop cannot produce one, this capacity can never become
  // authoritative, so it is not reserved. `wholeTakeReserved` makes the refusal explicit:
  // `beginCandidateBridge` denies before any bridge storage is touched, and the stopped-oracle
  // publishers check it too, so nothing depends on the buffers being absent by accident.
  if (reserveWholeTake) {
    for (auto& base : bridgeBases)
      if (!base.prewarm(RuntimePlaybackViewConfig::kEventsPerView)) return false;
    for (FixedPlaybackBridgeStorage& bridge : bridges) {
      if (!bridge.prewarm(RuntimePlaybackViewConfig::kRecordsPerBridge)) {
        for (RuntimePlaybackView& allocated : views) {
          allocated.storage.release();
        }
        for (FixedPlaybackBridgeStorage& allocated : bridges) {
          allocated.release();
        }
        return false;
      }
    }
    wholeTakeReserved = true;
  }
  if (!liveCaptureStaging().prewarm() || !frozenCaptureStaging().prewarm()) {
    for (RuntimePlaybackView& allocated : views) {
      allocated.storage.release();
    }
    for (FixedPlaybackBridgeStorage& allocated : bridges) {
      allocated.release();
    }
    liveCaptureStaging().storage.release();
    frozenCaptureStaging().storage.release();
    return false;
  }
  resetMetadata();
  prewarmed = true;
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void RuntimePlaybackViewSet::resetMetadata() {
  for (auto& base : bridgeBases) base.resetMetadata();
  for (RuntimePlaybackView& view : views) {
    view.resetMetadata();
  }
  for (FixedPlaybackBridgeStorage& bridge : bridges) {
    bridge.resetMetadata();
  }
  liveCaptureStaging().reset();
  frozenCaptureStaging().reset();
  liveStagingIndex = 0;
  frozenStagingIndex = 1;
  lifecycle.reset();
  bridgeLifecycle.reset();
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void RuntimePlaybackViewSet::resetMetadataPreserveRetained() {
  resetMetadataPreserveView(lifecycle.retained);
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM uint8_t RuntimePlaybackViewSet::firstRecordReturnWindowIndex() const {
  if (lifecycle.current < views.size() && views[lifecycle.current].firstRecordReturnWindow) {
    return lifecycle.current;
  }
  if (lifecycle.retained < views.size() && views[lifecycle.retained].firstRecordReturnWindow) {
    return lifecycle.retained;
  }
  return RuntimePlaybackViewLifecycle::kNoView;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void RuntimePlaybackViewSet::resetMetadataPreserveView(uint8_t keep) {
  if (keep >= views.size()) {
    resetMetadata();
    return;
  }
  for (uint8_t i = 0; i < views.size(); ++i) {
    if (i != keep) views[i].resetMetadata();
  }
  views[keep].published = false;
  lifecycle.current = keep;
  lifecycle.ready = RuntimePlaybackViewLifecycle::kNoView;
  lifecycle.retained = keep;
  lifecycle.build = RuntimePlaybackViewLifecycle::kNoView;
  lifecycle.idle = RuntimePlaybackViewLifecycle::kNoView;
  for (uint8_t i = 0; i < views.size(); ++i) {
    if (i == keep) continue;
    if (lifecycle.build == RuntimePlaybackViewLifecycle::kNoView) lifecycle.build = i;
    else lifecycle.idle = i;
  }
  bridgeLifecycle.reset();
}

bool RuntimePlaybackViewSet::handoffReadyCoverage(PlaybackViewHandoffMode mode) { return lifecycle.handoff(views, mode); }

bool RuntimePlaybackViewSet::publishCompletedBuildAsReady(PlaybackViewHandoffMode mode) {
  return lifecycle.publishCompletedBuildAsReady(views, mode);
}

bool RuntimePlaybackViewSet::beginCandidateBridge() {
  // RC-MEM-2: a slot that reserved no whole-take representation must deny here, before
  // `RuntimePlaybackBridgeLifecycle::beginCandidate` touches bridge storage, rather than rely
  // on the buffers happening to be null.
  if (!wholeTakeReserved) return false;
  return bridgeLifecycle.beginCandidate(bridges);
}

bool RuntimePlaybackViewSet::selectCandidateBridge(const EmissionCursor& earliestAffectedCursor) {
  if (!wholeTakeReserved) return false;
  return bridgeLifecycle.selectCandidate(bridges, earliestAffectedCursor);
}

size_t RuntimePlaybackViewSet::fixedExternalBytes() const {
  constexpr size_t eventBytes = sizeof(MidiEvent) + sizeof(uint16_t) + sizeof(uint32_t) +
                                sizeof(uint16_t) + sizeof(uint8_t);
  // Views and staging are always reserved; the whole-take part is reported only when it was
  // actually admitted, so this figure is the slot's real reservation (RC-MEM-2 assertion).
  const size_t viewBytes =
      RuntimePlaybackViewConfig::kViewCount * RuntimePlaybackViewConfig::kEventsPerView *
          (eventBytes + sizeof(PlaybackPassEventOrigin) + sizeof(uint16_t)) +
      RuntimePlaybackViewConfig::kCaptureStagingEvents * eventBytes * captureStagings.size();
  if (!wholeTakeReserved) {
    return viewBytes;
  }
  return viewBytes +
         RuntimePlaybackViewConfig::kBridgeCount * RuntimePlaybackViewConfig::kEventsPerView *
             (eventBytes + sizeof(PlaybackPassEventOrigin) + sizeof(uint16_t)) +
         RuntimePlaybackViewConfig::kBridgeCount *
             (RuntimePlaybackViewConfig::kRecordsPerBridge * (sizeof(PlaybackBridgeRecord) + eventBytes +
                                                            2 * sizeof(uint32_t) + 3 * sizeof(uint16_t) + sizeof(uint8_t)) +
              RuntimePlaybackViewConfig::kEventsPerView * (6 * sizeof(uint16_t) + sizeof(uint32_t) + 2 * sizeof(uint8_t)));
}
