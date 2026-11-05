//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The content engine at production capacity, as one root (fixed-memory-engine E9.2, plan § 3 and § 4).
//
// * Placement: two region structs the integration defines once — `EnginePsram` in EXTMEM (page bodies, windows,
//   compose cursors, checkpoint sets, tiles, the E: log: bodies written before read, or large) and `EngineRam2` in
//   DMAMEM (tables, writer, tracks, scheduler). Neither region is zeroed at reset (DEC-183): `EngineRoot::init` writes
//   every field the engine reads before writing, in the E5.3 order, and nothing relies on zero.
// * `census(out)` reports every region member's size (`#CAP,MEM_REGION` rows on the device).
// * Main loop: `submitJobs` once per pass (playback windows and transitions per track on its own timeline, reclaim,
//   one checkpoint sweep), then `runGrain` while the pass has time. The Clock calls `transport().tick` per track.
// * Checkpoint sweeps (background, one at a time): each playing track's slot current, once, when the table holds no
//   set for it and every run it lists has sealed — never a provisional source (E9.1b) or a state still waiting for
//   its completion (E9.1), whose rows may still grow under their identity.
// Display tiles, the edit writer and the output hub are placed and initialised here; their jobs are wired by E9.3,
// E9.5 and E9.6. No allocation.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/Composer.h"
#include "ContentEngine/DisplayTiles.h"
#include "ContentEngine/EditWriter.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/History.h"
#include "ContentEngine/OutputPort.h"
#include "ContentEngine/PlaybackJobs.h"
#include "ContentEngine/PlaybackTrack.h"
#include "ContentEngine/ReclaimJob.h"
#include "ContentEngine/SlotTransport.h"
#include "ContentEngine/WorkerScheduler.h"
#include "ContentEngine/Writer.h"

namespace ContentEngine {

constexpr uint8_t kTrackCount = 8;
constexpr uint32_t kWindowTicks = 1536;              // 2 bars of 4/4 at 192 PPQN (plan § 9.1)
constexpr uint32_t kCheckpointSpacing = 4 * kWindowTicks;  // 8 bars (plan § 7.2)
constexpr uint32_t kGrainUs = 810;                   // measured worst grain g (E5.8)

using ProductionWindow = SpanWindow<Config::kWindowEntries>;
using ProductionHistory = History<ProductionManifests, ProductionRuns>;
using ProductionWriter = Writer<ProductionPool, ProductionRuns>;
using ProductionTerminals = TerminalScheduler<Config::kMaxSoundingPerTrack>;
using ProductionPlayer = WindowPlayer<Config::kMaxSoundingPerTrack>;
using ProductionTrack = PlaybackTrack<ProductionComposer, ProductionCheckpoints, ProductionWindow, ProductionManifests,
                                      ProductionRuns, Config::kMaxSoundingPerTrack>;
using ProductionScheduler = WorkerScheduler<32>;
using ProductionJobs = PlaybackJobs<ProductionTrack, kTrackCount, ProductionScheduler>;
using ProductionReclaim = ReclaimJob<ProductionRuns, ProductionPool>;
using ProductionSweep =
    CheckpointSweep<ProductionComposer, ProductionCheckpoints, ProductionWindow, ProductionManifests, ProductionRuns>;
using ProductionTransport = SlotTransport<ProductionPool, ProductionRuns, ProductionManifests, ProductionHistory,
                                          ProductionTrack, kTrackCount>;
using ProductionTiles = TileStore<>;
using ProductionOverlay = LiveOverlay<>;
using ProductionEditWriter = EditWriter<ProductionPool, ProductionRuns, ProductionManifests, ProductionHistory>;
using ProductionHub = OutputHub<>;

/// EXTMEM.
struct EnginePsram {
  ProductionPool pool;
  ProductionWindow windows[kTrackCount][3];
  ProductionComposer composers[kTrackCount][2];  // steady + transition per track (16 of the 18 cursors)
  ProductionComposer sweepComposer;              // the checkpoint sweep's cursor
  ProductionWindow sweepWindow;
  ProductionCheckpoints checkpoints;
  ProductionTiles tiles;
  ProductionOverlay overlay;
  EditChange editLog[Config::kEditLogEntries];
};

/// DMAMEM.
struct EngineRam2 {
  ProductionRuns runs;
  ProductionManifests manifests;
  ProductionHistory history;
  ProductionReservations reservations;
  NoteIdSource ids;
  ProductionWriter writer;
  ProductionEditIndex index;
  ProductionEditWriter editWriter;
  ProductionTerminals terminals[kTrackCount];
  ProductionPlayer players[kTrackCount];
  ProductionTrack tracks[kTrackCount];
  ProductionScheduler scheduler;
  ProductionJobs jobs;
  ProductionReclaim reclaim;
  ProductionSweep sweep;
  ProductionTransport transport;
  ProductionHub hub;
};

// Budgets (plan § 4.1 / § 4.2, restated from the E9.2 census on the Teensy; owner 2026-11-05: two sweep scratch
// windows budgeted, checkpoint and tile sweeps independent). PSRAM: pool with free stack and owner tags 4,193,216;
// windows with headers 1,183,392; 18 compose cursors 221,184; 2 sweep scratch windows 98,616; checkpoint sets 417,284;
// tiles with headers 602,416; overlay 9,744; worker scratch 50,176 (E9.7); E: log 20,480. RAM2: the engine's tables,
// writer, edit writer, tracks, scheduler and jobs, transport and output hub as measured on the Teensy. A change here is
// a budget change and must update the plan.
constexpr size_t kPsramBudget = 6796508u;
constexpr size_t kRam2Budget = 143360u;  // measured 143,296 + 64 B: each playback job's boundary (diagnostic)
static_assert(sizeof(EnginePsram) <= kPsramBudget, "the engine's PSRAM regions fit plan § 4.1");
#if defined(__arm__)
static_assert(sizeof(EngineRam2) <= kRam2Budget, "the engine's RAM2 regions fit plan § 4.2 (ARM layout)");
#endif

class EngineRoot {
 public:
  enum class Region : uint8_t { Psram, Ram2 };
  struct Stats {
    uint32_t sweepsStarted;
    uint32_t sweepsDone;   // ended Done (a usable set) — Superseded / Refused are not counted here
    uint32_t sweepsEnded;  // every sweep that ended, any status
  };

