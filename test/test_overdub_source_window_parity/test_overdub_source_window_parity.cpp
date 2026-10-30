// `realtime-idle-owner-admission-bound` slice 2 (packet RIAO-2) — windowed vs whole-loop source parity.
//
// These are the gates that decide whether `rebuildOverdubSourceView` may stop deriving the whole pass:
//
//   A. an ordinary window's notes are identical to the whole-loop derivation restricted to that window;
//   B. a window that crosses the loop end keeps the wrap pair (design D3's stated UNKNOWN);
//   C. repeated / simultaneous same-pitch notes keep distinct identities through a window;
//   D. the **assembled** multi-window cache equals the whole-loop derivation (design D3a) -- per-window
//      correctness does not imply this, so it is a separate gate before `fullLoop=true`;
//   E. a boundary-crossing note appears exactly once across two adjacent windows' assembly.
//
// Deterministic by construction: no wall clock, no host timing. Geometry is compared as
// (noteId, pitch, startTick, endTick) tuples.
//
// Borrow the real display/storage/track closure the way the sibling integration suites do, so this
// suite exercises production `LoopContentResolution` over production `Loop` state rather than a model.

#define main borrowedLiveCaptureDisplayBoundaryMain
#include "../test_live_capture_display_boundary/test_live_capture_display_boundary.cpp"
#undef main

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <tuple>
#include <vector>

#include "Globals.h"
#include "CommittedEventRange.h"
#include "Loop.h"
#include "LoopContentResolution.h"
#include "LoopEventStore.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/CoveragePrerequisite.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"

namespace {

constexpr uint32_t kBar = Config::TICKS_PER_BAR;
constexpr uint32_t kLoopBars = 8;
constexpr uint32_t kLoopTicks = kLoopBars * kBar;

using Key = std::tuple<uint32_t, uint8_t, uint32_t, uint32_t>;  // noteId, pitch, start, end

Key keyOf(const NoteUtils::DisplayNote& n) {
  return Key{n.noteId, n.note, n.startTick, n.endTick};
}

std::vector<Key> keysOf(const NoteUtils::DisplayNoteVec& notes) {
  std::vector<Key> out;
  out.reserve(notes.size());
  for (const auto& n : notes) out.push_back(keyOf(n));
  std::sort(out.begin(), out.end());
  return out;
}

void appendPair(LoopEventStore& store, NoteId id, uint8_t pitch, uint32_t onTick, uint32_t offTick) {
  MidiEvent on = MidiEvent::NoteOn(onTick, 1, pitch, 100);
  on.noteId = id;
  TEST_ASSERT_TRUE(store.append(on));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, 1, pitch, 0)));
}

bool intersects(uint32_t startTick, uint32_t endTick, uint32_t windowStart, uint32_t windowLength) {
  return DisplayWindowUtils::noteIntersectsWindow(startTick, endTick, windowStart, windowLength,
                                                  kLoopTicks);
}

std::vector<Key> restrictedTo(const NoteUtils::DisplayNoteVec& notes, uint32_t windowStart,
                              uint32_t windowLength) {
  std::vector<Key> out;
  for (const auto& n : notes) {
    if (intersects(n.startTick, n.endTick, windowStart, windowLength)) out.push_back(keyOf(n));
  }
  std::sort(out.begin(), out.end());
  return out;
}

/// Record pass (5 notes, incl. a same-pitch pair, a wrap note) + one overdub pass with one note that
/// duplicates the record's pitch-67 geometry under a different NoteId.
void seedParityLoop(Loop& loop) {
  LoopEventStore record;
  appendPair(record, 1001, 60, 100, 300);                              // inside bar 0
  appendPair(record, 1002, 62, 3 * kBar + 100, 3 * kBar + 400);        // inside bar 3
  appendPair(record, 1003, 64, 2 * kBar - 100, 2 * kBar + 100);        // crosses bar2/bar3
  appendPair(record, 1004, 60, 200, 500);                              // same pitch, later start
  appendPair(record, 1005, 67, 5 * kBar + 200, 5 * kBar + 400);        // geometry twin of 2002
  appendPair(record, 1006, 72, kLoopTicks - 100, 100);                 // loop-wrap note
  loop.seedRecordPassFromStore(record);
  loop.loopLengthTicks = kLoopTicks;

  LoopEventStore overdub;
  appendPair(overdub, 2001, 69, 4 * kBar + 50, 4 * kBar + 250);
  appendPair(overdub, 2002, 67, 5 * kBar + 200, 5 * kBar + 400);       // equal geometry to 1005
  OverdubPass pass{};
  pass.id = 101;
  pass.mergeSequence = 1;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(overdub, pass.committedChunkIds));
  loop.passes.overdubPasses.push_back(std::move(pass));
  loop.markDisplayCachesStale();
}

/// A fresh loop over a fresh pool per case. `Loop` is non-copyable, so each helper builds its own.
struct SeededLoop {
  Loop loop;
  SeededLoop() {
    LoopEventStore::resetPoolForTests();
    LoopEventStore::initPool();
    seedParityLoop(loop);
  }
};

std::vector<Key> wholeLoopKeys() {
  SeededLoop seeded;
  NoteUtils::DisplayNoteVec whole;
  LoopContentResolution::resolveDisplayNotes(seeded.loop.passes, seeded.loop.loopLengthTicks, whole);
  return keysOf(whole);
}

NoteUtils::DisplayNoteVec wholeLoopNotes() {
  SeededLoop seeded;
  NoteUtils::DisplayNoteVec whole;
  LoopContentResolution::resolveDisplayNotes(seeded.loop.passes, seeded.loop.loopLengthTicks, whole);
  return whole;
}

