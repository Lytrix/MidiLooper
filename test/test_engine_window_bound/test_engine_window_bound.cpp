//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine CE-B1: span windows with fixed-capacity NoteOff terminals.
// Proves, on the reference model, that window entries <= start rows in the window + T (T = admitted sounding notes),
// independent of how many passes stack; that the consumer reproduces the expanded-row stream exactly (DEC-244 order,
// pitch/channel identity, wrap notes, notes held across windows, DEC-148 cold start); and records the comparison with the
// three row-based proposals of design.md on the CE-B1 counter-example.

#include <unity.h>

#include <cstdio>
#include <vector>

#include "ContentEngine/EngineConfig.h"
#include "span_window_model.h"

using namespace Oracle;
using namespace SpanModel;

constexpr uint32_t kWindow = 1536;        // 2 bars
constexpr uint32_t kSourceAdmission = ContentEngine::Config::kWindowSourceAdmission;
constexpr uint32_t kT = ContentEngine::Config::kMaxSoundingPerTrack;  // OUT-POLY, owner-ratified 16
constexpr uint32_t kWindowCapacity = ContentEngine::Config::kWindowEntries;

static uint32_t gSeed = 0xB1B1B1u;
static uint32_t rnd() {
  gSeed ^= gSeed << 13;
  gSeed ^= gSeed >> 17;
  gSeed ^= gSeed << 5;
  return gSeed;
}

// Source start rows (NoteOns, before occlusion) in each window of the timeline, all passes counted.
static std::vector<uint32_t> sourceStarts(const std::vector<Note>& n, uint32_t loop, uint32_t cycles) {
  std::vector<uint32_t> per((loop * cycles + kWindow - 1) / kWindow, 0);
  for (uint32_t c = 0; c < cycles; ++c)
    for (const auto& x : n) ++per[(c * loop + x.on) / kWindow];
  return per;
}

static std::vector<Note> counterExample(uint32_t batches) {
  std::vector<Note> n;
  uint32_t id = 1;
  for (uint32_t run = 0; run < batches; ++run)
    for (uint8_t ch = 0; ch < 16; ++ch)
      for (uint8_t p = 0; p < 8; ++p)
        n.push_back({id++, static_cast<uint8_t>(run), ch, static_cast<uint8_t>(60 + p), 10 + run * 60, 2000});
  return n;
}

void setUp() {}
void tearDown() {}

// The counter-example that broke the row-based bound: in span form it needs only START entries for the instances that
// actually sound. With OUT-POLY (16 sounding per track) most of the 2,560 stacked NoteOns are suppressed at output by name;
// the stored passes are untouched and the composed and consumed streams stay identical.
void test_span_windows_absorb_the_counterexample() {
  const uint32_t loop = 768 * 4;
  const auto notes = counterExample(20);
  const auto tl = timeline(effective(notes, loop), loop, 1);
  const auto windows = composeWindows(tl, loop, kWindow, kT);
  TEST_ASSERT_EQUAL_UINT32(2560, windows[0].startEntries + windows[0].refused);
  TEST_ASSERT_EQUAL_UINT32(0, windows[0].terminalEntries);
  TEST_ASSERT_TRUE(windows[0].entries.size() <= kSourceAdmission + kT);
  bool overflow = true;
  const auto stream = consume(windows, 0, kT, overflow);
  TEST_ASSERT_FALSE(overflow);
  TEST_ASSERT_TRUE(stream == expandedStream(tl, 0, loop, kT));
  TEST_ASSERT_EQUAL_UINT32(2560, static_cast<uint32_t>(notes.size()));  // storage unchanged
}

// Bound and exactness under random stacking: up to 40 passes, same-pitch overlaps on several channels, long notes
// crossing windows and the loop end, three cycles.
void test_span_window_bound_and_stream_equality_random() {
  const uint32_t loop = 768 * 8;
  const uint32_t cycles = 3;
  uint32_t refusedWhenBinding = 0, terminalsSeen = 0;
  for (uint32_t trial = 0; trial < 60; ++trial) {
    std::vector<Note> n;
    const uint32_t passes = 1 + rnd() % 40;
    uint32_t id = 1;
    for (uint32_t run = 0; run < passes; ++run) {
      const uint32_t notesInPass = rnd() % 120;
      for (uint32_t k = 0; k < notesInPass; ++k) {
        const uint32_t on = rnd() % loop;
        uint32_t len = 1 + rnd() % (rnd() % 4 == 0 ? 6000 : 400);
        if (len >= loop) len = loop - 1;
        const uint32_t off = (on + len) % loop;
        n.push_back({id++, static_cast<uint8_t>(run), static_cast<uint8_t>(rnd() % 4),
                     static_cast<uint8_t>(48 + rnd() % 12), on, off});
      }
    }
    const auto eff = effective(n, loop);
    const auto tl = timeline(eff, loop, cycles);
    const uint32_t to = loop * cycles;
    const auto starts = sourceStarts(n, loop, cycles);
    for (uint32_t T : {kT, 8u}) {  // both bind under dense random stacking
      const auto windows = composeWindows(tl, to, kWindow, T);
      for (size_t w = 0; w < windows.size(); ++w) {
        TEST_ASSERT_TRUE(windows[w].terminalEntries <= T);
        TEST_ASSERT_TRUE(windows[w].startEntries <= starts[w]);
        TEST_ASSERT_TRUE(windows[w].entries.size() <= starts[w] + T);
        if (T == kT) refusedWhenBinding += windows[w].refused;
        terminalsSeen += windows[w].terminalEntries;
      }
      bool overflow = true;
      TEST_ASSERT_TRUE(consume(windows, 0, T, overflow) == expandedStream(tl, 0, to, T));
      TEST_ASSERT_FALSE(overflow);
    }
    // DEC-148 cold start at a window boundary (admission not binding): notes started earlier are not sounded and their
    // TERMINAL entries are ignored.
    const auto windows = composeWindows(tl, to, kWindow, 100000);
    const uint32_t from = kWindow * (1 + rnd() % 8);
    bool overflow = true;
    TEST_ASSERT_TRUE(consume(windows, from, 100000, overflow) == expandedStream(tl, from, to, 100000));
  }
  TEST_ASSERT_TRUE(refusedWhenBinding > 0);  // the admission policy was exercised
  TEST_ASSERT_TRUE(terminalsSeen > 0);       // carried terminals were exercised
}

