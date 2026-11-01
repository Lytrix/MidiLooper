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
// Pool 320 pages x 64 rows x 16 B = 320 KiB. Store + import job + status window + producer window together stay inside
// the DEC-246 cap of 0.5 MiB (`!REVSHADOW_STATUS` prints the total). History 2: current + previous only.
using ShadowCap = Capacity</*pages*/ 320, /*rows per page*/ 64, /*runs*/ 48, /*pages per run*/ 320, /*manifests*/ 16,
                           /*runs per manifest*/ 48, /*history*/ 2>;
using ShadowStore = Store<ShadowCap>;
using ShadowImportJob = ImportJob<ShadowCap, /*edit targets*/ 128, /*edits*/ 256, /*open notes*/ 128>;

constexpr uint32_t kEditIndexRowsPerGrain = 256;  // B.5d: 256 rows measured at 1.12 ms max
constexpr uint32_t kImportRowsPerGrain = 64;      // declared start; rebased only from device evidence
constexpr uint32_t kWindowRowsPerGrain = 128;     // C3d producer step; declared start
constexpr uint32_t kProducerCapacity = 2048;      // one 2-bar window (dense measured ~1,600 rows); more = CapacityDenied
constexpr uint8_t kDigestCount = 64;              // 64 two-bar windows = 128 bars

struct SourceIdentity {
  uint8_t track = 0xFF;
  uint8_t slot = 0xFF;
  ImportSourceKey key;
  bool operator==(const SourceIdentity& o) const { return track == o.track && slot == o.slot && key == o.key; }
};

constexpr uint32_t kWindowCapacity = 4096;

// Order-independent multiset digest of a window's rows (tick, status, data1, data2): count, sum and xor of a per-row
// mix. Both sides (shadow window, production passes window) are digested the same way.
struct Digest {
  uint32_t count = 0;
  uint64_t sum = 0;
  uint64_t x = 0;
  void add(uint32_t tick, uint8_t status, uint8_t d1, uint8_t d2) {
    uint64_t h = (static_cast<uint64_t>(tick) << 24) ^ (static_cast<uint64_t>(status) << 16) ^
                 (static_cast<uint64_t>(d1) << 8) ^ d2;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    ++count;
    sum += h;
    x ^= h;
  }
  // Disjoint windows combine into the digest of their union (the C4 whole-loop comparison).
  void merge(const Digest& o) {
    count += o.count;
    sum += o.sum;
    x ^= o.x;
  }
  bool operator==(const Digest& o) const { return count == o.count && sum == o.sum && x == o.x; }
};
struct WindowDigest {
  bool valid = false;
  uint32_t identity = 0;  // the manifest identity the window actually read
  uint32_t revision = 0;
  uint32_t fromTick = 0;
  uint32_t toTick = 0;
  Digest digest;
};

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
  uint32_t importPhaseMaxUs[8] = {};  // by the phase a call started in: 0 = begin, 1..7 = Captures .. Publish
  Grain indexGrain;
  Grain windowGrain;
  PassEvent* producerOut = nullptr;  // the producer's window buffer, allocated at boot
  uint32_t producerIdentity = 0;     // the manifest identity the digest table describes
  uint32_t producerNextFrom = 0;
  bool producerComplete = false;     // every window of `producerIdentity` produced once: idle until the identity moves
  uint32_t producerHashed = 0;       // rows of the current window already digested
  Digest producerDigest;
  uint32_t windowsReady = 0;
  uint32_t windowsIndexOwed = 0;
  uint32_t windowsCapacityDenied = 0;
  uint32_t windowsSourceMoved = 0;
  WindowDigest* digests = nullptr;   // kDigestCount entries in PSRAM, allocated at boot (kept out of DTCM)
  uint32_t recentKeys[8] = {};       // inventory hashes of recent publications, for the churn rule
  uint8_t recentKeyCount = 0;
  uint32_t duplicatePublishes = 0;   // a publication of a source identity already published (DEC-246 amendment 12)
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
  void* producer = extmem_malloc(sizeof(PassEvent) * kProducerCapacity);
  void* digests = extmem_malloc(sizeof(WindowDigest) * kDigestCount);
  if (arena == nullptr || job == nullptr || window == nullptr || producer == nullptr || digests == nullptr) {
    if (digests != nullptr) extmem_free(digests);
    if (arena != nullptr) extmem_free(arena);
    if (job != nullptr) extmem_free(job);
    if (window != nullptr) extmem_free(window);
    if (producer != nullptr) extmem_free(producer);
    gState.initRefused = true;
    return;
  }
  gState.store = new (arena) ShadowStore();  // once, at boot; nothing is allocated afterwards
  gState.job = new (job) ShadowImportJob();
  gState.window = static_cast<PassEvent*>(window);
  gState.producerOut = static_cast<PassEvent*>(producer);
  gState.digests = static_cast<WindowDigest*>(digests);
  for (uint8_t i = 0; i < kDigestCount; ++i) new (&gState.digests[i]) WindowDigest();
}

