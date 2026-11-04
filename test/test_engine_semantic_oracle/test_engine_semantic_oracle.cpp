//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E0: semantic fixtures for the overlap contract (register clauses OVL-0..8) against the
// independent reference model, and the CE-B1 window effective-row counter-example.
//
// Every fixture's expectation is written by hand from the clause text. Status of all fixtures here: DRAFT (owner review
// pending, register § Review). Legacy output is not used as an expectation.

#include <unity.h>

#include <cstdio>
#include <vector>

#include "reference_model.h"

using namespace Oracle;

constexpr uint32_t kLoop = 768 * 4;  // 4-bar loop for geometry fixtures

static Effective fateOf(const std::vector<Effective>& eff, uint32_t id) {
  for (const auto& e : eff)
    if (e.note.id == id) return e;
  TEST_FAIL_MESSAGE("note id missing");
  return {};
}

void setUp() {}
void tearDown() {}

// OVL-1: A sounding when later B starts → A ends at B.on - 1 (storage Off tick, not exclusive end).
void test_ovl1_sounding_earlier_note_is_shortened_to_hold_minus_one() {
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 400}, {2, 1, 0, 60, 168, 300}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Shortened, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL_UINT32(167, fateOf(eff, 1).note.off);
  TEST_ASSERT_EQUAL(Fate::Kept, fateOf(eff, 2).fate);
}

// OVL-2: A starts at B's start → hidden.
void test_ovl2_equal_start_hides() {
  const std::vector<Note> n = {{1, 0, 0, 60, 200, 400}, {2, 1, 0, 60, 200, 300}};
  TEST_ASSERT_EQUAL(Fate::Hidden, fateOf(effective(n, kLoop), 1).fate);
}

// OVL-3: A starts strictly inside B → hidden; A starting at B's Off tick is not inside.
void test_ovl3_start_strictly_inside_hides_but_not_at_b_off() {
  const std::vector<Note> n = {{1, 0, 0, 60, 250, 500}, {3, 0, 0, 60, 300, 350}, {2, 1, 0, 60, 200, 300}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Hidden, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL(Fate::Kept, fateOf(eff, 3).fate);
}

// OVL-4: boundary touch (A.off == B.on) leaves A unchanged.
void test_ovl4_boundary_touch_unchanged() {
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 200}, {2, 1, 0, 60, 200, 300}};
  TEST_ASSERT_EQUAL(Fate::Kept, fateOf(effective(n, kLoop), 1).fate);
}

// OVL-5 (DEC-033): channel is ignored; pitch alone decides.
void test_ovl5_channel_ignored() {
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 400}, {2, 1, 9, 60, 168, 300}, {3, 0, 0, 61, 100, 400}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Shortened, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL(Fate::Kept, fateOf(eff, 3).fate);
}

// OVL-6: a remnant shorter than the minimum length is hidden.
void test_ovl6_min_length_remnant_hides() {
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 400}, {2, 1, 0, 60, 105, 300}};
  TEST_ASSERT_EQUAL(Fate::Hidden, fateOf(effective(n, kLoop), 1).fate);
}

// OVL-7: several later notes compose monotonically: the earliest shortening wins; a hide absorbs a shorten.
void test_ovl7_monotonic_composition() {
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 900}, {2, 1, 0, 60, 500, 520}, {3, 2, 0, 60, 300, 320},
                               {4, 0, 0, 60, 600, 700}, {5, 1, 0, 60, 550, 650}, {6, 2, 0, 60, 600, 610}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Shortened, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL_UINT32(299, fateOf(eff, 1).note.off);
  TEST_ASSERT_EQUAL(Fate::Hidden, fateOf(eff, 4).fate);  // inside 5 (hide) and equal start with 6 (hide)
}

// OVL-8: B's own fate does not cancel its effect on earlier runs. C (run 2) hides B (run 1); A (run 0) stays shortened
// by B. Same-pitch overlaps inside one run do not occlude each other (register question Q-OVL-1, draft).
void test_ovl8_effect_survives_later_hide_and_same_run_overlap_is_kept() {
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 600}, {2, 1, 0, 60, 300, 500}, {3, 2, 0, 60, 300, 400},
                               {4, 0, 1, 64, 100, 300}, {5, 0, 2, 64, 200, 400}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Shortened, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL_UINT32(299, fateOf(eff, 1).note.off);
  TEST_ASSERT_EQUAL(Fate::Hidden, fateOf(eff, 2).fate);
  TEST_ASSERT_EQUAL(Fate::Kept, fateOf(eff, 4).fate);
  TEST_ASSERT_EQUAL(Fate::Kept, fateOf(eff, 5).fate);
}

