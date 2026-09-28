// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include "../../src/LoopEventStore.cpp"

void test_partial_and_full_boundaries_retain_exclusive_ranges() {
  for (size_t count : {0u, 1u, 255u, 256u, 257u, 769u}) {
    LoopEventStore::resetPoolForTests();
    LoopEventStore producer;
    LoopEventStore::CaptureRange a, b;
    TEST_ASSERT_TRUE(producer.reserveCaptureTail());
    TEST_ASSERT_TRUE(a.reserveTail());
    TEST_ASSERT_TRUE(b.reserveTail());
    for (size_t i = 0; i < count; ++i)
      TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(i, 1, 60, 100)));
    const auto* ids = producer.chunkIds().data();
    const uint16_t aTail = producer.chunkIds().back();
    const uint16_t chunksBefore = LoopEventStore::usedChunkCount();
    TEST_ASSERT_TRUE(producer.sealCaptureRange(a));
    TEST_ASSERT_EQUAL(chunksBefore, LoopEventStore::usedChunkCount());
    TEST_ASSERT_TRUE(ids != producer.chunkIds().data());
    TEST_ASSERT_EQUAL(0, producer.size());
    TEST_ASSERT_EQUAL(count, a.size());
    const uint64_t receipt = a.generation();
    TEST_ASSERT_FALSE(a.releaseGrain(0));
    TEST_ASSERT_TRUE(a.ready());
    TEST_ASSERT_EQUAL_UINT64(receipt, a.generation());
    TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(900, 1, 62, 100)));
    TEST_ASSERT_TRUE(producer.sealCaptureRange(b));
    TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(901, 1, 64, 100)));
    TEST_ASSERT_EQUAL(count, a.size());
    TEST_ASSERT_EQUAL(1, b.size());
    MidiEvent event; bool end = false;
    for (size_t i = 0; i < count; ++i) {
      TEST_ASSERT_TRUE(a.read(receipt, i / 256, i % 256, event, end));
      TEST_ASSERT_FALSE(end);
      TEST_ASSERT_EQUAL(i, event.tick);
      TEST_ASSERT_EQUAL(60, event.data.noteData.note);
    }
    TEST_ASSERT_TRUE(a.read(receipt, a.chunkCount() - 1, count ? ((count - 1) % 256) + 1 : 0, event, end));
    TEST_ASSERT_TRUE(end);
    TEST_ASSERT_FALSE(producer.sealCaptureRange(a));
    TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(aTail));
    size_t remaining = a.chunkCount();
    while (remaining) {
      const uint16_t used = LoopEventStore::usedChunkCount();
      TEST_ASSERT_EQUAL(remaining == 1, a.releaseGrain(1));
      TEST_ASSERT_EQUAL(used - 1, LoopEventStore::usedChunkCount());
      TEST_ASSERT_FALSE(a.read(receipt, 0, 0, event, end));
      --remaining;
    }
    TEST_ASSERT_TRUE(b.read(b.generation(), 0, 0, event, end));
    TEST_ASSERT_EQUAL(62, event.data.noteData.note);
    TEST_ASSERT_EQUAL(64, producer.at(0).data.noteData.note);
    TEST_ASSERT_TRUE(a.reserveTail());
    TEST_ASSERT_TRUE(producer.sealCaptureRange(a));
    TEST_ASSERT_FALSE(a.read(receipt, 0, 0, event, end));
    TEST_ASSERT_TRUE(a.read(a.generation(), 0, 0, event, end));
    TEST_ASSERT_EQUAL(64, event.data.noteData.note);
    producer.clear();
  }
}

void test_reserved_cut_and_first_append_survive_pool_exhaustion() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore producer, pressure;
  LoopEventStore::CaptureRange a, unavailable;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(a.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(12, 1, 60, 100)));
  while (LoopEventStore::freeChunkCount()) {
    for (unsigned i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i)
      TEST_ASSERT_TRUE(pressure.append(MidiEvent::ControlChange(i, 1, 7, 80)));
  }
  TEST_ASSERT_FALSE(unavailable.reserveTail());
  TEST_ASSERT_FALSE(producer.sealCaptureRange(unavailable));
  TEST_ASSERT_EQUAL(1, producer.size());
  TEST_ASSERT_TRUE(producer.sealCaptureRange(a));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(24, 1, 62, 100)));
  MidiEvent event; bool end;
  TEST_ASSERT_TRUE(a.read(a.generation(), 0, 0, event, end));
  TEST_ASSERT_EQUAL(60, event.data.noteData.note);
  pressure.clear();
  producer.clear();
}

