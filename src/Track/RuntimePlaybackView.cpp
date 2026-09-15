//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RuntimePlaybackView.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#include "Utils/InternalHeapFirstAllocator.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define RUNTIME_PLAYBACK_VIEW_COLD_MEM FLASHMEM
#else
#define RUNTIME_PLAYBACK_VIEW_COLD_MEM
#endif

namespace {

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

bool ledgerEntryEquals(const ActiveNoteLedger::Entry& a, const ActiveNoteLedger::Entry& b) {
  return a.channel == b.channel && a.note == b.note && a.noteId == b.noteId &&
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

RUNTIME_PLAYBACK_VIEW_COLD_MEM RuntimePlaybackView::~RuntimePlaybackView() {
  releaseExternalMemoryFirst(passOrigins);
  releaseExternalMemoryFirst(canonicalRanks);
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

bool RuntimePlaybackView::bindRawCoverage(uint32_t loopLengthTicks) {
  if (!rawCoveragePrepared || !complete || !loopLengthTicks ||
      coverage.startPhase >= loopLengthTicks || !coverage.lengthTicks ||
      coverage.lengthTicks > loopLengthTicks - coverage.startPhase) return false;
  coverage.loopLengthTicks = loopLengthTicks;
  rawCoveragePrepared = false;
  return true;
}

void RuntimePlaybackView::resetMetadata() {
  ++storageGeneration;
  rawCoveragePrepared = false;
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

void FixedPlaybackBridgeStorage::resetMetadata() {
  ++storageGeneration;
  recordCount = 0;
  baseEventCount = 0;
  nextCycleBaseEventCount = 0;
  secondCycleBaseEventCount = steadyBaseEventCount = 0;
  currentCycleInsertionCount = nextCycleInsertionCount = secondCycleInsertionCount = steadyInsertionCount = 0;
  canonicalRankCount = 0; canonicalMasksReady = false;
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

bool RuntimePlaybackViewLifecycle::handoff(
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

RUNTIME_PLAYBACK_VIEW_COLD_MEM bool RuntimePlaybackViewSet::prewarm() {
  if (prewarmed) {
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
  if (!captureStaging.prewarm()) {
    for (RuntimePlaybackView& allocated : views) {
      allocated.storage.release();
    }
    for (FixedPlaybackBridgeStorage& allocated : bridges) {
      allocated.release();
    }
    captureStaging.storage.release();
    return false;
  }
  resetMetadata();
  prewarmed = true;
  return true;
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void RuntimePlaybackViewSet::resetMetadata() {
  for (RuntimePlaybackView& view : views) {
    view.resetMetadata();
  }
  for (FixedPlaybackBridgeStorage& bridge : bridges) {
    bridge.resetMetadata();
  }
  captureStaging.reset();
  lifecycle.reset();
  bridgeLifecycle.reset();
}

RUNTIME_PLAYBACK_VIEW_COLD_MEM void RuntimePlaybackViewSet::resetMetadataPreserveRetained() {
  const uint8_t keep = lifecycle.retained;
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
  return bridgeLifecycle.beginCandidate(bridges);
}

bool RuntimePlaybackViewSet::selectCandidateBridge(const EmissionCursor& earliestAffectedCursor) {
  return bridgeLifecycle.selectCandidate(bridges, earliestAffectedCursor);
}

size_t RuntimePlaybackViewSet::fixedExternalBytes() const {
  constexpr size_t eventBytes = sizeof(MidiEvent) + sizeof(uint16_t) + sizeof(uint32_t) +
                                sizeof(uint16_t) + sizeof(uint8_t);
  return RuntimePlaybackViewConfig::kViewCount * RuntimePlaybackViewConfig::kEventsPerView *
             (eventBytes + sizeof(PlaybackPassEventOrigin) + sizeof(uint16_t)) +
         RuntimePlaybackViewConfig::kBridgeCount *
             (RuntimePlaybackViewConfig::kRecordsPerBridge * (sizeof(PlaybackBridgeRecord) + eventBytes +
                                                            2 * sizeof(uint32_t) + 3 * sizeof(uint16_t) + sizeof(uint8_t)) +
              RuntimePlaybackViewConfig::kEventsPerView * (6 * sizeof(uint16_t) + sizeof(uint32_t) + 2 * sizeof(uint8_t))) +
         RuntimePlaybackViewConfig::kCaptureStagingEvents * eventBytes;
}