std::vector<Key> windowKeysWithPad(uint32_t windowStart, uint32_t windowLength, uint32_t padTicks) {
  SeededLoop seeded;
  // The pad is **loop-aware**: a window near either loop edge must be able to read the fragment on the
  // other side, or a wrap note whose note-on lies outside the window can never be paired. Clamping the
  // pad at 0/loop length (the visual-cache slice's shape) is what loses the wrap note.
  uint32_t totalLength = windowLength + 2 * padTicks;
  uint32_t start = (windowStart + kLoopTicks - (padTicks % kLoopTicks)) % kLoopTicks;
  if (totalLength > kLoopTicks) {
    totalLength = kLoopTicks;
    start = 0;
  }
  NoteUtils::DisplayNoteVec notes;
  LoopContentResolution::resolveDisplayNotesInWindow(seeded.loop.passes, seeded.loop.loopLengthTicks,
                                                     start, totalLength, notes);
  return restrictedTo(notes, windowStart, windowLength);
}

}  // namespace

// RIAO-2.A — an ordinary two-bar window is identical to the whole-loop derivation restricted to it.
void test_riao2_a_ordinary_window_matches_whole_loop() {
  const NoteUtils::DisplayNoteVec wholeNotes = wholeLoopNotes();
  TEST_ASSERT_EQUAL_UINT32(8, static_cast<uint32_t>(keysOf(wholeNotes).size()));

  char msg[192];
  for (uint32_t ws = 0; ws + 2 * kBar <= kLoopTicks; ws += 2 * kBar) {
    const std::vector<Key> expected = restrictedTo(wholeNotes, ws, 2 * kBar);
    const std::vector<Key> windowed = windowKeysWithPad(ws, 2 * kBar, kBar);
    std::snprintf(msg, sizeof(msg), "ws=%u expected=%u windowed=%u", static_cast<unsigned>(ws),
                  static_cast<unsigned>(expected.size()), static_cast<unsigned>(windowed.size()));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expected.size()),
                                     static_cast<uint32_t>(windowed.size()), msg);
    for (size_t i = 0; i < expected.size(); ++i) {
      char detail[192];
      std::snprintf(detail, sizeof(detail),
                    "ws=%u index=%u exp id=%u p=%u s=%u e=%u got id=%u p=%u s=%u e=%u",
                    static_cast<unsigned>(ws), static_cast<unsigned>(i),
                    std::get<0>(expected[i]), std::get<1>(expected[i]), std::get<2>(expected[i]),
                    std::get<3>(expected[i]), std::get<0>(windowed[i]), std::get<1>(windowed[i]),
                    std::get<2>(windowed[i]), std::get<3>(windowed[i]));
      TEST_ASSERT_TRUE_MESSAGE(expected[i] == windowed[i], detail);
    }
  }
}

// RIAO-2.B — the loop's wrap note survives a window that crosses the loop end. This is design D3's
// stated UNKNOWN: the whole-loop path relies on `appendOverdubPassWrapPairedNotes`, the windowed path
// relies on `CommittedEventRange::inWindow` understanding a wrapping window.
void test_riao2_b_wrap_window_keeps_the_wrap_note() {
  const uint32_t ws = kLoopTicks - kBar;      // last bar ...
  const uint32_t visible = 2 * kBar;          // ... wrapping into the first bar
  const NoteUtils::DisplayNoteVec wholeNotes = wholeLoopNotes();
  const std::vector<Key> expected = restrictedTo(wholeNotes, ws, visible);

  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(expected.size()),
                                              "the window must contain the wrap note");
  const std::vector<Key> windowed = windowKeysWithPad(ws, visible, kBar);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expected.size()),
                                   static_cast<uint32_t>(windowed.size()),
                                   "wrap window must resolve the same set");
  for (size_t i = 0; i < expected.size() && i < windowed.size(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(expected[i] == windowed[i],
                             "wrap note geometry must match the whole-loop derivation");
  }

  bool sawWrap = false;
  for (const Key& k : windowed) {
    if (std::get<0>(k) == 1006u) sawWrap = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(sawWrap, "note 1006 (on near the end, off near the start) must be present");
}

// RIAO-2.C — geometry twins in different passes keep distinct identities through a window.
void test_riao2_c_same_pitch_twins_keep_identity() {
  const std::vector<Key> windowed = windowKeysWithPad(5 * kBar, kBar, kBar);
  bool saw1005 = false, saw2002 = false;
  for (const Key& k : windowed) {
    if (std::get<0>(k) == 1005u) saw1005 = true;
    if (std::get<0>(k) == 2002u) saw2002 = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(saw1005, "record pass twin must survive");
  TEST_ASSERT_TRUE_MESSAGE(saw2002, "overdub pass twin must survive with its own identity");
}

// RIAO-2.D — the assembled multi-window cache equals the whole-loop derivation. Gate before `fullLoop`.
void test_riao2_d_assembled_cache_matches_whole_loop() {
  const std::vector<Key> whole = wholeLoopKeys();

  std::vector<Key> assembled;
  for (uint32_t ws = 0; ws < kLoopTicks; ws += 2 * kBar) {
    const uint32_t visible = std::min(2 * kBar, kLoopTicks - ws);
    const std::vector<Key> part = windowKeysWithPad(ws, visible, kBar);
    assembled.insert(assembled.end(), part.begin(), part.end());
  }
  std::sort(assembled.begin(), assembled.end());
  assembled.erase(std::unique(assembled.begin(), assembled.end()), assembled.end());

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(whole.size()),
                                   static_cast<uint32_t>(assembled.size()),
                                   "assembly must not duplicate or omit a note");
  for (size_t i = 0; i < whole.size() && i < assembled.size(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(whole[i] == assembled[i],
                             "assembled geometry must equal the whole-loop derivation");
  }
}

// RIAO-2.E — a note that crosses an ordinary window boundary intersects **both** windows, so the
// assembly must collapse it to exactly one entry with the full whole-loop geometry. Dedupe is by
// identity, not by the truncated key: a clipped half must not survive as a second note.
void test_riao2_e_boundary_crosser_appears_exactly_once() {
  std::vector<Key> assembled;
  for (uint32_t ws = 0; ws < kLoopTicks; ws += 2 * kBar) {
    const uint32_t visible = std::min(2 * kBar, kLoopTicks - ws);
    const std::vector<Key> part = windowKeysWithPad(ws, visible, kBar);
    assembled.insert(assembled.end(), part.begin(), part.end());
  }
  // Each window legitimately reports a straddling note; the per-window views overlap on it.
  unsigned perWindowOccurrences = 0;
  for (const Key& k : assembled) {
    if (std::get<0>(k) == 1003u) ++perWindowOccurrences;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2, perWindowOccurrences,
                                              "a straddling note is visible to both windows");

  // Identity-level collapse: one entry per NoteId.
  std::sort(assembled.begin(), assembled.end(),
            [](const Key& a, const Key& b) { return std::get<0>(a) < std::get<0>(b); });
  assembled.erase(std::unique(assembled.begin(), assembled.end(),
                              [](const Key& a, const Key& b) {
                                return std::get<0>(a) == std::get<0>(b);
                              }),
                  assembled.end());
  unsigned collapsedOccurrences = 0;
  const Key* seen = nullptr;
  for (const Key& k : assembled) {
    if (std::get<0>(k) == 1003u) {
      ++collapsedOccurrences;
      seen = &k;
    }
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, collapsedOccurrences,
                                   "identity-level collapse leaves exactly one entry");
  TEST_ASSERT_NOT_NULL(seen);

  const NoteUtils::DisplayNoteVec wholeNotes = wholeLoopNotes();
  bool found = false;
  for (const NoteUtils::DisplayNote& n : wholeNotes) {
    if (n.noteId == 1003u) {
      found = true;
      const Key expect = keyOf(n);
      TEST_ASSERT_TRUE_MESSAGE(*seen == expect,
                               "the boundary note keeps its full whole-loop geometry");
    }
  }
  TEST_ASSERT_TRUE(found);
}

// RIAO-2.X — the admission contract on the monotonic shape (unchanged behaviour): a chunk's advertised
// span is a superset of its content, so a window inside that content admits it.
void test_riao2_x_chunk_span_is_append_order_not_content_extent() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  appendPair(store, 7001, 60, 1000, 1100);
  appendPair(store, 7002, 62, 2000, 2100);

  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(store.chunkIds().size()));
  uint32_t firstTick = 0, lastTick = 0;
  TEST_ASSERT_TRUE(store.chunkTickSpan(store.chunkIds()[0], firstTick, lastTick));
  TEST_ASSERT_EQUAL_UINT32(1000, firstTick);
  TEST_ASSERT_EQUAL_UINT32(2100, lastTick);
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(firstTick, lastTick, 2000, 400,
                                                             kLoopTicks));
}

