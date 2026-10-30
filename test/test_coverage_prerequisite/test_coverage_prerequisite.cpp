// Task 4.1a/4.1b — touched-coverage prerequisites, owed obligations and the execution grain.
//
// These are contract tests: they pin the semantics the production wiring must satisfy, without asserting
// anything about timing. The obligation cases are the REDs requested in review round 2.

#include <unity.h>

#include "Globals.h"
#include "Utils/CoveragePrerequisite.h"

using namespace CoveragePrerequisite;

namespace {
constexpr uint32_t kBar = Config::TICKS_PER_BAR;
constexpr uint32_t kLoop31Bars = 31 * kBar;
}  // namespace

// --- Tile model (4.1b spatial envelope) -------------------------------------------------------------

void test_riao4_1b_tile_count_and_envelope() {
  TEST_ASSERT_EQUAL_UINT16(0, tileCount(0));
  TEST_ASSERT_EQUAL_UINT16(1, tileCount(4 * kBar));
  TEST_ASSERT_EQUAL_UINT16(2, tileCount(8 * kBar));
  TEST_ASSERT_EQUAL_UINT16(8, tileCount(31 * kBar));  // ceil(31/4)
  TEST_ASSERT_TRUE(tileCountFitsEnvelope(31 * kBar));
  // Above the declared envelope the caller takes the named whole-loop fallback, not partial coverage.
  TEST_ASSERT_FALSE(tileCountFitsEnvelope((kMaxTiles * kTileBars + 1) * kBar));
  TEST_ASSERT_TRUE(tileCountFitsEnvelope(kMaxTiles * kTileBars * kBar));
}

void test_riao4_1b_tile_index_is_loop_relative() {
  TEST_ASSERT_EQUAL_UINT16(0, tileIndexForPhase(0, kLoop31Bars));
  TEST_ASSERT_EQUAL_UINT16(0, tileIndexForPhase(3 * kBar, kLoop31Bars));
  TEST_ASSERT_EQUAL_UINT16(1, tileIndexForPhase(4 * kBar, kLoop31Bars));
  TEST_ASSERT_EQUAL_UINT16(7, tileIndexForPhase(28 * kBar, kLoop31Bars));
  // A phase at or beyond the loop wraps rather than falling outside the model.
  TEST_ASSERT_EQUAL_UINT16(0, tileIndexForPhase(kLoop31Bars, kLoop31Bars));
  TEST_ASSERT_EQUAL_UINT16(1, tileIndexForPhase(kLoop31Bars + 4 * kBar, kLoop31Bars));
}

void test_riao4_1b_span_marks_every_touched_tile_including_wrap() {
  uint64_t mask = 0;
  // One bar inside tile 1: exactly one tile.
  TEST_ASSERT_EQUAL_UINT8(1, markSpanTiles(5 * kBar, kBar, kLoop31Bars, mask));
  TEST_ASSERT_TRUE(tileIsRequired(mask, 1));

  // Crossing the 3/4 bar boundary (tile 0 -> tile 1): two tiles.
  mask = 0;
  TEST_ASSERT_EQUAL_UINT8(2, markSpanTiles(3 * kBar, 2 * kBar, kLoop31Bars, mask));
  TEST_ASSERT_TRUE(tileIsRequired(mask, 0));
  TEST_ASSERT_TRUE(tileIsRequired(mask, 1));

  // A wrap-crossing span touches the last tile and the first.
  mask = 0;
  TEST_ASSERT_EQUAL_UINT8(2, markSpanTiles(kLoop31Bars - kBar / 2, kBar, kLoop31Bars, mask));
  TEST_ASSERT_TRUE(tileIsRequired(mask, 7));
  TEST_ASSERT_TRUE(tileIsRequired(mask, 0));
}

// --- Sparse / monotonic coverage -------------------------------------------------------------------

void test_riao4_2_sparse_touches_do_not_require_the_middle() {
  uint64_t required = 0;
  (void)markSpanTiles(2 * kBar, kBar, kLoop31Bars, required);
  (void)markSpanTiles(27 * kBar, kBar, kLoop31Bars, required);
  TEST_ASSERT_TRUE(tileIsRequired(required, 0));
  TEST_ASSERT_TRUE(tileIsRequired(required, 6));
  for (uint16_t tile = 1; tile <= 5; ++tile) {
    TEST_ASSERT_FALSE_MESSAGE(tileIsRequired(required, tile), "the untouched middle is not required");
  }
}

