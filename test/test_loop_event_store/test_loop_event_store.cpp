//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include "../../src/LoopEventStore.cpp"
#include "../../src/Loop/LoopSnapshot.cpp"
#include "LoopEventStore.h"
#include "LoopEventBuffer.h"
#include "LoopSnapshot.h"
#include "MidiEvent.h"

namespace {

constexpr size_t kDirectoryMeasurementPageCapacity = 32;
constexpr size_t kDirectoryMeasurementBarCount = 1024;

struct MeasurementMembership {
  std::vector<uint32_t> references;
};

struct MeasurementPage {
  std::vector<std::shared_ptr<const MeasurementMembership>> memberships;
};

struct MeasurementPagedDirectory {
  std::vector<std::shared_ptr<const MeasurementPage>> pages;
};

MeasurementPagedDirectory makeMeasurementPagedDirectory(size_t barCount) {
  MeasurementPagedDirectory directory;
  const size_t pageCount =
      (barCount + kDirectoryMeasurementPageCapacity - 1) /
      kDirectoryMeasurementPageCapacity;
  directory.pages.reserve(pageCount);
  for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
    auto page = std::make_shared<MeasurementPage>();
    const size_t firstBar = pageIndex * kDirectoryMeasurementPageCapacity;
    const size_t pageBarCount =
        std::min(kDirectoryMeasurementPageCapacity, barCount - firstBar);
    page->memberships.resize(pageBarCount);
    for (size_t barOffset = 0; barOffset < pageBarCount; ++barOffset) {
      auto membership = std::make_shared<MeasurementMembership>();
      membership->references.push_back(static_cast<uint32_t>(firstBar + barOffset));
      page->memberships[barOffset] = std::move(membership);
    }
    directory.pages.push_back(std::move(page));
  }
  return directory;
}

}  // namespace

void test_append_fills_chunks_without_realloc_pattern() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < 300; ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  }
  TEST_ASSERT_EQUAL(300u, store.size());
  TEST_ASSERT_EQUAL(2u, store.chunkIds().size());
  TEST_ASSERT_EQUAL(256u, store.at(256).tick);
}

void test_adopt_all_moves_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore capture;
  LoopEventStore committed;

  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  committed.adoptAll(capture);
  TEST_ASSERT_TRUE(capture.empty());
  TEST_ASSERT_EQUAL(1u, committed.size());
  TEST_ASSERT_EQUAL(10u, committed.at(0).tick);
}

void test_merge_from_combines_sorted_events() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore base;
  LoopEventStore overdub;

  TEST_ASSERT_TRUE(base.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOn(20, 1, 62, 90)));
  base.mergeFrom(overdub);

  TEST_ASSERT_EQUAL(2u, base.size());
  TEST_ASSERT_EQUAL(10u, base.at(0).tick);
  TEST_ASSERT_EQUAL(20u, base.at(1).tick);
}

void test_merge_from_interleaves_across_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore base;
  LoopEventStore overdub;

  for (uint16_t i = 0; i < 300; i += 2) {
    TEST_ASSERT_TRUE(base.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  }
  for (uint16_t i = 1; i < 300; i += 2) {
    TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOn(i, 1, 62, 90)));
  }

  base.mergeFrom(overdub);
  TEST_ASSERT_EQUAL(300u, base.size());
  for (uint16_t i = 0; i < 300; ++i) {
    TEST_ASSERT_EQUAL(i, base.at(i).tick);
  }
  TEST_ASSERT_TRUE(base.chunkIds().size() >= 2u);
}

void test_merge_from_stable_on_equal_tick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore base;
  LoopEventStore overdub;

  TEST_ASSERT_TRUE(base.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOn(10, 1, 62, 90)));
  base.mergeFrom(overdub);

  TEST_ASSERT_EQUAL(2u, base.size());
  TEST_ASSERT_EQUAL(60, base.at(0).data.noteData.note);
  TEST_ASSERT_EQUAL(62, base.at(1).data.noteData.note);
}

void test_restore_snapshot_deep_copies() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CowLoopEventStore live;
  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(1, 1, 60, 100)));
  const auto snap = live.shareForSnapshot();
  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(2, 1, 62, 90)));
  TEST_ASSERT_EQUAL(2u, live.size());

  live.restoreFromSnapshot(snap);
  TEST_ASSERT_EQUAL(1u, live.size());
  TEST_ASSERT_EQUAL(60, live.at(0).data.noteData.note);

  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(99, 1, 99, 99)));
  TEST_ASSERT_EQUAL(1u, snap->size());
  TEST_ASSERT_EQUAL(60, snap->at(0).data.noteData.note);
}

void test_shift_all_ticks_bumps_negative() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(5, 1, 60, 100)));
  store.shiftAllTicks(-10);
  TEST_ASSERT_EQUAL(0u, store.at(0).tick);
}

void test_lower_bound_index_skips_prefix() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < 300; ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  }

  TEST_ASSERT_EQUAL(250u, store.lowerBoundIndex(250));
  TEST_ASSERT_EQUAL(300u, store.lowerBoundIndex(9999));
}

void test_undo_snapshot_ref_isolated_by_cow() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CowLoopEventStore live;
  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(1, 1, 60, 100)));
  const auto snap = live.shareForSnapshot();
  TEST_ASSERT_EQUAL(1u, snap->size());

  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(2, 1, 62, 90)));
  TEST_ASSERT_EQUAL(2u, live.size());
  TEST_ASSERT_EQUAL(1u, snap->size());
}

void test_drop_events_at_or_beyond_tick_removes_overflow() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1500, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1520, 1, 60, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1548, 1, 61, 100)));  // overflow
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1590, 1, 61, 0)));   // overflow

  store.dropEventsAtOrBeyondTick(1536);
  TEST_ASSERT_EQUAL(2u, store.size());
  TEST_ASSERT_EQUAL(1500u, store.at(0).tick);
  TEST_ASSERT_EQUAL(1520u, store.at(1).tick);
}