// RIAO-2.X2 — the RED that motivated task 4.1a: a pass that starts *and* ends near the loop start (an
// overdub started at the top and stopped just after the next top). The advertised span must stay a
// superset of the content, so a mid-loop window still admits it.
void test_riao2_x2_a_pass_ending_near_its_own_start_keeps_a_conservative_span() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  appendPair(store, 7201, 60, 96, 196);                // first, near the loop start
  appendPair(store, 7202, 62, 2400, 2500);             // mid-loop content
  appendPair(store, 7203, 64, kLoopTicks - 96, 100);   // wrap pair, appended last

  uint32_t firstTick = 0, lastTick = 0;
  TEST_ASSERT_TRUE(store.chunkTickSpan(store.chunkIds()[0], firstTick, lastTick));
  // Content extent, not the append-order endpoints (which were 96 and 100).
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(96, firstTick, "extent starts at the lowest contained tick");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kLoopTicks - 96, lastTick,
                                   "extent reaches the highest contained tick");
  TEST_ASSERT_TRUE(CommittedEventRange::chunkIntersectsWindow(firstTick, lastTick, 1800, 800,
                                                             kLoopTicks));
}

// RIAO-2.Y — the consequence at the primitive the windowed derive uses: the mid-loop window now
// returns the in-window pair from that chunk, which the endpoint semantics used to hide.
void test_riao2_y_mid_loop_window_sees_the_wrapped_pass_content() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  appendPair(store, 7301, 60, 96, 196);
  appendPair(store, 7302, 62, 2400, 2500);
  appendPair(store, 7303, 64, kLoopTicks - 96, 100);

  CommittedChunkIdList ids;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, ids));
  const CommittedChunkIdList* lists[1] = {&ids};
  SessionMidiEventVec inWindow;
  CommittedEventRange::inWindow(lists, 1, kLoopTicks, 1800, 800).appendTo(inWindow);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, static_cast<uint32_t>(inWindow.size()),
                                   "a window may over-admit but must never hide in-window content");
  bool sawOn = false, sawOff = false;
  for (const MidiEvent& e : inWindow) {
    if (e.isNoteOn() && e.tick == 2400) sawOn = true;
    if (e.isNoteOff() && e.tick == 2500) sawOff = true;
  }
  TEST_ASSERT_TRUE(sawOn);
  TEST_ASSERT_TRUE(sawOff);
  // The full traversal is unchanged: the same content, no duplication introduced by the bound.
  SessionMidiEventVec all;
  CommittedEventRange::full(lists, 1, kLoopTicks).appendTo(all);
  TEST_ASSERT_EQUAL_UINT32(6, static_cast<uint32_t>(all.size()));
}

// RIAO-2.R — 4.6: the grain-stepped tile derive is *identical* to the single-shot windowed derive at
// every budget. This is the parity that makes the tile a correctness unit and the grain an execution
// unit without changing the answer.
static std::vector<Key> grainSteppedWindow(uint32_t windowStart, uint32_t windowLength,
                                           size_t eventBudget, uint32_t* turnsOut = nullptr) {
  SeededLoop seeded;
  LoopContentResolution::TilePrepCursor cursor;
  uint32_t turns = 0;
  const uint32_t kMaxTurns = 100000;
  while (!LoopContentResolution::stepTilePreparation(
             seeded.loop.passes, seeded.loop.loopLengthTicks, windowStart, windowLength, eventBudget,
             cursor)) {
    if (++turns > kMaxTurns) break;
  }
  if (turnsOut != nullptr) *turnsOut = turns + 1;
  NoteUtils::DisplayNoteVec notes;
  notes.assign(cursor.notes.begin(), cursor.notes.end());
  return keysOf(notes);
}

