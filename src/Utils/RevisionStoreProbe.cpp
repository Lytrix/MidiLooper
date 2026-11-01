//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RevisionStoreProbe.h"

#if REVISION_STORE_PROBE

#include <Arduino.h>
#include <new>

#include "RevisionStore/RevisionStore.h"

// The whole TU is routed to FLASH by linker/imxrt1062_t41_lcr.ld (`*RevisionStoreProbe*.o`), so the store's template
// code costs no ITCM and the timings are a conservative (flash-resident) bound.

namespace RevisionStoreProbe {
namespace {

using namespace RevisionStore;
// 1,024 pages x 64 rows x 16 B = 1 MiB of rows; one run may span 512 pages (32,768 rows).
using ProbeCap = Capacity<1024, 64, 32, 512, 32, 16, 16>;
using ProbeStore = Store<ProbeCap>;

constexpr uint32_t kBarTicks = 768;
constexpr uint32_t kBars = 64;
constexpr uint32_t kLoopTicks = kBarTicks * kBars;
constexpr uint32_t kWindowCapacity = 4096;

uint32_t cyclesToUs(uint32_t cycles) { return cycles / (F_CPU_ACTUAL / 1000000u); }

struct Stat {
  uint32_t n = 0, sum = 0, max = 0;
  void add(uint32_t cycles) {
    ++n;
    sum += cycles;
    if (cycles > max) max = cycles;
  }
};

void printStat(const char* op, uint32_t rows, uint32_t runs, const Stat& s) {
  Serial.printf_P(PSTR("REVSTORE,%s,rows=%lu,runs=%lu,n=%lu,avg_us=%lu,max_us=%lu,total_us=%lu\r\n"), op,
                  static_cast<unsigned long>(rows), static_cast<unsigned long>(runs), static_cast<unsigned long>(s.n),
                  static_cast<unsigned long>(s.n ? cyclesToUs(s.sum / s.n) : 0),
                  static_cast<unsigned long>(cyclesToUs(s.max)), static_cast<unsigned long>(cyclesToUs(s.sum)));
}

uint32_t gLcg = 1;
uint32_t nextRandom() {
  gLcg = gLcg * 1664525u + 1013904223u;
  return gLcg >> 8;
}

// `notes` paired notes appended in capture order starting mid-loop (so the take wraps), 260 rows per bar at the
// densest observed device bar.
void appendTake(ProbeStore& store, uint32_t notes, uint8_t status, uint32_t& noteId, Stat& append) {
  const uint32_t startTick = kLoopTicks / 2;
  const uint32_t step = kLoopTicks / (notes == 0 ? 1 : notes);
  for (uint32_t i = 0; i < notes; ++i) {
    const uint32_t on = (startTick + i * step + nextRandom() % 8) % kLoopTicks;
    const uint32_t off = (on + 24 + nextRandom() % 96) % kLoopTicks;
    const uint8_t pitch = static_cast<uint8_t>(36 + nextRandom() % 48);
    const uint32_t id = noteId++;
    uint32_t t0 = ARM_DWT_CYCCNT;
    store.appendMidi(on, status, pitch, 100, id);
    append.add(ARM_DWT_CYCCNT - t0);
    t0 = ARM_DWT_CYCCNT;
    store.appendMidi(off, static_cast<uint8_t>((status & 0x0F) | 0x80), pitch, 0, id);
    append.add(ARM_DWT_CYCCNT - t0);
  }
}

}  // namespace

void run() {
  void* arena = extmem_malloc(sizeof(ProbeStore));
  PassEvent* window = static_cast<PassEvent*>(extmem_malloc(sizeof(PassEvent) * kWindowCapacity));
  if (arena == nullptr || window == nullptr) {
    Serial.printf_P(PSTR("REVSTORE,denied,arena=%lu\r\n"), static_cast<unsigned long>(sizeof(ProbeStore)));
    if (arena != nullptr) extmem_free(arena);
    if (window != nullptr) extmem_free(window);
    return;
  }
  Serial.printf_P(PSTR("REVSTORE,start,arena=%lu,row=%u,page_rows=%u,f_cpu=%lu\r\n"),
                  static_cast<unsigned long>(sizeof(ProbeStore)), static_cast<unsigned>(sizeof(PassEvent)),
                  static_cast<unsigned>(ProbeCap::kEventsPerPage), static_cast<unsigned long>(F_CPU_ACTUAL));
  uint32_t t0 = ARM_DWT_CYCCNT;
  ProbeStore& store = *new (arena) ProbeStore();  // placement into the probe arena; the store allocates nothing
  Stat reset;
  reset.add(ARM_DWT_CYCCNT - t0);
  printStat("reset", 0, 0, reset);

  // 1. Record seal cost against take size (commit = seal sort + manifest).
  const uint32_t takeNotes[] = {500, 2000, 8320};  // 1,000 / 4,000 / 16,640 rows (64 bars x 260 rows)
  for (uint32_t notes : takeNotes) {
    store.reset();
    gLcg = 1;
    uint32_t noteId = 1;
    Stat begin, append, commit;
    t0 = ARM_DWT_CYCCNT;
    store.beginPass(PassKind::Record, kLoopTicks);
    begin.add(ARM_DWT_CYCCNT - t0);
    appendTake(store, notes, 0x90, noteId, append);
    t0 = ARM_DWT_CYCCNT;
    store.commitPass();
    commit.add(ARM_DWT_CYCCNT - t0);
    printStat("record_begin", notes * 2, 1, begin);
    printStat("record_append", notes * 2, 1, append);
    printStat("record_commit_seal", notes * 2, 1, commit);
  }

  // 2. Eight overdubs (520 notes = 1,040 rows each) on the 16,640-row take; manifest-only commit (1-row overdub).
  uint32_t noteId = 100000;
  Stat odBegin, odAppend, odCommit;
  for (uint32_t od = 0; od < 8; ++od) {
    t0 = ARM_DWT_CYCCNT;
    store.beginPass(PassKind::Overdub);
    odBegin.add(ARM_DWT_CYCCNT - t0);
    appendTake(store, 520, static_cast<uint8_t>(0x90 | ((od % 3) + 1)), noteId, odAppend);
    t0 = ARM_DWT_CYCCNT;
    store.commitPass();
    odCommit.add(ARM_DWT_CYCCNT - t0);
  }
  printStat("overdub_begin", 1040, 9, odBegin);
  printStat("overdub_append", 1040, 9, odAppend);
  printStat("overdub_commit_seal", 1040, 9, odCommit);
  {
    Stat manifestOnly;
    store.beginPass(PassKind::Overdub);
    store.appendMidi(0, 0xB0, 64, 127, 0);
    t0 = ARM_DWT_CYCCNT;
    store.commitPass();
    manifestOnly.add(ARM_DWT_CYCCNT - t0);
    printStat("commit_manifest_1row", 1, 10, manifestOnly);
  }

  // 3. Window composition: every two-bar window at the 10-run revision, then at the 1-run revision (after undo).
  auto composeAll = [&](const char* op, uint32_t runs) {
    Stat compose;
    uint32_t rows = 0, maxRows = 0;
    const ManifestId current = store.current();
    for (uint32_t from = 0; from < kLoopTicks; from += 2 * kBarTicks) {
      t0 = ARM_DWT_CYCCNT;
      const ComposeStamp stamp = store.composeWindow(current, from, from + 2 * kBarTicks, window, kWindowCapacity);
      compose.add(ARM_DWT_CYCCNT - t0);
      rows += stamp.count;
      if (stamp.count > maxRows) maxRows = stamp.count;
    }
    printStat(op, rows, runs, compose);
    Serial.printf_P(PSTR("REVSTORE,%s_rows_per_window_max=%lu\r\n"), op, static_cast<unsigned long>(maxRows));
  };
  composeAll("compose_2bar_10runs", 10);
  Stat undo, redo;
  for (int i = 0; i < 9; ++i) {
    t0 = ARM_DWT_CYCCNT;
    store.undo();
    undo.add(ARM_DWT_CYCCNT - t0);
  }
  printStat("undo_select", 0, 1, undo);
  composeAll("compose_2bar_1run", 1);
  for (int i = 0; i < 9; ++i) {
    t0 = ARM_DWT_CYCCNT;
    store.redo();
    redo.add(ARM_DWT_CYCCNT - t0);
  }
  printStat("redo_select", 0, 10, redo);

  // 4. Compaction of all ten runs (synchronous; the grain is derived from rows per ms).
  {
    Stat compact;
    const uint16_t freeBefore = store.freePageCount();
    t0 = ARM_DWT_CYCCNT;
    const CompactResult result = store.compactCurrentPrefix(10);
    compact.add(ARM_DWT_CYCCNT - t0);
    printStat("compact_10runs", 16640 + 8 * 1040 + 1, 10, compact);
    Serial.printf_P(PSTR("REVSTORE,compact_result=%u,pages_free_before=%u,after=%u\r\n"),
                    static_cast<unsigned>(result), static_cast<unsigned>(freeBefore),
                    static_cast<unsigned>(store.freePageCount()));
    composeAll("compose_2bar_compacted", 1);
  }

  store.~ProbeStore();
  extmem_free(window);
  extmem_free(arena);
  Serial.println(F("REVSTORE,done"));
}

}  // namespace RevisionStoreProbe

#else

namespace RevisionStoreProbe {
void run() {}
}  // namespace RevisionStoreProbe

#endif