void test_map_overflow_note_on_keeps_in_loop_stop_off() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  MidiEvent overflowOn = MidiEvent::NoteOn(5529, 4, 81, 100);
  overflowOn.noteId = 9;
  MidiEvent stopOff = MidiEvent::NoteOff(192, 4, 81, 0);
  stopOff.noteId = 9;
  TEST_ASSERT_TRUE(store.append(overflowOn));
  TEST_ASSERT_TRUE(store.append(stopOff));

  store.mapNoteOnsAtOrBeyondTickIntoLoop(5376);
  TEST_ASSERT_EQUAL(2u, store.size());
  TEST_ASSERT_TRUE(store.at(0).isNoteOn());
  TEST_ASSERT_EQUAL(153u, store.at(0).tick);
  TEST_ASSERT_TRUE(store.at(1).isNoteOff());
  TEST_ASSERT_EQUAL(192u, store.at(1).tick);
}

void test_first_index_for_bar_uses_bar_index() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(0, 1, 60, 100)));      // bar 0
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(770, 1, 61, 100)));    // bar 1
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1536, 1, 62, 100)));   // bar 2

  TEST_ASSERT_EQUAL(0u, store.firstIndexForBar(0));
  TEST_ASSERT_EQUAL(1u, store.firstIndexForBar(1));
  TEST_ASSERT_EQUAL(2u, store.firstIndexForBar(2));
  TEST_ASSERT_EQUAL(3u, store.firstIndexForBar(9));
}

void test_transfer_capture_chunk_ids_to_published() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(20, 1, 64, 100)));

  CaptureChunkIdList captureIds;
  store.detachChunksTo(captureIds);
  TEST_ASSERT_EQUAL(1u, captureIds.size());
  TEST_ASSERT_TRUE(store.empty());

  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(committedChunkIds, captureIds));
  TEST_ASSERT_TRUE(captureIds.empty());
  TEST_ASSERT_EQUAL(1u, committedChunkIds.size());

  MidiEventVec flat;
  LoopEventStore::appendChunkRefEvents(committedChunkIds, flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
  LoopEventStore::releaseChunkRefs(committedChunkIds);
}

void test_detach_chunks_to_committed_seals_and_transfers() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(5, 1, 60, 100)));

  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(committedChunkIds));
  TEST_ASSERT_TRUE(store.empty());
  TEST_ASSERT_EQUAL(1u, committedChunkIds.size());
  TEST_ASSERT_EQUAL(ChunkLifecycleState::Sealed,
                    LoopEventStore::chunkLifecycleState(committedChunkIds[0]));
  LoopEventStore::releaseChunkRefs(committedChunkIds);
}

void test_deep_clone_committed_chunk_ids_duplicates_pool() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(20, 1, 64, 100)));

  CommittedChunkIdList sourceIds;
  TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(sourceIds));
  TEST_ASSERT_EQUAL(1u, sourceIds.size());
  TEST_ASSERT_EQUAL(static_cast<uint16_t>(1), LoopEventStore::usedChunkCount());

  CommittedChunkIdList clonedIds;
  TEST_ASSERT_TRUE(LoopEventStore::deepCloneCommittedChunkIds(clonedIds, sourceIds));
  TEST_ASSERT_EQUAL(sourceIds.size(), clonedIds.size());
  TEST_ASSERT_NOT_EQUAL(sourceIds[0], clonedIds[0]);
  TEST_ASSERT_EQUAL(static_cast<uint16_t>(2), LoopEventStore::usedChunkCount());

  MidiEventVec sourceFlat;
  MidiEventVec clonedFlat;
  LoopEventStore::appendChunkRefEvents(sourceIds, sourceFlat);
  LoopEventStore::appendChunkRefEvents(clonedIds, clonedFlat);
  TEST_ASSERT_EQUAL(2u, sourceFlat.size());
  TEST_ASSERT_EQUAL(2u, clonedFlat.size());
  TEST_ASSERT_EQUAL(sourceFlat[0].tick, clonedFlat[0].tick);
  TEST_ASSERT_EQUAL(sourceFlat[1].tick, clonedFlat[1].tick);

  LoopEventStore::releaseChunkRefs(sourceIds);
  LoopEventStore::releaseChunkRefs(clonedIds);
}

void test_assign_missing_note_ids_in_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  MidiEvent on = MidiEvent::NoteOn(10, 1, 60, 100);
  on.noteId = kInvalidNoteId;
  TEST_ASSERT_TRUE(store.append(on));
  TEST_ASSERT_EQUAL(1u, store.chunkIds().size());

  uint32_t nextId = 100;
  store.assignMissingNoteIdsToNoteOns([&nextId]() { return nextId++; });

  MidiEventVec flat;
  store.copyEventsTo(flat);
  TEST_ASSERT_EQUAL(1u, flat.size());
  TEST_ASSERT_EQUAL(100u, flat[0].noteId);
  TEST_ASSERT_EQUAL(1u, store.chunkIds().size());
}

void test_first_index_for_bar_skips_empty_bar() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(0, 1, 60, 100)));      // bar 0
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1536, 1, 62, 100)));   // bar 2

  TEST_ASSERT_EQUAL(1u, store.firstIndexForBar(1));  // bar 1 empty -> next event is bar 2
}