// --- Obligations: persistence, coalescing, generation ----------------------------------------------

// 4.1a RED (c): many hold ticks inside one unprepared tile stay one obligation.
void test_riao4_1a_many_hold_ticks_coalesce_to_one_obligation() {
  ObligationSet obligations;
  const ObligationKey key{42u, 3u, Kind::HeldParticipation};
  for (uint32_t step = 0; step < 1000; ++step) {
    TEST_ASSERT_TRUE(obligations.owe(key, 7u));
  }
  TEST_ASSERT_EQUAL_UINT8(1, obligations.size());
  TEST_ASSERT_EQUAL_UINT8(1, obligations.owedCount());
  // Distinct kinds on the same note/tile are distinct consequences.
  TEST_ASSERT_TRUE(obligations.owe({42u, 3u, Kind::SpanFinalization}, 7u));
  TEST_ASSERT_EQUAL_UINT8(2, obligations.size());
}

// 4.1a RED (a): a held note leaves the tile before it is prepared; the consequence still fires once.
void test_riao4_1a_obligation_survives_the_playhead_leaving_the_tile() {
  ObligationSet obligations;
  const ObligationKey key{100u, 2u, Kind::HeldParticipation};
  TEST_ASSERT_TRUE(obligations.owe(key, 7u));
  TEST_ASSERT_EQUAL(Status::Owed, heldParticipation(false));
  // The playhead has moved far away; nothing about the obligation changes.
  TEST_ASSERT_EQUAL_UINT8(1, obligations.owedCount());
  // The tile prepares later, for the same generation: the consequence discharges exactly once.
  const uint64_t prepared = uint64_t(1) << 2;
  TEST_ASSERT_EQUAL_UINT8(1, obligations.dischargePreparedTiles(prepared, 7u));
  TEST_ASSERT_EQUAL_UINT8(0, obligations.owedCount());
  // A second discharge attempt (a replayed completion) changes nothing.
  TEST_ASSERT_EQUAL_UINT8(0, obligations.dischargePreparedTiles(prepared, 7u));
}

// 4.1a RED (b): occupancy owed into completion; the final delta is produced once, after preparation.
void test_riao4_1a_occupancy_owed_then_note_completes() {
  // Note-on: tile not prepared, ledger cannot answer -> owed, and the note is NOT "not occupied".
  TEST_ASSERT_EQUAL(Status::Owed, occupancyAtPhase(false, false));
  ObligationSet obligations;
  TEST_ASSERT_TRUE(obligations.owe({200u, 5u, Kind::OccupancyAtPhase}, 9u));

  // Note-off arrives before the tile prepares: finalization is owed as well, not computed on partial data.
  uint64_t prepared = 0;
  const uint64_t span = (uint64_t(1) << 5);
  TEST_ASSERT_EQUAL(Status::Owed, spanFinalization(prepared, span));
  TEST_ASSERT_TRUE(obligations.owe({200u, 5u, Kind::SpanFinalization}, 9u));
  TEST_ASSERT_EQUAL_UINT8(2, obligations.owedCount());

  // The tile prepares; both consequences discharge once and the delta is computed exactly once.
  prepared |= span;
  TEST_ASSERT_EQUAL(Status::Ready, occupancyAtPhase(true, false));
  TEST_ASSERT_EQUAL(Status::Ready, spanFinalization(prepared, span));
  TEST_ASSERT_EQUAL_UINT8(2, obligations.dischargePreparedTiles(prepared, 9u));
  TEST_ASSERT_EQUAL_UINT8(0, obligations.owedCount());
}

