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
// * Clock path (E9.3, plan § 9.2): `clockTick` plays every track and routes its events into the output hub on the
//   track's ports (`ClockOutput`); gestures that end notes (`stop`, `undo`, `redo`, a live NoteOn) route theirs too.
//   The caller puts each port's `transmit` result on the wire: port 0 DIN, 1 USB device, 2 USB host.
// Display tiles and the edit writer are placed and initialised here; their jobs are wired by E9.5 and E9.6.
// No allocation.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/ClockOutput.h"
#include "ContentEngine/Composer.h"
#include "ContentEngine/DisplayTiles.h"
#include "ContentEngine/EditWriter.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/NoteEditSession.h"
#include "ContentEngine/PianoRollRender.h"
#include "ContentEngine/History.h"
#include "ContentEngine/OutputPort.h"
#include "ContentEngine/PlaybackJobs.h"
#include "ContentEngine/PlaybackTrack.h"
#include "ContentEngine/ReclaimJob.h"
#include "ContentEngine/SlotTransport.h"
#include "ContentEngine/TrackControl.h"
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
using ProductionDisplayComposer = Composer<ProductionPool, ProductionRuns, 64, 48>;  // every effective note (piano roll)
using ProductionDisplayJobs = DisplayJobs<ProductionDisplayComposer, ProductionWindow, ProductionManifests,
                                          ProductionRuns, ProductionTiles, ProductionScheduler, /*workers*/ 1>;
using ProductionNoteEdit = NoteEditSession<ProductionPool, ProductionRuns, ProductionManifests, ProductionHistory,
                                           ProductionDisplayComposer, ProductionWindow, ProductionEditWriter>;
using ProductionEmit = ProductionPlayer::Emit;
using ProductionOutput =
    ClockOutput<ProductionHub, ProductionEmit, kTrackCount, Config::kMaxSoundingPerTrack, /*ports*/ 3>;
constexpr uint8_t kPortDin = 0, kPortUsbDevice = 1, kPortUsbHost = 2;
using ProductionControl =
    TrackControl<ProductionTransport, ProductionOutput, ProductionEmit, kTrackCount, Config::kMaxSoundingPerTrack>;
constexpr uint32_t kTicksPerBar = 768;  // 4/4 at 192 PPQN (the runtime value comes from the time signature)
constexpr uint32_t kLaunchGridTicks = 48;  // a 16th
constexpr uint32_t kLateJoinTicks = 24;     // owner ruling 2026-11-05: a late join at most a 32nd ...
constexpr uint32_t kLateJoinUs = 25000;     // ... and at most 25 ms after the boundary

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
  // E9.5: the display cursor and scratch window (the 18th cursor and the 2nd sweep window: the NOTE_EDIT gather now,
  // shared with the tile sweep in E9.6), and the gathered selection (from the worker-scratch line).
  ProductionDisplayComposer displayComposer;
  ProductionWindow displayWindow;
  EditNote editGather[Config::kEditSelection];
  uint16_t editGatherRuns[Config::kEditSelection];
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
  ProductionOutput output;
  ProductionControl control;
  ProductionNoteEdit noteEdit;
  ProductionDisplayJobs displayJobs;
};