// OVL-1 wrap: a wrap-held A (off < on) with B starting in its head is shortened to B.on - 1; B starting in its tail is
// an ordinary shorten. OVL-3 wrap: a wrap-crossing B hides A starting in either of its linear segments.
void test_ovl_wrap_crossing() {
  const std::vector<Note> n = {{1, 0, 0, 60, 3000, 200}, {2, 1, 0, 60, 100, 150},
                               {3, 0, 0, 62, 50, 90},   {4, 1, 0, 62, 3050, 100}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Shortened, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL_UINT32(99, fateOf(eff, 1).note.off);
  TEST_ASSERT_EQUAL(Fate::Hidden, fateOf(eff, 3).fate);
}

// DEC-244: at one tick, NoteOff before NoteOn, then pass order. A boundary-touch pair emits A's Off before B's On.
void test_dec244_equal_tick_order_in_window() {
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 200}, {2, 1, 0, 60, 200, 300}, {3, 0, 0, 64, 200, 250}};
  const auto rows = windowRows(effective(n, kLoop), 0, 768);
  // tick 200: Off(1) then On(3, run 0) then On(2, run 1)
  size_t i = 0;
  while (rows[i].tick != 200) ++i;
  TEST_ASSERT_EQUAL_UINT8(0, rows[i].cls);
  TEST_ASSERT_EQUAL_UINT32(1, rows[i].noteId);
  TEST_ASSERT_EQUAL_UINT32(3, rows[i + 1].noteId);
  TEST_ASSERT_EQUAL_UINT32(2, rows[i + 2].noteId);
}

// CE-B1 counter-example. In one 2-bar window, k earlier runs each hold 128 notes (16 channels x 8 pitches) that start in
// the window and end after it; each later run starts its batch while the previous batch sounds. Every batch but the last
// is shortened inside the window, so each contributes a generated NoteOff whose source NoteOff lies outside the window.
// Effective rows = source rows + 128 (k - 1): the 128-sounding-note limit does not bound generated rows.
void test_ceb1_window_effective_rows_exceed_source_plus_polyphony() {
  constexpr uint32_t kWindow = 1536;
  constexpr uint32_t kBatches = 20;
  std::vector<Note> n;
  uint32_t id = 1;
  for (uint32_t run = 0; run < kBatches; ++run)
    for (uint8_t ch = 0; ch < 16; ++ch)
      for (uint8_t p = 0; p < 8; ++p)
        n.push_back({id++, static_cast<uint8_t>(run), ch, static_cast<uint8_t>(60 + p), 10 + run * 60, 2000});
  const auto eff = effective(n, kLoop);
  const auto rows = windowRows(eff, 0, kWindow);
  const uint32_t source = sourceRowsIn(n, 0, kWindow);
  uint32_t generated = 0;
  for (const auto& r : rows) generated += r.generated ? 1u : 0u;
  char line[160];
  std::snprintf(line, sizeof line, "CE-B1: source rows in window=%u effective rows=%u generated NoteOffs=%u",
                static_cast<unsigned>(source), static_cast<unsigned>(rows.size()), static_cast<unsigned>(generated));
  TEST_MESSAGE(line);
  TEST_ASSERT_EQUAL_UINT32(kBatches * 128, source);
  TEST_ASSERT_EQUAL_UINT32(source + 128 * (kBatches - 1), static_cast<uint32_t>(rows.size()));
  // Source admission (2,816) is respected, yet the effective rows exceed the 3,072-row window buffer.
  TEST_ASSERT_TRUE(source <= 2816);
  TEST_ASSERT_TRUE(rows.size() > 3072);
}