// 4.1a RED (d): a generation change retains and re-owes obligations.
void test_riao4_1a_generation_change_re_owes_obligations() {
  ObligationSet obligations;
  TEST_ASSERT_TRUE(obligations.owe({300u, 1u, Kind::HeldParticipation}, 4u));
  obligations.requalify(5u);
  TEST_ASSERT_EQUAL_UINT8(1, obligations.owedCount());
  TEST_ASSERT_EQUAL_UINT32(5u, obligations.at(0).generation);
  // Prepared-for-the-old-generation bits must not discharge a new-generation obligation.
  const uint64_t prepared = uint64_t(1) << 1;
  TEST_ASSERT_EQUAL_UINT8(1, obligations.dischargePreparedTiles(prepared, 5u));
  TEST_ASSERT_EQUAL_UINT8(0, obligations.owedCount());
}

void test_riao4_1a_obligation_set_is_bounded() {
  ObligationSet obligations;
  for (uint16_t tile = 0; tile < kMaxObligations; ++tile) {
    TEST_ASSERT_TRUE(obligations.owe({1u, tile, Kind::HeldParticipation}, 1u));
  }
  TEST_ASSERT_EQUAL_UINT8(kMaxObligations, obligations.size());
  // Overflow is a named refusal, never a silent drop or an unbounded queue.
  TEST_ASSERT_FALSE(obligations.owe({1u, kMaxObligations, Kind::HeldParticipation}, 1u));
  TEST_ASSERT_EQUAL_UINT8(kMaxObligations, obligations.size());
}

// --- 4.3: conservative closure -------------------------------------------------------------------

void test_riao4_3_closure_pad_is_the_max_committed_span() {
  TEST_ASSERT_EQUAL_UINT32(0, closurePadTicks(0, kLoop31Bars));
  TEST_ASSERT_EQUAL_UINT32(2 * kBar, closurePadTicks(2 * kBar, kLoop31Bars));
  // A note that can reach anywhere saturates: no ambiguous modular pad.
  TEST_ASSERT_EQUAL_UINT32(kLoop31Bars, closurePadTicks(kLoop31Bars, kLoop31Bars));
  TEST_ASSERT_EQUAL_UINT32(kLoop31Bars, closurePadTicks(3 * kLoop31Bars, kLoop31Bars));
}

void test_riao4_3_closure_window_widens_backwards_and_wraps() {
  uint32_t start = 0, length = 0;
  // Interior tile, 1-bar pad: [tileStart - pad, tileEnd).
  closureWindow(8 * kBar, 4 * kBar, kBar, kLoop31Bars, start, length);
  TEST_ASSERT_EQUAL_UINT32(7 * kBar, start);
  TEST_ASSERT_EQUAL_UINT32(5 * kBar, length);

  // Tile at the loop start: the pad wraps to the loop end rather than clamping at zero.
  closureWindow(0, 4 * kBar, kBar, kLoop31Bars, start, length);
  TEST_ASSERT_EQUAL_UINT32(kLoop31Bars - kBar, start);
  TEST_ASSERT_EQUAL_UINT32(5 * kBar, length);

  // Saturated closure reads the whole loop.
  closureWindow(8 * kBar, 4 * kBar, kLoop31Bars, kLoop31Bars, start, length);
  TEST_ASSERT_EQUAL_UINT32(0, start);
  TEST_ASSERT_EQUAL_UINT32(kLoop31Bars, length);

  // A tile plus pad that covers the whole loop also returns the whole loop (no double representation).
  closureWindow(0, kLoop31Bars - kBar, 2 * kBar, kLoop31Bars, start, length);
  TEST_ASSERT_EQUAL_UINT32(0, start);
  TEST_ASSERT_EQUAL_UINT32(kLoop31Bars, length);
}

// --- 4.2: the three transitions, in one coverage state ---------------------------------------------

// Sparse touches in distant tiles; the untouched middle is never required.
void test_riao4_2_state_sparse_touches_leave_the_middle_unrequired() {
  CoverageState coverage;
  coverage.beginSession(kLoop31Bars, 11u);
  TEST_ASSERT_EQUAL_UINT8(1, coverage.markNoteOn(2 * kBar));
  TEST_ASSERT_EQUAL_UINT8(1, coverage.markNoteOn(27 * kBar));
  TEST_ASSERT_EQUAL_UINT8(2, coverage.required == 0 ? 0 : coverage.owedCount());
  TEST_ASSERT_TRUE(coverage.tileRequired(0));
  TEST_ASSERT_TRUE(coverage.tileRequired(6));
  for (uint16_t tile = 1; tile <= 5; ++tile) {
    TEST_ASSERT_FALSE_MESSAGE(coverage.tileRequired(tile), "the middle stays unrequired");
  }
}