void test_pool_reset_invalidates_range_before_reusing_chunk_ids() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore producer;
  LoopEventStore::CaptureRange a;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(a.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(12, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(a));
  const auto generation = a.generation();
  LoopEventStore::resetPoolForTests();
  LoopEventStore replacement;
  TEST_ASSERT_TRUE(replacement.reserveCaptureTail());
  TEST_ASSERT_TRUE(replacement.append(MidiEvent::NoteOn(24, 1, 62, 100)));
  MidiEvent event; bool end;
  TEST_ASSERT_FALSE(a.ready());
  TEST_ASSERT_FALSE(a.read(generation, 0, 0, event, end));
  TEST_ASSERT_FALSE(producer.append(MidiEvent::NoteOn(30, 1, 64, 100)));
  TEST_ASSERT_TRUE(a.releaseGrain(1));
  producer.clear();
  TEST_ASSERT_EQUAL(1, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(replacement.chunkIds()[0]));
  TEST_ASSERT_EQUAL(62, replacement.at(0).data.noteData.note);
  replacement.clear();
}

void test_rotation_invalidates_old_rewrite_without_mutating_either_interval() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore producer;
  LoopEventStore::CaptureRange a;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(a.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(12, 1, 60, 100)));
  LoopEventStoreRewriteCursor stale;
  TEST_ASSERT_TRUE(producer.beginRewriteFromOrderedSource(stale, 1));
  const MidiEvent replacement = MidiEvent::NoteOn(50, 1, 70, 100);
  const uint16_t order = 0;
  TEST_ASSERT_TRUE(producer.sealCaptureRange(a));
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(24, 1, 62, 100)));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
      producer.stepRewriteFromOrderedSource(stale, &replacement, &order, 1, 1, 1));
  TEST_ASSERT_EQUAL(62, producer.at(0).data.noteData.note);
  MidiEvent event; bool end;
  TEST_ASSERT_TRUE(a.read(a.generation(), 0, 0, event, end));
  TEST_ASSERT_EQUAL(60, event.data.noteData.note);
  producer.clear();
}

void test_committed_transfer_is_bounded_ordered_and_reference_neutral() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore source, successor;
  TEST_ASSERT_TRUE(source.reserveCaptureTail());
  TEST_ASSERT_TRUE(successor.reserveCaptureTail());
  constexpr size_t rows = 2056;
  for (size_t i = 0; i < rows; ++i) {
    auto row = MidiEvent::NoteOn(i, 1, 60, 100); row.noteId = i + 1;
    TEST_ASSERT_TRUE(source.append(row));
  }
  TEST_ASSERT_TRUE(successor.append(MidiEvent::NoteOn(17, 2, 72, 80)));
  const auto original = source.chunkIds();
  const auto chunks = LoopEventStore::usedChunkCount();
  CommittedChunkIdList destination; destination.reserve(original.size());
  const auto* data = destination.data(); const auto capacity = destination.capacity();
  uint64_t generation = 41;
  LoopEventStoreCommitCursor cursor;
  TEST_ASSERT_TRUE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  for (size_t step = 0; step < original.size() + original.size() / 2; ++step) {
    const auto beforeSource = source.chunkIds().size();
    const auto beforeDestination = destination;
    auto staleCursor = cursor;
    const auto result = source.stepCommittedChunkTransfer(cursor, 1);
    TEST_ASSERT_EQUAL(step + 1 == original.size() + original.size() / 2
        ? LoopEventStoreRewriteResult::Complete : LoopEventStoreRewriteResult::Pending, result);
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
        source.stepCommittedChunkTransfer(staleCursor, 1));
    TEST_ASSERT_EQUAL(chunks, LoopEventStore::usedChunkCount());
    TEST_ASSERT_EQUAL_PTR(data, destination.data()); TEST_ASSERT_EQUAL(capacity, destination.capacity());
    TEST_ASSERT_EQUAL(original.size(), source.chunkIds().size() + destination.size());
    if (beforeSource) {
      TEST_ASSERT_EQUAL(beforeSource - 1, source.chunkIds().size());
      TEST_ASSERT_EQUAL(beforeDestination.size() + 1, destination.size());
      TEST_ASSERT_EQUAL(original[beforeSource - 1], destination.back());
    } else {
      size_t changed = 0;
      for (size_t i = 0; i < destination.size(); ++i) changed += destination[i] != beforeDestination[i];
      TEST_ASSERT_EQUAL(2, changed);
    }
    for (auto id : original) TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id));
    TEST_ASSERT_EQUAL(1, successor.size()); TEST_ASSERT_EQUAL(72, successor.at(0).data.noteData.note);
  }
  TEST_ASSERT_TRUE(cursor.complete()); TEST_ASSERT_FALSE(cursor.active());
  TEST_ASSERT_EQUAL(0, source.size());
  for (size_t i = 0; i < original.size(); ++i) TEST_ASSERT_EQUAL(original[i], destination[i]);
  MidiEventVec output; LoopEventStore::appendChunkRefEvents(destination, output);
  TEST_ASSERT_EQUAL(rows, output.size());
  for (size_t i = 0; i < rows; ++i) {
    TEST_ASSERT_EQUAL(i, output[i].tick); TEST_ASSERT_EQUAL(i + 1, output[i].noteId);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, source.stepCommittedChunkTransfer(cursor, 1));
  LoopEventStore::releaseChunkRefs(destination); destination.clear();
  successor.clear();
  TEST_ASSERT_EQUAL(0, LoopEventStore::usedChunkCount());
  printf("commit_cursor_bytes=%zu\n", sizeof(LoopEventStoreCommitCursor));
}