void test_riao2_r_grain_stepped_derive_matches_the_single_shot_window() {
  const uint32_t ws = 8 * kBar;
  const uint32_t wl = 8 * kBar;
  const std::vector<Key> reference = windowKeysWithPad(ws, wl, 0);
  TEST_ASSERT_FALSE(reference.empty());

  const size_t budgets[] = {1, 3, 7, 64, 100000};
  for (size_t budget : budgets) {
    uint32_t turns = 0;
    const std::vector<Key> stepped = grainSteppedWindow(ws, wl, budget, &turns);
    char msg[160];
    std::snprintf(msg, sizeof(msg), "RIAO-2.R budget=%u turns=%u ref=%u got=%u",
                  static_cast<unsigned>(budget), static_cast<unsigned>(turns),
                  static_cast<unsigned>(reference.size()), static_cast<unsigned>(stepped.size()));
    TEST_MESSAGE(msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(reference.size()),
                                     static_cast<uint32_t>(stepped.size()), msg);
    for (size_t i = 0; i < reference.size(); ++i) {
      TEST_ASSERT_TRUE_MESSAGE(reference[i] == stepped[i], msg);
    }
    if (budget < 64) {
      TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(1, turns, msg);  // a dense tile needs several turns
    }
  }
}

// RIAO-2.S — the dense-tile RED from 4.6: a tile whose content exceeds the grain must stop at the grain,
// report incomplete, and publish nothing. Written before the worker so a tile-sized turn fails it.
void test_riao2_s_dense_tile_stops_at_the_declared_grain() {
  constexpr uint32_t kDenseBars = 31;
  constexpr uint32_t kNotesPerBar = 10;   // 620 events across the loop
  const uint32_t length = kDenseBars * kBar;

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  LoopEventStore seeded;
  NoteId id = 9600;
  for (uint32_t bar = 0; bar < kDenseBars; ++bar) {
    for (uint32_t n = 0; n < kNotesPerBar; ++n) {
      const uint32_t on = bar * kBar + 40 + n * 60;
      appendPair(seeded, id++, static_cast<uint8_t>(48 + (n % 12)), on, on + 40);
    }
  }
  loop.seedRecordPassFromStore(seeded);
  loop.loopLengthTicks = length;
  loop.markDisplayCachesStale();

  constexpr size_t kGrain = CoveragePrerequisite::kGrainEventsPerTurn;
  LoopContentResolution::TilePrepCursor cursor;
  uint32_t turns = 0;
  bool complete = false;
  while (!complete && turns < 1000) {
    const size_t before = cursor.notes.size();
    complete = LoopContentResolution::stepTilePreparation(loop.passes, length, 0, 4 * kBar, kGrain,
                                                          cursor);
    ++turns;
    if (!complete) {
      // Nothing is published on an incomplete tile: the accumulated set may grow, but the caller has no
      // "ready" signal until `complete`, and the set must not yet equal the single-shot result.
      TEST_ASSERT_TRUE(cursor.notes.size() >= before);
    }
  }
  char msg[128];
  std::snprintf(msg, sizeof(msg), "RIAO-2.S turns=%u grain=%u notes=%u", static_cast<unsigned>(turns),
                static_cast<unsigned>(kGrain), static_cast<unsigned>(cursor.notes.size()));
  TEST_MESSAGE(msg);
  TEST_ASSERT_TRUE_MESSAGE(complete, msg);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(1, turns, msg);  // a 4-bar dense tile needs several turns

  // The completed result equals the single-shot windowed derive for the same window.
  NoteUtils::DisplayNoteVec reference;
  LoopContentResolution::resolveDisplayNotesInWindow(loop.passes, length, 0, 4 * kBar, reference);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(reference.size()),
                           static_cast<uint32_t>(cursor.notes.size()));
  for (size_t i = 0; i < reference.size(); ++i) {
    TEST_ASSERT_TRUE(reference[i].noteId == cursor.notes[i].noteId);
    TEST_ASSERT_TRUE(reference[i].startTick == cursor.notes[i].startTick);
    TEST_ASSERT_TRUE(reference[i].endTick == cursor.notes[i].endTick);
  }
}

// RIAO-2.T — 4.3 composition: two adjacent tile closures composed by **identity** yield the union with
// no duplicate candidates, and the composed set equals the whole-loop derivation restricted to the
// union of the tiles. Per-tile correctness alone does not give this.
void test_riao2_t_adjacent_closure_composition_is_by_identity() {
  constexpr uint32_t kBars = 8;
  const uint32_t length = kBars * kBar;
  const uint32_t tileLength = 4 * kBar;

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  LoopEventStore seeded;
  NoteId id = 9700;
  // A note that starts in tile 0 and sustains well into tile 1: it is a candidate for BOTH closures.
  appendPair(seeded, id++, 60, 3 * kBar, 6 * kBar);
  appendPair(seeded, id++, 62, kBar, kBar + 40);
  appendPair(seeded, id++, 64, 5 * kBar, 5 * kBar + 40);
  loop.seedRecordPassFromStore(seeded);
  loop.loopLengthTicks = length;
  loop.markDisplayCachesStale();

  const uint32_t maxSpan = 4 * kBar;  // >= the sustaining note's own span
  NoteUtils::DisplayNoteVec composed;
  for (uint16_t tile = 0; tile < 2; ++tile) {
    uint32_t lookupStart = 0, lookupLength = 0;
    CoveragePrerequisite::closureWindow(tile * tileLength, tileLength, maxSpan, length, lookupStart,
                                        lookupLength);
    NoteUtils::DisplayNoteVec tileNotes;
    LoopContentResolution::resolveDisplayNotesInWindow(loop.passes, length, lookupStart, lookupLength,
                                                       tileNotes);
    LoopContentResolution::upsertResolvedDisplayNotes(composed, tileNotes);
  }

  // Identity: no note appears twice, even though it was in both closures.
  std::vector<uint32_t> ids;
  for (const auto& n : composed) ids.push_back(n.noteId);
  std::sort(ids.begin(), ids.end());
  for (size_t i = 1; i < ids.size(); ++i) {
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ids[i - 1], ids[i], "composition must be by identity, not concat");
  }

  // And the composed set covers the whole-loop derivation restricted to the two-tile union.
  NoteUtils::DisplayNoteVec whole;
  LoopContentResolution::resolveDisplayNotes(loop.passes, length, whole);
  const std::vector<Key> expected = restrictedTo(whole, 0, 2 * tileLength);
  const std::vector<Key> covered = restrictedTo(composed, 0, 2 * tileLength);
  char msg[160];
  std::snprintf(msg, sizeof(msg), "expected=%u covered=%u composed=%u",
                static_cast<unsigned>(expected.size()), static_cast<unsigned>(covered.size()),
                static_cast<unsigned>(composed.size()));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expected.size()),
                                   static_cast<uint32_t>(covered.size()), msg);
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(expected[i] == covered[i], msg);
  }
}