// Repeated wraps that do not carry a held note anywhere new add nothing.
void test_riao4_2_state_repeated_wraps_add_nothing() {
  CoverageState coverage;
  coverage.beginSession(kLoop31Bars, 11u);
  const uint8_t first = coverage.markNoteOn(2 * kBar + 10);
  TEST_ASSERT_EQUAL_UINT8(1, first);
  const uint64_t requiredAfterFirst = coverage.required;
  for (uint32_t wrap = 0; wrap < 16; ++wrap) {
    // The playhead passes through the same tile repeatedly (no held note crossing a boundary).
    TEST_ASSERT_EQUAL_UINT8(0, coverage.markHeldAdvance(2 * kBar + 10 + wrap));
  }
  TEST_ASSERT_EQUAL_UINT64(requiredAfterFirst, coverage.required);
}

// A held note carried across a tile boundary adds exactly that tile, and only then.
void test_riao4_2_state_held_advance_adds_the_crossed_tile() {
  CoverageState coverage;
  coverage.beginSession(kLoop31Bars, 11u);
  TEST_ASSERT_EQUAL_UINT8(1, coverage.markHeldAdvance(0 * kBar + 10));  // tile 0 becomes required
  TEST_ASSERT_TRUE(coverage.tileRequired(0));
  const uint64_t before = coverage.required;
  // Still inside tile 0: nothing new.
  for (uint32_t tick = kBar; tick < 4 * kBar; ++tick) {
    TEST_ASSERT_EQUAL_UINT8(0, coverage.markHeldAdvance(tick));
  }
  TEST_ASSERT_EQUAL_UINT64(before, coverage.required);
  // Crossing into tile 1 marks it.
  TEST_ASSERT_EQUAL_UINT8(1, coverage.markHeldAdvance(4 * kBar));
  TEST_ASSERT_TRUE(coverage.tileRequired(1));
}

// Revisiting an already required (and prepared) tile re-requires and re-derives nothing.
void test_riao4_2_state_revisiting_a_prepared_tile_is_free() {
  CoverageState coverage;
  coverage.beginSession(kLoop31Bars, 11u);
  (void)coverage.markNoteOn(2 * kBar);
  coverage.publishPrepared(0);
  TEST_ASSERT_TRUE(coverage.tilePrepared(0));
  TEST_ASSERT_EQUAL_UINT8(0, coverage.markNoteOn(2 * kBar + 50));
  TEST_ASSERT_TRUE(coverage.tilePrepared(0));
  TEST_ASSERT_EQUAL_UINT8(0, coverage.owedCount());
}

// NoteOff seals the whole span, including the wrap case.
void test_riao4_2_state_span_seal_covers_wrap() {
  CoverageState coverage;
  coverage.beginSession(kLoop31Bars, 11u);
  TEST_ASSERT_EQUAL_UINT8(2, coverage.markSpanSealed(kLoop31Bars - kBar / 2, kBar));
  TEST_ASSERT_TRUE(coverage.tileRequired(7));
  TEST_ASSERT_TRUE(coverage.tileRequired(0));
}

// Generation change: required survives, prepared is re-owed.
void test_riao4_2_state_generation_change_re_owes_required() {
  CoverageState coverage;
  coverage.beginSession(kLoop31Bars, 3u);
  (void)coverage.markNoteOn(2 * kBar);
  (void)coverage.markNoteOn(27 * kBar);
  coverage.publishPrepared(0);
  coverage.publishPrepared(6);
  TEST_ASSERT_EQUAL_UINT8(0, coverage.owedCount());
  coverage.changeGeneration(4u);
  TEST_ASSERT_EQUAL_UINT32(4u, coverage.generation);
  TEST_ASSERT_EQUAL_UINT8(2, coverage.owedCount());          // both re-owed
  TEST_ASSERT_TRUE(coverage.tileRequired(0));                 // required preserved
  TEST_ASSERT_FALSE(coverage.tilePrepared(0));                // prepared invalidated
}

