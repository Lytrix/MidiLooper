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
#include "ContentEngine/Persistence.h"
#include "ContentEngine/History.h"
#include "ContentEngine/LibraryLoader.h"
#include "ContentEngine/LibraryWriter.h"
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
using ProductionPersistence = Persistence<CardIo, ProductionPool, ProductionRuns, ProductionManifests,
                                          ProductionHistory, Config::kSlotCount>;
using ProductionLibrary = LibraryWriter<CardIo, ProductionPool, ProductionRuns, ProductionManifests, ProductionHistory,
                                        Config::kSlotCount>;
using ProductionSetLoad = SetLoad<CardIo, ProductionPool, ProductionRuns, ProductionManifests, ProductionHistory,
                                  Config::kSlotCount>;
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
  // E9.7: the worker scratch line — save, load and V1-import chunks (its 1 KiB merge heap is the V1 importer's held-note
  // heap, inside `persistence`).
  uint8_t persistScratch[3][ProductionPersistence::kChunk];
  // E9.7 (owner 2026-11-05): the save / load / import jobs' state, touched only in persistence grains.
  ProductionPersistence persistence;
  // DEC-258: the milestone library's writer (its whole-set snapshot, slot-file job and sector) and its save chunk.
  ProductionLibrary libraryWriter;
  uint8_t libraryScratch[ProductionPersistence::kSector];  // the writer's, then the load's (one runs at a time)
  // DEC-259: loading a milestone and returning (its blob reader and slot-file load job).
  ProductionSetLoad setLoad;
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
// tiles with headers 602,416; overlay 9,744; worker scratch 50,176 (E9.7); E: log 20,480; NOTE_EDIT selection 9,216
// (owner 2026-11-05: its own line, one selection owner); persistence job state 7,224 (owner 2026-11-05); the milestone
// library writer 4,200 with its 512 B save chunk (DEC-258: its whole-set snapshot and slot-file job); the set load 4,560 (DEC-259: its blob
// reader and slot-file load job; DEC-261: its staged states; DEC-269: + 16 B the working set in the writer's two index
// headers; DEC-270: + 8 B the got state and its slots; + 3,752 B (host) the get's state reader: one sector, the
// run directory's kinds and row counts, the current state's run list — plan §6.2 fix 1; + 206,160 B (host) the
// get-a-loop preview partition, owner 2026-11-09: 8,192 rows — 199 pool pages with their tags, and the per-run tables
// of 63 preview descriptors). The line is the host's 64-bit layout; the Teensy's is a few hundred bytes below it. RAM2: the
// engine's tables,
// writer, edit writer, tracks, scheduler and jobs, transport and output hub as measured on the Teensy. A change here is
// a budget change and must update the plan.
constexpr size_t kPsramBudget = 7031944u;
constexpr size_t kRam2Budget = 159864u;  // measured (get-a-loop preview, plan §6.2 fix 1: + 5,440 B its 63 run descriptors and their ledger and free-list entries; sets browser slice 4: + 8 B the save suggestion's evidence counters; DEC-261: + 16 B a set switch's staged launches; DEC-257: + 4,104 B the loop-length fit builder's scratch, 256 lookups and 128 open notes; NOTE_EDIT edit joins: + 32 B counters; E9.8k: + 64 B track activity levels for the screen; E9.8j: + 520 B pre-roll notes, pickups and tick-0 notes shown; E9.8i: + 104 B trimmed Record Stop state; E4.7: + 136 B history pressure counters; E9.7b: + 8 B track output channels; E9.6: + 2,376 B display jobs; E9.5: + 1,024 B NOTE_EDIT; E9.4): + 64 B job boundaries, + 1,160 B output and thru keys,
                                          // + 1,248 B late-join state, + 248 B gesture state