// RIAO-2.U — task 4.4 wiring: a committed-content edge keeps required coverage (it describes what the
// session touched) and re-owes prepared coverage for the new source generation.
void test_riao2_u_generation_change_keeps_required_and_re_owes_prepared() {
  constexpr uint32_t kBars = 31;
  const uint32_t length = kBars * kBar;
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  LoopEventStore seeded;
  appendPair(seeded, 9801, 60, 2 * kBar, 2 * kBar + 40);
  appendPair(seeded, 9802, 62, 27 * kBar, 27 * kBar + 40);
  loop.seedRecordPassFromStore(seeded);
  loop.loopLengthTicks = length;
  loop.markDisplayCachesStale();

  CoveragePrerequisite::CoverageState& coverage = loop.overdubCoverageForTest();
  coverage.beginSession(length, 1u);
  TEST_ASSERT_EQUAL_UINT8(1, coverage.markNoteOn(2 * kBar));
  TEST_ASSERT_EQUAL_UINT8(1, coverage.markNoteOn(27 * kBar));
  coverage.publishPrepared(0);
  coverage.publishPrepared(6);
  TEST_ASSERT_EQUAL_UINT8(0, coverage.owedCount());

  // A committed-content edge is the production generation move.
  loop.notifyCommittedContentChangedForTest();

  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, coverage.owedCount(), "both touched tiles are re-owed");
  TEST_ASSERT_TRUE_MESSAGE(coverage.tileRequired(0), "required coverage is session state and survives");
  TEST_ASSERT_TRUE(coverage.tileRequired(6));
  TEST_ASSERT_FALSE_MESSAGE(coverage.tilePrepared(0), "prepared coverage is invalidated");
  TEST_ASSERT_FALSE(coverage.tilePrepared(6));
}

// RIAO-2.M — `cer_win` cost check (task 4.1a): how much a window actually copies on a dense,
// wrap-heavy pass, at two window sizes. This is the measurement that decides whether the conservative
// min/max bound is affordable, and whether "windowed" is cheaper than the whole-pass derive at all.
void test_riao2_m_window_copy_stays_below_the_whole_pass() {
  constexpr uint32_t kBars = 31;
  constexpr uint32_t kNotesPerBar = 10;
  const uint32_t length = kBars * kBar;

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  // Play order, like a real overdub: start at bar 0, run through the loop, wrap near the start, stop.
  LoopEventStore store;
  NoteId id = 8000;
  uint32_t passEvents = 0;
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    for (uint32_t n = 0; n < kNotesPerBar; ++n) {
      const uint32_t on = bar * kBar + 40 + n * 60;
      appendPair(store, id++, static_cast<uint8_t>(48 + (n % 12)), on, on + 40);
      passEvents += 2;
    }
  }
  appendPair(store, id++, 72, length - 120, 120);  // the wrap close
  passEvents += 2;

  CommittedChunkIdList ids;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, ids));
  TEST_ASSERT_GREATER_THAN_UINT32(1, static_cast<uint32_t>(ids.size()));
  const CommittedChunkIdList* lists[1] = {&ids};

  struct Reading {
    uint32_t windowBars;
    uint32_t chunksCopied;
    uint32_t eventsAppended;
  };
  Reading readings[2] = {};
  const uint32_t windowBars[2] = {4, 16};
  for (uint8_t i = 0; i < 2; ++i) {
    CommittedEventRange::WindowTraversalCursor cursor;
    CommittedEventRange::resetWindowTraversalCursor(cursor);
    SessionMidiEventVec window;
    const uint32_t windowStart = 8 * kBar;
    const CommittedEventRange::WindowTraversalResult result =
        CommittedEventRange::appendWindowBudgeted(lists, 1, length, windowStart,
                                                  windowBars[i] * kBar, passEvents, cursor, window);
    TEST_ASSERT_TRUE(result.complete);
    for (const MidiEvent& e : window) {
      TEST_ASSERT_TRUE(e.tick >= windowStart && e.tick < windowStart + windowBars[i] * kBar);
    }
    readings[i] = {windowBars[i], static_cast<uint32_t>(result.chunksCopied),
                   static_cast<uint32_t>(window.size())};
  }

  char msg[224];
  std::snprintf(msg, sizeof(msg),
                "passEvents=%u chunks=%u | 4bars: copied=%u events=%u | 16bars: copied=%u events=%u",
                static_cast<unsigned>(passEvents), static_cast<unsigned>(ids.size()),
                static_cast<unsigned>(readings[0].chunksCopied),
                static_cast<unsigned>(readings[0].eventsAppended),
                static_cast<unsigned>(readings[1].chunksCopied),
                static_cast<unsigned>(readings[1].eventsAppended));

  // The event filter always does work: neither window appends the whole pass.
  TEST_MESSAGE(msg);
  TEST_ASSERT_TRUE_MESSAGE(readings[0].eventsAppended < passEvents, msg);
  TEST_ASSERT_TRUE_MESSAGE(readings[1].eventsAppended < passEvents, msg);
  // A 4-bar window is smaller than a chunk's span, so it must not admit every chunk.
  TEST_ASSERT_TRUE_MESSAGE(readings[0].chunksCopied < static_cast<uint32_t>(ids.size()), msg);
  // MEASURED (recorded, not asserted): a 16-bar window on a 31-bar loop at this density admits every
  // chunk, because a 256-event chunk spans ~10 bars. "Windowed" is therefore only cheaper than the
  // whole-pass derive when the window is small relative to the chunk span -- the source-view bound has
  // to be a *sub-window* grain, not the 16-bar window alone.
  TEST_ASSERT_TRUE_MESSAGE(readings[1].chunksCopied <= static_cast<uint32_t>(ids.size()), msg);
}