// Preparation never reports ready on partial coverage.
void test_riao4_2_state_span_ready_only_when_all_tiles_prepared() {
  CoverageState coverage;
  coverage.beginSession(kLoop31Bars, 3u);
  const uint32_t start = 3 * kBar;      // tiles 0 and 1
  const uint32_t length = 2 * kBar;
  (void)coverage.markSpanSealed(start, length);
  TEST_ASSERT_FALSE(coverage.spanPrepared(start, length));
  coverage.publishPrepared(0);
  TEST_ASSERT_FALSE_MESSAGE(coverage.spanPrepared(start, length), "partial coverage is not ready");
  coverage.publishPrepared(1);
  TEST_ASSERT_TRUE(coverage.spanPrepared(start, length));
}

// Worker selection walks owed tiles only and reports "none" when the set is clean.
void test_riao4_2_state_worker_selects_owed_tiles() {
  CoverageState coverage;
  coverage.beginSession(kLoop31Bars, 3u);
  TEST_ASSERT_EQUAL_UINT16(kMaxTiles, coverage.nextOwedTile(0));
  (void)coverage.markNoteOn(2 * kBar);    // tile 0
  (void)coverage.markNoteOn(27 * kBar);   // tile 6
  TEST_ASSERT_EQUAL_UINT16(0, coverage.nextOwedTile(0));
  coverage.publishPrepared(0);
  TEST_ASSERT_EQUAL_UINT16(6, coverage.nextOwedTile(0));
  coverage.publishPrepared(6);
  TEST_ASSERT_EQUAL_UINT16(kMaxTiles, coverage.nextOwedTile(0));
}

// --- Execution grain (4.1b) ------------------------------------------------------------------------

void test_riao4_1b_grain_is_events_and_operations_not_tiles() {
  GrainCursor cursor;
  TEST_ASSERT_FALSE(grainExhausted(cursor));
  cursor.events = kGrainEventsPerTurn;
  TEST_ASSERT_TRUE(grainExhausted(cursor));  // a dense tile yields here regardless of tile index
  GrainCursor byOps;
  byOps.operations = kGrainOperationsPerTurn;
  TEST_ASSERT_TRUE(grainExhausted(byOps));
  GrainCursor partial;
  partial.events = kGrainEventsPerTurn - 1;
  partial.operations = kGrainOperationsPerTurn - 1;
  TEST_ASSERT_FALSE(grainExhausted(partial));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_riao4_1b_tile_count_and_envelope);
  RUN_TEST(test_riao4_1b_tile_index_is_loop_relative);
  RUN_TEST(test_riao4_1b_span_marks_every_touched_tile_including_wrap);
  RUN_TEST(test_riao4_2_sparse_touches_do_not_require_the_middle);
  RUN_TEST(test_riao4_1a_many_hold_ticks_coalesce_to_one_obligation);
  RUN_TEST(test_riao4_1a_obligation_survives_the_playhead_leaving_the_tile);
  RUN_TEST(test_riao4_1a_occupancy_owed_then_note_completes);
  RUN_TEST(test_riao4_1a_generation_change_re_owes_obligations);
  RUN_TEST(test_riao4_1a_obligation_set_is_bounded);
  RUN_TEST(test_riao4_1b_grain_is_events_and_operations_not_tiles);
  RUN_TEST(test_riao4_2_state_sparse_touches_leave_the_middle_unrequired);
  RUN_TEST(test_riao4_2_state_repeated_wraps_add_nothing);
  RUN_TEST(test_riao4_2_state_held_advance_adds_the_crossed_tile);
  RUN_TEST(test_riao4_2_state_revisiting_a_prepared_tile_is_free);
  RUN_TEST(test_riao4_2_state_span_seal_covers_wrap);
  RUN_TEST(test_riao4_2_state_generation_change_re_owes_required);
  RUN_TEST(test_riao4_2_state_span_ready_only_when_all_tiles_prepared);
  RUN_TEST(test_riao4_2_state_worker_selects_owed_tiles);
  RUN_TEST(test_riao4_3_closure_pad_is_the_max_committed_span);
  RUN_TEST(test_riao4_3_closure_window_widens_backwards_and_wraps);
  return UNITY_END();
}