  /// `blockUs`: B_max, the longest non-engine block of the main loop (plan § 7.4). False when a fixed partition does
  /// not fit (the edit index's pages): a build error in practice.
  bool init(EnginePsram& psram, EngineRam2& ram2, uint32_t blockUs) {
    p_ = &psram;
    r_ = &ram2;
    p_->pool.init();
    r_->runs.init();
    r_->manifests.init();
    if (!r_->index.init(p_->pool, r_->runs)) return false;  // the index partition leaves the free stack first
    r_->reservations.init(p_->pool, r_->runs, kLedgerFixedReservePages);
    r_->history.init(r_->manifests, r_->runs);
    r_->ids.init(1);
    r_->writer.init(p_->pool, r_->runs, r_->reservations, r_->ids);
    r_->editWriter.init(p_->pool, r_->runs, r_->manifests, r_->reservations, r_->history, p_->editLog);
    p_->checkpoints.init();
    p_->tiles.init();
    p_->overlay.init();
    r_->hub.init();
    ProductionTrack* tracks[kTrackCount];
    for (uint8_t t = 0; t < kTrackCount; ++t) {
      p_->composers[t][0].init(p_->pool, r_->runs, &r_->index);
      p_->composers[t][1].init(p_->pool, r_->runs, &r_->index);
      r_->terminals[t].init();
      r_->players[t].init(r_->terminals[t]);
      ProductionWindow* bufs[3] = {&p_->windows[t][0], &p_->windows[t][1], &p_->windows[t][2]};
      r_->tracks[t].init(p_->composers[t][0], p_->composers[t][1], bufs, p_->checkpoints, r_->manifests, r_->runs,
                         r_->players[t], kWindowTicks);
      tracks[t] = &r_->tracks[t];
    }
    p_->sweepComposer.init(p_->pool, r_->runs, &r_->index);
    r_->scheduler.init(kGrainUs, blockUs);
    r_->jobs.init(tracks, r_->scheduler);
    r_->reclaim.bind(r_->runs, p_->pool);
    r_->sweep = ProductionSweep{};
    r_->transport.init(p_->pool, r_->runs, r_->manifests, r_->history, r_->writer, tracks);
    sweeping_ = false;
    sweepNext_ = 0;
    stats_ = Stats{};
    return true;
  }

  /// Once per main-loop pass at Clock tick `clock` (`nowUs`, `usPerTick`): the jobs that are due.
  void submitJobs(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    uint32_t now[kTrackCount];
    for (uint8_t t = 0; t < kTrackCount; ++t) now[t] = clock - r_->transport.origin(t);
    r_->jobs.service(now, nowUs, usPerTick);
    r_->reclaim.submitIfPending(r_->scheduler);
    if (!sweeping_) startSweep(nowUs);
  }
  /// One grain; false when nothing is pending.
  bool runGrain(uint32_t nowUs) { return r_->scheduler.runGrain(nowUs) != ProductionScheduler::kNoJob; }

