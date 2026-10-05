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

// ---- Slice 7.2 (DEC-113): the exact, modular chunk gate ----------------------------------------------------------------
// The previous gate delegated to a probe sampled every TICKS_PER_16TH_STEP, which can skip a chunk that holds a served event
// when the served window is shorter than a step. These fixtures pin the exact half-open modular span overlap.

namespace {

// The previous implementation, kept verbatim as the reference the exact gate must be a superset of.
bool previousSampledGate(uint32_t firstTick, uint32_t lastTick, uint32_t windowStart, uint32_t windowLength,
                         uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0 || windowLength == 0) return false;
  if (windowLength >= loopLengthTicks) return true;
  return DisplayWindowUtils::noteIntersectsWindow(firstTick, lastTick, windowStart, windowLength, loopLengthTicks);
}

}  // namespace

// The mandated red-proof: chunk span [0,100], one event at tick 8, a one-tick window at tick 8, driven through the
// production budgeted walk. With the sampled gate restored the chunk is skipped and the served event is dropped.
void test_exact_gate_short_window_keeps_chunk_with_served_event() {
  resetStore();
  LoopEventStore list;
  const uint32_t loopLength = 24u * Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(list.append(noteOnAt(0, 60)));
  TEST_ASSERT_TRUE(list.append(noteOnAt(8, 61)));
  TEST_ASSERT_TRUE(list.append(noteOnAt(100, 62)));
  CommittedChunkIdList ids;
  TEST_ASSERT_TRUE(list.detachChunksToCommittedChunkIds(ids));
  const CommittedChunkIdList* lists[] = {&ids};
  uint32_t first = 0, last = 0;
  TEST_ASSERT_TRUE(LoopEventStore::chunkTickSpan(ids[0], first, last));
  TEST_ASSERT_EQUAL_UINT32(0, first);
  TEST_ASSERT_EQUAL_UINT32(100, last);
  SessionMidiEventVec out;
  CommittedEventRange::WindowTraversalCursor cursor{};
  CommittedEventRange::resetWindowTraversalCursor(cursor);
  const auto result = CommittedEventRange::appendWindowBudgeted(lists, 1, loopLength, 8, 1,
                                                                std::numeric_limits<size_t>::max(), cursor, out);
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(out.size()),
                                   "chunk skipped although it holds a served event");
  TEST_ASSERT_EQUAL_UINT32(8, out[0].tick);
}

// A wrapped chunk (firstTick > lastTick: content before the wrap and after it) against a linear window, and a linear chunk
// against a wrapped window, in both directions.
void test_exact_gate_is_modular_for_wrapped_chunks_and_windows() {
  const uint32_t L = 24u * Config::TICKS_PER_BAR;  // 18432
  // wrapped chunk [17000 .. wrap .. 100] vs linear window [50,100): overlaps in the head segment
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(17000, 100, 50, 50, L));
  // ... vs a linear window wholly between the segments: no overlap
  TEST_ASSERT_FALSE(CommittedEventRange::chunkIntersectsWindow(17000, 100, 5000, 100, L));
  // ... vs a linear window in the tail segment
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(17000, 100, 18000, 100, L));
  // linear chunk [18000,18400] vs a wrapped window [18300 .. wrap .. 40): overlaps in the tail segment
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(18000, 18400, 18300, 400, L));
  // linear chunk [10,20] vs the same wrapped window: head segment [0,40) overlaps
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(10, 20, 18300, 400, L));
  // linear chunk [5000,5100] vs the wrapped window: no overlap
  TEST_ASSERT_FALSE(CommittedEventRange::chunkIntersectsWindow(5000, 5100, 18300, 400, L));
  // half-open at the window end: chunk starting exactly at windowStart+windowLength does not intersect
  TEST_ASSERT_FALSE(CommittedEventRange::chunkIntersectsWindow(150, 200, 100, 50, L));
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(149, 200, 100, 50, L));
  // chunk ending exactly at windowStart intersects (inclusive lastTick)
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(0, 100, 100, 50, L));
  TEST_ASSERT_FALSE(CommittedEventRange::chunkIntersectsWindow(0, 99, 100, 50, L));
  // degenerate inputs keep their previous answers
  TEST_ASSERT_FALSE(CommittedEventRange::chunkIntersectsWindow(0, 10, 0, 0, L));
  TEST_ASSERT_FALSE(CommittedEventRange::chunkIntersectsWindow(0, 10, 0, 10, 0));
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(0, 10, 0, L, L));
}