void test_measure_paged_directory_successor_locality() {
  const MeasurementPagedDirectory parent =
      makeMeasurementPagedDirectory(kDirectoryMeasurementBarCount);
  const size_t dirtyBar = 513;
  const size_t dirtyPageIndex =
      dirtyBar / kDirectoryMeasurementPageCapacity;
  const auto& parentPage = parent.pages[dirtyPageIndex];

  MeasurementPagedDirectory successor = parent;
  auto copiedPage = std::make_shared<MeasurementPage>(*parentPage);
  auto copiedMembership =
      std::make_shared<MeasurementMembership>(*parentPage->memberships[
          dirtyBar % kDirectoryMeasurementPageCapacity]);
  copiedMembership->references.push_back(9001);
  copiedPage->memberships[dirtyBar % kDirectoryMeasurementPageCapacity] =
      std::move(copiedMembership);
  successor.pages[dirtyPageIndex] = std::move(copiedPage);

  size_t flatCopiedReferences = 0;
  for (const auto& page : parent.pages) {
    for (const auto& membership : page->memberships) {
      flatCopiedReferences += membership->references.size();
    }
  }

  const size_t pagedCopiedDirectoryEntries = successor.pages.size();
  const size_t pagedCopiedMembershipReferences =
      successor.pages[dirtyPageIndex]
          ->memberships[dirtyBar % kDirectoryMeasurementPageCapacity]
          ->references.size();
  std::printf("snapshot-directory-measurement bars=%zu flat_refs=%zu "
              "paged_directory_entries=%zu paged_dirty_refs=%zu\n",
              kDirectoryMeasurementBarCount, flatCopiedReferences,
              pagedCopiedDirectoryEntries, pagedCopiedMembershipReferences);

  TEST_ASSERT_EQUAL(1u, parentPage->memberships[dirtyBar % kDirectoryMeasurementPageCapacity]
                             ->references.size());
  TEST_ASSERT_EQUAL(2u, successor.pages[dirtyPageIndex]
                              ->memberships[dirtyBar % kDirectoryMeasurementPageCapacity]
                              ->references.size());
  TEST_ASSERT_TRUE(pagedCopiedDirectoryEntries < flatCopiedReferences);
  TEST_ASSERT_TRUE(pagedCopiedMembershipReferences < flatCopiedReferences);
  TEST_ASSERT_EQUAL(1u, parent.pages[dirtyPageIndex]
                             ->memberships[dirtyBar % kDirectoryMeasurementPageCapacity]
                             ->references.size());
}

bool drainSnapshotConstruction(LoopSnapshotConstruction& construction,
                               uint32_t playheadPhaseTick = 0) {
  size_t steps = 0;
  while (!construction.complete() && !construction.failed() && steps < 10000) {
    if (!construction.step(1, playheadPhaseTick)) {
      break;
    }
    ++steps;
  }
  return construction.complete() && !construction.failed();
}

std::shared_ptr<const LoopSnapshot> buildSnapshotForTests(
    const LoopSnapshotChangeSet& changes, uint32_t loopLength = 3072) {
  auto empty = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(7, loopLength, 0, 100, 100));
  LoopSnapshotConstruction construction(empty, changes);
  if (!drainSnapshotConstruction(construction)) {
    return {};
  }
  return construction.freeze();
}

SnapshotNoteChange addSnapshotNote(NoteId noteId, uint8_t pitch,
                                   uint32_t startTick, uint32_t endTick) {
  SnapshotNoteChange change{};
  change.kind = SnapshotChangeKind::Add;
  change.next.loopId = 7;
  change.next.noteId = noteId;
  change.next.pitch = pitch;
  change.next.velocity = 100;
  change.next.startTick = startTick;
  change.next.endTick = endTick;
  return change;
}

void test_snapshot_replaces_note_and_shares_unchanged_membership() {
  LoopSnapshotChangeSet initialChanges;
  initialChanges.notes.push_back(addSnapshotNote(41, 60, 100, 900));
  initialChanges.notes.push_back(addSnapshotNote(42, 62, 100, 200));
  SnapshotEventChange initialEvent{};
  initialEvent.kind = SnapshotChangeKind::Add;
  initialEvent.next.loopId = 7;
  initialEvent.next.eventId = 9;
  initialEvent.next.canonicalOrder = 1;
  initialEvent.next.value = MidiEvent::ControlChange(120, 1, 7, 64);
  initialChanges.events.push_back(initialEvent);
  const auto parent = buildSnapshotForTests(initialChanges);
  const auto oldNote = parent->noteMembership(60, 0)->references[0];
  const auto unchangedMembership = parent->noteMembership(62, 0);
  const auto unchangedEventMembership = parent->eventMembership(0);

  SnapshotNoteChange replacement{};
  replacement.kind = SnapshotChangeKind::Replace;
  replacement.previous = oldNote;
  replacement.next = *oldNote;
  replacement.next.startTick = 900;
  replacement.next.endTick = 1700;
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(replacement);
  LoopSnapshotConstruction construction(parent, changes);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(construction));
  const auto successor = construction.freeze();

  TEST_ASSERT_NOT_NULL(successor.get());
  TEST_ASSERT_EQUAL(100u, oldNote->startTick);
  TEST_ASSERT_EQUAL(0u, snapshotMembershipCount(successor->noteMembership(60, 0).get()));
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(successor->noteMembership(60, 1).get()));
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(successor->noteMembership(60, 2).get()));
  const auto newNote = successor->noteMembership(60, 1)->references[0];
  TEST_ASSERT_NOT_EQUAL(oldNote.get(), newNote.get());
  TEST_ASSERT_EQUAL(oldNote->noteId, newNote->noteId);
  TEST_ASSERT_EQUAL_PTR(unchangedMembership.get(),
                        successor->noteMembership(62, 0).get());
  TEST_ASSERT_EQUAL_PTR(unchangedEventMembership.get(),
                        successor->eventMembership(0).get());
}

void test_snapshot_wrap_membership_uses_one_note_reference() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(55, 64, 2300, 200));
  const auto snapshot = buildSnapshotForTests(changes);
  const auto tail = snapshot->noteMembership(64, 2);
  const auto head = snapshot->noteMembership(64, 0);
  TEST_ASSERT_NOT_NULL(tail.get());
  TEST_ASSERT_NOT_NULL(head.get());
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(tail.get()));
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(head.get()));
  TEST_ASSERT_EQUAL_PTR(tail->references[0].get(), head->references[0].get());

  CanonicalNoteSpanVec spans;
  snapshot->appendCanonicalSpans(tail->references[0], spans);
  TEST_ASSERT_TRUE(tail->references[0]->isWrap(snapshot->loopLengthTicks()));
  TEST_ASSERT_FALSE(tail->references[0]->isWrap(0));
  TEST_ASSERT_EQUAL(2u, spans.size());
  TEST_ASSERT_EQUAL(2300, spans[0].interval.start);
  TEST_ASSERT_EQUAL(3072, spans[0].interval.end);
  TEST_ASSERT_EQUAL(0, spans[1].interval.start);
  TEST_ASSERT_EQUAL(200, spans[1].interval.end);
}