  ProductionTransport& transport() { return r_->transport; }
  ProductionHistory& history() { return r_->history; }
  ProductionTrack& track(uint8_t t) { return r_->tracks[t]; }
  const ProductionScheduler& scheduler() const { return r_->scheduler; }
  const ProductionJobs& jobs() const { return r_->jobs; }
  const ProductionEditWriter& editWriter() const { return r_->editWriter; }
  const ProductionTiles& tiles() const { return p_->tiles; }
  const ProductionHub& hub() const { return r_->hub; }
  const ProductionCheckpoints& checkpoints() const { return p_->checkpoints; }
  const ProductionPool& pool() const { return p_->pool; }
  const ProductionRuns& runs() const { return r_->runs; }
  const ProductionManifests& manifests() const { return r_->manifests; }
  const ProductionReservations& reservations() const { return r_->reservations; }
  const Stats& stats() const { return stats_; }

  /// Every region member, `out(region, name, bytes)`.
  template <typename Out>
  static void census(Out out) {
    out(Region::Psram, "pool", sizeof(ProductionPool));
    out(Region::Psram, "windows", sizeof(EnginePsram::windows));
    out(Region::Psram, "composers", sizeof(EnginePsram::composers) + sizeof(ProductionComposer));
    out(Region::Psram, "sweep_window", sizeof(ProductionWindow));
    out(Region::Psram, "checkpoints", sizeof(ProductionCheckpoints));
    out(Region::Psram, "tiles", sizeof(ProductionTiles));
    out(Region::Psram, "overlay", sizeof(ProductionOverlay));
    out(Region::Psram, "edit_log", sizeof(EnginePsram::editLog));
    out(Region::Psram, "total", sizeof(EnginePsram));
    out(Region::Ram2, "runs", sizeof(ProductionRuns));
    out(Region::Ram2, "manifests", sizeof(ProductionManifests));
    out(Region::Ram2, "history", sizeof(ProductionHistory));
    out(Region::Ram2, "reservations", sizeof(ProductionReservations));
    out(Region::Ram2, "writer", sizeof(ProductionWriter));
    out(Region::Ram2, "edit_index", sizeof(ProductionEditIndex));
    out(Region::Ram2, "edit_writer", sizeof(ProductionEditWriter));
    out(Region::Ram2, "terminals", sizeof(EngineRam2::terminals));
    out(Region::Ram2, "players", sizeof(EngineRam2::players));
    out(Region::Ram2, "tracks", sizeof(EngineRam2::tracks));
    out(Region::Ram2, "scheduler", sizeof(ProductionScheduler));
    out(Region::Ram2, "jobs", sizeof(ProductionJobs) + sizeof(ProductionReclaim) + sizeof(ProductionSweep));
    out(Region::Ram2, "transport", sizeof(ProductionTransport));
    out(Region::Ram2, "hub", sizeof(ProductionHub));
    out(Region::Ram2, "total", sizeof(EngineRam2));
  }

 private:
  bool sealed(ManifestId m) const {
    const auto& x = r_->manifests.manifest(m);
    for (uint8_t i = 0; i < x.runCount; ++i)
      if (r_->runs.run(x.runs[i]).state == RunState::Writing) return false;
    return true;
  }

  /// The next playing slot whose current has no set and has sealed, round robin over the tracks.
  void startSweep(uint32_t nowUs) {
    for (uint8_t k = 0; k < kTrackCount; ++k) {
      const uint8_t t = static_cast<uint8_t>((sweepNext_ + k) % kTrackCount);
      const uint8_t slot = r_->transport.playingSlot(t);
      if (slot == ProductionTransport::kNoSlot) continue;
      const ManifestId m = r_->history.current(slot);
      if (m == kNoManifest || !sealed(m) || p_->checkpoints.holds(slot, r_->manifests.manifest(m).identity)) continue;
      uint8_t id;
      if (!r_->scheduler.submit(JobSpec{JobClass::Background, nowUs, kSweepGrains, &sweepStep, this}, id)) return;
      r_->sweep.start(p_->sweepComposer, p_->checkpoints, r_->manifests, r_->runs, slot, m, kWindowTicks,
                      kCheckpointSpacing);
      sweeping_ = true;
      sweepNext_ = static_cast<uint8_t>(t + 1);
      ++stats_.sweepsStarted;
      return;
    }
  }
  static JobStep sweepStep(void* ctx) {
    EngineRoot& e = *static_cast<EngineRoot*>(ctx);
    const ProductionSweep::Status st = e.r_->sweep.step(e.p_->sweepWindow, ProductionJobs::kRowsPerGrain);
    if (st == ProductionSweep::Status::Running) return JobStep::Continue;
    e.sweeping_ = false;
    ++e.stats_.sweepsEnded;
    if (st == ProductionSweep::Status::Done) ++e.stats_.sweepsDone;
    return JobStep::Done;
  }

  static constexpr uint32_t kSweepGrains = 6000;  // two loops of a 64-bar take at 128 row units (background: no deadline)

  EnginePsram* p_ = nullptr;
  EngineRam2* r_ = nullptr;
  bool sweeping_ = false;
  uint8_t sweepNext_ = 0;
  Stats stats_{};
};

}  // namespace ContentEngine