// The exact identity behind any bound: effective = source - hidden rows - suppressed source NoteOffs + generated.
void test_ceb1_effective_row_identity_holds() {
  constexpr uint32_t kWindow = 1536;
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 2000}, {2, 1, 0, 60, 300, 400},  {3, 0, 0, 62, 500, 900},
                               {4, 1, 0, 62, 500, 600},  {5, 0, 0, 64, 700, 1000}, {6, 1, 0, 64, 650, 800}};
  const auto eff = effective(n, kLoop);
  const auto rows = windowRows(eff, 0, kWindow);
  uint32_t hiddenRows = 0, suppressedOffs = 0, generated = 0;
  for (const auto& e : eff) {
    const Note* src = nullptr;
    for (const auto& x : n)
      if (x.id == e.note.id) src = &x;
    const bool onIn = src->on < kWindow, offIn = src->off < kWindow;
    if (e.fate == Fate::Hidden) hiddenRows += (onIn ? 1u : 0u) + (offIn ? 1u : 0u);
    if (e.fate == Fate::Shortened) {
      suppressedOffs += offIn ? 1u : 0u;
      generated += e.note.off < kWindow ? 1u : 0u;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(sourceRowsIn(n, 0, kWindow) - hiddenRows - suppressedOffs + generated,
                           static_cast<uint32_t>(rows.size()));
}

// Q-OVL-1 (owner default): within one pass, independently recorded notes keep their identities; occlusion is between
// later and earlier passes only. Two overlapping same-pitch notes of one Record pass both survive unchanged.
void test_qovl1_same_pass_overlap_keeps_both_notes() {
  const std::vector<Note> n = {{1, 0, 0, 60, 100, 400}, {2, 0, 0, 60, 200, 300}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Kept, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL(Fate::Kept, fateOf(eff, 2).fate);
}

// Q-OVL-2 (owner default): matching ignores channel (DEC-033) even when a same-channel note is also sounding; emission
// keeps each note's own channel. Legacy picks occupy candidates from the ledger by channel first and skips the
// geometric lookup when the ledger set is complete, so the channel-2 note escapes (HIB-9).
void test_qovl2_other_channel_shortened_even_with_same_channel_sounding() {
  const std::vector<Note> n = {{1, 0, 1, 60, 100, 600}, {2, 0, 2, 60, 150, 700}, {3, 1, 1, 60, 300, 400}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Shortened, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL(Fate::Shortened, fateOf(eff, 2).fate);
  TEST_ASSERT_EQUAL_UINT32(299, fateOf(eff, 2).note.off);
  TEST_ASSERT_EQUAL_UINT8(2, fateOf(eff, 2).note.channel);
}

// Q-OVL-3 (owner default): a later note starting at tick 0 inside a wrap-held note's head keeps the pre-wrap portion.
// Legacy `resolveOccupancyAtNoteOn` requires holdPhaseTick > 0 and hides the note (HIB-8).
void test_qovl3_hold_at_zero_keeps_pre_wrap_portion() {
  const std::vector<Note> n = {{1, 0, 0, 60, 3000, 200}, {2, 1, 0, 60, 0, 100}};
  const auto eff = effective(n, kLoop);
  TEST_ASSERT_EQUAL(Fate::Shortened, fateOf(eff, 1).fate);
  TEST_ASSERT_EQUAL_UINT32(kLoop - 1, fateOf(eff, 1).note.off);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ovl1_sounding_earlier_note_is_shortened_to_hold_minus_one);
  RUN_TEST(test_ovl2_equal_start_hides);
  RUN_TEST(test_ovl3_start_strictly_inside_hides_but_not_at_b_off);
  RUN_TEST(test_ovl4_boundary_touch_unchanged);
  RUN_TEST(test_ovl5_channel_ignored);
  RUN_TEST(test_ovl6_min_length_remnant_hides);
  RUN_TEST(test_ovl7_monotonic_composition);
  RUN_TEST(test_ovl8_effect_survives_later_hide_and_same_run_overlap_is_kept);
  RUN_TEST(test_ovl_wrap_crossing);
  RUN_TEST(test_dec244_equal_tick_order_in_window);
  RUN_TEST(test_ceb1_window_effective_rows_exceed_source_plus_polyphony);
  RUN_TEST(test_ceb1_effective_row_identity_holds);
  RUN_TEST(test_qovl1_same_pass_overlap_keeps_both_notes);
  RUN_TEST(test_qovl2_other_channel_shortened_even_with_same_channel_sounding);
  RUN_TEST(test_qovl3_hold_at_zero_keeps_pre_wrap_portion);
  return UNITY_END();
}