void test_snapshot_wrap_spans_follow_root_length() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(55, 64, 1400, 100));
  const auto snapshot = buildSnapshotForTests(changes, 1536);
  TEST_ASSERT_NOT_NULL(snapshot.get());
  TEST_ASSERT_EQUAL(1536u, snapshot->loopLengthTicks());
  TEST_ASSERT_TRUE(snapshot->findNote(55, 64)->isWrap(snapshot->loopLengthTicks()));

  CanonicalNoteSpanVec spans;
  snapshot->appendCanonicalSpans(snapshot->findNote(55, 64), spans);
  TEST_ASSERT_EQUAL(2u, spans.size());
  TEST_ASSERT_EQUAL(1400, spans[0].interval.start);
  TEST_ASSERT_EQUAL(1536, spans[0].interval.end);
  TEST_ASSERT_EQUAL(0, spans[1].interval.start);
  TEST_ASSERT_EQUAL(100, spans[1].interval.end);

  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(snapshot->noteMembership(64, 1).get()));
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(snapshot->noteMembership(64, 0).get()));
  TEST_ASSERT_NULL(snapshot->noteMembership(64, 2).get());

  LoopSnapshotChangeSet linearChanges;
  linearChanges.notes.push_back(addSnapshotNote(56, 60, 100, 200));
  const auto linear = buildSnapshotForTests(linearChanges, 1536);
  TEST_ASSERT_FALSE(linear->findNote(56, 60)->isWrap(linear->loopLengthTicks()));
  CanonicalNoteSpanVec linearSpans;
  linear->appendCanonicalSpans(linear->findNote(56, 60), linearSpans);
  TEST_ASSERT_EQUAL(1u, linearSpans.size());
  TEST_ASSERT_EQUAL(100, linearSpans[0].interval.start);
  TEST_ASSERT_EQUAL(200, linearSpans[0].interval.end);
}

void test_snapshot_with_loop_length_shares_membership() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(56, 60, 100, 200));
  const auto parent = buildSnapshotForTests(changes, 21504);
  TEST_ASSERT_NOT_NULL(parent.get());
  const auto membership = parent->noteMembership(60, 0);
  const auto note = parent->findNote(56, 60);
  TEST_ASSERT_NOT_NULL(membership.get());
  TEST_ASSERT_NOT_NULL(note.get());

  const auto handedOff = parent->withLoopLengthTicks(9216);
  TEST_ASSERT_NOT_NULL(handedOff.get());
  TEST_ASSERT_EQUAL(21504u, parent->loopLengthTicks());
  TEST_ASSERT_EQUAL(9216u, handedOff->loopLengthTicks());
  TEST_ASSERT_EQUAL_PTR(membership.get(), handedOff->noteMembership(60, 0).get());
  TEST_ASSERT_EQUAL_PTR(note.get(), handedOff->findNote(56, 60).get());
}

void test_snapshot_event_index_preserves_canonical_order() {
  LoopSnapshotChangeSet changes;
  SnapshotEventChange later{};
  later.kind = SnapshotChangeKind::Add;
  later.next.loopId = 7;
  later.next.eventId = 2;
  later.next.canonicalOrder = 20;
  later.next.value = MidiEvent::ControlChange(120, 1, 7, 20);
  changes.events.push_back(later);
  SnapshotEventChange earlier = later;
  earlier.next.eventId = 1;
  earlier.next.canonicalOrder = 10;
  earlier.next.value = MidiEvent::ControlChange(120, 1, 7, 10);
  changes.events.push_back(earlier);
  const auto snapshot = buildSnapshotForTests(changes);
  const auto events = snapshot->eventMembership(0);
  TEST_ASSERT_EQUAL(2u, snapshotMembershipCount(events.get()));
  TEST_ASSERT_EQUAL(1u, events->references[0]->eventId);
  TEST_ASSERT_EQUAL(2u, events->references[1]->eventId);
}

void test_snapshot_candidate_progress_and_alias_safety() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(70, 60, 100, 200));
  auto parent = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(7, 3072, 0, 100, 100));
  LoopSnapshotConstruction construction(parent, changes);
  changes.notes[0].next.startTick = 999;

  TEST_ASSERT_FALSE(construction.complete());
  TEST_ASSERT_FALSE(construction.step(0));
  TEST_ASSERT_TRUE(construction.step(1));
  const auto candidate = construction.freeze();
  TEST_ASSERT_NOT_NULL(candidate.get());
  TEST_ASSERT_EQUAL(100u, candidate->noteMembership(60, 0)->references[0]->startTick);
  TEST_ASSERT_EQUAL(0u, parent->noteMembership(60, 0).get() == nullptr ? 0 : 1);
}

void test_snapshot_replaces_pitch_and_clears_old_membership() {
  LoopSnapshotChangeSet initialChanges;
  initialChanges.notes.push_back(addSnapshotNote(41, 60, 100, 200));
  const auto parent = buildSnapshotForTests(initialChanges);
  SnapshotNoteChange replacement{};
  replacement.kind = SnapshotChangeKind::Replace;
  replacement.previous = parent->findNote(41, 60);
  replacement.next = *replacement.previous;
  replacement.next.pitch = 67;
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(replacement);
  LoopSnapshotConstruction construction(parent, changes);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(construction));
  const auto successor = construction.freeze();
  const auto oldPitch = successor->noteMembership(60, 0);
  const size_t oldCount = snapshotMembershipCount(oldPitch.get());
  TEST_ASSERT_EQUAL(0u, oldCount);
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(successor->noteMembership(67, 0).get()));
  TEST_ASSERT_EQUAL(41u, successor->noteMembership(67, 0)->references[0]->noteId);
}

void test_snapshot_find_note_uses_pitch_hint_then_scan() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(90, 60, 100, 200));
  const auto snapshot = buildSnapshotForTests(changes);
  TEST_ASSERT_NOT_NULL(snapshot->findNote(90, 60).get());
  TEST_ASSERT_EQUAL(90u, snapshot->findNote(90, 64)->noteId);
  TEST_ASSERT_NULL(snapshot->findNote(91, 60).get());
}

