//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-286 (groove design slice 1, § 3): the groove builder reads a state's *effective content* — every effective note
// and event, closed, no output limit. The playback composer tracks at most 48 open instances and selects voices (Q-OUT-2),
// so a busy loop loses notes there; the extraction composer (`T` = `MaxOpen` = 128) keeps every one, its notes crossing
// window ends closed by their terminal entries in a later window.

#include <unity.h>

#include <cstdint>
#include <map>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/GroovePass.h"

using namespace ContentEngine;

using Pool = PagePool<256, 64>;
using Runs = RunTable<16, Pool>;
using Store = EditIndexStore<Pool, Runs, 2, 64>;
using Playback = Composer<Pool, Runs, 64, Config::kMaxSoundingPerTrack, Store>;
using Extraction = Playback;  // the same type, `extractAll` (DEC-287: one code copy)
using Window = SpanWindow<512>;

static Pool gPool;
static Runs gRuns;
static Store gStore;
static Playback gPlay;
static Extraction gExtract;
static Window gWin;
static Extraction::Open gLent[128];  // the open list lent to the extraction composer (DEC-286)

constexpr uint32_t kLoop = 4 * 768, kWindow = 768;

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t pitch, uint8_t vel) {
  PassEvent e{};
  e.tick = tick;
  e.noteId = id;
  e.seq = id;
  e.status = status;
  e.data1 = pitch;
  e.data2 = vel;
  return e;
}

void setUp() {
  gPool.init();
  gRuns.init();
  TEST_ASSERT_TRUE(gStore.init(gPool, gRuns));
  gPlay.init(gPool, gRuns, &gStore);
  gExtract.init(gPool, gRuns, &gStore);
  gExtract.extractAll(gLent, 128);
}
void tearDown() {}

struct Note {
  uint32_t start, end;
  uint8_t pitch;
};

/// Every effective note of one loop cycle: STARTs paired with their end — the entry's length, or a later TERMINAL.
template <typename C>
static std::map<uint32_t, Note> extract(C& c, const SourceRun* src, uint8_t n, uint32_t identity) {
  std::map<uint32_t, Note> notes;
  TEST_ASSERT_TRUE(c.begin(src, n, kLoop, identity));
  while (c.composeNext(gWin, kWindow))
    for (uint16_t k = 0; k < gWin.count; ++k) {
      const SpanEntry& e = gWin.entries[k];
      if ((e.status & 0xF0) != 0x90 && (e.status & 0xF0) != 0x80) continue;
      if (e.terminal) {
        notes[e.noteId].end = e.tick;
      } else {
        notes[e.noteId] = Note{e.tick, e.length == kOpenLength ? 0xFFFFFFFFu : e.tick + e.length, e.data1};
      }
    }
  c.end();
  return notes;
}

void test_every_note_of_a_busy_loop_is_extracted() {
  // 60 notes on distinct pitches, all sounding at once from bar 1 to bar 3 (crossing two window ends).
  const RunId r = gRuns.allocate(0, RunKind::kRecord);
  for (uint32_t i = 0; i < 60; ++i) gRuns.append(r, row(10 + i, 1 + i, 0x90, static_cast<uint8_t>(30 + i), 100), gPool);
  for (uint32_t i = 0; i < 60; ++i) gRuns.append(r, row(2000 + i, 1 + i, 0x80, static_cast<uint8_t>(30 + i), 0), gPool);
  gRuns.seal(r);
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  const auto got = extract(gExtract, src, 1, 7);
  TEST_ASSERT_EQUAL_UINT32(60, got.size());
  for (uint32_t i = 0; i < 60; ++i) {
    const Note& n = got.at(1 + i);
    TEST_ASSERT_EQUAL_UINT32(10 + i, n.start);
    TEST_ASSERT_EQUAL_UINT32(2000 + i, n.end);  // closed in a later window
    TEST_ASSERT_EQUAL_UINT8(30 + i, n.pitch);
  }
  TEST_ASSERT_EQUAL_UINT32(0, gExtract.stats().openOverflow);
  // The playback composer cannot: 48 tracked, the rest refused.
  extract(gPlay, src, 1, 8);
  TEST_ASSERT_TRUE(gPlay.stats().openOverflow > 0);
}