void test_committed_transfer_preflight_and_zero_quantum_preserve_owners() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore source, occupied;
  TEST_ASSERT_TRUE(source.reserveCaptureTail());
  TEST_ASSERT_TRUE(source.append(MidiEvent::NoteOn(1, 1, 60, 100)));
  TEST_ASSERT_TRUE(occupied.append(MidiEvent::NoteOn(2, 1, 62, 100)));
  CommittedChunkIdList destination;
  uint64_t generation = 7; LoopEventStoreCommitCursor cursor;
  TEST_ASSERT_FALSE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  TEST_ASSERT_TRUE(occupied.detachChunksToCommittedChunkIds(destination));
  TEST_ASSERT_FALSE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  TEST_ASSERT_EQUAL(1, source.size()); TEST_ASSERT_EQUAL(1, destination.size());
  LoopEventStore::releaseChunkRefs(destination); destination.clear();
  TEST_ASSERT_TRUE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  TEST_ASSERT_FALSE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, source.stepCommittedChunkTransfer(cursor, 0));
  TEST_ASSERT_EQUAL(1, source.size()); TEST_ASSERT_EQUAL(0, destination.size());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, source.stepCommittedChunkTransfer(cursor, 1));
  LoopEventStore::releaseChunkRefs(destination); destination.clear();
  TEST_ASSERT_EQUAL(0, LoopEventStore::usedChunkCount());
}

void test_committed_transfer_rejects_stale_identity_and_preserves_partial_ownership() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore source, unrelated;
  TEST_ASSERT_TRUE(source.reserveCaptureTail());
  for (size_t i = 0; i < 513; ++i) TEST_ASSERT_TRUE(source.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  CommittedChunkIdList destination; destination.reserve(3);
  uint64_t generation = 9; LoopEventStoreCommitCursor cursor;
  TEST_ASSERT_TRUE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, unrelated.stepCommittedChunkTransfer(cursor, 1));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending, source.stepCommittedChunkTransfer(cursor, 1));
  ++generation;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, source.stepCommittedChunkTransfer(cursor, 1));
  TEST_ASSERT_EQUAL(512, source.size()); TEST_ASSERT_EQUAL(1, destination.size());
  --generation; // Restore the fixture's unchanged logical owner to isolate the next fault.
  source.clear();
  TEST_ASSERT_TRUE(source.reserveCaptureTail());
  // Same extent and reused chunk ids: source generation is the distinguishing receipt.
  for (size_t i = 0; i < 512; ++i)
    TEST_ASSERT_TRUE(source.append(MidiEvent::NoteOn(90 + i, 2, 72, 100)));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, source.stepCommittedChunkTransfer(cursor, 1));
  TEST_ASSERT_EQUAL(72, source.at(0).data.noteData.note);
  TEST_ASSERT_EQUAL(512, source.size()); TEST_ASSERT_EQUAL(1, destination.size());
  for (size_t i = 0; i < 2; ++i) source.releaseCaptureChunkGrain();
  TEST_ASSERT_TRUE(source.empty());
  TEST_ASSERT_TRUE(LoopEventStore::releaseLastCommittedChunkRef(destination));
  TEST_ASSERT_EQUAL(0, LoopEventStore::usedChunkCount());
}