// Budgets (plan § 4.1 / § 4.2, restated from the E9.2 census on the Teensy; owner 2026-11-05: two sweep scratch
// windows budgeted, checkpoint and tile sweeps independent). PSRAM: pool with free stack and owner tags 4,193,216;
// windows with headers 1,183,392; 18 compose cursors 221,184; 2 sweep scratch windows 98,616; checkpoint sets 417,284;
// tiles with headers 602,416; overlay 9,744; worker scratch 50,176 (E9.7); E: log 20,480. RAM2: the engine's tables,
// writer, edit writer, tracks, scheduler and jobs, transport and output hub as measured on the Teensy. A change here is
// a budget change and must update the plan.
constexpr size_t kPsramBudget = 6796508u;
constexpr size_t kRam2Budget = 149432u;  // measured (E9.6: + 2,376 B display jobs; E9.5: + 1,024 B NOTE_EDIT; E9.4): + 64 B job boundaries, + 1,160 B output and thru keys,
                                          // + 1,248 B late-join state, + 248 B gesture state
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
    r_->hub.configure(kPortDin, kDinMessageMicros);
    r_->hub.configure(kPortUsbDevice, kUsbMessageMicros);
    r_->hub.configure(kPortUsbHost, kUsbMessageMicros);
    r_->output.init(r_->hub);
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
    r_->control.init(r_->transport, r_->output, kTicksPerBar, kLaunchGridTicks);
    p_->displayComposer.init(p_->pool, r_->runs, &r_->index);
    r_->noteEdit.init(p_->pool, r_->runs, r_->manifests, r_->history, p_->displayComposer, p_->displayWindow,
                      r_->editWriter, p_->editGather, p_->editGatherRuns, kWindowTicks);
    gatherLive_ = rebuildLive_ = false;
    ProductionDisplayComposer* dc[1] = {&p_->displayComposer};
    ProductionWindow* dw[1] = {&p_->displayWindow};
    r_->displayJobs.init(dc, dw, p_->tiles, r_->manifests, r_->runs, r_->scheduler, kWindowTicks, kTicksPerBar);
    overlaySlot_ = ProductionTransport::kNoSlot;
    overlayUntil_ = 0;
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
    // NOTE_EDIT (E9.5): the gather and the rebuild are transition-class (an edit is heard at once).
    uint8_t id;
    if (!gatherLive_ && r_->noteEdit.gatherPending() &&
        r_->scheduler.submit(JobSpec{JobClass::Transition, nowUs, kEditGrains, &gatherStep, this}, id))
      gatherLive_ = true;
    if (!rebuildLive_ && r_->noteEdit.rebuildPending() &&
        r_->scheduler.submit(JobSpec{JobClass::Transition, nowUs, kEditGrains, &rebuildStep, this}, id))
      rebuildLive_ = true;
    serviceDisplay(clock);
    uint8_t slot;
    if (r_->noteEdit.takePublished(slot)) {
      ProductionEmit buf[kGestureEmits];
      r_->output.route(buf, r_->transport.follow(slot, clock, buf, kGestureEmits), nowUs, usPerTick);
    }
  }
  /// The Clock tick `clock` at `nowUs`: the gestures due (E9.4), then every track plays; its events go to the hub on
  /// its ports.
  void clockTick(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    r_->control.beforeTick(clock, nowUs, usPerTick);
    // Bounded late join (owner ruling 2026-11-05): the earlier of 24 ticks and 25 ms at this tempo.
    const uint32_t lateTicks = usPerTick == 0 ? 0 : (kLateJoinUs / usPerTick < kLateJoinTicks ? kLateJoinUs / usPerTick : kLateJoinTicks);
    for (uint8_t t = 0; t < kTrackCount; ++t) r_->tracks[t].setLateJoinLimit(lateTicks);
    ProductionEmit buf[kTickEmits];
    for (uint8_t t = 0; t < kTrackCount; ++t)
      r_->output.route(buf, r_->transport.tick(t, clock, buf, kTickEmits), nowUs, usPerTick);
  }
  /// Port `port`'s messages due at `nowUs` (at most `max`), in order, for the wire.
  uint8_t transmit(uint8_t port, uint32_t nowUs, ProductionHub::Sent* out, uint8_t max) {
    return r_->hub.transmit(nowUs, port, out, max);
  }

  /// Gestures whose NoteOffs reach the ports (the transport's own results otherwise).
  ProductionTransport::Result stop(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    ProductionEmit buf[Config::kMaxSoundingPerTrack];
    uint8_t n;
    const auto r = r_->transport.stop(clock, buf, Config::kMaxSoundingPerTrack, n);
    r_->output.route(buf, n, nowUs, usPerTick);
    return r;
  }
  ProductionTransport::Result undo(uint8_t slot, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    ProductionEmit buf[kGestureEmits];
    uint8_t n;
    const auto r = r_->transport.undo(slot, clock, buf, kGestureEmits, n);
    r_->output.route(buf, n, nowUs, usPerTick);
    return r;
  }
  ProductionTransport::Result redo(uint8_t slot, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    ProductionEmit buf[kGestureEmits];
    uint8_t n;
    const auto r = r_->transport.redo(slot, clock, buf, kGestureEmits, n);
    r_->output.route(buf, n, nowUs, usPerTick);
    return r;
  }
  /// A live NoteOn on the writing track (the played note itself goes out through the input path, E9.4).
  ProductionTransport::Result noteOn(uint32_t clock, uint8_t channel, uint8_t pitch, uint8_t velocity, uint32_t nowUs,
                                     uint32_t usPerTick) {
    ProductionEmit buf[Config::kMaxSoundingPerTrack];
    uint8_t n;
    const auto r = r_->transport.noteOn(clock, channel, pitch, velocity, buf, Config::kMaxSoundingPerTrack, n);
    r_->output.route(buf, n, nowUs, usPerTick);
    return r;
  }

  // ---- Display (E9.6) ---------------------------------------------------------------------------------------------
  /// The slot the piano roll shows and the one after it (both get span tiles; focus is swept first).
  void setDisplayFocus(uint8_t slot, uint8_t successor) {
    p_->tiles.setDetail(0, slot);
    p_->tiles.setDetail(1, successor);
    r_->displayJobs.setFocus(slot);
    // Detail tiles now show this slot: sweep it again for them (its density tiles stay meanwhile).
    p_->tiles.setWanted(slot, 0);
  }
  /// The live overlay's view (the detailed piano roll's): loop length, first position, ticks per column, lowest pitch.
  void setOverlayView(uint32_t loop, uint32_t first, uint32_t ticksPerColumn, uint8_t lowPitch) {
    p_->overlay.setView(loop, first, ticksPerColumn, lowPitch);
  }
  /// Live input with the overlay painted when the take is on the shown slot.
  void inputNoteOn(uint32_t clock, uint8_t status, uint8_t pitch, uint8_t velocity, uint32_t nowUs, uint32_t usPerTick) {
    r_->control.inputNoteOn(clock, status, pitch, velocity, nowUs, usPerTick);
    if (overlayLive()) p_->overlay.noteOn(r_->transport.writePosition(clock), pitch, velocity);
  }
  void inputNoteOff(uint32_t clock, uint8_t status, uint8_t pitch, uint32_t nowUs) {
    if (overlayLive()) p_->overlay.noteOff(r_->transport.writePosition(clock), pitch);
    r_->control.inputNoteOff(clock, status, pitch, nowUs);
  }
  /// Bands of the detailed note layer and the overview strip (`PianoRollRender.h`).
  void drawRollBand(const RollView& v, uint8_t* fb, uint16_t width, int16_t rowFrom, int16_t rowTo) const {
    const bool live = v.slot == overlaySlot_;
    ContentEngine::drawRollBand(p_->tiles, live ? &p_->overlay : nullptr, v, fb, width, rowFrom, rowTo);
  }
  const ProductionDisplayJobs& displayJobs() const { return r_->displayJobs; }

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
  ProductionOutput& output() { return r_->output; }
  ProductionControl& control() { return r_->control; }
  ProductionNoteEdit& noteEdit() { return r_->noteEdit; }
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
    out(Region::Psram, "display_cursor", sizeof(ProductionDisplayComposer) + sizeof(ProductionWindow));
    out(Region::Psram, "edit_gather", sizeof(EnginePsram::editGather) + sizeof(EnginePsram::editGatherRuns));
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
    out(Region::Ram2, "output_keys", sizeof(ProductionOutput));
    out(Region::Ram2, "control", sizeof(ProductionControl));
    out(Region::Ram2, "note_edit", sizeof(ProductionNoteEdit));
    out(Region::Ram2, "display_jobs", sizeof(ProductionDisplayJobs));
    out(Region::Ram2, "total", sizeof(EngineRam2));
  }

 private:
  bool overlayLive() const {
    const uint8_t s = r_->transport.writingSlot();
    return s != ProductionTransport::kNoSlot && s == p_->tiles.detailSlot(0);
  }
  /// Every slot's tiles follow its state (commits, Undo / Redo, edits, completions, Clear); the shown take's overlay
  /// clears once the committed tiles show it; sweeps yield the display cursor to a NOTE_EDIT gather.
  void serviceDisplay(uint32_t clock) {
    for (uint8_t s = 0; s < Config::kSlotCount; ++s) {
      const ManifestId m = r_->transport.contentOf(s);
      if (m == kNoManifest) {
        if (p_->tiles.wanted(s) != 0 || p_->tiles.densityIdentity(s) != 0) p_->tiles.clearSlot(s);
        continue;
      }
      const uint32_t id = r_->manifests.manifest(m).identity;
      if (p_->tiles.wanted(s) != id && sealed(m)) r_->displayJobs.request(s, m);
    }
    const uint8_t w = r_->transport.writingSlot();
    if (w != ProductionTransport::kNoSlot && w == p_->tiles.detailSlot(0)) {
      overlaySlot_ = w;
      overlayUntil_ = 0;
      p_->overlay.advance(r_->transport.writePosition(clock));
    } else if (overlaySlot_ != ProductionTransport::kNoSlot) {
      if (overlayUntil_ == 0) {
        const ManifestId m = r_->transport.contentOf(overlaySlot_);
        overlayUntil_ = m == kNoManifest ? 1 : r_->manifests.manifest(m).identity;
      }
      if (p_->tiles.densityIdentity(overlaySlot_) == overlayUntil_ || overlayUntil_ == 1) {
        p_->overlay.clear();
        overlaySlot_ = ProductionTransport::kNoSlot;
      }
    }
    if (r_->noteEdit.gatherPending())
      r_->displayJobs.yield();
    else
      r_->displayJobs.service();
  }

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
      if (slot == ProductionTransport::kNoSlot || !r_->transport.playing(t)) continue;
      const ManifestId m = r_->transport.contentOf(slot);  // a cleared slot's empty state is never swept
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
  static JobStep gatherStep(void* ctx) {
    EngineRoot& e = *static_cast<EngineRoot*>(ctx);
    if (e.r_->noteEdit.gatherStep(ProductionJobs::kRowsPerGrain)) return JobStep::Continue;
    e.gatherLive_ = false;
    return JobStep::Done;
  }
  static JobStep rebuildStep(void* ctx) {
    EngineRoot& e = *static_cast<EngineRoot*>(ctx);
    if (e.r_->noteEdit.rebuildStep(ProductionJobs::kRowsPerGrain)) return JobStep::Continue;
    e.rebuildLive_ = false;
    return JobStep::Done;
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

  static constexpr uint8_t kTickEmits = 64;  // per track per tick (the player spills the rest to the next tick)
  static constexpr uint8_t kGestureEmits = kTrackCount * Config::kMaxSoundingPerTrack;  // every track playing a slot
  static constexpr uint32_t kSweepGrains = 6000;  // two loops of a 64-bar take at 128 row units (background: no deadline)

  EnginePsram* p_ = nullptr;
  EngineRam2* r_ = nullptr;
  bool sweeping_ = false;
  bool gatherLive_ = false, rebuildLive_ = false;
  uint8_t overlaySlot_ = 0xFF;   // the slot whose take the overlay shows (0xFF: none)
  uint32_t overlayUntil_ = 0;    // cleared once the slot's tiles show this identity
  static constexpr uint32_t kEditGrains = 2000;  // a gather composes up to two loops; a rebuild <= 24 grains (§ 8.4)
  uint8_t sweepNext_ = 0;
  Stats stats_{};
};

}  // namespace ContentEngine