// The bound is tight: T instances sounding across the window start that end inside it, plus a full window of admitted
// START entries (groups of 16, so OUT-POLY admits every one): 2,816 + 16 = 2,832 <= 3,072.
void test_span_window_bound_is_tight() {
  const uint32_t loop = 768 * 8;
  std::vector<Note> n;
  uint32_t id = 1;
  for (uint32_t k = 0; k < kT; ++k)  // run 0: T notes on distinct pitches, crossing into window 1
    n.push_back({id++, 0, 0, static_cast<uint8_t>(k), 100, kWindow + 50});
  for (uint32_t k = 0; k < kSourceAdmission; ++k)  // run 1: 16 one-tick notes every 2 ticks after those end
    n.push_back({id++, 1, 0, static_cast<uint8_t>(32 + k % kT), kWindow + 60 + (k / kT) * 2,
                 kWindow + 61 + (k / kT) * 2});
  const auto tl = timeline(effective(n, loop), loop, 1);
  const auto windows = composeWindows(tl, loop, kWindow, kT);
  TEST_ASSERT_EQUAL_UINT32(kT, windows[1].terminalEntries);
  TEST_ASSERT_EQUAL_UINT32(kSourceAdmission, windows[1].startEntries);
  TEST_ASSERT_EQUAL_UINT32(0, windows[1].refused);
  TEST_ASSERT_EQUAL_UINT32(2832, static_cast<uint32_t>(windows[1].entries.size()));
  TEST_ASSERT_TRUE(windows[1].entries.size() <= kWindowCapacity);
}

// A note held across windows: START with an open end, nothing in between, one TERMINAL where it ends.
void test_held_note_crosses_windows_with_one_terminal() {
  const uint32_t loop = 768 * 8;
  const std::vector<Note> n = {{1, 0, 3, 60, 100, 4 * kWindow - 7}};
  const auto windows = composeWindows(timeline(effective(n, loop), loop, 1), loop, kWindow, kT);
  TEST_ASSERT_EQUAL_UINT32(kOpenEnd, windows[0].entries[0].end);
  TEST_ASSERT_EQUAL_UINT32(0, windows[1].entries.size());
  TEST_ASSERT_EQUAL_UINT32(0, windows[2].entries.size());
  TEST_ASSERT_EQUAL_UINT32(1, windows[3].terminalEntries);
  TEST_ASSERT_EQUAL_UINT8(3, windows[3].entries[0].channel);
}

// A carried note shortened by a later pass inside the next window: its generated NoteOff is the TERMINAL entry; the
// occluding NoteOn follows at the next tick (DEC-244 via distinct ticks), same pitch, channels preserved (Q-OVL-2).
void test_carried_note_shortened_in_next_window() {
  const uint32_t loop = 768 * 8;
  const std::vector<Note> n = {{1, 0, 2, 60, 100, 5000}, {2, 1, 9, 60, 2000, 2100}};
  const auto tl = timeline(effective(n, loop), loop, 1);
  const auto windows = composeWindows(tl, loop, kWindow, kT);
  TEST_ASSERT_EQUAL_UINT32(1, windows[1].terminalEntries);
  bool overflow = true;
  const auto s = consume(windows, 0, kT, overflow);
  TEST_ASSERT_EQUAL_UINT32(4, s.size());
  TEST_ASSERT_EQUAL_UINT32(1999, s[1].tick);  // generated NoteOff of note 1
  TEST_ASSERT_EQUAL_UINT8(2, s[1].channel);
  TEST_ASSERT_EQUAL_UINT32(2000, s[2].tick);
  TEST_ASSERT_EQUAL_UINT8(9, s[2].channel);
}

