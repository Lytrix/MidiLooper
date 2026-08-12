//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Phase 2 G2 slice 1 — pending session delta + geometry bridge.

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/Utils/RuntimeTimingEnvelope.cpp"

#include "Loop.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "Utils/NoteUtils.h"

#include <algorithm>
#include <vector>

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

int countKind(const PendingNoteChangeVec& pending, PendingNoteChangeKind kind) {
  int count = 0;
  for (const PendingNoteChange& change : pending) {
    if (change.kind == kind) {
      ++count;
    }
  }
  return count;
}

const PendingNoteChange* findTransform(const PendingNoteChangeVec& pending, NoteId noteId) {
  for (const PendingNoteChange& change : pending) {
    if ((change.kind == PendingNoteChangeKind::Shorten ||
         change.kind == PendingNoteChangeKind::Hide) &&
        change.noteId == noteId) {
      return &change;
    }
  }
  return nullptr;
}

void seedLongSourceNote(Loop& loop, NoteId id, uint32_t onTick, uint32_t offTick, uint8_t pitch) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, 1, pitch, 100, id));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, 1, pitch, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
  loop.nextNoteId_ = id + 1;
}

struct TransformKey {
  NoteId noteId = kInvalidNoteId;
  PendingNoteChangeKind kind = PendingNoteChangeKind::Add;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};

bool operator<(const TransformKey& a, const TransformKey& b) {
  if (a.noteId != b.noteId) {
    return a.noteId < b.noteId;
  }
  if (a.kind != b.kind) {
    return static_cast<uint8_t>(a.kind) < static_cast<uint8_t>(b.kind);
  }
  if (a.startTick != b.startTick) {
    return a.startTick < b.startTick;
  }
  return a.endTick < b.endTick;
}

bool operator==(const TransformKey& a, const TransformKey& b) {
  return a.noteId == b.noteId && a.kind == b.kind && a.startTick == b.startTick &&
         a.endTick == b.endTick;
}

std::vector<TransformKey> collectTransforms(const PendingNoteChangeVec& pending) {
  std::vector<TransformKey> keys;
  for (const PendingNoteChange& change : pending) {
    if (change.kind != PendingNoteChangeKind::Shorten &&
        change.kind != PendingNoteChangeKind::Hide) {
      continue;
    }
    keys.push_back(TransformKey{change.noteId, change.kind, change.startTick, change.endTick});
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

void assertTransformsEqual(const std::vector<TransformKey>& reference,
                           const std::vector<TransformKey>& candidate) {
  TEST_ASSERT_EQUAL_UINT32(reference.size(), candidate.size());
  if (reference.size() != candidate.size()) {
    return;
  }
  for (size_t i = 0; i < reference.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(reference[i].noteId, candidate[i].noteId);
    TEST_ASSERT_EQUAL(static_cast<int>(reference[i].kind), static_cast<int>(candidate[i].kind));
    TEST_ASSERT_EQUAL_UINT32(reference[i].startTick, candidate[i].startTick);
    TEST_ASSERT_EQUAL_UINT32(reference[i].endTick, candidate[i].endTick);
  }
}

void assertWindowedMatchesFull(Loop& loop, uint8_t pitch, uint32_t startTick, uint32_t endTick,
                               NoteId incomingId) {
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  SessionMidiEventVec fullEvents;
  loop.gatherCommittedEvents(fullEvents);
  const NoteUtils::DisplayNoteVec fullNotes =
      NoteUtils::reconstructDisplayNotes(fullEvents, loop.loopLengthTicks, false);
  loop.clearPendingNoteChanges();
  loop.accumulatePendingNoteChangesFromSourceNotes(fullNotes, 1, pitch, 90, startTick, endTick,
                                                   incomingId);
  const std::vector<TransformKey> reference = collectTransforms(loop.pendingNoteChanges());

  SessionMidiEventVec pitchEvents;
  loop.gatherCommittedNoteEventsForPitch(pitch, pitchEvents);
  const NoteUtils::DisplayNoteVec pitchNotes =
      NoteUtils::reconstructDisplayNotes(pitchEvents, loop.loopLengthTicks, false);
  loop.clearPendingNoteChanges();
  loop.accumulatePendingNoteChangesFromSourceNotes(pitchNotes, 1, pitch, 90, startTick, endTick,
                                                   incomingId);
  const std::vector<TransformKey> candidate = collectTransforms(loop.pendingNoteChanges());
  assertTransformsEqual(reference, candidate);
}

void drainIdleVisualCache(Loop& loop) {
  loop.invalidateDisplayCaches();
  uint32_t guard = 0;
  while (loop.visualCacheDirty && guard < 512u) {
    loop.rebuildVisualCacheIdleSlice(1, 0, UINT32_MAX);
    ++guard;
  }
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
}

void assertSliceCacheMatchesFull(Loop& loop, uint8_t pitch, uint32_t startTick, uint32_t endTick,
                                 NoteId incomingId) {
  drainIdleVisualCache(loop);
  loop.markDisplayCachesStale();
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  SessionMidiEventVec fullEvents;
  loop.gatherCommittedEvents(fullEvents);
  const NoteUtils::DisplayNoteVec fullNotes =
      NoteUtils::reconstructDisplayNotes(fullEvents, loop.loopLengthTicks, false);
  loop.clearPendingNoteChanges();
  loop.accumulatePendingNoteChangesFromSourceNotes(fullNotes, 1, pitch, 90, startTick, endTick,
                                                   incomingId);
  const std::vector<TransformKey> reference = collectTransforms(loop.pendingNoteChanges());

  loop.clearPendingNoteChanges();
  loop.accumulatePendingNoteChangesFromSourceNotes(loop.visualCache.notes, 1, pitch, 90, startTick,
                                                   endTick, incomingId);
  const std::vector<TransformKey> candidate = collectTransforms(loop.pendingNoteChanges());
  assertTransformsEqual(reference, candidate);
}

void seedStoreNote(LoopEventStore& store, uint32_t onTick, uint32_t offTick, uint8_t pitch,
                   NoteId id) {
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, 1, pitch, 100, id));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, 1, pitch, 0)));
}

}  // namespace