void test_committed_transfer_rejects_destination_replacement() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore source;
  TEST_ASSERT_TRUE(source.reserveCaptureTail());
  TEST_ASSERT_TRUE(source.append(MidiEvent::NoteOn(1, 1, 60, 100)));
  CommittedChunkIdList destination; destination.reserve(1);
  uint64_t generation = 12; LoopEventStoreCommitCursor cursor;
  TEST_ASSERT_TRUE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  destination.reserve(destination.capacity() + 1);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, source.stepCommittedChunkTransfer(cursor, 1));
  TEST_ASSERT_EQUAL(1, source.size()); TEST_ASSERT_TRUE(destination.empty());
  source.clear();
}

void test_committed_transfer_pool_reset_cannot_touch_reused_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore source;
  TEST_ASSERT_TRUE(source.reserveCaptureTail());
  TEST_ASSERT_TRUE(source.append(MidiEvent::NoteOn(1, 1, 60, 100)));
  CommittedChunkIdList destination; destination.reserve(1);
  uint64_t generation = 13; LoopEventStoreCommitCursor cursor;
  TEST_ASSERT_TRUE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  LoopEventStore::resetPoolForTests();
  LoopEventStore replacement;
  TEST_ASSERT_TRUE(replacement.reserveCaptureTail());
  TEST_ASSERT_TRUE(replacement.append(MidiEvent::NoteOn(2, 1, 72, 100)));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, source.stepCommittedChunkTransfer(cursor, 1));
  source.clear();
  TEST_ASSERT_TRUE(destination.empty());
  TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(replacement.chunkIds()[0]));
  TEST_ASSERT_EQUAL(72, replacement.at(0).data.noteData.note);
  replacement.clear();
}

void test_committed_transfer_empty_and_reserved_tail() {
  LoopEventStore::resetPoolForTests();
  for (bool tail : {false, true}) {
    LoopEventStore source; CommittedChunkIdList destination;
    if (tail) { TEST_ASSERT_TRUE(source.reserveCaptureTail()); destination.reserve(1); }
    uint64_t generation = 1; LoopEventStoreCommitCursor cursor;
    TEST_ASSERT_TRUE(source.beginCommittedChunkTransfer(cursor, destination, generation));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, source.stepCommittedChunkTransfer(cursor, 1));
    TEST_ASSERT_EQUAL(tail ? 1 : 0, destination.size());
    MidiEventVec rows; LoopEventStore::appendChunkRefEvents(destination, rows);
    TEST_ASSERT_TRUE(rows.empty());
    LoopEventStore::releaseChunkRefs(destination); destination.clear();
    TEST_ASSERT_EQUAL(0, LoopEventStore::usedChunkCount());
  }
}

void test_committed_transfer_requires_source_persistence_prewarm() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore source;
  TEST_ASSERT_TRUE(source.append(MidiEvent::NoteOn(12, 1, 60, 100)));
  CommittedChunkIdList destination; destination.reserve(1);
  uint64_t generation = 1; LoopEventStoreCommitCursor cursor;
  TEST_ASSERT_FALSE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  TEST_ASSERT_EQUAL(1, source.size()); TEST_ASSERT_TRUE(destination.empty());
  TEST_ASSERT_TRUE(source.prewarmCaptureDirectory());
  TEST_ASSERT_TRUE(source.beginCommittedChunkTransfer(cursor, destination, generation));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, source.stepCommittedChunkTransfer(cursor, 1));
  LoopEventStore::releaseChunkRefs(destination); destination.clear();
  TEST_ASSERT_EQUAL(0, LoopEventStore::usedChunkCount());
}

