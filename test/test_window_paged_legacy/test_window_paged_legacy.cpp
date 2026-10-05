// Copyright (c) 2025 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include <unity.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Globals.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"
#include "CommittedEventRange.h"
#include "WindowPagedStream.h"

#include "../../src/LoopEventStore.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"
#include "../../src/CommittedEventRange.cpp"

namespace {
void resetStore() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
}

MidiEvent noteOn(uint32_t tick, NoteId id, uint8_t pitch = 60) {
  MidiEvent event = MidiEvent::NoteOn(tick, 1, pitch, 100);
  event.noteId = id;
  return event;
}

MidiEvent noteOff(uint32_t tick, NoteId id, uint8_t pitch = 60) {
  MidiEvent event = MidiEvent::NoteOff(tick, 1, pitch, 0);
  event.noteId = id;
  return event;
}

struct StreamResult {
  std::vector<MidiEvent> events;
  size_t producedPages = 0;
  size_t pageHighWater = 0;
};

StreamResult drainWindow(const CommittedChunkIdList* const* lists, size_t listCount,
                         uint32_t loopLength, uint32_t start, uint32_t length,
                         uint32_t revision) {
  WindowPagedStream stream;
  stream.reset(revision);
  WindowPageCursor cursor{};
  StreamResult result;
  bool exhausted = false;
  for (size_t guard = 0; guard < 10000 && !exhausted; ++guard) {
    while (!stream.sourceComplete() && stream.pagesPublished() < kWindowPageCount) {
      const auto produced = stream.produce(lists, listCount, loopLength, start, length, revision);
      if (produced == WindowPagedStream::ProduceOutcome::Produced) {
        ++result.producedPages;
        result.pageHighWater = std::max(result.pageHighWater, stream.pagesPublished());
      } else {
        TEST_ASSERT_EQUAL(static_cast<int>(WindowPagedStream::ProduceOutcome::SourceComplete),
                          static_cast<int>(produced));
      }
    }

    MidiEvent batch[kWindowPageCapacity];
    size_t emitted = 0;
    const auto consumed = stream.consume(cursor, batch, kWindowPageCapacity, emitted);
    if (consumed == WindowPagedStream::ConsumeOutcome::Exhausted) {
      exhausted = true;
    } else {
      TEST_ASSERT_TRUE(consumed == WindowPagedStream::ConsumeOutcome::Emitted ||
                       consumed == WindowPagedStream::ConsumeOutcome::NeedsPage);
      for (size_t i = 0; i < emitted; ++i) result.events.push_back(batch[i]);
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(exhausted, "paged stream must terminate without waiting or rebuilding inline");
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(kWindowPageCount, result.pageHighWater);
  return result;
}
}  // namespace

/// DEC-108 §14 / task 5.5: over-capacity same-tick events accumulated across two pass lists.
/// The input-list ordering is the independent source-order oracle for equal ticks.
void test_paged_stream_preserves_overcapacity_same_tick_order_across_pass_lists() {
  resetStore();
  constexpr uint32_t loopLength = 8u * Config::TICKS_PER_BAR;
  constexpr size_t perPass = 1200;
  LoopEventStore first;
  LoopEventStore second;
  for (size_t i = 0; i < perPass; ++i) {
    TEST_ASSERT_TRUE(first.append(noteOn(100, static_cast<NoteId>(i + 1), static_cast<uint8_t>(i % 128))));
    TEST_ASSERT_TRUE(second.append(noteOn(100, static_cast<NoteId>(perPass + i + 1), static_cast<uint8_t>(i % 128))));
  }
  CommittedChunkIdList firstIds;
  CommittedChunkIdList secondIds;
  TEST_ASSERT_TRUE(first.detachChunksToCommittedChunkIds(firstIds));
  TEST_ASSERT_TRUE(second.detachChunksToCommittedChunkIds(secondIds));
  const CommittedChunkIdList* lists[] = {&firstIds, &secondIds};
  // Fill the fixed ring, prove explicit backpressure, consume one page, and prove the slot is reusable.
  WindowPagedStream backpressure;
  backpressure.reset(31);
  for (size_t page = 0; page < kWindowPageCount; ++page) {
    TEST_ASSERT_EQUAL(static_cast<int>(WindowPagedStream::ProduceOutcome::Produced),
                      static_cast<int>(backpressure.produce(lists, 2, loopLength, 0, loopLength, 31)));
  }
  TEST_ASSERT_EQUAL(static_cast<int>(WindowPagedStream::ProduceOutcome::RingFull),
                    static_cast<int>(backpressure.produce(lists, 2, loopLength, 0, loopLength, 31)));
  WindowPageCursor backpressureCursor{};
  MidiEvent drained[kWindowPageCapacity];
  size_t drainedCount = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(WindowPagedStream::ConsumeOutcome::Emitted),
                    static_cast<int>(backpressure.consume(backpressureCursor, drained,
                                                          kWindowPageCapacity, drainedCount)));
  TEST_ASSERT_EQUAL_UINT32(kWindowPageCapacity, static_cast<uint32_t>(drainedCount));
  TEST_ASSERT_EQUAL(static_cast<int>(WindowPagedStream::ProduceOutcome::Produced),
                    static_cast<int>(backpressure.produce(lists, 2, loopLength, 0, loopLength, 31)));

  const StreamResult result = drainWindow(lists, 2, loopLength, 0, loopLength, 31);
  TEST_ASSERT_EQUAL_UINT32(2u * perPass, static_cast<uint32_t>(result.events.size()));
  TEST_ASSERT_TRUE(result.producedPages > kWindowPageCount);  // proves drained-page reuse, not just initial fill
  TEST_ASSERT_EQUAL_UINT32(kWindowPageCount, static_cast<uint32_t>(result.pageHighWater));
  for (size_t i = 0; i < result.events.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(100, result.events[i].tick);
    TEST_ASSERT_EQUAL_UINT32(i + 1, result.events[i].noteId);
  }
}