// Property: over many chunk spans and windows the exact gate (a) never rejects a chunk that holds an event inside the
// window (soundness against the per-event half-open filter) and (b) accepts everything the previous sampled gate accepted for a
// real span (the previous gate over-admitted single-tick spans as a full loop; event output is unaffected).
void test_exact_gate_is_sound_and_a_superset_of_the_previous_gate() {
  const uint32_t L = 24u * Config::TICKS_PER_BAR;
  uint32_t seed = 12345u;
  const auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
  for (unsigned i = 0; i < 4000; ++i) {
    const uint32_t first = next() % L;
    uint32_t last;
    switch (i % 3) {
      case 0: last = first + (next() % 200); break;           // short linear
      case 1: last = first + (next() % (L / 2)); break;       // long linear
      default: last = next() % L; break;                      // possibly wrapped
    }
    if (last >= L) last = L - 1;
    const uint32_t ws = next() % L;
    uint32_t wl = 1 + (next() % (i % 2 ? 40u : 2u * Config::TICKS_PER_BAR));
    if (ws + wl > L) wl = L - ws;  // linear windows (the lane never crosses); wrapped ones are covered above
    const bool exact = CommittedEventRange::chunkIntersectsWindow(first, last, ws, wl, L);
    // The previous gate treated a single-tick span (firstTick == lastTick) as a full loop (its end-before-start fix-up) and
    // admitted it for almost any window; the exact gate answers it precisely (checked below). Event output is unaffected:
    // events are still filtered per tick.
    if (first != last && previousSampledGate(first, last, ws, wl, L)) {
      TEST_ASSERT_TRUE_MESSAGE(exact, "exact gate rejected a chunk the previous gate accepted");
    }
    // truth for a sorted, non-wrapped chunk: its events lie within [first,last]; any window tick inside the span may hold one
    if (first <= last) {
      const bool overlaps = first < ws + wl && ws <= last;
      TEST_ASSERT_EQUAL_MESSAGE(overlaps, exact, "exact gate disagrees with the span overlap");
    }
  }
}

// Oracle (b): copies are bounded by the chunks the gate admits (never more than the whole-loop walk copies), whole-loop
// windows copy every chunk (today's behaviour), and event output is exactly the half-open window's events. (The events here
// share chunks, so the per-chunk copy count is not a per-window proof; the gate's admission is pinned by the fixtures above.)
void test_exact_gate_copies_only_chunks_that_intersect_the_window() {
  resetStore();
  const uint32_t loopLength = 24u * Config::TICKS_PER_BAR;
  CommittedChunkIdList ids;
  {
    LoopEventStore list;
    for (uint32_t bar = 0; bar < 24; ++bar) {
      TEST_ASSERT_TRUE(list.append(noteOnAt(bar * Config::TICKS_PER_BAR + 10, static_cast<uint8_t>(40 + bar))));
    }
    TEST_ASSERT_TRUE(list.detachChunksToCommittedChunkIds(ids));
  }
  const CommittedChunkIdList* lists[] = {&ids};
  const auto run = [&](uint32_t start, uint32_t length, SessionMidiEventVec& out) {
    CommittedEventRange::WindowTraversalCursor cursor{};
    CommittedEventRange::resetWindowTraversalCursor(cursor);
    return CommittedEventRange::appendWindowBudgeted(lists, 1, loopLength, start, length,
                                                     std::numeric_limits<size_t>::max(), cursor, out);
  };
  SessionMidiEventVec whole;
  const auto wholeResult = run(0, loopLength, whole);
  TEST_ASSERT_EQUAL_UINT32(24, static_cast<uint32_t>(whole.size()));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(ids.size()), static_cast<uint32_t>(wholeResult.chunksCopied));
  // an unaligned two-bar window over the 24-bar loop
  SessionMidiEventVec two;
  const uint32_t start = 5u * Config::TICKS_PER_BAR + 700u;
  const auto twoResult = run(start, 2u * Config::TICKS_PER_BAR, two);
  TEST_ASSERT_TRUE(twoResult.chunksCopied <= wholeResult.chunksCopied);
  uint32_t expected = 0;
  for (uint32_t bar = 0; bar < 24; ++bar) {
    const uint32_t tick = bar * Config::TICKS_PER_BAR + 10;
    if (tick >= start && tick < start + 2u * Config::TICKS_PER_BAR) ++expected;
  }
  TEST_ASSERT_EQUAL_UINT32(expected, static_cast<uint32_t>(two.size()));
  // a window between two events yields no events
  SessionMidiEventVec none;
  const auto noneResult = run(5u * Config::TICKS_PER_BAR + 20u, 100u, none);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(none.size()));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chunk_intersects_window_linear);
  RUN_TEST(test_chunk_intersects_window_wrap_span);
  RUN_TEST(test_exact_gate_short_window_keeps_chunk_with_served_event);
  RUN_TEST(test_exact_gate_is_modular_for_wrapped_chunks_and_windows);
  RUN_TEST(test_exact_gate_is_sound_and_a_superset_of_the_previous_gate);
  RUN_TEST(test_exact_gate_copies_only_chunks_that_intersect_the_window);
  RUN_TEST(test_committed_event_range_window_skips_out_of_range_chunks);
  RUN_TEST(test_committed_event_range_full_includes_all);
  RUN_TEST(test_window_budgeted_matches_one_shot);
  RUN_TEST(test_window_budgeted_resume_budget_one);
  return UNITY_END();
}