void test_snapshot_find_note_at_pitch_stays_on_event_pitch() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(90, 60, 100, 200));
  const auto snapshot = buildSnapshotForTests(changes);
  TEST_ASSERT_NOT_NULL(snapshot->findNoteAtPitch(90, 60).get());
  TEST_ASSERT_EQUAL_PTR(snapshot->findNote(90, 60).get(),
                        snapshot->findNoteAtPitch(90, 60).get());
  TEST_ASSERT_NULL(snapshot->findNoteAtPitch(90, 64).get());
  TEST_ASSERT_EQUAL(90u, snapshot->findNote(90, 64)->noteId);
  TEST_ASSERT_NULL(snapshot->findNoteAtPitch(91, 60).get());
}

void test_snapshot_applies_one_bar_in_playhead_arrival_order() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(70, 60, 0, 3072));
  auto parent = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(7, 3072, 0, 100, 100));
  LoopSnapshotConstruction construction(parent, changes);

  auto noteCount = [&construction](uint32_t barIndex) -> size_t {
    const auto membership = construction.candidate().noteMembership(60, barIndex);
    return snapshotMembershipCount(membership.get());
  };

  TEST_ASSERT_TRUE(construction.step(1));
  TEST_ASSERT_FALSE(construction.complete());
  TEST_ASSERT_EQUAL(1u, noteCount(0));
  TEST_ASSERT_EQUAL(0u, noteCount(1));
  TEST_ASSERT_EQUAL(0u, noteCount(2));
  TEST_ASSERT_EQUAL(0u, noteCount(3));

  TEST_ASSERT_TRUE(construction.step(1));
  TEST_ASSERT_EQUAL(1u, noteCount(0));
  TEST_ASSERT_EQUAL(1u, noteCount(1));
  TEST_ASSERT_EQUAL(0u, noteCount(2));
  TEST_ASSERT_EQUAL(0u, noteCount(3));

  TEST_ASSERT_TRUE(construction.step(1));
  TEST_ASSERT_EQUAL(1u, noteCount(0));
  TEST_ASSERT_EQUAL(1u, noteCount(1));
  TEST_ASSERT_EQUAL(1u, noteCount(2));
  TEST_ASSERT_EQUAL(0u, noteCount(3));

  TEST_ASSERT_TRUE(construction.step(1));
  TEST_ASSERT_TRUE(construction.complete());
  TEST_ASSERT_EQUAL(1u, noteCount(0));
  TEST_ASSERT_EQUAL(1u, noteCount(1));
  TEST_ASSERT_EQUAL(1u, noteCount(2));
  TEST_ASSERT_EQUAL(1u, noteCount(3));
  TEST_ASSERT_NOT_NULL(construction.freeze().get());
}

void test_snapshot_hide_removes_same_note_id() {
  LoopSnapshotChangeSet initialChanges;
  initialChanges.notes.push_back(addSnapshotNote(44, 60, 100, 400));
  const auto parent = buildSnapshotForTests(initialChanges);
  SnapshotNoteChange hide{};
  hide.kind = SnapshotChangeKind::Remove;
  hide.previous = parent->findNote(44, 60);
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(hide);
  LoopSnapshotConstruction construction(parent, changes);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(construction));
  const auto successor = construction.freeze();
  TEST_ASSERT_NOT_NULL(successor.get());
  TEST_ASSERT_NULL(successor->findNote(44, 60).get());
  TEST_ASSERT_EQUAL(0u, snapshotMembershipCount(successor->noteMembership(60, 0).get()));
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(parent->noteMembership(60, 0).get()));
}

void test_snapshot_one_bar_node_count_and_unchanged_membership_share() {
  LoopSnapshotChangeSet firstChanges;
  for (uint8_t index = 0; index < 16; ++index) {
    const uint32_t startTick = static_cast<uint32_t>(10 + index * 20);
    firstChanges.notes.push_back(
        addSnapshotNote(100 + index, static_cast<uint8_t>(40 + index), startTick,
                        startTick + 10));
  }
  for (uint8_t index = 0; index < 32; ++index) {
    SnapshotEventChange event{};
    event.kind = SnapshotChangeKind::Add;
    event.next.loopId = 7;
    event.next.eventId = static_cast<EventId>(1 + index);
    event.next.canonicalOrder = static_cast<uint32_t>(1 + index);
    event.next.value = MidiEvent::ControlChange(20 + index * 10, 1, 7, index);
    firstChanges.events.push_back(event);
  }

  auto empty = std::make_shared<const LoopSnapshot>(LoopSnapshot(7, 3072, 0, 100, 100));
  resetSnapshotAllocCounters();
  LoopSnapshotConstruction firstConstruction(empty, firstChanges);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(firstConstruction));
  const auto parent = firstConstruction.freeze();
  TEST_ASSERT_NOT_NULL(parent.get());
  const uint32_t firstNodes = snapshotAllocNodeCount();
  const uint32_t firstSlabs = snapshotAllocSlabCount();
  std::printf("snapshot-alloc first-bar nodes=%u bytes=%u slabs=%u\n",
              firstNodes, snapshotAllocByteCount(), firstSlabs);
  TEST_ASSERT_TRUE(firstNodes >= 51);
  TEST_ASSERT_TRUE(firstNodes <= 200);
  TEST_ASSERT_TRUE(firstSlabs >= 1);
  TEST_ASSERT_TRUE(firstSlabs <= 3);
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(parent->noteMembership(40, 0).get()));
  TEST_ASSERT_EQUAL(32u, snapshotMembershipCount(parent->eventMembership(0).get()));

  const auto unchangedMembership = parent->noteMembership(41, 0);
  TEST_ASSERT_NOT_NULL(unchangedMembership.get());
  SnapshotNoteChange replacement{};
  replacement.kind = SnapshotChangeKind::Replace;
  replacement.previous = parent->findNote(100, 40);
  replacement.next = *replacement.previous;
  replacement.next.endTick = 40;
  LoopSnapshotChangeSet successorChanges;
  successorChanges.notes.push_back(replacement);
  resetSnapshotAllocCounters();
  LoopSnapshotConstruction successorConstruction(parent, successorChanges);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(successorConstruction));
  const auto successor = successorConstruction.freeze();
  TEST_ASSERT_NOT_NULL(successor.get());
  TEST_ASSERT_EQUAL_PTR(unchangedMembership.get(), successor->noteMembership(41, 0).get());
  TEST_ASSERT_TRUE(snapshotAllocNodeCount() >= 4);
  TEST_ASSERT_TRUE(snapshotAllocNodeCount() < firstNodes);
  TEST_ASSERT_EQUAL(40u, successor->findNote(100, 40)->endTick);
}

