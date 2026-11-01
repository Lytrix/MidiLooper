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

#include "CommittedEventRange.h"
#include "RevisionStore/RevisionImport.h"
#include "RevisionStore/RevisionStore.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/MidiServiceDrain.h"

// The whole TU is routed to FLASH by linker/imxrt1062_t41_lcr.ld (`*RevisionShadow*.o`).

namespace RevisionShadow {
namespace {

using namespace RevisionStore;
// Pool 416 pages x 64 rows x 16 B = 416 KiB; with tables and the import job's fixed state the shadow stays inside the
// DEC-246 cap of 0.5 MiB (the boot row prints the exact bytes). History 2: current + previous only.
using ShadowCap = Capacity</*pages*/ 416, /*rows per page*/ 64, /*runs*/ 48, /*pages per run*/ 416, /*manifests*/ 16,
                           /*runs per manifest*/ 48, /*history*/ 2>;
using ShadowStore = Store<ShadowCap>;
using ShadowImportJob = ImportJob<ShadowCap, /*edit targets*/ 128, /*edits*/ 256, /*open notes*/ 128>;

constexpr uint32_t kEditIndexRowsPerGrain = 256;  // B.5d: 256 rows measured at 1.12 ms max
constexpr uint32_t kImportRowsPerGrain = 64;      // declared start; rebased only from device evidence

struct SourceIdentity {
  uint8_t track = 0xFF;
  uint8_t slot = 0xFF;
  ImportSourceKey key;
  bool operator==(const SourceIdentity& o) const { return track == o.track && slot == o.slot && key == o.key; }
};

constexpr uint32_t kWindowCapacity = 8192;

struct Grain {
  uint32_t calls = 0;
  uint32_t deferred = 0;
  uint32_t maxUs = 0;
  void record(uint32_t us) {
    ++calls;
    if (us > maxUs) maxUs = us;
  }
};

struct State {
  ShadowStore* store = nullptr;
  ShadowImportJob* job = nullptr;
  PassEvent* window = nullptr;  // status/parity diagnostic buffer, allocated with the store at boot
  bool initRefused = false;
  SourceIdentity published;     // the source the published shadow manifest was imported from
  SourceIdentity building;      // the source of the candidate in progress
  SourceIdentity lastAttempt;   // the last source a candidate was begun for (a refused key is not retried)
  ImportResult lastResult = ImportResult::NoCommittedSource;
  ImportStats lastStats;
  uint32_t publishes = 0;
  uint32_t sourceMoved = 0;
  uint32_t refusals = 0;
  Grain importGrain;
  Grain indexGrain;
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
  id.key = importSourceKeyOf(track.getActiveLoop());
  return id;
}

}  // namespace

void initAtBoot() {
  void* arena = extmem_malloc(sizeof(ShadowStore));
  void* job = extmem_malloc(sizeof(ShadowImportJob));
  void* window = extmem_malloc(sizeof(PassEvent) * kWindowCapacity);
  if (arena == nullptr || job == nullptr || window == nullptr) {
    if (arena != nullptr) extmem_free(arena);
    if (job != nullptr) extmem_free(job);
    if (window != nullptr) extmem_free(window);
    gState.initRefused = true;
    return;
  }
  gState.store = new (arena) ShadowStore();  // once, at boot; nothing is allocated afterwards
  gState.job = new (job) ShadowImportJob();
  gState.window = static_cast<PassEvent*>(window);
}

void serviceIdleTurn(bool timingCritical, bool /*transportRunning*/) {
  if (gState.store == nullptr) return;
  ShadowStore& store = *gState.store;
  ShadowImportJob& job = *gState.job;
  // C3b: the registered `RevisionShadowImportGrain` child -- a bounded, transactional candidate in both regimes. The
  // frozen key makes it safe under transport: a moving source cancels the candidate instead of mixing states.
  const SourceIdentity source = identityOfSelected();
  const bool importOwed = job.active() || (source.track != 0xFF && !(source == gState.published) &&
                                           !(source == gState.lastAttempt && !job.active()));
  if (MidiServiceDrain::boundedIdleOwnerChildMayRun(importOwed, timingCritical,
                                                    MidiServiceDrain::kRevisionShadowImportMaxUs,
                                                    MidiServiceDrain::kOwnerWorkBudgetUs,
                                                    MidiServiceDrain::kRevisionShadowImportFineGrainUs)) {
    const uint32_t t0 = ARM_DWT_CYCCNT;
    ImportStep step = ImportStep::Pending;
    if (!job.active()) {
      gState.lastAttempt = source;
      gState.building = source;
      step = job.begin(trackManager.getTrack(source.track).getLoop(source.slot), store);
    } else {
      step = job.step(kImportRowsPerGrain);
    }
    gState.importGrain.record(cyclesToUs(ARM_DWT_CYCCNT - t0));
    if (step == ImportStep::Published) {
      gState.published = gState.building;
      gState.lastResult = ImportResult::Ok;
      gState.lastStats = job.stats();
      ++gState.publishes;
    } else if (step == ImportStep::Refused) {
      gState.lastResult = job.result();
      gState.lastStats = job.stats();
      if (job.result() == ImportResult::SourceMoved) {
        ++gState.sourceMoved;
        gState.lastAttempt = SourceIdentity{};  // the new source may begin on the next turn
      } else {
        ++gState.refusals;
      }
    }
    return;  // one unit of shadow work per turn
  } else if (importOwed) {
    ++gState.importGrain.deferred;
  }
  // C2b: the registered `RevisionEditIndexGrain` child.
  const bool owed = store.editIndexOwed();
  if (!MidiServiceDrain::boundedIdleOwnerChildMayRun(owed, timingCritical, MidiServiceDrain::kRevisionEditIndexMaxUs,
                                                     MidiServiceDrain::kOwnerWorkBudgetUs,
                                                     MidiServiceDrain::kRevisionEditIndexFineGrainUs)) {
    if (owed) ++gState.indexGrain.deferred;
    return;
  }
  const uint32_t t0 = ARM_DWT_CYCCNT;
  const IndexStep step = store.stepEditIndex(kEditIndexRowsPerGrain);
  gState.indexGrain.record(cyclesToUs(ARM_DWT_CYCCNT - t0));
  if (step == IndexStep::Published) ++gState.indexesPublished;
  if (step == IndexStep::CapacityDenied) ++gState.capacityDenied;
}

void printStatus() {
  if (gState.store == nullptr) {
    Serial.printf_P(PSTR("REVSHADOW,off,init_refused=%u\r\n"), gState.initRefused ? 1u : 0u);
    return;
  }
  ShadowStore& store = *gState.store;
  const SourceIdentity& id = gState.published;
  Serial.printf_P(PSTR("REVSHADOW,import,last_result=%u,track=%u,slot=%u,loop=%lu,prev=%lu,cpr=%lu,publishes=%lu,"
                       "source_moved=%lu,refusals=%lu,building=%u,capture_runs=%lu,edit_runs=%lu,rows=%lu,hides=%lu,"
                       "unpaired_offs=%lu,job_steps=%lu\r\n"),
                  static_cast<unsigned>(gState.lastResult), id.track, id.slot, static_cast<unsigned long>(id.key.loopId),
                  static_cast<unsigned long>(id.key.playbackRevision),
                  static_cast<unsigned long>(id.key.committedPassRevision), static_cast<unsigned long>(gState.publishes),
                  static_cast<unsigned long>(gState.sourceMoved), static_cast<unsigned long>(gState.refusals),
                  gState.job->active() ? 1u : 0u, static_cast<unsigned long>(gState.lastStats.captureRuns),
                  static_cast<unsigned long>(gState.lastStats.editRuns),
                  static_cast<unsigned long>(gState.lastStats.rows), static_cast<unsigned long>(gState.lastStats.hides),
                  static_cast<unsigned long>(gState.lastStats.unpairedNoteOffs),
                  static_cast<unsigned long>(gState.lastStats.steps));
  Serial.printf_P(PSTR("REVSHADOW,grains,import_calls=%lu,import_max_us=%lu,import_deferred=%lu,index_calls=%lu,"
                       "index_max_us=%lu,index_deferred=%lu,index_owed=%u,index_ready=%u,indexes_published=%lu,"
                       "capacity_denied=%lu,pages_used=%u,shadow_bytes=%lu\r\n"),
                  static_cast<unsigned long>(gState.importGrain.calls),
                  static_cast<unsigned long>(gState.importGrain.maxUs),
                  static_cast<unsigned long>(gState.importGrain.deferred),
                  static_cast<unsigned long>(gState.indexGrain.calls),
                  static_cast<unsigned long>(gState.indexGrain.maxUs),
                  static_cast<unsigned long>(gState.indexGrain.deferred), store.editIndexOwed() ? 1u : 0u,
                  store.editIndexReady(store.current()) ? 1u : 0u,
                  static_cast<unsigned long>(gState.indexesPublished),
                  static_cast<unsigned long>(gState.capacityDenied),
                  static_cast<unsigned>(ShadowCap::kPageCount - store.freePageCount()),
                  static_cast<unsigned long>(sizeof(ShadowStore) + sizeof(ShadowImportJob)));
  // Shadow parity (stopped-only diagnostic): the shadow's real-time read vs production's effective view.
  if (gState.publishes == 0 || id.track >= trackManager.getTrackCount()) return;
  const Track& track = trackManager.getTrack(id.track);
  const Loop& loop = track.getLoop(id.slot);
  const bool sourceStillCurrent = identityOfSelected() == id;
  PassEvent* window = gState.window;
  // Production's two effective surfaces: the published snapshot (when current) and the passes-based gather.
  using Key = std::tuple<uint32_t, uint8_t, uint8_t, uint8_t, uint8_t>;  // tick, class, status, data1, data2
  auto classOf = [](uint8_t status, uint8_t data2) -> uint8_t {
    const uint8_t type = status & 0xF0;
    if (type == 0x80 || (type == 0x90 && data2 == 0)) return 0;
    return type == 0x90 ? 1 : 2;
  };
  auto keysOf = [&](const SessionMidiEventVec& events) {
    std::vector<Key> keys;
    for (const MidiEvent& e : events) {
      const uint8_t status = static_cast<uint8_t>(static_cast<uint8_t>(e.type) | ((e.channel - 1) & 0x0F));
      keys.emplace_back(e.tick, classOf(status, e.data.noteData.velocity), status, e.data.noteData.note,
                        e.data.noteData.velocity);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
  };
  SessionMidiEventVec snapshotEvents;
  const bool snapshotOk = loop.tryCopyCurrentSnapshotEditViewEvents(snapshotEvents);
  SessionMidiEventVec passesEvents;
  loop.copyEffectiveCommittedEventsInRange(passesEvents, 0, loop.loopLengthTicks);
  const std::vector<Key> snapshotKeys = keysOf(snapshotEvents);
  const std::vector<Key> passesKeys = keysOf(passesEvents);
  Serial.printf_P(PSTR("REVSHADOW,production,snapshot_current=%u,pending_publish=%u,snapshot_rows=%u,passes_rows=%u\r\n"),
                  snapshotOk ? 1u : 0u, loop.hasPendingSnapshotPublish() ? 1u : 0u,
                  static_cast<unsigned>(snapshotKeys.size()), static_cast<unsigned>(passesKeys.size()));
  const ComposeStamp stamp = store.composeWindow(store.current(), 0, loop.loopLengthTicks, window, kWindowCapacity,
                                                 EffectivePolicy::revision().with(HideLookup::IndexOnly));
  std::vector<Key> b;
  for (uint32_t i = 0; i < stamp.count; ++i) {
    b.emplace_back(window[i].tick, classOf(window[i].status, window[i].data2), window[i].status, window[i].data1,
                   window[i].data2);
  }
  std::sort(b.begin(), b.end());
  auto report = [&](const char* label, const std::vector<Key>& a, bool available) {
    if (!available) {
      Serial.printf_P(PSTR("REVSHADOW,parity_%s,unavailable\r\n"), label);
      return;
    }
    std::vector<Key> onlyProduction, onlyShadow;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(onlyProduction));
    std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(onlyShadow));
    Serial.printf_P(PSTR("REVSHADOW,parity_%s,status=%u,used_index=%u,identity=%lu,revision=%lu,source_current=%u,"
                         "production_rows=%u,shadow_rows=%lu,production_only=%u,shadow_only=%u\r\n"),
                    label, static_cast<unsigned>(stamp.status), stamp.usedIndex ? 1u : 0u,
                    static_cast<unsigned long>(stamp.identity), static_cast<unsigned long>(stamp.revision),
                    sourceStillCurrent ? 1u : 0u, static_cast<unsigned>(a.size()),
                    static_cast<unsigned long>(stamp.count), static_cast<unsigned>(onlyProduction.size()),
                    static_cast<unsigned>(onlyShadow.size()));
    for (size_t i = 0; i < onlyProduction.size() && i < 6; ++i) {
      Serial.printf_P(PSTR("REVSHADOW,diff_%s,production_only,t=%lu,s=%02X,d=%u/%u\r\n"), label,
                      static_cast<unsigned long>(std::get<0>(onlyProduction[i])), std::get<2>(onlyProduction[i]),
                      std::get<3>(onlyProduction[i]), std::get<4>(onlyProduction[i]));
    }
    for (size_t i = 0; i < onlyShadow.size() && i < 6; ++i) {
      Serial.printf_P(PSTR("REVSHADOW,diff_%s,shadow_only,t=%lu,s=%02X,d=%u/%u\r\n"), label,
                      static_cast<unsigned long>(std::get<0>(onlyShadow[i])), std::get<2>(onlyShadow[i]),
                      std::get<3>(onlyShadow[i]), std::get<4>(onlyShadow[i]));
    }
  };
  report("rev_vs_snapshot", snapshotKeys, snapshotOk);
  report("rev_vs_passes", passesKeys, true);
  // Edit diagnostics: the active Note edit passes and their targets' committed rows as production stores them.
  SessionMidiEventVec committedRows;
  {
    std::vector<const CommittedChunkIdList*> lists;
    if (loop.passes.hasRecordPass()) lists.push_back(&loop.passes.recordPass.committedChunkIds);
    for (const OverdubPass& pass : loop.passes.overdubPasses) {
      if (pass.state == CapturePassState::Active) lists.push_back(&pass.committedChunkIds);
    }
    CommittedEventRange::full(lists.data(), lists.size(), loop.loopLengthTicks).appendTo(committedRows);
  }
  uint8_t shown = 0;
  for (const EditPass& edit : loop.passes.editPasses) {
    if (edit.state != EditPassState::Active || shown >= 6) continue;
    ++shown;
    Serial.printf_P(PSTR("REVSHADOW,edit,id=%lu,action=%u,prop=%u,target=%lu,start=%lu,end=%lu,pitch=%u\r\n"),
                    static_cast<unsigned long>(edit.id), static_cast<unsigned>(edit.actionType),
                    static_cast<unsigned>(edit.propertyType), static_cast<unsigned long>(edit.targetNoteId),
                    static_cast<unsigned long>(edit.startTick), static_cast<unsigned long>(edit.endTick), edit.pitch);
    for (const MidiEvent& e : committedRows) {
      if ((e.noteId == edit.targetNoteId && e.noteId != kInvalidNoteId) ||
          (e.isNoteOff() && e.data.noteData.note == edit.pitch)) {
        Serial.printf_P(PSTR("REVSHADOW,edit_target_row,target=%lu,t=%lu,type=%u,pitch=%u,vel=%u\r\n"),
                        static_cast<unsigned long>(edit.targetNoteId), static_cast<unsigned long>(e.tick),
                        static_cast<unsigned>(e.type), e.data.noteData.note, e.data.noteData.velocity);
      }
    }
  }
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