// RIAO-2.P — cost profile for task 4.5. Sub-window resumability only helps if the per-window cost is
// superlinear in the window's note count (the O(n²) merge/duplicate scans), and only if a grain can be
// made small enough to finish before a wrap needs it. This reports notes/ops/time for the whole pass,
// a 16-bar window and a 4-bar window on the same dense content.
void test_riao2_p_derive_cost_profile() {
  constexpr uint32_t kBars = 31;
  constexpr uint32_t kNotesPerBar = 10;
  const uint32_t length = kBars * kBar;

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  NoteId id = 9000;
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    for (uint32_t n = 0; n < kNotesPerBar; ++n) {
      const uint32_t on = bar * kBar + 40 + n * 60;
      appendPair(store, id++, static_cast<uint8_t>(48 + (n % 12)), on, on + 40);
    }
  }
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  LoopEventStore seeded;
  id = 9000;
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    for (uint32_t n = 0; n < kNotesPerBar; ++n) {
      const uint32_t on = bar * kBar + 40 + n * 60;
      appendPair(seeded, id++, static_cast<uint8_t>(48 + (n % 12)), on, on + 40);
    }
  }
  loop.seedRecordPassFromStore(seeded);
  loop.loopLengthTicks = length;
  loop.markDisplayCachesStale();

  struct Profile {
    const char* label;
    uint32_t notes;
    uint32_t ops;
    uint32_t micros;
  };
  Profile profiles[3] = {};
  const char* labels[3] = {"whole", "16bar", "4bar"};
  const uint32_t windows[3] = {length, 16 * kBar, 4 * kBar};
  for (uint8_t i = 0; i < 3; ++i) {
    NoteUtils::DisplayNoteVec out;
    ResolutionCostCounters counters;
    const auto start = std::chrono::steady_clock::now();
    LoopContentResolution::resolveDisplayNotesInWindow(loop.passes, length, 8 * kBar, windows[i], out,
                                                       &counters);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    profiles[i] = {labels[i], static_cast<uint32_t>(out.size()),
                   static_cast<uint32_t>(counters.resolutionOperations),
                   static_cast<uint32_t>(
                       std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count())};
  }

  char msg[256];
  std::snprintf(msg, sizeof(msg), "whole: n=%u ops=%u %uus | 16bar: n=%u ops=%u %uus | 4bar: n=%u ops=%u %uus",
                profiles[0].notes, profiles[0].ops, profiles[0].micros, profiles[1].notes,
                profiles[1].ops, profiles[1].micros, profiles[2].notes, profiles[2].ops,
                profiles[2].micros);
  TEST_MESSAGE(msg);
  // The windows really are different sizes, so the profile is not degenerate.
  TEST_ASSERT_TRUE(profiles[0].notes > profiles[1].notes);
  TEST_ASSERT_TRUE(profiles[1].notes > profiles[2].notes);
  TEST_ASSERT_TRUE(profiles[0].micros >= profiles[2].micros);
}

// RIAO-2.Q — task 4.3 acceptance: the production span-cache rebuild derives the **requested window**,
// not the whole pass. The derive reports its own resolution cost through a test-only seam, so the
// assertion is about the production call path rather than about the primitive.
void test_riao2_q_production_rebuild_is_window_bounded() {
  constexpr uint32_t kBars = 31;
  constexpr uint32_t kNotesPerBar = 10;
  const uint32_t length = kBars * kBar;

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  LoopEventStore seeded;
  NoteId id = 9500;
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    for (uint32_t n = 0; n < kNotesPerBar; ++n) {
      const uint32_t on = bar * kBar + 40 + n * 60;
      appendPair(seeded, id++, static_cast<uint8_t>(48 + (n % 12)), on, on + 40);
    }
  }
  loop.seedRecordPassFromStore(seeded);
  loop.loopLengthTicks = length;
  loop.markDisplayCachesStale();

  loop.beginOverdubSpanDeriveMeasureForTest();
  loop.rebuildOverdubSourceSpanCacheForTest(0, length);  // whole loop: the reference cost
  const uint32_t wholeOps = loop.overdubSpanDeriveOpsForTest();
  TEST_ASSERT_TRUE(wholeOps > 0);

  loop.beginOverdubSpanDeriveMeasureForTest();
  loop.rebuildOverdubSourceSpanCacheForTest(8 * kBar, 16 * kBar);
  const uint32_t window16Ops = loop.overdubSpanDeriveOpsForTest();

  loop.beginOverdubSpanDeriveMeasureForTest();
  loop.rebuildOverdubSourceSpanCacheForTest(8 * kBar, 4 * kBar);
  const uint32_t window4Ops = loop.overdubSpanDeriveOpsForTest();

  char msg[192];
  std::snprintf(msg, sizeof(msg), "wholeOps=%u 16barOps=%u 4barOps=%u",
                static_cast<unsigned>(wholeOps), static_cast<unsigned>(window16Ops),
                static_cast<unsigned>(window4Ops));
  TEST_MESSAGE(msg);
  // The windowed rebuild costs strictly less than the whole pass, and a quarter-size window costs
  // roughly a quarter. A whole-pass resolve behind the request would make these equal.
  TEST_ASSERT_TRUE_MESSAGE(window16Ops < wholeOps, msg);
  TEST_ASSERT_TRUE_MESSAGE(window4Ops < window16Ops, msg);
  TEST_ASSERT_TRUE_MESSAGE(window4Ops * 3u <= wholeOps, msg);
}


// ---------------------------------------------------------------------------------------------
// Slice RIAO-3 (tasks 4.7/4.8): one producer, two drivers.
// ---------------------------------------------------------------------------------------------

