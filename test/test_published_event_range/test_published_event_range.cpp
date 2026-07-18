//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Globals.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"
#include "PublishedEventRange.h"

#include "../../src/LoopEventStore.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"
#include "../../src/PublishedEventRange.cpp"

namespace {

MidiEvent noteOnAt(uint32_t tick, uint8_t note = 60) {
  return MidiEvent::NoteOn(tick, 1, note, 100);
}

void resetStore() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
}

}  // namespace

void test_chunk_intersects_window_linear() {
  const uint32_t loopLength = 64u * Config::TICKS_PER_BAR;
  const uint32_t windowStart = 0;
  const uint32_t windowLength = 16u * Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(PublishedEventRange::chunkIntersectsWindow(
      0, Config::TICKS_PER_BAR, windowStart, windowLength, loopLength));
  TEST_ASSERT_FALSE(PublishedEventRange::chunkIntersectsWindow(
      32u * Config::TICKS_PER_BAR, 33u * Config::TICKS_PER_BAR, windowStart, windowLength,
      loopLength));
}

void test_chunk_intersects_window_wrap_span() {
  const uint32_t loopLength = 64u * Config::TICKS_PER_BAR;
  // Window at start of loop; chunk spans wrap (near end → near start).
  const uint32_t windowStart = 0;
  const uint32_t windowLength = 4u * Config::TICKS_PER_BAR;
  const uint32_t firstTick = 63u * Config::TICKS_PER_BAR;
  const uint32_t lastTick = Config::TICKS_PER_BAR / 2;
  TEST_ASSERT_TRUE(PublishedEventRange::chunkIntersectsWindow(firstTick, lastTick, windowStart,
                                                              windowLength, loopLength));
}

void test_published_event_range_window_skips_out_of_range_chunks() {
  resetStore();
  LoopEventStore early;
  LoopEventStore late;
  const uint32_t loopLength = 64u * Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(early.append(noteOnAt(100)));
  TEST_ASSERT_TRUE(late.append(noteOnAt(40u * Config::TICKS_PER_BAR)));

  PublishedChunkIdList earlyIds;
  PublishedChunkIdList lateIds;
  TEST_ASSERT_TRUE(early.detachChunksToPublished(earlyIds));
  TEST_ASSERT_TRUE(late.detachChunksToPublished(lateIds));

  const PublishedChunkIdList* lists[] = {&earlyIds, &lateIds};
  SessionMidiEventVec out;
  PublishedEventRange::inWindow(lists, 2, loopLength, 0, 16u * Config::TICKS_PER_BAR).appendTo(out);
  TEST_ASSERT_EQUAL(1u, out.size());
  TEST_ASSERT_EQUAL_UINT32(100u, out[0].tick);
}

void test_published_event_range_full_includes_all() {
  resetStore();
  LoopEventStore early;
  LoopEventStore late;
  const uint32_t loopLength = 64u * Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(early.append(noteOnAt(100)));
  TEST_ASSERT_TRUE(late.append(noteOnAt(40u * Config::TICKS_PER_BAR)));

  PublishedChunkIdList earlyIds;
  PublishedChunkIdList lateIds;
  TEST_ASSERT_TRUE(early.detachChunksToPublished(earlyIds));
  TEST_ASSERT_TRUE(late.detachChunksToPublished(lateIds));

  const PublishedChunkIdList* lists[] = {&earlyIds, &lateIds};
  SessionMidiEventVec out;
  PublishedEventRange::full(lists, 2, loopLength).appendTo(out);
  TEST_ASSERT_EQUAL(2u, out.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chunk_intersects_window_linear);
  RUN_TEST(test_chunk_intersects_window_wrap_span);
  RUN_TEST(test_published_event_range_window_skips_out_of_range_chunks);
  RUN_TEST(test_published_event_range_full_includes_all);
  return UNITY_END();
}