void test_snapshot_failed_candidate_preserves_parent() {
  LoopSnapshotChangeSet initialChanges;
  initialChanges.notes.push_back(addSnapshotNote(80, 60, 100, 200));
  const auto parent = buildSnapshotForTests(initialChanges);
  SnapshotNoteChange invalid = addSnapshotNote(81, 60, 300, 400);
  invalid.next.loopId = 8;
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(invalid);
  LoopSnapshotConstruction construction(parent, changes);
  TEST_ASSERT_FALSE(construction.step(1));
  TEST_ASSERT_TRUE(construction.failed());
  TEST_ASSERT_NULL(construction.freeze().get());
  TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(parent->noteMembership(60, 0).get()));
  TEST_ASSERT_NULL(parent->noteMembership(60, 1).get());
}

void test_pool_backs_storage_only_as_content_reaches_it() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  // RC-MEM-5: boot reserves nothing. The old initPool allocated all 512 chunks (2,566 KB) in
  // one block, and the worst occupancy ever observed in a captured run was 167 chunks.
  TEST_ASSERT_EQUAL_UINT16(0, LoopEventStore::poolBlockCount());
  TEST_ASSERT_EQUAL_UINT64(0, static_cast<uint64_t>(LoopEventStore::poolBackedBytes()));
  TEST_ASSERT_EQUAL_UINT16(LoopEventStoreConfig::POOL_CHUNK_COUNT,
                           LoopEventStore::freeChunkCount());

  LoopEventStore store;
  // One event opens exactly one block, i.e. POOL_CHUNKS_PER_BLOCK chunks, not the whole array.
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(0, 1, 60, 100)));
  TEST_ASSERT_EQUAL_UINT16(1, LoopEventStore::poolBlockCount());
  TEST_ASSERT_EQUAL_UINT64(
      static_cast<uint64_t>(LoopEventStoreConfig::POOL_CHUNKS_PER_BLOCK) * sizeof(EventChunk),
      static_cast<uint64_t>(LoopEventStore::poolBackedBytes()));

  // Crossing a block boundary opens the next block and keeps the flat id space correct, which
  // is the contract the committed chunk-id lists, PersistenceQueue::onChunkFreed and the
  // storage I/O format all rely on.
  const size_t eventsPerBlock =
      static_cast<size_t>(LoopEventStoreConfig::POOL_CHUNKS_PER_BLOCK) *
      LoopEventStoreConfig::CHUNK_CAPACITY;
  for (size_t i = 1; i <= eventsPerBlock + 7; ++i) {
    TEST_ASSERT_TRUE(store.append(
        MidiEvent::NoteOn(static_cast<uint32_t>(i), 1, 62, 100)));
  }
  TEST_ASSERT_EQUAL_UINT16(2, LoopEventStore::poolBlockCount());
  TEST_ASSERT_EQUAL_UINT32(eventsPerBlock + 8, store.size());
  TEST_ASSERT_EQUAL_UINT32(0, store.at(0).tick);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(eventsPerBlock + 7), store.at(store.size() - 1).tick);

  // The win: backed bytes stay far below the full array, and the ceiling is untouched.
  TEST_ASSERT_TRUE(static_cast<uint64_t>(LoopEventStore::poolBackedBytes()) <
                   static_cast<uint64_t>(LoopEventStoreConfig::POOL_CHUNK_COUNT) *
                       sizeof(EventChunk));
  TEST_ASSERT_EQUAL_UINT16(LoopEventStoreConfig::POOL_CHUNK_COUNT,
                           LoopEventStore::freeChunkCount() + LoopEventStore::usedChunkCount());
  LoopEventStore::resetPoolForTests();
}

void test_sealed_pass_read_preserves_order_and_quantum() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  MidiEventVec expected;
  for (size_t i = 0; i < 2048; ++i) {
    auto event = i % 2 ? MidiEvent::NoteOff(i % 97, 1, 60, 0)
                       : MidiEvent::NoteOn(i % 97, 1, 60, 100);
    expected.push_back(event);
    TEST_ASSERT_TRUE(store.append(event));
  }
  CaptureChunkIdList refs;
  store.detachChunksTo(refs);
  const auto references = LoopEventStore::chunkReferenceCount(refs.front());
  std::vector<MidiEvent> output(expected.size());
  uint64_t generation = 7;
  for (size_t quantum : {size_t(1), size_t(7), size_t(32), size_t(4096)}) {
    LoopEventStoreReadCursor cursor;
    TEST_ASSERT_TRUE(LoopEventStore::beginReadSealedChunks(
        cursor, refs.data(), refs.size(), output.data(), output.size(), generation));
    TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Pending,
                     LoopEventStore::stepReadSealedChunks(cursor, 0));
    size_t calls = 0;
    while (!cursor.ready()) {
      TEST_ASSERT_NULL(cursor.data());
      TEST_ASSERT_EQUAL(0u, cursor.size());
      const auto result = LoopEventStore::stepReadSealedChunks(cursor, quantum);
      TEST_ASSERT_NOT_EQUAL(LoopEventStoreReadResult::Denied, result);
      TEST_ASSERT_TRUE(++calls <= (expected.size() + refs.size() + quantum - 1) / quantum);
    }
    TEST_ASSERT_EQUAL((expected.size() + refs.size() + quantum - 1) / quantum, calls);
    TEST_ASSERT_EQUAL(expected.size(), cursor.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      TEST_ASSERT_EQUAL(expected[i].tick, cursor.data()[i].tick);
      TEST_ASSERT_EQUAL(expected[i].type, cursor.data()[i].type);
    }
    TEST_ASSERT_EQUAL(references, LoopEventStore::chunkReferenceCount(refs.front()));
    ++generation;
    TEST_ASSERT_FALSE(cursor.ready());
    TEST_ASSERT_NULL(cursor.data());
    TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Denied,
                     LoopEventStore::stepReadSealedChunks(cursor, quantum));
  }
  LoopEventStore::releaseChunkRefs(refs);
}