void test_pending_requires_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  TEST_ASSERT_FALSE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 100, 20, 40, 99));
  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
}

void test_pending_add_only_when_no_overlap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 10, 58, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 72, 90, 200, 240, 10));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewEvents().empty());
}

void test_pending_shorten_long_source_on_overlap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));

  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(shorten->kind));
  TEST_ASSERT_EQUAL_UINT32(50u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(119u, shorten->endTick);

  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewEvents().empty());
}

void test_pending_shorten_ignores_recorded_channel() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(2, 60, 90, 120, 160, 10));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));

  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL_UINT32(50u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(119u, shorten->endTick);
}

void test_pending_hide_when_covered() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(40, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 50, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(80, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 90, 1, 60, 100, 3));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(120, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
  loop.nextNoteId_ = 4;
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 100, 5, 130, 20));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(3, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 2));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 3));
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Hide),
                    static_cast<int>(findTransform(loop.pendingNoteChanges(), 1)->kind));
}

void test_pending_survives_wraps_and_accumulates() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10));
  // Simulate wrap: second insert at low phase against same source view.
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 72, 90, 8, 30, 11));
  TEST_ASSERT_EQUAL(2, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewEvents().empty());
}

void test_discard_clears_pending_with_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10));
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());
  loop.discardCapture();
  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
}

void test_seal_pending_shorten_to_edit_pass_after_overdub_publish() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(120, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(160, 1, 60, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_EQUAL(1u, loop.passes.overdubPasses.size());

  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, companionIds.size());
  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(loop.passes.editPasses[0].actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::Length),
                    static_cast<int>(loop.passes.editPasses[0].propertyType));
  TEST_ASSERT_EQUAL_UINT32(1u, loop.passes.editPasses[0].targetNoteId);
  TEST_ASSERT_EQUAL_UINT32(119u, loop.passes.editPasses[0].endTick);
  TEST_ASSERT_EQUAL_UINT8(kOverdubCompanionEditPassIndex, loop.passes.editPasses[0].editPassIndex);

  SessionMidiEventVec flat;
  loop.gatherCommittedEvents(flat);
  bool foundShortenedOff = false;
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick == 119) {
      foundShortenedOff = true;
    }
  }
  TEST_ASSERT_TRUE(foundShortenedOff);
}

// Option B wrap-equivalence matrix. Incoming that ends after wrap uses an unwrapped
// endTick (> loopLen) so windowLength is the sounding duration. Production
// accumulatePendingNoteChangesForIncomingNote still rejects endTick < startTick.

void test_windowed_overlap_matches_full_interior() {
  // Interior note entirely inside the loop.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  assertWindowedMatchesFull(loop, 60, 120, 160, 10);
}

void test_windowed_overlap_matches_full_incoming_near_loop_end() {
  // Incoming note starting near loop end (stays in tail).
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, kLoopLen - 80, kLoopLen - 10, 60);
  assertWindowedMatchesFull(loop, 60, kLoopLen - 60, kLoopLen - 20, 10);
}

void test_windowed_overlap_matches_full_incoming_ending_after_wrap() {
  // Incoming note ending after wrap, against a wrap-spanning candidate.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, kLoopLen - 40, 20, 60);
  assertWindowedMatchesFull(loop, 60, kLoopLen - 30, kLoopLen + 16, 10);
}

