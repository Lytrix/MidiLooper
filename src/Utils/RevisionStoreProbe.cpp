//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RevisionStoreProbe.h"

#if REVISION_STORE_PROBE

#include <Arduino.h>
#include <new>

#include "RevisionStore/RevisionStore.h"
#include "LoopEventStore.h"
#include "Utils/ExternalPoolAccounting.h"
#include "Utils/MemoryMonitor.h"

extern "C" uint8_t external_psram_size;

// The whole TU is routed to FLASH by linker/imxrt1062_t41_lcr.ld (`*RevisionStoreProbe*.o`), so the store's template
// code costs no ITCM and the timings are a conservative (flash-resident) bound.

namespace RevisionStoreProbe {
namespace {

using namespace RevisionStore;
// 1,024 pages x 64 rows x 16 B = 1 MiB of rows; one run may span 512 pages (32,768 rows).
using ProbeCap = Capacity<1024, 64, 48, 512, 48, 32, 40>;  // 32 runs per manifest: undo depth means no compaction
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

// `notes` paired notes appended the way capture delivers them: in time order from mid-loop (one wrap), each NoteOff
// emitted when it falls due. 260 rows per bar at the densest observed device bar.
void appendTake(ProbeStore& store, uint32_t notes, uint8_t status, uint32_t& noteId, Stat& append) {
  struct Pending {
    uint32_t offTick;  // unwrapped
    uint8_t pitch;
    uint32_t id;
  };
  Pending pending[64];
  uint32_t pendingCount = 0;
  const uint32_t startTick = kLoopTicks / 2;
  const uint32_t step = kLoopTicks / (notes == 0 ? 1 : notes);
  const uint8_t offStatus = static_cast<uint8_t>((status & 0x0F) | 0x80);
  auto timed = [&](uint32_t tick, uint8_t st, uint8_t pitch, uint8_t velocity, uint32_t id) {
    const uint32_t t0 = ARM_DWT_CYCCNT;
    store.appendMidi(tick % kLoopTicks, st, pitch, velocity, id);
    append.add(ARM_DWT_CYCCNT - t0);
  };
  auto flushDue = [&](uint32_t now) {
    for (;;) {
      uint32_t best = pendingCount;
      for (uint32_t k = 0; k < pendingCount; ++k) {
        if (pending[k].offTick <= now && (best == pendingCount || pending[k].offTick < pending[best].offTick)) best = k;
      }
      if (best == pendingCount) return;
      timed(pending[best].offTick, offStatus, pending[best].pitch, 0, pending[best].id);
      pending[best] = pending[--pendingCount];
    }
  };
  for (uint32_t i = 0; i < notes; ++i) {
    const uint32_t on = startTick + i * step;
    flushDue(on);
    const uint8_t pitch = static_cast<uint8_t>(36 + nextRandom() % 48);
    const uint32_t id = noteId++;
    timed(on, status, pitch, 100, id);
    if (pendingCount < 64) {
      pending[pendingCount++] = {on + 24 + nextRandom() % 96, pitch, id};
    } else {
      timed(on, offStatus, pitch, 0, id);  // never reached at these densities; keeps the generator bounded
    }
  }
  flushDue(UINT32_MAX);
}

}  // namespace

void run() {
  {
    // B.5: the firmware's existing PSRAM obligation at this (stopped) moment, before the probe takes its arena.
    // The pool walk is slow; acceptable here because the command is stopped-only.
#if defined(EXTERNAL_POOL_WRAP)
    const ExternalPoolAccounting::Counters& c = ExternalPoolAccounting::counters();
    const unsigned long live = c.liveBytes, high = c.highWaterLiveBytes;
#else
    const unsigned long live = 0, high = 0;
#endif
    Serial.printf_P(PSTR("REVSTORE,psram,size_mb=%u,pool_total=%lu,pool_used=%lu,pool_free=%lu,acct_live=%lu,acct_high=%lu\r\n"),
                    static_cast<unsigned>(external_psram_size),
                    static_cast<unsigned long>(MemoryMonitor::getExternalMemoryPoolTotalBytes()),
                    static_cast<unsigned long>(MemoryMonitor::getExternalMemoryPoolUsedBytes()),
                    static_cast<unsigned long>(MemoryMonitor::getExternalMemoryPoolFreeBytes()), live, high);
#if defined(EXTERNAL_POOL_WRAP)
    char tags[192];
    ExternalPoolAccounting::formatTagVector(tags, sizeof(tags));
    Serial.printf_P(PSTR("REVSTORE,psram_tags,%s\r\n"), tags);
    Serial.printf_P(PSTR("REVSTORE,psram_classes,le128=%lu,le1k=%lu,le8k=%lu,le32k=%lu,le128k=%lu,gt128k=%lu\r\n"),
                    static_cast<unsigned long>(c.liveBytesByClass[0]), static_cast<unsigned long>(c.liveBytesByClass[1]),
                    static_cast<unsigned long>(c.liveBytesByClass[2]), static_cast<unsigned long>(c.liveBytesByClass[3]),
                    static_cast<unsigned long>(c.liveBytesByClass[4]), static_cast<unsigned long>(c.liveBytesByClass[5]));
#endif
    Serial.printf_P(PSTR("REVSTORE,event_chunks,used=%u,free=%u,backed_bytes=%lu,chunk_bytes=%u\r\n"),
                    static_cast<unsigned>(LoopEventStore::usedChunkCount()),
                    static_cast<unsigned>(LoopEventStore::freeChunkCount()),
                    static_cast<unsigned long>(LoopEventStore::poolBackedBytes()),
                    static_cast<unsigned>(sizeof(EventChunk)));
  }
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

  // 3b. B.5a: with undo depth 99 nothing is history-closed, so a manifest holds every overdub. 21 more overdubs (31
  // runs, 38,480 rows) and the heap-merge compose at that run count.
  for (uint32_t od = 0; od < 21; ++od) {
    Stat ignore;
    store.beginPass(PassKind::Overdub);
    appendTake(store, 520, static_cast<uint8_t>(0x90 | ((od % 3) + 1)), noteId, ignore);
    store.commitPass();
  }
  composeAll("compose_2bar_31runs", 31);
  Serial.printf_P(PSTR("REVSTORE,pages_used_31runs=%u\r\n"),
                  static_cast<unsigned>(ProbeCap::kPageCount - store.freePageCount()));

  Serial.printf_P(PSTR("REVSTORE,slow_seals=%lu\r\n"), static_cast<unsigned long>(store.slowSeals()));

  // Rebuild Record + 8 overdubs + a 1-row overdub (10 runs) for the compaction measurements.
  auto rebuildTenRuns = [&]() {
    store.reset();
    gLcg = 1;
    uint32_t id = 1;
    Stat ignore;
    store.beginPass(PassKind::Record, kLoopTicks);
    appendTake(store, 8320, 0x90, id, ignore);
    store.commitPass();
    for (uint32_t od = 0; od < 8; ++od) {
      store.beginPass(PassKind::Overdub);
      appendTake(store, 520, static_cast<uint8_t>(0x90 | ((od % 3) + 1)), id, ignore);
      store.commitPass();
    }
    store.beginPass(PassKind::Overdub);
    store.appendMidi(0, 0xB0, 64, 127, 0);
    store.commitPass();
  };
  rebuildTenRuns();

  // 4a. Compaction of all ten runs in bounded steps (2,048 source rows per step), then 4b synchronous.
  {
    Stat steps;
    store.beginCompaction(0, 10, CompactionScope::AllowCopies);
    CompactStep step = CompactStep::Pending;
    while (step == CompactStep::Pending) {
      t0 = ARM_DWT_CYCCNT;
      step = store.stepCompaction(2048);
      steps.add(ARM_DWT_CYCCNT - t0);
    }
    printStat("compact_step_2048", 16640 + 8 * 1040 + 1, 10, steps);
    Serial.printf_P(PSTR("REVSTORE,compact_step_result=%u\r\n"), static_cast<unsigned>(step));
    composeAll("compose_2bar_stepcompacted", 1);
  }
  // 4b. Synchronous, for the rows-per-ms rate, on a rebuilt store (two compacted copies do not fit the arena).
  {
    rebuildTenRuns();
    Stat compact;
    const uint16_t freeBefore = store.freePageCount();
    t0 = ARM_DWT_CYCCNT;
    const CompactResult result = store.compactCurrentPrefix(10);
    compact.add(ARM_DWT_CYCCNT - t0);
    printStat("compact_10runs_sync", 16640 + 8 * 1040 + 1, 10, compact);
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