void test_sealed_pass_read_denies_invalid_capacity_and_stale_inputs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(96, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(0, 1, 60, 0)));
  MidiEvent output[2];
  uint64_t generation = 1;
  LoopEventStoreReadCursor cursor;
  // The live tail is not an immutable input.
  TEST_ASSERT_TRUE(LoopEventStore::beginReadSealedChunks(cursor,
      store.chunkIds().data(), store.chunkIds().size(), output, 2, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Denied,
                   LoopEventStore::stepReadSealedChunks(cursor, 1));
  CaptureChunkIdList refs;
  store.detachChunksTo(refs);
  TEST_ASSERT_TRUE(LoopEventStore::beginReadSealedChunks(
      cursor, refs.data(), refs.size(), output, 1, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Pending,
                   LoopEventStore::stepReadSealedChunks(cursor, 1));
  TEST_ASSERT_FALSE(LoopEventStore::beginReadSealedChunks(
      cursor, refs.data(), refs.size(), output, 2, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Denied,
                   LoopEventStore::stepReadSealedChunks(cursor, 1));
  TEST_ASSERT_NULL(cursor.data());
  TEST_ASSERT_EQUAL(0u, cursor.size());
  TEST_ASSERT_TRUE(LoopEventStore::beginReadSealedChunks(
      cursor, refs.data(), refs.size(), output, 2, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Pending,
                   LoopEventStore::stepReadSealedChunks(cursor, 1));
  ++generation;
  TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Denied,
                   LoopEventStore::stepReadSealedChunks(cursor, 2));
  TEST_ASSERT_NULL(cursor.data());
  const uint16_t invalid[] = {refs.front(), UINT16_MAX};
  TEST_ASSERT_TRUE(LoopEventStore::beginReadSealedChunks(
      cursor, invalid, 2, output, 2, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Denied,
                   LoopEventStore::stepReadSealedChunks(cursor, 8));
  TEST_ASSERT_NULL(cursor.data());
  LoopEventStore::releaseChunkRefs(refs);
  TEST_ASSERT_TRUE(LoopEventStore::beginReadSealedChunks(
      cursor, refs.data(), refs.size(), output, 2, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Denied,
                   LoopEventStore::stepReadSealedChunks(cursor, 1));
}

void test_sealed_pass_read_empty_and_invalid_bindings() {
  uint64_t generation = 1;
  LoopEventStoreReadCursor cursor;
  TEST_ASSERT_FALSE(LoopEventStore::beginReadSealedChunks(cursor, nullptr, 1, nullptr, 0, generation));
  TEST_ASSERT_FALSE(LoopEventStore::beginReadSealedChunks(cursor, nullptr, 0, nullptr, 1, generation));
  TEST_ASSERT_TRUE(LoopEventStore::beginReadSealedChunks(cursor, nullptr, 0, nullptr, 0, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreReadResult::Complete,
                   LoopEventStore::stepReadSealedChunks(cursor, 1));
  TEST_ASSERT_TRUE(cursor.ready());
  TEST_ASSERT_EQUAL(0u, cursor.size());
  cursor.discard();
  TEST_ASSERT_FALSE(cursor.ready());
}

void test_snapshot_release_shared_root_is_one_unit_and_readable() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(41, 60, 10, 100));
  auto root = buildSnapshotForTests(changes);
  auto reader = root;
  LoopSnapshot::ReleaseCursor release;
  TEST_ASSERT_TRUE(LoopSnapshot::beginRelease(release, root));
  TEST_ASSERT_NULL(root.get());
  TEST_ASSERT_FALSE(LoopSnapshot::stepRelease(release, 0));
  TEST_ASSERT_EQUAL(0u, release.completedUnits());
  TEST_ASSERT_TRUE(LoopSnapshot::stepRelease(release, 1));
  TEST_ASSERT_EQUAL(1u, release.completedUnits());
  TEST_ASSERT_EQUAL(41u, reader->findNoteAtPitch(41, 60)->noteId);
  TEST_ASSERT_TRUE(LoopSnapshot::stepRelease(release, 32));
  TEST_ASSERT_EQUAL(1u, release.completedUnits());
}

void test_snapshot_release_unique_graph_is_quantized_and_releases_all_leaves() {
  for (const size_t quantum : {size_t(1), size_t(7), size_t(32)}) {
    LoopSnapshotChangeSet changes;
    // Three linked membership pages, multiple pitches and directory pages.
    for (NoteId id = 1; id <= 40; ++id) {
      changes.notes.push_back(addSnapshotNote(id, 60, 10, 100));
    }
    changes.notes.push_back(addSnapshotNote(41, 62, 33 * 768, 33 * 768 + 100));
    SnapshotEventChange event{};
    event.kind = SnapshotChangeKind::Add;
    event.next.loopId = 7;
    event.next.eventId = 9;
    event.next.value = MidiEvent::ControlChange(33 * 768, 1, 7, 64);
    changes.events.push_back(event);
    auto root = buildSnapshotForTests(changes, 64 * 768);
    TEST_ASSERT_NOT_NULL(root.get());
    std::weak_ptr<const LoopSnapshot> rootLifetime = root;
    std::weak_ptr<const LoopSnapshotNote> first = root->findNoteAtPitch(1, 60);
    std::weak_ptr<const LoopSnapshotNote> last = root->findNoteAtPitch(41, 62);
    std::weak_ptr<const LoopSnapshotEvent> eventLifetime =
        root->eventMembership(33)->references[0];
    LoopSnapshot::ReleaseCursor release;
    TEST_ASSERT_TRUE(LoopSnapshot::beginRelease(release, root));
    auto other = LoopSnapshot::createEmpty(9, 768, 0, 1, 1);
    const auto* otherIdentity = other.get();
    TEST_ASSERT_FALSE(LoopSnapshot::beginRelease(release, other));
    TEST_ASSERT_EQUAL_PTR(otherIdentity, other.get());
    const auto allocations = snapshotAllocNodeCount();
    size_t turns = 0;
    while (!release.complete() && turns++ < 10000) {
      const size_t before = release.completedUnits();
      LoopSnapshot::stepRelease(release, quantum);
      TEST_ASSERT_TRUE(release.completedUnits() - before <= quantum);
      TEST_ASSERT_TRUE(release.completedUnits() > before);
    }
    TEST_ASSERT_TRUE(release.complete());
    TEST_ASSERT_TRUE(rootLifetime.expired());
    TEST_ASSERT_TRUE(first.expired());
    TEST_ASSERT_TRUE(last.expired());
    TEST_ASSERT_TRUE(eventLifetime.expired());
    TEST_ASSERT_EQUAL(allocations, snapshotAllocNodeCount());
    TEST_ASSERT_TRUE(LoopSnapshot::beginRelease(release, other));
    while (!LoopSnapshot::stepRelease(release, quantum)) {}
  }
}

void test_snapshot_release_preserves_shared_descendants_during_every_grain() {
  LoopSnapshotChangeSet changes;
  changes.notes.push_back(addSnapshotNote(41, 60, 10, 100));
  changes.notes.push_back(addSnapshotNote(42, 62, 20, 120));
  auto parent = buildSnapshotForTests(changes);
  auto reader = parent->withLoopLengthTicks(6144);
  // Parent and reader are distinct roots sharing the entire indexed graph.
  TEST_ASSERT_NOT_EQUAL(parent.get(), reader.get());
  auto membership = parent->noteMembership(60, 0);
  LoopSnapshot::ReleaseCursor release;
  TEST_ASSERT_TRUE(LoopSnapshot::beginRelease(release, parent));
  do {
    LoopSnapshot::stepRelease(release, 1);
    TEST_ASSERT_EQUAL_PTR(membership.get(), reader->noteMembership(60, 0).get());
    TEST_ASSERT_EQUAL(10u, reader->findNoteAtPitch(41, 60)->startTick);
    TEST_ASSERT_EQUAL(20u, reader->findNoteAtPitch(42, 62)->startTick);
  } while (!release.complete());
  // Now retire the last root, retaining just one membership as an external reader.
  TEST_ASSERT_TRUE(LoopSnapshot::beginRelease(release, reader));
  do {
    LoopSnapshot::stepRelease(release, 1);
    TEST_ASSERT_EQUAL(1u, snapshotMembershipCount(membership.get()));
    TEST_ASSERT_EQUAL(41u, membership->references[0]->noteId);
  } while (!release.complete());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_snapshot_release_shared_root_is_one_unit_and_readable);
  RUN_TEST(test_snapshot_release_unique_graph_is_quantized_and_releases_all_leaves);
  RUN_TEST(test_snapshot_release_preserves_shared_descendants_during_every_grain);
  RUN_TEST(test_pool_backs_storage_only_as_content_reaches_it);
  RUN_TEST(test_sealed_pass_read_preserves_order_and_quantum);
  RUN_TEST(test_sealed_pass_read_denies_invalid_capacity_and_stale_inputs);
  RUN_TEST(test_sealed_pass_read_empty_and_invalid_bindings);
  RUN_TEST(test_append_fills_chunks_without_realloc_pattern);
  RUN_TEST(test_adopt_all_moves_chunks);
  RUN_TEST(test_merge_from_combines_sorted_events);
  RUN_TEST(test_merge_from_interleaves_across_chunks);
  RUN_TEST(test_merge_from_stable_on_equal_tick);
  RUN_TEST(test_restore_snapshot_deep_copies);
  RUN_TEST(test_shift_all_ticks_bumps_negative);
  RUN_TEST(test_lower_bound_index_skips_prefix);
  RUN_TEST(test_undo_snapshot_ref_isolated_by_cow);
  RUN_TEST(test_drop_events_at_or_beyond_tick_removes_overflow);
  RUN_TEST(test_map_overflow_note_on_keeps_in_loop_stop_off);
  RUN_TEST(test_first_index_for_bar_uses_bar_index);
  RUN_TEST(test_first_index_for_bar_skips_empty_bar);
  RUN_TEST(test_transfer_capture_chunk_ids_to_published);
  RUN_TEST(test_detach_chunks_to_committed_seals_and_transfers);
  RUN_TEST(test_deep_clone_committed_chunk_ids_duplicates_pool);
  RUN_TEST(test_assign_missing_note_ids_in_chunks);
  RUN_TEST(test_measure_paged_directory_successor_locality);
  RUN_TEST(test_snapshot_replaces_note_and_shares_unchanged_membership);
  RUN_TEST(test_snapshot_wrap_membership_uses_one_note_reference);
  RUN_TEST(test_snapshot_wrap_spans_follow_root_length);
  RUN_TEST(test_snapshot_with_loop_length_shares_membership);
  RUN_TEST(test_snapshot_event_index_preserves_canonical_order);
  RUN_TEST(test_snapshot_candidate_progress_and_alias_safety);
  RUN_TEST(test_snapshot_applies_one_bar_in_playhead_arrival_order);
  RUN_TEST(test_snapshot_hide_removes_same_note_id);
  RUN_TEST(test_snapshot_one_bar_node_count_and_unchanged_membership_share);
  RUN_TEST(test_snapshot_failed_candidate_preserves_parent);
  RUN_TEST(test_snapshot_replaces_pitch_and_clears_old_membership);
  RUN_TEST(test_snapshot_find_note_uses_pitch_hint_then_scan);
  RUN_TEST(test_snapshot_find_note_at_pitch_stays_on_event_pitch);
  return UNITY_END();
}