// C3d: the registered `RevisionShadowWindowGrain` child. Consecutive 2-bar windows of the current shadow manifest,
// each digested and labelled with the identity it actually read. A new manifest identity restarts the table.
static void serviceWindowProducer(bool timingCritical) {
  ShadowStore& store = *gState.store;
  const ManifestId current = store.current();
  // One manifest identity -> each window produced once; a complete table is not rebuilt until the identity moves.
  const bool identityMoved = current != kNoManifest && store.identityOf(current) != gState.producerIdentity;
  const bool owed = store.windowActive() || (current != kNoManifest && (identityMoved || !gState.producerComplete));
  if (!MidiServiceDrain::boundedIdleOwnerChildMayRun(owed, timingCritical, MidiServiceDrain::kRevisionShadowWindowMaxUs,
                                                     MidiServiceDrain::kOwnerWorkBudgetUs,
                                                     MidiServiceDrain::kRevisionShadowWindowFineGrainUs)) {
    if (owed) ++gState.windowGrain.deferred;
    return;
  }
  const uint32_t t0 = ARM_DWT_CYCCNT;
  WindowResult r = WindowResult::Pending;
  if (!store.windowActive()) {
    const uint32_t identity = store.identityOf(current);
    const uint32_t length = store.manifest(current).loopLengthTicks;
    if (identity != gState.producerIdentity) {
      for (uint8_t i = 0; i < kDigestCount; ++i) gState.digests[i].valid = false;
      gState.producerIdentity = identity;
      gState.producerNextFrom = 0;
      gState.producerComplete = false;
    }
    if (length == 0) return;
    const uint32_t windowTicks = 2u * Config::TICKS_PER_BAR;
    const uint32_t from = gState.producerNextFrom;
    const uint32_t to = from + windowTicks < length ? from + windowTicks : length;
    gState.producerHashed = 0;
    gState.producerDigest = Digest{};
    r = store.beginWindow(current, identity, store.manifest(current).revision, from, to, gState.producerOut,
                          kProducerCapacity);
  } else {
    r = store.stepWindow(kWindowRowsPerGrain);
  }
  // Digest the rows emitted so far (incremental: bounded by this step's rows).
  const WindowStamp& stamp = store.windowStamp();
  for (; gState.producerHashed < stamp.count; ++gState.producerHashed) {
    const PassEvent& e = gState.producerOut[gState.producerHashed];
    gState.producerDigest.add(e.tick, e.status, e.data1, e.data2);
  }
  if (r == WindowResult::Ready) {
    const uint32_t slot = (stamp.fromTick / (2u * Config::TICKS_PER_BAR)) % kDigestCount;
    gState.digests[slot] = WindowDigest{true, stamp.identity, stamp.revision, stamp.fromTick, stamp.toTick,
                                        gState.producerDigest};
    gState.producerNextFrom = stamp.toTick;
    gState.producerComplete = stamp.toTick >= store.manifest(current).loopLengthTicks;
    ++gState.windowsReady;
  } else if (r == WindowResult::IndexOwed) {
    ++gState.windowsIndexOwed;
  } else if (r == WindowResult::CapacityDenied) {
    ++gState.windowsCapacityDenied;
    gState.producerNextFrom = stamp.toTick;  // skip this window; it is named, never labelled complete
    gState.producerComplete = stamp.toTick >= store.manifest(current).loopLengthTicks;
  } else if (r == WindowResult::SourceMoved) {
    ++gState.windowsSourceMoved;
  }
  gState.windowGrain.record(cyclesToUs(ARM_DWT_CYCCNT - t0));
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
    const uint8_t phaseAtEntry = job.phaseCode();
    ImportStep step = ImportStep::Pending;
    if (!job.active()) {
      gState.lastAttempt = source;
      gState.building = source;
      step = job.begin(trackManager.getTrack(source.track).getLoop(source.slot), store);
    } else {
      step = job.step(kImportRowsPerGrain);
    }
    const uint32_t us = cyclesToUs(ARM_DWT_CYCCNT - t0);
    gState.importGrain.record(us);
    if (phaseAtEntry < 8 && us > gState.importPhaseMaxUs[phaseAtEntry]) gState.importPhaseMaxUs[phaseAtEntry] = us;
    if (step == ImportStep::Published) {
      const uint32_t keyHash = gState.building.key.inventory ^ (gState.building.key.playbackRevision * 2654435761u) ^
                               (static_cast<uint32_t>(gState.building.track) << 24) ^ gState.building.slot;
      for (uint8_t i = 0; i < gState.recentKeyCount; ++i) {
        if (gState.recentKeys[i] == keyHash) ++gState.duplicatePublishes;
      }
      gState.recentKeys[gState.recentKeyCount % 8] = keyHash;
      if (gState.recentKeyCount < 8) ++gState.recentKeyCount;
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
  if (owed && !MidiServiceDrain::boundedIdleOwnerChildMayRun(owed, timingCritical,
                                                              MidiServiceDrain::kRevisionEditIndexMaxUs,
                                                              MidiServiceDrain::kOwnerWorkBudgetUs,
                                                              MidiServiceDrain::kRevisionEditIndexFineGrainUs)) {
    ++gState.indexGrain.deferred;
    return;
  }
  if (owed) {
    const uint32_t t0 = ARM_DWT_CYCCNT;
    const IndexStep step = store.stepEditIndex(kEditIndexRowsPerGrain);
    gState.indexGrain.record(cyclesToUs(ARM_DWT_CYCCNT - t0));
    if (step == IndexStep::Published) ++gState.indexesPublished;
    if (step == IndexStep::CapacityDenied) ++gState.capacityDenied;
    return;
  }
  serviceWindowProducer(timingCritical);
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
                       "capacity_denied=%lu,pages_used=%u,shadow_total_bytes=%lu\r\n"),
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
                  static_cast<unsigned long>(sizeof(ShadowStore) + sizeof(ShadowImportJob) +
                                             sizeof(PassEvent) * (kWindowCapacity + kProducerCapacity)));
  {
    const uint32_t* m = gState.importPhaseMaxUs;
    Serial.printf_P(PSTR("REVSHADOW,import_phases,begin_us=%lu,captures_us=%lu,pair_walk_us=%lu,pair_wrap_us=%lu,"
                         "edit_apply_us=%lu,edit_sort_us=%lu,edit_emit_us=%lu,publish_us=%lu,slow_seals=%lu\r\n"),
                    static_cast<unsigned long>(m[0]), static_cast<unsigned long>(m[1]),
                    static_cast<unsigned long>(m[2]), static_cast<unsigned long>(m[3]),
                    static_cast<unsigned long>(m[4]), static_cast<unsigned long>(m[5]),
                    static_cast<unsigned long>(m[6]), static_cast<unsigned long>(m[7]),
                    static_cast<unsigned long>(store.slowSeals()));
  }
  Serial.printf_P(PSTR("REVSHADOW,windows,calls=%lu,max_us=%lu,deferred=%lu,ready=%lu,index_owed=%lu,capacity_denied=%lu,"
                       "source_moved=%lu,duplicate_publishes=%lu\r\n"),
                  static_cast<unsigned long>(gState.windowGrain.calls),
                  static_cast<unsigned long>(gState.windowGrain.maxUs),
                  static_cast<unsigned long>(gState.windowGrain.deferred),
                  static_cast<unsigned long>(gState.windowsReady), static_cast<unsigned long>(gState.windowsIndexOwed),
                  static_cast<unsigned long>(gState.windowsCapacityDenied),
                  static_cast<unsigned long>(gState.windowsSourceMoved),
                  static_cast<unsigned long>(gState.duplicatePublishes));
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