namespace {
void driveWorker(Loop& loop, size_t budget = CoveragePrerequisite::kGrainEventsPerTurn,
                 unsigned maxTurns = 4096) {
  for (unsigned turn = 0; turn < maxTurns && loop.overdubCoverageOwedCountForTest() > 0; ++turn) {
    if (loop.serviceOverdubCoverageTurn(budget) == Loop::OverdubCoverageTurn::Idle) {
      break;
    }
  }
}

void seedDenseTileLoop(Loop& loop) {
  constexpr uint32_t kBars = 8;
  constexpr uint32_t kPerBar = 8;
  const uint32_t length = kBars * kBar;
  LoopEventStore seeded;
  NoteId id = 4000;
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    for (uint32_t n = 0; n < kPerBar; ++n) {
      const uint32_t on = bar * kBar + 40 + n * 40;
      appendPair(seeded, id++, static_cast<uint8_t>(48 + (n % 12)), on, on + 30);
    }
  }
  loop.seedRecordPassFromStore(seeded);
  loop.loopLengthTicks = length;
  loop.markDisplayCachesStale();
}
}  // namespace

// RIAO-3.1 — the view is a **read-only cut**: with no producer completion it writes nothing, and the
// worker publishes content only on the grain that completes the tile (content + bit together).
void test_riao3_1_view_cut_never_derives_and_publication_is_atomic() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  seedDenseTileLoop(loop);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.markOverdubCoverageSpan(0, kLoopTicks);
  loop.notifyCommittedContentChangedForTest();
  driveWorker(loop);
  loop.notifyCommittedContentChangedForTest();
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheNotesForTest().empty());
  TEST_ASSERT_GREATER_THAN_UINT8(0, loop.overdubCoverageOwedCountForTest());

  driveWorker(loop, 1, 0);
  loop.rebuildOverdubSourceView(0, "idle");
  TEST_ASSERT_TRUE_MESSAGE(loop.overdubSourceSpanCacheNotesForTest().empty(),
                           "the view cut is not a producer");
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());

  const size_t cacheBefore = loop.overdubSourceSpanCacheNotesForTest().size();
  const Loop::OverdubCoverageTurn first = loop.serviceOverdubCoverageTurn(1);
  TEST_ASSERT_EQUAL(Loop::OverdubCoverageTurn::Advanced, first);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(cacheBefore),
                                   static_cast<uint32_t>(
                                       loop.overdubSourceSpanCacheNotesForTest().size()),
                                   "an incomplete grain publishes no content");
  TEST_ASSERT_EQUAL_UINT8(0, loop.overdubCoveragePreparedCountForTest());

  driveWorker(loop);
  TEST_ASSERT_GREATER_THAN_UINT16(0, loop.overdubCoveragePreparedCountForTest());
  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheNotesForTest().empty());
  loop.rebuildOverdubSourceView(0, "tile");
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceSpanCacheNotesForTest()) {
    TEST_ASSERT_TRUE(loop.overdubCoveragePhasePrepared(note.startTick));
  }
}

// RIAO-3.2 — the session-open edge is the second driver of the **same** worker: the open window is
// required and prepared without the test calling the worker, and the cut then serves it.
void test_riao3_2_session_open_drives_the_worker() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  seedDenseTileLoop(loop);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.overdubCoverageSessionActive());
  TEST_ASSERT_EQUAL_UINT8(0, loop.overdubCoverageOwedCountForTest());
  TEST_ASSERT_GREATER_THAN_UINT16(0, loop.overdubCoveragePreparedCountForTest());
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
}

// RIAO-3.3 — convergence: touch every tile, prepare all, and the cut equals the whole-loop derivation.
void test_riao3_3_convergence_equals_the_whole_loop_derivation() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  seedParityLoop(loop);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.markOverdubCoverageSpan(0, kLoopTicks);
  loop.notifyCommittedContentChangedForTest();
  driveWorker(loop, 64, 8192);
  loop.rebuildOverdubSourceView(0, "tile");

  auto convergeKey = [](const NoteUtils::DisplayNote& n) {
    const uint32_t start = n.endTick < n.startTick ? 0u : n.startTick;
    return Key{n.noteId, n.note, start, n.endTick};
  };
  std::vector<Key> expected;
  {
    NoteUtils::DisplayNoteVec whole;
    LoopEventStore::resetPoolForTests();
    LoopEventStore::initPool();
    Loop seeded;
    seedParityLoop(seeded);
    LoopContentResolution::resolveDisplayNotes(seeded.passes, seeded.loopLengthTicks, whole);
    for (const auto& n : whole) expected.push_back(convergeKey(n));
  }
  std::vector<Key> got;
  for (const auto& n : loop.overdubSourceViewNotes()) got.push_back(convergeKey(n));
  std::sort(expected.begin(), expected.end());
  std::sort(got.begin(), got.end());
  char msg[160];
  std::snprintf(msg, sizeof(msg), "converged expected=%u got=%u",
                static_cast<unsigned>(expected.size()), static_cast<unsigned>(got.size()));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expected.size()),
                                   static_cast<uint32_t>(got.size()), msg);
  for (size_t i = 0; i < expected.size() && i < got.size(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(expected[i] == got[i], msg);
  }
}