void test_rewrite_retires_only_directory_tail() {
  for (size_t count : {0u, 1u, 256u, 257u, 2056u}) {
    LoopEventStore::resetPoolForTests(); LoopEventStore store;
    for (size_t i=0; i<4097; ++i)
      TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(i,1,60,100)));
    const auto original=store.chunkIds(); const auto capacity=store.chunkIds().capacity();
    std::vector<MidiEvent> source; std::vector<uint16_t> order;
    for(size_t i=0;i<count;++i) {source.push_back(MidiEvent::NoteOn(i*2,2,61,90));order.push_back(i);}
    LoopEventStoreRewriteCursor cursor;
    TEST_ASSERT_TRUE(store.beginRewriteFromOrderedSource(cursor,count));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
      store.stepRewriteFromOrderedSource(cursor,source.data(),order.data(),count,count,0));
    auto result=LoopEventStoreRewriteResult::Pending; size_t previous=original.size();
    for(size_t turn=0;turn<count/32+original.size()*2+10 && result==LoopEventStoreRewriteResult::Pending;++turn) {
      result=store.stepRewriteFromOrderedSource(cursor,source.data(),order.data(),count,count,32);
      TEST_ASSERT_TRUE(result!=LoopEventStoreRewriteResult::Denied);
      const auto& ids=store.chunkIds();
      TEST_ASSERT_TRUE(ids.size()==previous || ids.size()+1==previous);
      TEST_ASSERT_EQUAL(capacity,ids.capacity());
      for(size_t i=0;i<original.size();++i) {
        if(i<ids.size()) TEST_ASSERT_EQUAL(original[i],ids[i]);
        TEST_ASSERT_EQUAL(i<ids.size()?1:0,LoopEventStore::chunkReferenceCount(original[i]));
      }
      previous=ids.size();
    }
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,result);
    TEST_ASSERT_EQUAL(count,store.size());
    TEST_ASSERT_EQUAL((count+255)/256,store.chunkIds().size());
    for(size_t i=0;i<count;++i) {
      TEST_ASSERT_EQUAL(source[i].tick,store.at(i).tick);
      TEST_ASSERT_EQUAL(2,store.at(i).channel); TEST_ASSERT_EQUAL(61,store.at(i).data.noteData.note);
    }
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      store.stepRewriteFromOrderedSource(cursor,source.data(),order.data(),count,count,32));
  }
}
void test_rewrite_stale_retirement_preserves_replacement() {
  LoopEventStore::resetPoolForTests(); LoopEventStore store;
  for(size_t i=0;i<769;++i) TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(i,1,60,100)));
  LoopEventStoreRewriteCursor cursor;
  TEST_ASSERT_TRUE(store.beginRewriteFromOrderedSource(cursor,0));
  for(unsigned i=0;i<2;++i) TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
    store.stepRewriteFromOrderedSource(cursor,nullptr,nullptr,0,0,1));
  TEST_ASSERT_EQUAL(3,store.chunkIds().size());
  store.clear(); TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(8,2,62,100)));
  const auto id=store.chunkIds().front();
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
    store.stepRewriteFromOrderedSource(cursor,nullptr,nullptr,0,0,1));
  TEST_ASSERT_EQUAL(1,LoopEventStore::chunkReferenceCount(id)); TEST_ASSERT_EQUAL(8,store.at(0).tick);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rewrite_retires_only_directory_tail);
  RUN_TEST(test_rewrite_stale_retirement_preserves_replacement);
  RUN_TEST(test_committed_transfer_requires_source_persistence_prewarm);
  RUN_TEST(test_committed_transfer_is_bounded_ordered_and_reference_neutral);
  RUN_TEST(test_committed_transfer_preflight_and_zero_quantum_preserve_owners);
  RUN_TEST(test_committed_transfer_rejects_stale_identity_and_preserves_partial_ownership);
  RUN_TEST(test_committed_transfer_rejects_destination_replacement);
  RUN_TEST(test_committed_transfer_pool_reset_cannot_touch_reused_chunks);
  RUN_TEST(test_committed_transfer_empty_and_reserved_tail);

  RUN_TEST(test_rotation_invalidates_old_rewrite_without_mutating_either_interval);
  RUN_TEST(test_partial_and_full_boundaries_retain_exclusive_ranges);
  RUN_TEST(test_reserved_cut_and_first_append_survive_pool_exhaustion);
  RUN_TEST(test_pool_reset_invalidates_range_before_reusing_chunk_ids);
  return UNITY_END();
}