static_assert(sizeof(EnginePsram) <= kPsramBudget, "the engine's PSRAM regions fit plan § 4.1");
#if defined(__arm__)
static_assert(sizeof(ProductionPersistence) <= 7776u + 1024u,
              "persistence state (7,776 B line: + 552 B its per-run tables over the 63 preview descriptors, plan §6.2 "
              "fix 1) plus the V1 importer in the worker scratch's 1 KiB merge heap (ARM)");
#endif
static_assert(sizeof(EnginePsram::editGather) + sizeof(EnginePsram::editGatherRuns) == 9216u,
              "the NOTE_EDIT selection line (owner 2026-11-05): 512 notes x 16 B + their runs x 2 B");
#if defined(__arm__)
static_assert(sizeof(EngineRam2) <= kRam2Budget, "the engine's RAM2 regions fit plan § 4.2 (ARM layout)");
#endif

class EngineRoot {
 public:
  enum class Region : uint8_t { Psram, Ram2 };
  using Transport = ProductionTransport;
  using Control = ProductionControl;
  using Tiles = ProductionTiles;
  using NoteEdit = ProductionNoteEdit;
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
    r_->reservations.init(p_->pool, r_->runs, kLedgerFixedReservePages, Config::kPreviewPages);
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
    card_ = nullptr;
    persistLive_ = false;
    switched_ = 0;
    stagedPlaying_ = 0;
    stagedLaunched_ = false;
    sweeping_ = false;
    sweepNext_ = 0;
    stats_ = Stats{};
    return true;
  }

  /// Once per main-loop pass at Clock tick `clock` (`nowUs`, `usPerTick`): the jobs that are due.
  void submitJobs(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    if (p_->setLoad.waiting() && p_->setLoad.stagingDone() && !stagedLaunched_) launchStaged();
    if (stagedLaunched_ && !p_->setLoad.busy()) endStaged(clock, nowUs, usPerTick);
    if (switched_ != 0 && !p_->setLoad.busy()) resumeSwitched(clock);
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
    if (card_ != nullptr) {
      const uint8_t editing = r_->noteEdit.editing() ? r_->noteEdit.slot() : ProductionTransport::kNoSlot;
      // While a milestone writes, no save starts (DEC-258: the slots' saves follow it; the card has one stream).
      if (!libraryWorking()) p_->persistence.service(nowUs, r_->transport.writingSlot(), editing);
      if (!persistLive_ && (p_->persistence.pending() || libraryWorking()) &&
          r_->scheduler.submit(JobSpec{JobClass::Persistence, nowUs, kPersistGrains, &persistStep, this}, id))
        persistLive_ = true;
    }
    uint8_t slot;
    if (r_->noteEdit.takePublished(slot)) {
      ProductionEmit buf[kGestureEmits];
      r_->output.route(buf, r_->transport.follow(slot, clock, buf, kGestureEmits), nowUs, usPerTick);
    }
  }
  /// The Clock tick `clock` at `nowUs`: the gestures due (E9.4), then every track plays; its events go to the hub on
  /// its ports.
  void clockTick(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    // A live load at the switch point (DEC-260): every playing track stops before this tick plays.
    if (p_->setLoad.waiting() && r_->control.switchPoint(switchAt_, clock)) switchSet(clock, nowUs, usPerTick);
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

  // ---- Persistence (E9.7) -------------------------------------------------------------------------------------------
  /// The card. Probes the remembered approval (the engine folder); nothing is written until `approveSaving`.
  void attachCard(CardIo& card) {
    card_ = &card;
    p_->persistence.init(card, p_->pool, r_->runs, r_->manifests, r_->history, r_->reservations, r_->ids,
                         p_->persistScratch[0], p_->persistScratch[1], p_->persistScratch[2], v1Layout_);
    p_->persistence.probe();
    p_->setLoad.bind(card, p_->libraryWriter, p_->pool, r_->runs, r_->manifests, r_->history, r_->reservations, r_->ids,
                     p_->libraryScratch, &clearSet, this);
  }
  /// Tests: the `MidiEvent` layout V1 files were written with (the Teensy's by default).
  void setV1Layout(MidiEventLayout l) { v1Layout_ = l; }
  /// The owner approved saving on the device (owner 2026-11-05: once, remembered by the folder).
  bool approveSaving() { return card_ != nullptr && p_->persistence.approve(); }
  /// Loads every slot that has an engine file (before playing; nothing is written).
  void startBootLoad() {
    if (card_ != nullptr) p_->persistence.startBootLoad();
  }
  const ProductionPersistence& persistence() const { return p_->persistence; }
  /// The surface's state (selected track and slots, `ProductionPersistence::kStateBytes`): saved like a slot.
  void setSurfaceState(const uint8_t* s, uint32_t nowUs) {
    if (card_ != nullptr) p_->persistence.setSurfaceState(s, nowUs);
  }

  // ---- Milestone library (DEC-258) ---------------------------------------------------------------------------------
  /// Opens the library file at `path` (`sectors` long) and reads its index; `create` creates and preallocates it when
  /// missing — the caller allows that only while stopped with saving approved (file-system work of seconds).
  bool openLibrary(const char* path, bool create, bool& created) {
    created = false;
    return card_ != nullptr && !p_->libraryWriter.busy() &&
           card_->openLibrary(path, library::kSectors, create, created) && p_->libraryWriter.mount(*card_);
  }
  /// Starts writing milestone `m`: the whole set as it is now. The caller refuses it while a take is open.
  ProductionLibrary::Result saveMilestone(const ProductionLibrary::Milestone& m) {
    if (card_ == nullptr || p_->persistence.booting() || p_->setLoad.busy()) return ProductionLibrary::Result::Busy;
    return p_->libraryWriter.beginMilestone(p_->pool, r_->runs, r_->manifests, r_->history, r_->ids.peek(), m,
                                            p_->libraryScratch);
  }
  /// Starts publishing a trash, restore or empty-trash record (DEC-268; `LibraryWriter::beginMark`). The working set is
  /// not touched.
  ProductionLibrary::Result markLibrary(uint8_t kind, uint16_t set, uint32_t target, uint32_t wallTime) {
    if (card_ == nullptr || p_->persistence.booting() || p_->setLoad.busy()) return ProductionLibrary::Result::Busy;
    return p_->libraryWriter.beginMark(kind, set, target, wallTime);
  }
  /// Starts naming `set` (base `base`) as the working set in the library's index header (DEC-269;
  /// `LibraryWriter::beginWorking`): after a load or return, so the next boot starts in it.
  ProductionLibrary::Result publishWorking(uint16_t set, uint32_t base) {
    if (card_ == nullptr || p_->persistence.booting() || p_->setLoad.busy()) return ProductionLibrary::Result::Busy;
    return p_->libraryWriter.beginWorking(set, base);
  }
  /// Starts replacing the recovery checkpoint with the whole set as it is now.
  ProductionLibrary::Result saveCheckpoint(const ProductionLibrary::Checkpoint& c) {
    if (card_ == nullptr || p_->persistence.booting() || p_->setLoad.busy()) return ProductionLibrary::Result::Busy;
    return p_->libraryWriter.beginCheckpoint(p_->pool, r_->runs, r_->manifests, r_->history, r_->ids.peek(), c,
                                             p_->libraryScratch);
  }
  /// Get a loop (DEC-270; the caller checked `loadRefusal`): starts reading slot `from` of milestone `index` for slot
  /// `into` — nothing changes until `placeGot`.
  ProductionSetLoad::Result getLoop(uint16_t index, uint8_t from, uint8_t into, bool preview = false) {
    return p_->setLoad.beginGet(index, from, into, preview);
  }
  /// Diagnostics (plan §6.2 fix 1): drops the state a preview get read, unplaced; its pages return through reclaim.
  void dropGot() {
    const ManifestId m = p_->setLoad.takeGot();
    if (m != kNoManifest) r_->manifests.release(m, r_->runs);
  }
  /// The state a get read onto slot `into`: one undo step, heard on the slot's playing tracks as an undo is. Refused
  /// (the slot written meanwhile) or nothing read: the state is released, nothing changes.
  ProductionTransport::Result placeGot(uint8_t into, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    const ManifestId m = p_->setLoad.takeGot();
    if (m == kNoManifest) return ProductionTransport::Result::NoChange;
    ProductionEmit buf[kGestureEmits];
    uint8_t n;
    const auto r = r_->transport.place(into, m, clock, buf, kGestureEmits, n);
    if (r != ProductionTransport::Result::Ok) r_->manifests.release(m, r_->runs);
    r_->output.route(buf, n, nowUs, usPerTick);
    return r;
  }
  ProductionLibrary& library() { return p_->libraryWriter; }
  /// Why a load or return cannot start now (DEC-259), or nullptr.
  const char* loadRefusal() const {
    if (card_ == nullptr || !p_->libraryWriter.mounted()) return "LIBRARY NOT READY";
    if (r_->transport.writingSlot() != ProductionTransport::kNoSlot) return "STOP THE TAKE FIRST";
    if (r_->noteEdit.editing()) return "CLOSE THE EDIT FIRST";
    if (libraryBusy() || p_->persistence.booting()) return "BUSY";
    return nullptr;
  }
  /// Loads milestone `index` (the caller checked `loadRefusal`): the working set becomes the checkpoint `c`, then is
  /// replaced. The tempo and surface state are the caller's to apply from `setLoad().blob()` when it ends. `live` (the
  /// clock runs, DEC-260): the set plays on until the switch point, where every playing track stops; the loaded set's
  /// tracks resume together at the first bar after the load (a short gap).
  /// A live load stages the playing tracks' destination slots first (DEC-261): those switch without a gap.
  ProductionSetLoad::Result loadMilestone(uint16_t index, const ProductionLibrary::Checkpoint& c, bool live = false) {
    return p_->setLoad.beginLoad(index, c, live, live ? stageMask() : 0);
  }
  ProductionSetLoad::Result returnToCheckpoint(bool live = false) {
    return p_->setLoad.beginReturn(live, live ? stageMask() : 0);
  }
  /// Staging on (default) or off (every live switch takes the gap; diagnostics, DEC-261).
  void setStaging(bool on) { staging_ = on; }
  bool staging() const { return staging_; }
  /// The tracks a live switch took over seamlessly (bit t), until they follow the loaded set.
  uint8_t stagedTracks() const { return stagedPlaying_; }
  /// The global switch point (DEC-260): the next bar, or the selected track's loop end.
  void setSwitchAt(ProductionControl::SwitchAt at) { switchAt_ = at; }
  ProductionControl::SwitchAt switchAt() const { return switchAt_; }
  /// The switch, now (the clock stopped while a live load waited for it).
  void switchNow(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    if (p_->setLoad.waiting()) switchSet(clock, nowUs, usPerTick);
  }
  /// The tracks a live load stopped, until they resume (bit t).
  uint8_t switchedTracks() const { return switched_; }
  const ProductionSetLoad& setLoad() const { return p_->setLoad; }
  bool libraryBusy() const { return p_->libraryWriter.busy() || p_->setLoad.busy(); }
  /// A library job has card work now (a live load waiting for its switch point has none: it takes no grains, and
  /// autosave goes on meanwhile).
  bool libraryWorking() const { return p_->libraryWriter.busy() || (p_->setLoad.busy() && !p_->setLoad.waiting()); }

  // ---- Display (E9.6) ---------------------------------------------------------------------------------------------
  /// The slot the piano roll shows and the one after it (both get span tiles; focus is swept first).
  void setDisplayFocus(uint8_t slot, uint8_t successor) {
    p_->tiles.setDetail(0, slot);
    p_->tiles.setDetail(1, successor);
    r_->displayJobs.setFocus(slot);
    // Detail tiles now show this slot: sweep it again for them (its density tiles stay meanwhile).
    p_->tiles.setWanted(slot, 0);
  }
  /// The live overlay: the shown take's notes, drawn at the view's scale (`drawRollBand`).
  const ProductionOverlay& overlay() const { return p_->overlay; }
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
  NoteIdSource& noteIds() { return r_->ids; }  // a created note's fresh id (ids are never reused)
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
    out(Region::Psram, "worker_scratch", sizeof(EnginePsram::persistScratch));
    out(Region::Psram, "persistence", sizeof(ProductionPersistence));
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
      if (overlaySlot_ != w) {  // a new take on the shown slot: its pre-roll (E9.8j) is shown at once
        p_->overlay.clear();
        const auto& c = r_->control;
        if (c.pickupTrack() == r_->transport.writingTrack()) {
          for (uint8_t i = 0; i < c.startNoteCount(); ++i) p_->overlay.noteOn(0, c.startNotePitch(i), c.startNoteVelocity(i));
          for (uint8_t i = 0; i < c.pickupCount(); ++i) {
            const auto& pk = c.pickup(i);
            p_->overlay.pickup(pk.before, pk.off, c.pickupOpen(i), pk.pitch, pk.velocity);
          }
        }
      }
      overlaySlot_ = w;
      overlayUntil_ = 0;
      // Positions wrap at the slot's loop while overdubbing; a first take has none yet (its length so far).
      const ManifestId m = r_->transport.contentOf(w);
      p_->overlay.setLoop(r_->transport.mode() == ProductionTransport::Mode::Overdubbing && m != kNoManifest
                              ? r_->manifests.manifest(m).loopLengthTicks
                              : 0);
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
  uint8_t stageMask() const {
    uint8_t mask = 0;
    for (uint8_t t = 0; t < kTrackCount; ++t)
      if (staging_ && r_->transport.playing(t)) mask = static_cast<uint8_t>(mask | (1u << t));
    return mask;
  }
  /// The switch point of a live load: a track whose staged launch is ready takes it now, at its position 0 (DEC-261);
  /// any other playing track stops (its notes end) for the gap. Each keeps its slot for a load that changes nothing.
  /// Then the load clears and reads.
  void switchSet(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    for (uint8_t t = 0; t < kTrackCount; ++t) {
      if (!r_->transport.playing(t)) continue;
      switchedSlot_[t] = r_->transport.playingSlot(t);
      const uint8_t bit = static_cast<uint8_t>(1u << t);
      if (r_->transport.stagedLaunch(t) && r_->control.commitStaged(t, clock, nowUs, usPerTick) == ProductionControl::Result::Ok) {
        stagedPlaying_ |= bit;
        continue;
      }
      switched_ |= bit;
      r_->control.stopTrack(t, clock, nowUs, usPerTick);
    }
    p_->setLoad.release();
  }
  /// While a live load waits: its staged content composes ahead on the tracks still playing (as a queued switch).
  void launchStaged() {
    stagedLaunched_ = true;
    for (uint8_t t = 0; t < kTrackCount; ++t) {
      const ManifestId m = p_->setLoad.staged(t);
      if (m != kNoManifest && r_->transport.playing(t)) r_->transport.launchStaged(t, p_->setLoad.stagedSlot(t), m);
    }
  }
  /// After a live load: the tracks that switched seamlessly play their slots' current states already (the load adopted
  /// the staged states); one whose slot's state is another follows it. If the load did not replace the set as asked,
  /// they stop and resume with the gapped tracks. The staged references go back (History holds its own).
  void endStaged(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    const bool loaded = p_->setLoad.status() == ProductionSetLoad::Status::Done;
    for (uint8_t t = 0; t < kTrackCount; ++t) {
      const uint8_t bit = static_cast<uint8_t>(1u << t);
      if (!(stagedPlaying_ & bit)) continue;
      if (loaded) {
        const uint8_t slot = r_->transport.playingSlot(t);
        if (r_->transport.contentOf(slot) != p_->setLoad.staged(t)) r_->control.followSlot(slot, clock, nowUs, usPerTick);
      } else {
        r_->control.stopTrack(t, clock, nowUs, usPerTick);
        switched_ |= bit;
      }
    }
    stagedPlaying_ = 0;
    stagedLaunched_ = false;
    p_->setLoad.releaseStaged();
  }
  /// After a live load: the tracks it stopped resume together at the next bar — the loaded set's selected slot on each
  /// (the checkpoint's after a restore), or the slot it played when the load changed nothing.
  void resumeSwitched(uint32_t clock) {
    const ProductionSetLoad::Status st = p_->setLoad.status();
    const bool replaced = st == ProductionSetLoad::Status::Done || st == ProductionSetLoad::Status::Restored;
    for (uint8_t t = 0; t < kTrackCount; ++t) {
      if (!(switched_ & (1u << t))) continue;
      const uint8_t sel = p_->setLoad.blob().surface[1 + t];
      const uint8_t slot = replaced ? (sel / 8 == t ? sel : ProductionTransport::kNoSlot) : switchedSlot_[t];
      if (slot != ProductionTransport::kNoSlot && r_->transport.contentOf(slot) != kNoManifest)
        r_->control.queueSwitch(t, slot, ProductionControl::Quantize::NextBar, clock, false);
    }
    switched_ = 0;
  }
  /// A load's clear (DEC-259): the tracks reset, then every slot's history forgotten. Refused while a take or an edit is
  /// open (the caller checked; a session would refuse its slot).
  static bool clearSet(void* ctx) {
    EngineRoot& e = *static_cast<EngineRoot*>(ctx);
    if (e.r_->transport.writingSlot() != ProductionTransport::kNoSlot || e.r_->noteEdit.editing()) return false;
    e.r_->control.resetForLoad();
    for (uint8_t s = 0; s < Config::kSlotCount; ++s)
      if (!e.r_->history.clearSlot(s)) return false;
    return true;
  }
  static JobStep persistStep(void* ctx) {
    EngineRoot& e = *static_cast<EngineRoot*>(ctx);
    // A milestone or a load waits for a save already on the card, then has the card to itself (DEC-258); a load drives
    // its own checkpoint write.
    if (e.p_->persistence.onCard() || !e.libraryWorking()) {
      if (e.p_->persistence.step()) return JobStep::Continue;
    }
    if (e.p_->setLoad.busy() && !e.p_->setLoad.waiting()) {
      if (e.p_->setLoad.step() == ProductionSetLoad::Status::Running) return JobStep::Continue;
    } else if (e.p_->libraryWriter.busy() && e.p_->libraryWriter.step() == ProductionLibrary::Status::Running) {
      return JobStep::Continue;
    }
    e.persistLive_ = false;
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
  bool gatherLive_ = false, rebuildLive_ = false, persistLive_ = false;
  // DEC-260: the set switch point; the tracks a live load stopped (bit t) and the slots they played.
  ProductionControl::SwitchAt switchAt_ = ProductionControl::SwitchAt::NextBar;
  uint8_t switched_ = 0;
  uint8_t switchedSlot_[kTrackCount] = {};
  // DEC-261: staging on; the staged launches made; the tracks that switched to staged content.
  bool staging_ = true, stagedLaunched_ = false;
  uint8_t stagedPlaying_ = 0;
  CardIo* card_ = nullptr;
  MidiEventLayout v1Layout_ = kTeensyMidiEvent;
  static constexpr uint32_t kPersistGrains = 2000;
  uint8_t overlaySlot_ = 0xFF;   // the slot whose take the overlay shows (0xFF: none)
  uint32_t overlayUntil_ = 0;    // cleared once the slot's tiles show this identity
  static constexpr uint32_t kEditGrains = 2000;  // a gather composes up to two loops; a rebuild <= 24 grains (§ 8.4)
  uint8_t sweepNext_ = 0;
  Stats stats_{};
};

}  // namespace ContentEngine