void test_extraction_keeps_the_effective_rules() {
  // A later pass's note on the same pitch shortens the earlier one (OVL-1); an edit run's hide removes a note.
  const RunId a = gRuns.allocate(0, RunKind::kRecord);
  gRuns.append(a, row(100, 1, 0x90, 60, 100), gPool);
  gRuns.append(a, row(500, 2, 0x90, 64, 100), gPool);
  gRuns.append(a, row(900, 1, 0x80, 60, 0), gPool);
  gRuns.append(a, row(1000, 2, 0x80, 64, 0), gPool);
  gRuns.seal(a);
  const RunId b = gRuns.allocate(0, RunKind::kOverdub);
  gRuns.append(b, row(400, 10, 0x90, 60, 90), gPool);
  gRuns.append(b, row(600, 10, 0x80, 60, 0), gPool);
  gRuns.seal(b);
  const RunId e = gRuns.allocate(0, RunKind::kEdit);
  gRuns.append(e, row(0, 2, kHideNoteStatus, 0, 0), gPool);
  gRuns.seal(e);
  const SourceRun src[] = {{a, 4}, {b, 2}, {e, 1}};
  const auto got = extract(gExtract, src, 3, 9);
  TEST_ASSERT_EQUAL_UINT32(2, got.size());
  TEST_ASSERT_EQUAL_UINT32(399, got.at(1).end);  // shortened to one tick before the later pass's note
  TEST_ASSERT_EQUAL_UINT32(400, got.at(10).start);
  TEST_ASSERT_TRUE(got.find(2) == got.end());  // hidden by the edit
}

// DEC-287 (design review C1): the prefix rule — a rebuild folds the passes after the groove pass up to the last
// record / overdub into the sources, in order (precedence never reordered), never the groove pass itself; edit passes
// after that stay after it.
using Res = RowReservations<Pool, Runs>;
static Res gRes;
static PassEvent gRows[64];
static GrooveBuilder<Pool, Runs, Res, Store, Extraction, 512, 64> gBuilder;
void test_the_prefix_rule_keeps_pass_order() {
  gRes.init(gPool, gRuns, 0);
  gBuilder.init(gPool, gRuns, gRes, gStore, gExtract, gWin, gRows);
  const RunId a = gRuns.allocate(0, RunKind::kRecord), g = gRuns.allocate(0, RunKind::kGroove),
              e = gRuns.allocate(0, RunKind::kEdit), o = gRuns.allocate(0, RunKind::kOverdub),
              e2 = gRuns.allocate(0, RunKind::kEdit);
  RunId prefix[8], after[8];
  uint8_t pc = 0, ac = 0;
  const RunId list1[] = {a, g, e, o, e2};  // groove → edit → overdub → edit
  gBuilder.partition(list1, 5, prefix, pc, after, ac);
  TEST_ASSERT_EQUAL_UINT8(3, pc);
  TEST_ASSERT_TRUE(prefix[0] == a && prefix[1] == e && prefix[2] == o);  // the edit before the overdub, as made
  TEST_ASSERT_EQUAL_UINT8(1, ac);
  TEST_ASSERT_TRUE(after[0] == e2);
  const RunId list2[] = {a, g, e};  // edits only after the groove: they stay after it
  gBuilder.partition(list2, 3, prefix, pc, after, ac);
  TEST_ASSERT_EQUAL_UINT8(1, pc);
  TEST_ASSERT_TRUE(prefix[0] == a);
  TEST_ASSERT_EQUAL_UINT8(1, ac);
  TEST_ASSERT_TRUE(after[0] == e);
  const RunId list3[] = {a, o};  // no groove yet: everything is the source
  gBuilder.partition(list3, 2, prefix, pc, after, ac);
  TEST_ASSERT_EQUAL_UINT8(2, pc);
  TEST_ASSERT_EQUAL_UINT8(0, ac);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_note_of_a_busy_loop_is_extracted);
  RUN_TEST(test_extraction_keeps_the_effective_rules);
  RUN_TEST(test_the_prefix_rule_keeps_pass_order);
  return UNITY_END();
}