// RIAO-3.4 — a completion in an unprepared tile defers **by name** and is discharged exactly once when
// the worker publishes that tile. The source note starts before the incoming note, so the consequence is
// a Shorten to the hold tick.
void test_riao3_4_unprepared_span_defers_then_discharges_exactly_once() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  static Loop loop;
  constexpr uint32_t kBars = 40;
  const uint32_t length = kBars * kBar;
  LoopEventStore seeded;
  appendPair(seeded, 3001, 70, 29 * kBar + 100, 31 * kBar);
  loop.seedRecordPassFromStore(seeded);
  loop.loopLengthTicks = length;
  loop.markDisplayCachesStale();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  const uint32_t held = 30 * kBar;
  TEST_ASSERT_FALSE(loop.overdubCoveragePhasePrepared(held));

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 70, 90, held, held + 200,
                                                                  500, OverlapNoteIdSet{}, nullptr));
  TEST_ASSERT_EQUAL_UINT8(1, loop.overdubCoverageDeferredCompletionsForTest());
  bool transformedEarly = false;
  for (const PendingNoteChange& change : loop.livePendingNoteChangesForTest()) {
    if (change.noteId == 3001 && change.kind != PendingNoteChangeKind::Add) {
      transformedEarly = true;
    }
  }
  TEST_ASSERT_FALSE_MESSAGE(transformedEarly, "no consequence may be computed against partial coverage");

  driveWorker(loop, 64, 8192);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, loop.overdubCoverageDeferredCompletionsForTest(),
                                  "the deferred completion was discharged");
  unsigned shortens = 0;
  uint32_t shortenedEnd = 0;
  for (const PendingNoteChange& change : loop.livePendingNoteChangesForTest()) {
    if (change.noteId == 3001 && change.kind == PendingNoteChangeKind::Shorten) {
      ++shortens;
      shortenedEnd = change.endTick;
    }
  }
  char msg[128];
  std::snprintf(msg, sizeof(msg), "shortens=%u end=%u held=%u", shortens,
                static_cast<unsigned>(shortenedEnd), static_cast<unsigned>(held));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, shortens, msg);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(held - 1, shortenedEnd, msg);
}


// RIAO-4.13 — cost acceptance: a two-bar touch derives proportional work, not loop-proportional work.
// The same two bars of touched content sit in an 8-bar and a 31-bar loop; the tile window that covers the
// touch costs the same `resolutionOperations` in both, while the whole-loop derivation grows with the loop.
void test_riao4_13_two_bar_touch_is_region_proportional() {
  struct Reading {
    uint32_t tileOps = 0;
    uint32_t wholeOps = 0;
    uint32_t tileNotes = 0;
    uint32_t wholeNotes = 0;
  };
  auto measure = [](uint32_t loopBars) {
    LoopEventStore::resetPoolForTests();
    LoopEventStore::initPool();
    Loop loop;
    LoopEventStore store;
    const uint32_t length = loopBars * kBar;
    // The touched two bars (bar 1 and bar 2).
    appendPair(store, 5001, 60, kBar + 100, kBar + 400);
    appendPair(store, 5002, 62, 2 * kBar + 100, 2 * kBar + 300);
    // One note in every other bar, so the whole-loop derivation scales with the loop length.
    for (uint32_t b = 4; b < loopBars; ++b) {
      appendPair(store, 6000 + b, 64, b * kBar + 10, b * kBar + 40);
    }
    loop.seedRecordPassFromStore(store);
    loop.loopLengthTicks = length;
    Reading reading{};
    NoteUtils::DisplayNoteVec tileNotes;
    ResolutionCostCounters tile;
    LoopContentResolution::resolveDisplayNotesInWindow(loop.passes, length, 0, 4 * kBar, tileNotes,
                                                       &tile);
    NoteUtils::DisplayNoteVec wholeNotes;
    ResolutionCostCounters whole;
    LoopContentResolution::resolveDisplayNotes(loop.passes, length, wholeNotes, &whole);
    reading.tileOps = tile.resolutionOperations;
    reading.wholeOps = whole.resolutionOperations;
    reading.tileNotes = static_cast<uint32_t>(tileNotes.size());
    reading.wholeNotes = static_cast<uint32_t>(wholeNotes.size());
    return reading;
  };

  const Reading shortLoop = measure(8);
  const Reading longLoop = measure(31);
  char msg[224];
  std::snprintf(msg, sizeof(msg),
                "8bar tile=%u/%u whole=%u/%u | 31bar tile=%u/%u whole=%u/%u",
                shortLoop.tileOps, shortLoop.tileNotes, shortLoop.wholeOps, shortLoop.wholeNotes,
                longLoop.tileOps, longLoop.tileNotes, longLoop.wholeOps, longLoop.wholeNotes);
  TEST_MESSAGE(msg);
  // Same touched region, same tile cost: the tile's derivation does not grow with the loop.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(shortLoop.tileOps, longLoop.tileOps, msg);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(shortLoop.tileNotes, longLoop.tileNotes, msg);
  // The whole-loop derivation does grow with the loop length.
  TEST_ASSERT_TRUE_MESSAGE(longLoop.wholeOps > shortLoop.wholeOps, msg);
  // And the tile is a small fraction of the long loop's whole derivation.
  TEST_ASSERT_TRUE_MESSAGE(longLoop.tileOps * 3u <= longLoop.wholeOps, msg);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_riao2_a_ordinary_window_matches_whole_loop);
  RUN_TEST(test_riao2_b_wrap_window_keeps_the_wrap_note);
  RUN_TEST(test_riao2_c_same_pitch_twins_keep_identity);
  RUN_TEST(test_riao2_d_assembled_cache_matches_whole_loop);
  RUN_TEST(test_riao2_e_boundary_crosser_appears_exactly_once);
  RUN_TEST(test_riao2_x_chunk_span_is_append_order_not_content_extent);
  RUN_TEST(test_riao2_x2_a_pass_ending_near_its_own_start_keeps_a_conservative_span);
  RUN_TEST(test_riao2_y_mid_loop_window_sees_the_wrapped_pass_content);
  RUN_TEST(test_riao2_m_window_copy_stays_below_the_whole_pass);
  RUN_TEST(test_riao2_p_derive_cost_profile);
  RUN_TEST(test_riao2_q_production_rebuild_is_window_bounded);
  RUN_TEST(test_riao2_r_grain_stepped_derive_matches_the_single_shot_window);
  RUN_TEST(test_riao2_s_dense_tile_stops_at_the_declared_grain);
  RUN_TEST(test_riao2_t_adjacent_closure_composition_is_by_identity);
  RUN_TEST(test_riao2_u_generation_change_keeps_required_and_re_owes_prepared);
  RUN_TEST(test_riao3_1_view_cut_never_derives_and_publication_is_atomic);
  RUN_TEST(test_riao3_2_session_open_drives_the_worker);
  RUN_TEST(test_riao3_3_convergence_equals_the_whole_loop_derivation);
  RUN_TEST(test_riao3_4_unprepared_span_defers_then_discharges_exactly_once);
  RUN_TEST(test_riao4_13_two_bar_touch_is_region_proportional);
  return UNITY_END();
}