void printWindows() {
  if (gState.store == nullptr) {
    Serial.println(F("REVSHADOW,windows_off"));
    return;
  }
  ShadowStore& store = *gState.store;
  const SourceIdentity& id = gState.published;
  const bool sourceCurrent = identityOfSelected() == id;
  const uint32_t currentIdentity = store.identityOf(store.current());
  uint32_t compared = 0, matched = 0, stale = 0;
  bool firstMismatchShown = false;
  // Whole loop (C4a): a windowed gather keeps the NoteOff of a note whose NoteOn lies outside the window, so per-window
  // agreement can hide a misattributed NoteOff on both sides. The union of the shadow's windows is compared against
  // production's whole-loop passes gather as well.
  Digest shadowWhole, productionWhole;
  uint32_t coveredTicks = 0, loopTicks = 0;
  bool wholeAvailable = false;
  if (id.track < trackManager.getTrackCount()) {
    const Loop& loop = trackManager.getTrack(id.track).getLoop(id.slot);
    loopTicks = loop.loopLengthTicks;
    SessionMidiEventVec whole;
    loop.copyEffectiveCommittedEventsInRange(whole, 0, loopTicks);
    for (const MidiEvent& e : whole) {
      if (e.tick >= loopTicks) continue;
      const uint8_t status = static_cast<uint8_t>(static_cast<uint8_t>(e.type) | ((e.channel - 1) & 0x0F));
      productionWhole.add(e.tick, status, e.data.noteData.note, e.data.noteData.velocity);
    }
    wholeAvailable = true;
    for (uint8_t i = 0; i < kDigestCount; ++i) {
      const WindowDigest& w = gState.digests[i];
      if (!w.valid) continue;
      if (w.identity != currentIdentity) {
        ++stale;
        continue;
      }
      // The C4 oracle: production's passes-derived effective view of the same interval.
      SessionMidiEventVec production;
      loop.copyEffectiveCommittedEventsInRange(production, w.fromTick, w.toTick - w.fromTick);
      Digest p;
      for (const MidiEvent& e : production) {
        if (e.tick < w.fromTick || e.tick >= w.toTick) continue;
        const uint8_t status = static_cast<uint8_t>(static_cast<uint8_t>(e.type) | ((e.channel - 1) & 0x0F));
        p.add(e.tick, status, e.data.noteData.note, e.data.noteData.velocity);
      }
      ++compared;
      shadowWhole.merge(w.digest);
      coveredTicks += w.toTick - w.fromTick;
      if (p == w.digest) {
        ++matched;
      } else if (!firstMismatchShown) {
        firstMismatchShown = true;
        Serial.printf_P(PSTR("REVSHADOW,window_mismatch,from=%lu,to=%lu,shadow_rows=%lu,production_rows=%lu\r\n"),
                        static_cast<unsigned long>(w.fromTick), static_cast<unsigned long>(w.toTick),
                        static_cast<unsigned long>(w.digest.count), static_cast<unsigned long>(p.count));
      }
    }
  }
  Serial.printf_P(PSTR("REVSHADOW,windows_compare,source_current=%u,identity=%lu,compared=%lu,matched=%lu,"
                       "stale_identity=%lu\r\n"),
                  sourceCurrent ? 1u : 0u, static_cast<unsigned long>(currentIdentity),
                  static_cast<unsigned long>(compared), static_cast<unsigned long>(matched),
                  static_cast<unsigned long>(stale));
  const bool covered = wholeAvailable && loopTicks > 0 && coveredTicks == loopTicks;
  const bool wholeMatch = covered && shadowWhole == productionWhole;
  Serial.printf_P(PSTR("REVSHADOW,windows_whole_loop,covered=%u,shadow_rows=%lu,production_rows=%lu,match=%u,"
                       "c4_clean=%u\r\n"),
                  covered ? 1u : 0u, static_cast<unsigned long>(shadowWhole.count),
                  static_cast<unsigned long>(productionWhole.count), wholeMatch ? 1u : 0u,
                  (sourceCurrent && compared > 0 && matched == compared && wholeMatch) ? 1u : 0u);
}

}  // namespace RevisionShadow

#else

namespace RevisionShadow {
void initAtBoot() {}
void serviceIdleTurn(bool, bool) {}
void printStatus() {}
void printWindows() {}
}  // namespace RevisionShadow

#endif
