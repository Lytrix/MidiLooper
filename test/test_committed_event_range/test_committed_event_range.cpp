//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Globals.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"
#include "CommittedEventRange.h"

#include "../../src/LoopEventStore.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"
#include "../../src/CommittedEventRange.cpp"

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
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(
      0, Config::TICKS_PER_BAR, windowStart, windowLength, loopLength));
  TEST_ASSERT_FALSE(CommittedEventRange::chunkIntersectsWindow(
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
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(firstTick, lastTick, windowStart,
                                                              windowLength, loopLength));
}

void test_committed_event_range_window_skips_out_of_range_chunks() {
  resetStore();
  LoopEventStore early;
  LoopEventStore late;
  const uint32_t loopLength = 64u * Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(early.append(noteOnAt(100)));
  TEST_ASSERT_TRUE(late.append(noteOnAt(40u * Config::TICKS_PER_BAR)));

  CommittedChunkIdList earlyIds;
  CommittedChunkIdList lateIds;
  TEST_ASSERT_TRUE(early.detachChunksToCommittedChunkIds(earlyIds));
  TEST_ASSERT_TRUE(late.detachChunksToCommittedChunkIds(lateIds));

  const CommittedChunkIdList* lists[] = {&earlyIds, &lateIds};
  SessionMidiEventVec out;
  CommittedEventRange::inWindow(lists, 2, loopLength, 0, 16u * Config::TICKS_PER_BAR).appendTo(out);
  TEST_ASSERT_EQUAL(1u, out.size());
  TEST_ASSERT_EQUAL_UINT32(100u, out[0].tick);
}

void test_committed_event_range_full_includes_all() {
  resetStore();
  LoopEventStore early;
  LoopEventStore late;
  const uint32_t loopLength = 64u * Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(early.append(noteOnAt(100)));
  TEST_ASSERT_TRUE(late.append(noteOnAt(40u * Config::TICKS_PER_BAR)));

  CommittedChunkIdList earlyIds;
  CommittedChunkIdList lateIds;
  TEST_ASSERT_TRUE(early.detachChunksToCommittedChunkIds(earlyIds));
  TEST_ASSERT_TRUE(late.detachChunksToCommittedChunkIds(lateIds));

  const CommittedChunkIdList* lists[] = {&earlyIds, &lateIds};
  SessionMidiEventVec out;
  CommittedEventRange::full(lists, 2, loopLength).appendTo(out);
  TEST_ASSERT_EQUAL(2u, out.size());
}

void test_window_budgeted_matches_one_shot() {
  resetStore();
  LoopEventStore listA;
  LoopEventStore listB;
  const uint32_t loopLength = 64u * Config::TICKS_PER_BAR;
  const uint32_t windowStart = 0;
  const uint32_t windowLength = 16u * Config::TICKS_PER_BAR;

  TEST_ASSERT_TRUE(listA.append(noteOnAt(100)));
  TEST_ASSERT_TRUE(listA.append(noteOnAt(200)));
  TEST_ASSERT_TRUE(listA.append(noteOnAt(30u * Config::TICKS_PER_BAR)));  // filtered out
  TEST_ASSERT_TRUE(listB.append(noteOnAt(150)));
  TEST_ASSERT_TRUE(listB.append(noteOnAt(250)));

  CommittedChunkIdList idsA;
  CommittedChunkIdList idsB;
  TEST_ASSERT_TRUE(listA.detachChunksToCommittedChunkIds(idsA));
  TEST_ASSERT_TRUE(listB.detachChunksToCommittedChunkIds(idsB));
  const CommittedChunkIdList* lists[] = {&idsA, &idsB};

  SessionMidiEventVec oneShot;
  CommittedEventRange::inWindow(lists, 2, loopLength, windowStart, windowLength).appendTo(oneShot);

  SessionMidiEventVec chunked;
  CommittedEventRange::WindowTraversalCursor cursor{};
  CommittedEventRange::resetWindowTraversalCursor(cursor);
  bool complete = false;
  uint16_t guard = 0;
  while (!complete && guard < 64) {
    const auto result = CommittedEventRange::appendWindowBudgeted(
        lists, 2, loopLength, windowStart, windowLength, 2, cursor, chunked);
    complete = result.complete;
    ++guard;
  }
  TEST_ASSERT_TRUE(complete);
  TEST_ASSERT_TRUE(guard <= 64);
  TEST_ASSERT_EQUAL_UINT32(oneShot.size(), chunked.size());
  for (size_t i = 0; i < oneShot.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(oneShot[i].tick, chunked[i].tick);
  }
}

void test_window_budgeted_resume_budget_one() {
  resetStore();
  LoopEventStore listA;
  const uint32_t loopLength = 64u * Config::TICKS_PER_BAR;
  const uint32_t windowStart = 0;
  const uint32_t windowLength = 16u * Config::TICKS_PER_BAR;

  TEST_ASSERT_TRUE(listA.append(noteOnAt(100)));
  TEST_ASSERT_TRUE(listA.append(noteOnAt(120)));
  TEST_ASSERT_TRUE(listA.append(noteOnAt(140)));
  TEST_ASSERT_TRUE(listA.append(noteOnAt(160)));

  CommittedChunkIdList idsA;
  TEST_ASSERT_TRUE(listA.detachChunksToCommittedChunkIds(idsA));
  const CommittedChunkIdList* lists[] = {&idsA};

  SessionMidiEventVec oneShot;
  CommittedEventRange::inWindow(lists, 1, loopLength, windowStart, windowLength).appendTo(oneShot);

  SessionMidiEventVec chunked;
  CommittedEventRange::WindowTraversalCursor cursor{};
  CommittedEventRange::resetWindowTraversalCursor(cursor);
  bool complete = false;
  uint16_t guard = 0;
  while (!complete && guard < 128) {
    const auto result = CommittedEventRange::appendWindowBudgeted(
        lists, 1, loopLength, windowStart, windowLength, 1, cursor, chunked);
    complete = result.complete;
    ++guard;
  }
  TEST_ASSERT_TRUE(complete);
  TEST_ASSERT_TRUE(guard <= 128);
  TEST_ASSERT_EQUAL_UINT32(oneShot.size(), chunked.size());
  for (size_t i = 0; i < oneShot.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(oneShot[i].tick, chunked[i].tick);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chunk_intersects_window_linear);
  RUN_TEST(test_chunk_intersects_window_wrap_span);
  RUN_TEST(test_committed_event_range_window_skips_out_of_range_chunks);
  RUN_TEST(test_committed_event_range_full_includes_all);
  RUN_TEST(test_window_budgeted_matches_one_shot);
  RUN_TEST(test_window_budgeted_resume_budget_one);
  return UNITY_END();
}
