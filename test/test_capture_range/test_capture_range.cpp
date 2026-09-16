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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rotation_invalidates_old_rewrite_without_mutating_either_interval);
  RUN_TEST(test_partial_and_full_boundaries_retain_exclusive_ranges);
  RUN_TEST(test_reserved_cut_and_first_append_survive_pool_exhaustion);
  RUN_TEST(test_pool_reset_invalidates_range_before_reusing_chunk_ids);
  return UNITY_END();
}