/// A wrapped interval retains canonical source order (ascending source ticks); the playback projection applies its own
/// phase ordering later. The stream must neither drop nor duplicate the head/tail events while paging.
void test_paged_stream_wrap_window_matches_independent_half_open_projection() {
  resetStore();
  constexpr uint32_t loopLength = 8u * Config::TICKS_PER_BAR;
  LoopEventStore store;
  const uint32_t ticks[] = {10, 100, loopLength - 100, loopLength - 10};
  for (size_t i = 0; i < 4; ++i) TEST_ASSERT_TRUE(store.append(noteOn(ticks[i], static_cast<NoteId>(i + 1))));
  CommittedChunkIdList ids;
  TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(ids));
  const CommittedChunkIdList* lists[] = {&ids};

  const StreamResult result = drainWindow(lists, 1, loopLength, loopLength - 100, 200, 44);
  const uint32_t expected[] = {10, loopLength - 100, loopLength - 10};
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(result.events.size()));
  for (size_t i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_UINT32(expected[i], result.events[i].tick);
}

/// The note-off is separated from its note-on by over three pages. Page reuse must preserve the pair exactly once; this
/// fixture does not claim the pages themselves perform ledger closure or prove a timing bound.
void test_paged_stream_preserves_note_closure_across_page_reuse() {
  resetStore();
  constexpr uint32_t loopLength = 8u * Config::TICKS_PER_BAR;
  constexpr NoteId spanningId = 9001;
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(noteOn(0, spanningId, 60)));
  for (size_t i = 0; i < 700; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(100, static_cast<NoteId>(10000 + i), static_cast<uint8_t>(i % 128))));
  }
  TEST_ASSERT_TRUE(store.append(noteOff(200, spanningId, 60)));
  CommittedChunkIdList ids;
  TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(ids));
  const CommittedChunkIdList* lists[] = {&ids};

  const StreamResult result = drainWindow(lists, 1, loopLength, 0, loopLength, 55);
  TEST_ASSERT_EQUAL_UINT32(702, static_cast<uint32_t>(result.events.size()));
  TEST_ASSERT_TRUE(result.producedPages > kWindowPageCount);
  TEST_ASSERT_TRUE(result.events.front().isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(spanningId, result.events.front().noteId);
  TEST_ASSERT_TRUE(result.events.back().isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(spanningId, result.events.back().noteId);
  TEST_ASSERT_EQUAL_UINT32(0, result.events.front().tick);
  TEST_ASSERT_EQUAL_UINT32(200, result.events.back().tick);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_paged_stream_preserves_overcapacity_same_tick_order_across_pass_lists);
  RUN_TEST(test_paged_stream_wrap_window_matches_independent_half_open_projection);
  RUN_TEST(test_paged_stream_preserves_note_closure_across_page_reuse);
  return UNITY_END();
}