// Comparison with design.md's row-based proposals on the counter-example (k stacked batches of 128 long notes).
void test_row_based_proposals_on_counterexample() {
  const uint32_t batches = 20;
  const uint32_t rowsEffective = 128 * batches + 128 * (batches - 1);  // measured in test_engine_semantic_oracle
  // (1) effective-count admission / (2) bounding stacked passes: largest b with 128b + 128(b-1) <= 3,072.
  uint32_t b = 0;
  while (128 * (b + 1) + 128 * b <= kWindowCapacity) ++b;
  const uint32_t refusedNotes = (batches - b) * 128;
  // (3) split buffers: row-based worst case is effective <= 2 x source starts.
  const uint32_t splitRows = 2 * kSourceAdmission;
  const uint32_t splitExtraBytes = 24u * (splitRows - kWindowCapacity) * 16u;
  char line[220];
  std::snprintf(line, sizeof line,
                "CE-B1 compare: rows=%u; admission/pass-bound admit %u of %u batches (refuse %u stored notes); split "
                "buffers need %u rows/window (+%u B PSRAM); span: <= %u entries, storage never refused",
                rowsEffective, b, batches, refusedNotes, splitRows, splitExtraBytes, kSourceAdmission + kT);
  TEST_MESSAGE(line);
  TEST_ASSERT_EQUAL_UINT32(4992, rowsEffective);
  TEST_ASSERT_EQUAL_UINT32(12, b);
}

// Effective-polyphony analysis, lemma: under OVL-1..3 two effective notes of the same pitch from different passes never
// sound at the same tick (the later one shortens or hides the earlier). Checked over random stacked passes.
void test_same_pitch_notes_of_different_passes_never_overlap() {
  const uint32_t loop = 768 * 8;
  for (uint32_t trial = 0; trial < 40; ++trial) {
    std::vector<Note> n;
    uint32_t id = 1;
    const uint32_t passes = 2 + rnd() % 20;
    for (uint32_t run = 0; run < passes; ++run)
      for (uint32_t k = 0, c = rnd() % 60; k < c; ++k) {
        const uint32_t on = rnd() % (loop - 3000);
        n.push_back({id++, static_cast<uint8_t>(run), static_cast<uint8_t>(rnd() % 3),
                     static_cast<uint8_t>(60 + rnd() % 4), on, on + 1 + rnd() % 2500});
      }
    const auto eff = effective(n, loop);
    for (size_t i = 0; i < eff.size(); ++i)
      for (size_t j = 0; j < eff.size(); ++j) {
        const auto &a = eff[i], &b = eff[j];
        if (a.fate == Fate::Hidden || b.fate == Fate::Hidden || a.note.pitch != b.note.pitch ||
            a.note.run >= b.note.run)
          continue;
        const bool overlap = a.note.on < b.note.off && b.note.on < a.note.off;
        TEST_ASSERT_FALSE(overlap);
      }
  }
}

// Source polyphony above T: three passes of 128 notes on three pitches sound 384 notes at once. OUT-POLY (T = 16,
// owner-ratified) sends the first 16 in DEC-244 order and suppresses the rest at output by name; stored content is
// unchanged and the composed and consumed streams stay identical.
void test_polyphony_exceeds_t_and_out_poly_refuses_by_name() {
  const uint32_t loop = 768 * 8;
  std::vector<Note> n;
  uint32_t id = 1;
  for (uint8_t run = 0; run < 3; ++run)
    for (uint8_t ch = 0; ch < 16; ++ch)
      for (uint8_t k = 0; k < 8; ++k) n.push_back({id++, run, ch, static_cast<uint8_t>(40 + run), 100u + k, 1000});
  const auto eff = effective(n, loop);
  uint32_t effectiveSounding = 0;
  for (const auto& e : eff)
    if (e.fate != Fate::Hidden && e.note.on <= 200 && 200 < e.note.off) ++effectiveSounding;
  TEST_ASSERT_EQUAL_UINT32(384, effectiveSounding);
  const auto tl = timeline(eff, loop, 1);
  const auto windows = composeWindows(tl, loop, kWindow, kT);
  TEST_ASSERT_EQUAL_UINT32(384 - kT, windows[0].refused);
  bool overflow = true;
  TEST_ASSERT_TRUE(consume(windows, 0, kT, overflow) == expandedStream(tl, 0, loop, kT));
  TEST_ASSERT_FALSE(overflow);
  TEST_ASSERT_EQUAL_UINT32(384, static_cast<uint32_t>(n.size()));  // stored content untouched
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_span_windows_absorb_the_counterexample);
  RUN_TEST(test_span_window_bound_and_stream_equality_random);
  RUN_TEST(test_span_window_bound_is_tight);
  RUN_TEST(test_held_note_crosses_windows_with_one_terminal);
  RUN_TEST(test_carried_note_shortened_in_next_window);
  RUN_TEST(test_row_based_proposals_on_counterexample);
  RUN_TEST(test_same_pitch_notes_of_different_passes_never_overlap);
  RUN_TEST(test_polyphony_exceeds_t_and_out_poly_refuses_by_name);
  return UNITY_END();
}