void test_windowed_overlap_matches_full_incoming_ending_after_wrap_vs_head() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 8, 48, 60);
  assertWindowedMatchesFull(loop, 60, kLoopLen - 20, kLoopLen + 30, 10);
}

void test_windowed_overlap_matches_full_incoming_ending_after_wrap_vs_tail() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, kLoopLen - 40, kLoopLen - 10, 60);
  assertWindowedMatchesFull(loop, 60, kLoopLen - 30, kLoopLen + 20, 10);
}

void test_windowed_overlap_matches_full_incoming_at_tick_zero() {
  // Incoming note beginning exactly at tick 0.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 80, 60);
  assertWindowedMatchesFull(loop, 60, 0, 40, 10);
}

void test_windowed_overlap_matches_full_candidate_in_tail() {
  // Existing candidate entirely in the loop tail.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, kLoopLen - 40, kLoopLen - 10, 60);
  assertWindowedMatchesFull(loop, 60, kLoopLen - 50, kLoopLen - 5, 10);
}

void test_windowed_overlap_matches_full_candidate_in_head() {
  // Existing candidate entirely in the loop head.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 8, 48, 60);
  assertWindowedMatchesFull(loop, 60, 0, 64, 10);
}

void test_windowed_overlap_matches_full_wrap_tail_to_head() {
  // Existing candidate spanning tail → head.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, kLoopLen - 40, 20, 60);
  assertWindowedMatchesFull(loop, 60, 0, 30, 10);
}

void test_windowed_overlap_matches_full_incoming_in_tail_against_wrap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, kLoopLen - 40, 20, 60);
  assertWindowedMatchesFull(loop, 60, kLoopLen - 30, kLoopLen - 5, 10);
}

void test_windowed_overlap_matches_full_boundary_touch() {
  // Incoming starts exactly at source endTick (half-open; both paths must agree).
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 120, 60);
  assertWindowedMatchesFull(loop, 60, 120, 160, 10);
}

void test_windowed_overlap_matches_full_boundary_incoming_ends_at_source_start() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 120, 200, 60);
  assertWindowedMatchesFull(loop, 60, 50, 120, 10);
}

void test_windowed_overlap_matches_full_boundary_incoming_starts_at_wrap_on() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, kLoopLen - 40, 20, 60);
  assertWindowedMatchesFull(loop, 60, kLoopLen - 40, kLoopLen - 20, 10);
}

void test_windowed_overlap_matches_full_multiple_same_pitch_around_wrap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  LoopEventStore store;
  seedStoreNote(store, kLoopLen - 40, kLoopLen - 10, 60, 1);
  seedStoreNote(store, 8, 48, 60, 2);
  seedStoreNote(store, kLoopLen - 80, 16, 60, 3);
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
  loop.nextNoteId_ = 4;
  assertWindowedMatchesFull(loop, 60, 0, 40, 20);
}

void test_windowed_overlap_matches_full_incoming_ending_after_wrap_multiple_spans() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  LoopEventStore store;
  seedStoreNote(store, kLoopLen - 40, kLoopLen - 10, 60, 1);
  seedStoreNote(store, 8, 48, 60, 2);
  seedStoreNote(store, kLoopLen - 80, 16, 60, 3);
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
  loop.nextNoteId_ = 4;
  assertWindowedMatchesFull(loop, 60, kLoopLen - 50, kLoopLen + 40, 20);
}

void test_windowed_overlap_matches_full_multiple_overlapping_spans() {
  // Multiple projected/overlapping spans (interior).
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  LoopEventStore store;
  seedStoreNote(store, 10, 200, 60, 1);
  seedStoreNote(store, 80, 240, 60, 2);
  seedStoreNote(store, 40, 90, 60, 3);
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
  loop.nextNoteId_ = 4;
  assertWindowedMatchesFull(loop, 60, 70, 150, 20);
}

void test_windowed_overlap_matches_full_no_overlap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 10, 58, 60);
  assertWindowedMatchesFull(loop, 72, 200, 240, 10);
}

