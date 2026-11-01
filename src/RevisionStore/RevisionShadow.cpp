//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RevisionStore/RevisionShadow.h"

#if REVISION_STORE_PROBE

#include <Arduino.h>
#include <algorithm>
#include <new>
#include <iterator>
#include <tuple>
#include <vector>

#include "RevisionStore/RevisionImport.h"
#include "RevisionStore/RevisionStore.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/MidiServiceDrain.h"

// The whole TU is routed to FLASH by linker/imxrt1062_t41_lcr.ld (`*RevisionShadow*.o`).

namespace RevisionShadow {
namespace {

using namespace RevisionStore;
// Pool 448 pages x 64 rows x 16 B = 448 KiB; with tables the store is ~0.47 MiB (DEC-246 cap 0.5 MiB).
// History 2: the shadow keeps current + previous only (disposable imported history).
using ShadowCap = Capacity</*pages*/ 448, /*rows per page*/ 64, /*runs*/ 48, /*pages per run*/ 448, /*manifests*/ 16,
                           /*runs per manifest*/ 48, /*history*/ 2>;
using ShadowStore = Store<ShadowCap>;

constexpr uint32_t kEditIndexRowsPerGrain = 256;  // B.5d: 256 rows measured at 1.12 ms max

struct SourceIdentity {
  uint8_t track = 0xFF;
  uint8_t slot = 0xFF;
  uint32_t loopId = 0;
  uint32_t playbackRevision = 0;
  uint32_t committedPassRevision = 0;
  bool operator==(const SourceIdentity& o) const {
    return std::tie(track, slot, loopId, playbackRevision, committedPassRevision) ==
           std::tie(o.track, o.slot, o.loopId, o.playbackRevision, o.committedPassRevision);
  }
};

constexpr uint32_t kWindowCapacity = 8192;

struct State {
  ShadowStore* store = nullptr;
  PassEvent* window = nullptr;  // status/parity diagnostic buffer, allocated with the store at boot
  bool initRefused = false;
  SourceIdentity imported;
  ImportResult lastImport = ImportResult::NoRecordPass;
  ImportStats lastStats;
  uint32_t imports = 0;
  uint32_t lastImportUs = 0;
  uint32_t maxImportUs = 0;
  uint32_t grains = 0;
  uint32_t grainDeferred = 0;
  uint32_t maxGrainUs = 0;
  uint32_t indexesPublished = 0;
  uint32_t capacityDenied = 0;
} gState;

uint32_t cyclesToUs(uint32_t cycles) { return cycles / (F_CPU_ACTUAL / 1000000u); }

SourceIdentity identityOfSelected() {
  SourceIdentity id;
  id.track = trackManager.getSelectedTrackIndex();
  if (id.track >= trackManager.getTrackCount()) return id;
  const Track& track = trackManager.getTrack(id.track);
  id.slot = track.getActiveLoopIndex();
  const Loop& loop = track.getActiveLoop();
  id.loopId = loop.loopId;
  id.playbackRevision = loop.playbackRevision;
  id.committedPassRevision = loop.committedPassRevision();
  return id;
}

}  // namespace

void initAtBoot() {
  void* arena = extmem_malloc(sizeof(ShadowStore));
  void* window = extmem_malloc(sizeof(PassEvent) * kWindowCapacity);
  if (arena == nullptr || window == nullptr) {
    if (arena != nullptr) extmem_free(arena);
    if (window != nullptr) extmem_free(window);
    gState.initRefused = true;
    return;
  }
  gState.store = new (arena) ShadowStore();  // once, at boot; the store allocates nothing afterwards
  gState.window = static_cast<PassEvent*>(window);
}

void serviceIdleTurn(bool timingCritical, bool transportRunning) {
  if (gState.store == nullptr) return;
  ShadowStore& store = *gState.store;
  // C3a: import only while STOPPED and only when the frozen source identity changed.
  if (!transportRunning && !timingCritical) {
    const SourceIdentity source = identityOfSelected();
    if (source.track != 0xFF && !(source == gState.imported)) {
      const Loop& loop = trackManager.getTrack(source.track).getActiveLoop();
      const uint32_t t0 = ARM_DWT_CYCCNT;
      gState.lastImport = importLoop(loop, store, &gState.lastStats);
      gState.lastImportUs = cyclesToUs(ARM_DWT_CYCCNT - t0);
      if (gState.lastImportUs > gState.maxImportUs) gState.maxImportUs = gState.lastImportUs;
      gState.imported = source;  // recorded whether the import succeeded or was refused (named in lastImport)
      ++gState.imports;
      return;  // one unit of shadow work per turn
    }
  }
  // C2b: the registered `RevisionEditIndexGrain` child.
  const bool owed = store.editIndexOwed();
  if (!MidiServiceDrain::boundedIdleOwnerChildMayRun(owed, timingCritical, MidiServiceDrain::kRevisionEditIndexMaxUs,
                                                     MidiServiceDrain::kOwnerWorkBudgetUs,
                                                     MidiServiceDrain::kRevisionEditIndexFineGrainUs)) {
    if (owed) ++gState.grainDeferred;
    return;
  }
  const uint32_t t0 = ARM_DWT_CYCCNT;
  const IndexStep step = store.stepEditIndex(kEditIndexRowsPerGrain);
  const uint32_t us = cyclesToUs(ARM_DWT_CYCCNT - t0);
  ++gState.grains;
  if (us > gState.maxGrainUs) gState.maxGrainUs = us;
  if (step == IndexStep::Published) ++gState.indexesPublished;
  if (step == IndexStep::CapacityDenied) ++gState.capacityDenied;
}

void printStatus() {
  if (gState.store == nullptr) {
    Serial.printf_P(PSTR("REVSHADOW,off,init_refused=%u\r\n"), gState.initRefused ? 1u : 0u);
    return;
  }
  ShadowStore& store = *gState.store;
  const SourceIdentity& id = gState.imported;
  Serial.printf_P(PSTR("REVSHADOW,import,result=%u,track=%u,slot=%u,loop=%lu,prev=%lu,cpr=%lu,imports=%lu,last_us=%lu,"
                       "max_us=%lu,capture_runs=%lu,edit_runs=%lu,rows=%lu,hides=%lu,unpaired_offs=%lu\r\n"),
                  static_cast<unsigned>(gState.lastImport), id.track, id.slot, static_cast<unsigned long>(id.loopId),
                  static_cast<unsigned long>(id.playbackRevision), static_cast<unsigned long>(id.committedPassRevision),
                  static_cast<unsigned long>(gState.imports), static_cast<unsigned long>(gState.lastImportUs),
                  static_cast<unsigned long>(gState.maxImportUs),
                  static_cast<unsigned long>(gState.lastStats.captureRuns),
                  static_cast<unsigned long>(gState.lastStats.editRuns),
                  static_cast<unsigned long>(gState.lastStats.rows), static_cast<unsigned long>(gState.lastStats.hides),
                  static_cast<unsigned long>(gState.lastStats.unpairedNoteOffs));
  Serial.printf_P(PSTR("REVSHADOW,index,owed=%u,ready=%u,grains=%lu,max_grain_us=%lu,deferred=%lu,published=%lu,"
                       "capacity_denied=%lu,pages_used=%u,store_bytes=%lu\r\n"),
                  store.editIndexOwed() ? 1u : 0u, store.editIndexReady(store.current()) ? 1u : 0u,
                  static_cast<unsigned long>(gState.grains), static_cast<unsigned long>(gState.maxGrainUs),
                  static_cast<unsigned long>(gState.grainDeferred), static_cast<unsigned long>(gState.indexesPublished),
                  static_cast<unsigned long>(gState.capacityDenied),
                  static_cast<unsigned>(ShadowCap::kPageCount - store.freePageCount()),
                  static_cast<unsigned long>(sizeof(ShadowStore)));
  // Shadow parity (stopped-only diagnostic): the shadow's real-time read vs production's effective view.
  if (gState.lastImport != ImportResult::Ok || id.track >= trackManager.getTrackCount()) return;
  const Track& track = trackManager.getTrack(id.track);
  const Loop& loop = track.getLoop(id.slot);
  const bool sourceStillCurrent = identityOfSelected() == id;
  PassEvent* window = gState.window;
  // Production's effective view: the published snapshot when current, else production's passes-based gather.
  SessionMidiEventVec production;
  const bool snapshotCurrent = loop.publishedSnapshotIsCurrent();
  bool productionOk = loop.tryCopyCurrentSnapshotEditViewEvents(production);
  const char* productionSource = "snapshot";
  if (!productionOk) {
    loop.copyEffectiveCommittedEventsInRange(production, 0, loop.loopLengthTicks);
    productionOk = true;
    productionSource = "passes";
  }
  Serial.printf_P(PSTR("REVSHADOW,production,source=%s,snapshot_current=%u,pending_publish=%u,rows=%u\r\n"),
                  productionSource, snapshotCurrent ? 1u : 0u, loop.hasPendingSnapshotPublish() ? 1u : 0u,
                  static_cast<unsigned>(production.size()));
  using Key = std::tuple<uint32_t, uint8_t, uint8_t, uint8_t, uint8_t>;  // tick, class, status, data1, data2
  auto classOf = [](uint8_t status, uint8_t data2) -> uint8_t {
    const uint8_t type = status & 0xF0;
    if (type == 0x80 || (type == 0x90 && data2 == 0)) return 0;
    return type == 0x90 ? 1 : 2;
  };
  std::vector<Key> a;
  for (const MidiEvent& e : production) {
    const uint8_t status = static_cast<uint8_t>(static_cast<uint8_t>(e.type) | ((e.channel - 1) & 0x0F));
    a.emplace_back(e.tick, classOf(status, e.data.noteData.velocity), status, e.data.noteData.note,
                   e.data.noteData.velocity);
  }
  std::sort(a.begin(), a.end());
  // Multiset diff of the shadow's real-time read against production, under both projections.
  auto report = [&](const char* label, EffectivePolicy policy) {
    const ComposeStamp stamp = store.composeWindow(store.current(), 0, loop.loopLengthTicks, window, kWindowCapacity,
                                                   policy.with(HideLookup::IndexOnly));
    std::vector<Key> b;
    for (uint32_t i = 0; i < stamp.count; ++i) {
      b.emplace_back(window[i].tick, classOf(window[i].status, window[i].data2), window[i].status, window[i].data1,
                     window[i].data2);
    }
    std::sort(b.begin(), b.end());
    std::vector<Key> onlyProduction, onlyShadow;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(onlyProduction));
    std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(onlyShadow));
    Serial.printf_P(PSTR("REVSHADOW,parity_%s,status=%u,used_index=%u,identity=%lu,revision=%lu,source_current=%u,"
                         "production_ok=%u,production_rows=%u,shadow_rows=%lu,production_only=%u,shadow_only=%u\r\n"),
                    label, static_cast<unsigned>(stamp.status), stamp.usedIndex ? 1u : 0u,
                    static_cast<unsigned long>(stamp.identity), static_cast<unsigned long>(stamp.revision),
                    sourceStillCurrent ? 1u : 0u, productionOk ? 1u : 0u, static_cast<unsigned>(a.size()),
                    static_cast<unsigned long>(stamp.count), static_cast<unsigned>(onlyProduction.size()),
                    static_cast<unsigned>(onlyShadow.size()));
    for (size_t i = 0; i < onlyProduction.size() && i < 4; ++i) {
      Serial.printf_P(PSTR("REVSHADOW,diff_%s,production_only,t=%lu,s=%02X,d=%u/%u\r\n"), label,
                      static_cast<unsigned long>(std::get<0>(onlyProduction[i])), std::get<2>(onlyProduction[i]),
                      std::get<3>(onlyProduction[i]), std::get<4>(onlyProduction[i]));
    }
    for (size_t i = 0; i < onlyShadow.size() && i < 4; ++i) {
      Serial.printf_P(PSTR("REVSHADOW,diff_%s,shadow_only,t=%lu,s=%02X,d=%u/%u\r\n"), label,
                      static_cast<unsigned long>(std::get<0>(onlyShadow[i])), std::get<2>(onlyShadow[i]),
                      std::get<3>(onlyShadow[i]), std::get<4>(onlyShadow[i]));
    }
  };
  report("revision", EffectivePolicy::revision());
  report("compat", EffectivePolicy::productionCompat());
  Serial.println(F("REVSHADOW,parity_done"));
}

}  // namespace RevisionShadow

#else

namespace RevisionShadow {
void initAtBoot() {}
void serviceIdleTurn(bool, bool) {}
void printStatus() {}
}  // namespace RevisionShadow

#endif