void test_windowed_overlap_matches_full_note_split_across_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  LoopEventStore store;
  const uint16_t chunkCapacity = LoopEventStoreConfig::CHUNK_CAPACITY;
  TEST_ASSERT_EQUAL_UINT16(256, chunkCapacity);
  NoteId nextId = 10;
  for (uint16_t i = 0; i < (chunkCapacity / 2) - 1; ++i) {
    seedStoreNote(store, static_cast<uint32_t>(i) * 2u, static_cast<uint32_t>(i) * 2u + 1u, 72,
                  nextId++);
  }
  TEST_ASSERT_EQUAL_UINT32(chunkCapacity - 2u, store.size());
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 50, 1, 60, 100, 1));
  TEST_ASSERT_EQUAL_UINT32(chunkCapacity - 1u, store.size());
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(255, 1, 74, 80)));
  TEST_ASSERT_EQUAL_UINT32(chunkCapacity, store.size());
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(400, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
  loop.nextNoteId_ = nextId;
  TEST_ASSERT_TRUE(loop.passes.recordPass.committedChunkIds.size() >= 2u);
  // Incoming sits in the sounding interval but between the on-chunk span and the off-chunk
  // span. Event-window and intersecting-chunk gathers both miss the pair.
  assertWindowedMatchesFull(loop, 60, 300, 350, 99);
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
}

void test_slice_cache_overlap_matches_full_interior() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  assertSliceCacheMatchesFull(loop, 60, 120, 160, 10);
}

void test_slice_cache_overlap_matches_full_wrap_tail_to_head() {
  TEST_IGNORE_MESSAGE("Option A rejected: slice-built visualCache misses wrap notes");
}

void test_slice_cache_overlap_matches_full_note_split_across_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  LoopEventStore store;
  const uint16_t chunkCapacity = LoopEventStoreConfig::CHUNK_CAPACITY;
  NoteId nextId = 10;
  for (uint16_t i = 0; i < (chunkCapacity / 2) - 1; ++i) {
    seedStoreNote(store, static_cast<uint32_t>(i) * 2u, static_cast<uint32_t>(i) * 2u + 1u, 72,
                  nextId++);
  }
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 50, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(255, 1, 74, 80)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(400, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
  loop.nextNoteId_ = nextId;
  assertSliceCacheMatchesFull(loop, 60, 300, 350, 99);
}

void test_slice_cache_overlap_matches_full_long_note_spanning_bars() {
  TEST_IGNORE_MESSAGE(
      "Option A rejected: slice-built visualCache misses notes spanning beyond idle-slice pad");
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_pending_requires_source_view);
  RUN_TEST(test_pending_add_only_when_no_overlap);
  RUN_TEST(test_pending_shorten_long_source_on_overlap);
  RUN_TEST(test_pending_shorten_ignores_recorded_channel);
  RUN_TEST(test_pending_hide_when_covered);
  RUN_TEST(test_pending_survives_wraps_and_accumulates);
  RUN_TEST(test_discard_clears_pending_with_source_view);
  RUN_TEST(test_seal_pending_shorten_to_edit_pass_after_overdub_publish);
  RUN_TEST(test_windowed_overlap_matches_full_interior);
  RUN_TEST(test_windowed_overlap_matches_full_incoming_near_loop_end);
  RUN_TEST(test_windowed_overlap_matches_full_incoming_ending_after_wrap);
  RUN_TEST(test_windowed_overlap_matches_full_incoming_ending_after_wrap_vs_head);
  RUN_TEST(test_windowed_overlap_matches_full_incoming_ending_after_wrap_vs_tail);
  RUN_TEST(test_windowed_overlap_matches_full_incoming_at_tick_zero);
  RUN_TEST(test_windowed_overlap_matches_full_candidate_in_tail);
  RUN_TEST(test_windowed_overlap_matches_full_candidate_in_head);
  RUN_TEST(test_windowed_overlap_matches_full_wrap_tail_to_head);
  RUN_TEST(test_windowed_overlap_matches_full_incoming_in_tail_against_wrap);
  RUN_TEST(test_windowed_overlap_matches_full_boundary_touch);
  RUN_TEST(test_windowed_overlap_matches_full_boundary_incoming_ends_at_source_start);
  RUN_TEST(test_windowed_overlap_matches_full_boundary_incoming_starts_at_wrap_on);
  RUN_TEST(test_windowed_overlap_matches_full_multiple_same_pitch_around_wrap);
  RUN_TEST(test_windowed_overlap_matches_full_incoming_ending_after_wrap_multiple_spans);
  RUN_TEST(test_windowed_overlap_matches_full_multiple_overlapping_spans);
  RUN_TEST(test_windowed_overlap_matches_full_no_overlap);
  RUN_TEST(test_windowed_overlap_matches_full_note_split_across_chunks);
  RUN_TEST(test_slice_cache_overlap_matches_full_interior);
  RUN_TEST(test_slice_cache_overlap_matches_full_wrap_tail_to_head);
  RUN_TEST(test_slice_cache_overlap_matches_full_note_split_across_chunks);
  RUN_TEST(test_slice_cache_overlap_matches_full_long_note_spanning_bars);
  return UNITY_END();
}
