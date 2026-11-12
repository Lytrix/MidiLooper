//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Saving and loading the engine's slots on the card.
//
// * New format, new folder (`kFolder`); V1 files are never written (they are imported, `V1Import.h`).
// * Approval (once, remembered): nothing is written until the owner approves saving on the device; `approve`
//   creates the folder, and the folder's existence is the remembered approval (`probe` at boot). Without it the engine
//   only reads.
// * Autosave (when settled): a slot whose history changed (current state identity, undo / redo depth) is saved
//   once it has not changed for `kSettleUs` and no take or NOTE_EDIT session is open on it, one slot at a time, as
//   persistence-class grains. A slot that changes during its save is saved again later.
// * The store (a save must not hold the main loop — a file-system operation takes 4–8 ms on the device, a save
//   ~30 ms): one file, `store.bin`, created and preallocated in one contiguous run when saving is
//   approved (or at the first boot after), then only read and written sector by sector, with no file-system work. Each
//   slot has two copies (A / B, `kCopySectors` each: a slot may hold the whole content pool); a save writes the copy
//   that is not current, one sector write a grain — the body (`SaveJob`), its last partial sector, then the header with
//   the next sequence (one counter past every copy the boot found, so a save reads nothing) — so an interrupted save
//   leaves the current copy whole. The surface state alternates between two sectors. No write waits for the card
//   (a stop that waited for the card to program took up to 8 ms): a grain writes only when the card says it is
//   `ready` for that sector, and a save or state write is complete only once the card is idle again (`closing_`: what
//   it holds programmed).
// * Boot load: every slot's valid copy with the later sequence is loaded (`LoadJob`: admission, CRC, full release on
//   failure), the other copy when it fails; a slot whose copies fail stays empty and is counted by name
//   (`Stats::loadFailed`); loading never writes. A slot with no copy in the store is taken from before the store: its
//   engine file `slot_NN.mle` (or the complete `slot_NN.tmp` an interrupted rename left), then written into the store
//   (`Stats::migrated`; the files stay on the card, never written again). A slot with neither whose V1 file
//   (`/MidiLooper/current/slots/loop_TT_SS.bin`, slot = TT x 8 + SS) holds an LSV1 snapshot is imported from it
//   (`Lsv1Reader`: exact current content), as is one holding a pass stream (its Active passes as runs, V1
//   note edits discarded and named: `v1EditedMask`); a loop-start-shifted or empty V1 slot is skipped and named
//   (`v1SkippedMask`), a failed one named too (`v1FailedMask`). Imported slots are saved once approved; V1 files are
//   never written.
// * ControlSurface state: the selected track and each track's selected slot, as `kStateBytes` bytes
//   from the surface (`setSurfaceState`), saved like a slot — after approval, `kSettleUs` after its last change, as a
//   persistence grain — into the store's state sectors ("MLS2", sequence, the state packed in 27 bits, a check word;
//   before the store: `surface.mls`, read once); read at boot (`restoredSurfaceState`), none valid meaning none.
//   The muted tracks too (DEC-293), a fifth word in the sector with its own check.
// `Card`: the files read only (exists, makeDir, openRead, read, closeRead) and the store (openStore, readSector,
// writeSector) — see `CardIo`.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/History/ColdTier.h"
#include "ContentEngine/Storage/RunStore.h"
#include "ContentEngine/Storage/SlotFile.h"
#include "ContentEngine/Storage/V1Import.h"

namespace ContentEngine {

/// The card, as the integration provides it (SD on the Teensy, a fake on the host).
struct CardIo {
  // Files, read only: engine files written before the store, V1 files.
  virtual bool exists(const char* path) = 0;
  virtual bool makeDir(const char* path) = 0;
  virtual bool openRead(const char* path) = 0;
  virtual bool read(uint8_t* data, uint32_t n) = 0;
  virtual void closeRead() = 0;
  // The store: one file of `sectors` × 512 B in one contiguous run, opened once. Missing, it is created and
  // preallocated only when `create` (`created` says so: its sectors hold whatever the card held). Sector `i` of the
  // store is then read and written directly, with no file-system work.
  virtual bool openStore(const char* path, uint32_t sectors, bool create, bool& created) = 0;
  virtual bool readSector(uint32_t i, uint8_t* data) = 0;         // 512 B
  virtual bool writeSector(uint32_t i, const uint8_t* data) = 0;  // 512 B
  /// False while writing sector `i` now would wait for the card (it is still taking or programming an earlier one);
  /// a save or state grain then does nothing and is tried again. `kIdle`: a save or state write is complete — false
  /// until the card has everything programmed. The default: a card that never waits.
  static constexpr uint32_t kIdle = 0xFFFFFFFFu;
  virtual bool ready(uint32_t i) {
    (void)i;
    return true;
  }
  // The milestone library (DEC-258): a second preallocated contiguous file, sector I/O only, opened once and only with
  // the transport stopped (`create` preallocates it: file-system work that may take seconds). `libraryReady` as
  // `ready`, for the library's sectors (`kIdle`: everything programmed). Defaults: no library.
  virtual bool openLibrary(const char* path, uint32_t sectors, bool create, bool& created) {
    (void)path, (void)sectors, (void)create;
    created = false;
    return false;
  }
  virtual bool readLibrarySector(uint32_t i, uint8_t* data) {
    (void)i, (void)data;
    return false;
  }
  virtual bool writeLibrarySector(uint32_t i, const uint8_t* data) {
    (void)i, (void)data;
    return false;
  }
  virtual bool libraryReady(uint32_t i) {
    (void)i;
    return true;
  }
  /// Renames a file (false: not done — the target exists, the source is missing, or no card). Boot only (the
  /// store's names); the default renames nothing.
  virtual bool rename(const char* from, const char* to) {
    (void)from, (void)to;
    return false;
  }
  /// A file's size in whole sectors (0: missing). The default: missing.
  virtual uint32_t fileSectors(const char* path) {
    (void)path;
    return 0;
  }
  virtual ~CardIo() = default;
};

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
class Persistence : public ColdSink {
 public:
  static constexpr const char* kFolder = "/MidiLooper/engine";
  static constexpr uint32_t kSettleUs = 2000000;  // 2 s without a change
  static constexpr uint8_t kStateBytes = 10;  // the selected track, each track's selected slot, the muted tracks (DEC-293)
  static constexpr uint32_t kChunk = 16384;
  // A save grain produces at most one sector (the CRC runs across chunks, so the file is the same whatever the chunk).
  static constexpr uint32_t kSaveChunk = 512;
  static constexpr uint32_t kSector = 512;
  // The run-once store (DEC-254). Sectors 0–1 the surface state; per slot two state-record copies of
  // `kCopySectors` (a header sector, then the slot stream without its rows: header, directory with each run's record
  // sector, states, CRC); then the run area (`RunStore.h`): each run once, as its own record.
  static constexpr const char* kStoreFile = "/MidiLooper/engine/store.bin";
  // The two-copy store (DEC-253, whole slot copies): read once at boot to migrate into the run-once store, never
  // written again; kept as `store.bak`.
  static constexpr const char* kOldStoreFile = "/MidiLooper/engine/store.bak";
  // The run-once store's first name: renamed to `kStoreFile` at the next boot (`nameStores`).
  static constexpr const char* kInterimStoreFile = "/MidiLooper/engine/store2.bin";
  // The largest slot file: one slot may hold every content row (the pool is shared), each run once, every state.
  static constexpr uint32_t kMaxFileBytes = 24 + Config::kRunCount * 8u + Config::kContentRows * kSetFileRowBytes +
                                            Config::kHistoryDepth * (9u + 2u * Config::kRunsPerManifest) + 4;
  static constexpr uint32_t kOldCopySectors = 1 + (kMaxFileBytes + kSector - 1) / kSector;  // header + body
  static constexpr uint32_t kOldStoreSectors = 2 + 2u * Slots * kOldCopySectors;
  // A state record's body: the slot stream's header, its directory with a record sector a run, its states, its CRC.
  static constexpr uint32_t kRecordBodyBytes =
      24 + Config::kRunCount * 12u + Config::kHistoryDepth * (9u + 2u * Config::kRunsPerManifest) + 4;
  static constexpr uint32_t kCopySectors = 1 + (kRecordBodyBytes + kSector - 1) / kSector;  // header + body
  static constexpr uint32_t kRunAreaBase = 2 + 2u * Slots * kCopySectors;
  static constexpr uint32_t kStoreSectors = kRunAreaBase + runstore::kAreaSectors;
  // Each state-record copy's cold record — the slot's cold states, in the run area's last region
  // (never taken for runs): magic "MLK1", the copy's sequence, the count, per state its revision, loop length and run
  // records (sector, rows, kind), then a CRC-32. A copy without one has no cold states.
  static constexpr uint32_t kColdMaxBytes = 10 + 99u * (9u + 9u * Config::kRunsPerManifest) + 4;
  static constexpr uint32_t kColdSectors = (kColdMaxBytes + kSector - 1) / kSector;
  static constexpr uint32_t kColdBase = runstore::kAreaSectors - 2u * Slots * kColdSectors;
  static_assert(kColdBase % runstore::kCluster == 0, "the cold region starts on a cluster");
  static constexpr uint32_t kColdMagic = 0x314B4C4Du;  // "MLK1"
  static uint32_t coldSector(uint8_t s, uint8_t c) { return kRunAreaBase + kColdBase + (2u * s + c) * kColdSectors; }
  using Reservations = RowReservations<Pool, Runs>;

  // A save's body into the store, and a load's bytes out of it (or out of a file of before the store), through one
  // cursor (`io*_`: a save and a load never run together; the persistence state is a fixed PSRAM line).
  struct Sink {
    Persistence* p;
    bool write(const uint8_t* d, uint32_t n) {
      if (!p->legacy_) return p->fifoPush(d, n);  // split by the persistence grain, one sector at a time
      uint8_t* buf = p->sectorBuf();
      p->ioBytes_ += n;
      while (n != 0) {
        const uint32_t k = n < kSector - p->ioFill_ ? n : kSector - p->ioFill_;
        for (uint32_t i = 0; i < k; ++i) buf[p->ioFill_ + i] = d[i];
        p->ioFill_ = static_cast<uint16_t>(p->ioFill_ + k);
        d += k;
        n -= k;
        if (p->ioFill_ == kSector) {
          if (!p->card_->writeSector(p->ioNext_++, buf)) return false;
          p->ioFill_ = 0;
        }
      }
      return true;
    }
  };
  struct Source {
    Persistence* p;
    bool read(uint8_t* d, uint32_t n) {
      if (!p->ioStore_) return p->card_->read(d, n);
      if (!p->legacy_) return p->recordRead(d, n);  // the state record and the run records
      if (n > p->ioBytes_) return false;
      p->ioBytes_ -= n;
      uint8_t* buf = p->importScratch_;  // no V1 import runs while a store copy loads
      while (n != 0) {
        if (p->ioFill_ == kSector) {
          if (!p->card_->readSector(p->ioNext_++, buf)) return false;
          p->ioFill_ = 0;
        }
        const uint32_t k = n < kSector - p->ioFill_ ? n : kSector - p->ioFill_;
        for (uint32_t i = 0; i < k; ++i) d[i] = buf[p->ioFill_ + i];
        p->ioFill_ = static_cast<uint16_t>(p->ioFill_ + k);
        d += k;
        n -= k;
      }
      return true;
    }
  };
  using Save = SaveJob<Pool, Runs, Manifests, HistoryT, Sink, kSaveChunk>;
  using Load = LoadJob<Pool, Runs, Manifests, HistoryT, Source, kChunk>;

  struct Stats {
    uint32_t saves;          // slot files completed
    uint32_t saveFailed;     // a sector write failed, or no store (the previous copy stays)
    uint32_t loads;          // slots loaded at boot
    uint32_t loadFailed;     // engine files that failed to load (slot left empty)
    uint64_t loadFailedMask; // which slots
    uint32_t v1Imported;     // slots imported from V1 files
    uint64_t v1SkippedMask;  // V1 slots skipped (reported): a shifted loop start, or empty
    uint64_t v1OffsetMask;   // ... of them, skipped for a shifted loop start
    uint64_t v1EmptyMask;    // ... of them, without notes or kept events
    uint8_t v1Outcome[Slots];  // per slot: the reader's status (and the importer's in the high nibble when refused)
    uint64_t v1FailedMask;   // V1 slots that failed to read or import
    uint64_t v1EditedMask;   // imported slots whose V1 note edits were discarded
    uint32_t v1HeldWhole;    // imported notes that had no NoteOff in their pass: held for the whole loop
    uint32_t migrated;       // slots loaded from an engine file of before the store (written into the store)
    uint8_t store;           // 0: not opened (no approval yet), 1: ready, 2: could not be opened or created
    // The run-once store.
    uint32_t runsWritten;    // run records written
    uint32_t runSectors;     // ... their sectors
    uint32_t runsKept;       // runs a save found on the card already (not written again)
    uint32_t storeFull;      // saves refused: no contiguous room in the run area
    uint32_t badRun;         // run records that failed their check on load
    uint32_t migratedSlots;  // slots carried from the two-copy store into the run-once store
    uint32_t storeRenamed;   // boot renames that put the run-once store at `store.bin` (`nameStores`)
    // The cold tier.
    uint32_t coldLoaded;     // cold states read at boot
    uint32_t coldBad;        // cold records that failed their check (the slot's cold states not loaded)
    uint32_t coldRestarts;   // saves restarted: the slot's cold states changed while they were written
    // Fetches.
    uint32_t fetches;        // fetches that arrived (switched to, or kept when the slot moved meanwhile)
    uint32_t fetchedStates;  // cold states made hot
    uint32_t fetchSectors;   // sectors read by fetches
    uint32_t fetchRefused;   // no room (rows, runs, manifests, table) or a damaged record: the slot stays
    uint32_t fetchCancelled; // the slot changed under a fetch
    uint32_t fetchBusy;      // presses on another slot while one fetch runs
  };
  using Import = ResolvedImport<Pool, Runs, Manifests, HistoryT, /*held*/ 64>;
  using Reader = Lsv1Reader<Card, Import>;

  /// `saveScratch` / `loadScratch`: `kChunk` bytes each (the persistence worker scratch).
  void init(Card& card, Pool& pool, Runs& runs, Manifests& manifests, HistoryT& history, Reservations& res,
            NoteIdSource& ids, uint8_t* saveScratch, uint8_t* loadScratch, uint8_t* importScratch,
            MidiEventLayout v1Layout = kTeensyMidiEvent) {
    importScratch_ = importScratch;
    v1Layout_ = v1Layout;
    importing_ = kNone;
    card_ = &card;
    pool_ = &pool;
    runs_ = &runs;
    manifests_ = &manifests;
    history_ = &history;
    res_ = &res;
    ids_ = &ids;
    saveScratch_ = saveScratch;
    loadScratch_ = loadScratch;
    sink_ = Sink{this};
    source_ = Source{this};
    ioNext_ = ioBytes_ = 0;
    ioFill_ = 0;
    ioStore_ = false;
    save_ = Save{};
    load_ = Load{};
    approved_ = false;
    saving_ = loading_ = kNone;
    bootNext_ = Slots;
    store_ = sealing_ = fromStore_ = stateHave_ = closing_ = closingSave_ = false;
    haveCopy_ = copyB_ = 0;
    stateSeq_ = savingSeq_ = 0;
    nextSeq_ = 1;
    stateCopy_ = 0;
    for (uint8_t s = 0; s < Slots; ++s) {
      saved_[s] = seen_[s] = Fingerprint{};
      changedAt_[s] = 0;
    }
    state_ = stateSaved_ = 0;
    mutes_ = mutesSaved_ = mutesRead_ = 0;
    settings_ = settingsSaved_ = settingsRead_ = 0;
    stateRead_ = kNoState;
    stateDue_ = stateKnown_ = false;
    stateChangedUs_ = 0;
    stats_ = Stats{};
    legacy_ = migrate_ = false;
    pendSector_ = runstore::kNoSector;
    fetchSlot_ = kNone;
    fetchArrived_ = false;
    fGot_ = 0;
    coldTier_.clear();
    clusters_.clear();
    clusters_.reserve(kColdBase, 2u * Slots * kColdSectors);
    for (auto& c : cardRun_) c = runstore::CardRun{};
  }

  // ---- Approval -------------------------------------------------------------------------------------------------------
  /// Boot: the folder is the remembered approval.
  void probe() { approved_ = card_->exists(kFolder); }
  /// The owner approved saving on the device: the folder is created (the one write approval permits first).
  bool approve() {
    if (!approved_) approved_ = card_->exists(kFolder) || card_->makeDir(kFolder);
    if (approved_ && !store_ && saving_ == kNone) {
      openStore(sectorBuf(), false);
      for (uint8_t s = 0; s < Slots && store_; ++s)
        if (seen_[s].any) saved_[s] = Fingerprint{};  // loaded or imported before the store: written into it
    }
    return approved_;
  }
  bool storeReady() const { return store_; }
  bool approved() const { return approved_; }

  // ---- Boot load ------------------------------------------------------------------------------------------------------
  void startBootLoad() { bootNext_ = 0; }
  bool booting() const { return bootNext_ < Slots || loading_ != kNone || importing_ != kNone; }

  // ---- Service --------------------------------------------------------------------------------------------------------
  /// Once per pass: tracks changes and starts the next save when one is due. `busySlotA` / `busySlotB`: slots a take or
  /// NOTE_EDIT session holds (0xFF: none).
  void service(uint32_t nowUs, uint8_t busySlotA, uint8_t busySlotB) {
    if (migrate_ && !booting()) finishMigration();
    for (uint8_t s = 0; s < Slots; ++s) {
      const Fingerprint f = fingerprint(s);
      if (!(f == seen_[s])) {
        seen_[s] = f;
        changedAt_[s] = stamp(nowUs);
      }
    }
    if (!approved_ || booting() || saving_ != kNone || stateDue_ || fetching()) return;  // a fetch first
    for (uint8_t s = 0; s < Slots; ++s) {
      if (seen_[s] == saved_[s] || s == busySlotA || s == busySlotB) continue;
      if (static_cast<uint32_t>(static_cast<uint16_t>(stamp(nowUs) - changedAt_[s])) * 1024u < kSettleUs) continue;
      startSave(s);
      return;
    }
    if (stateKnown_ && nowUs - stateChangedUs_ >= kSettleUs &&
        (state_ != stateSaved_ || mutes_ != mutesSaved_ || settings_ != settingsSaved_))
      stateDue_ = true;  // written by the next persistence grain
  }
  bool pending() const { return saving_ != kNone || booting() || stateDue_ || closing_ || fetching(); }
  /// A fetch is under way (not yet arrived).
  bool fetching() const { return fetchSlot_ != kNone && !fetchArrived_; }
  /// A fetch arrived: its slot and the state the presses were made on; the engine switches (once).
  bool takeFetch(uint8_t& slot, ManifestId& floor) {
    if (fetchSlot_ == kNone || !fetchArrived_) return false;
    slot = fetchSlot_;
    floor = fetchFloor_;
    fetchSlot_ = kNone;
    fetchArrived_ = false;
    ++stats_.fetches;
    return true;
  }
  /// A save or state write is on the card and not yet programmed (the milestone library waits for it, DEC-258).
  bool onCard() const { return saving_ != kNone || closing_; }
  /// For the display's save dots: a slot is being written. The surface state (the selection) is saved
  /// silently: a track switch must not hide the screen's progress line behind the dots.
  bool writing() const { return saving_ != kNone || closingSave_; }
  /// A slot changed since its last save (it is saved once settled).
  bool unsaved() const {
    for (uint8_t s = 0; s < Slots; ++s)
      if (!(seen_[s] == saved_[s])) return true;
    return false;
  }

  // ---- ControlSurface state ----------------------------------------------------------------------------------------------------
  /// The surface's state now (`kStateBytes`); saved once settled and approved.
  void setSurfaceState(const uint8_t* s, uint32_t nowUs) {
    const uint32_t packed = pack(s);
    if (stateKnown_ && packed == state_ && s[kStateBytes - 1] == mutes_) return;
    state_ = packed;
    mutes_ = s[kStateBytes - 1];
    stateKnown_ = true;
    stateChangedUs_ = nowUs;
  }
  /// The engine's settings saved with the surface state — the GROOVE cycle's bars (1–255; 0: not saved).
  void setSettings(uint8_t cycleBars, uint32_t nowUs) {
    if (cycleBars == settings_) return;
    settings_ = cycleBars;
    stateChangedUs_ = nowUs;
  }
  /// The settings read at boot (0: none — a state sector without a settings word).
  uint8_t restoredCycleBars() const { return settingsRead_; }
  /// The state read at boot. False: none (no file, or a bad one).
  bool restoredSurfaceState(uint8_t* out) const {
    if (stateRead_ == kNoState) return false;
    out[0] = stateRead_ & 7;  // the selected track; then track t's slot, t x 8 + its index
    for (uint8_t t = 0; t < kTracks; ++t) out[1 + t] = static_cast<uint8_t>(t * 8 + ((stateRead_ >> (3 + 3 * t)) & 7));
    out[kStateBytes - 1] = mutesRead_;  // bit t: track t muted (zeros: none)
    return true;
  }
  /// One persistence grain: a save chunk, or a load grain, or the next boot file opened.
  bool step() {
    if (closing_) {  // a save or state write is complete only once the card has it programmed
      if (!card_->ready(CardIo::kIdle)) return true;
      closing_ = closingSave_ = false;
    }
    if (saving_ != kNone) return stepSave();
    if (loading_ != kNone) return stepLoad();
    if (importing_ != kNone) return stepImport();
    if (fetching()) return stepFetch();
    if (stateDue_) {  // the card is idle here (`closing_`): the write does not wait
      writeState();
      closing_ = store_;
      return closing_;
    }
    if (bootNext_ == 0) {  // the boot's first grain: the store (created on the first boot after approval), the state
      if (approved_) openStore(importScratch_, true);
      readState();
    }
    while (bootNext_ < Slots) {
      const uint8_t s = bootNext_++;
      if (store_ && pickCopy(s) && startStoreLoad(s, (copyB_ >> s & 1) != 0)) return true;
      if (haveCopy_ >> s & 1) {  // the store's copies failed: the slot stays empty (an older file is not taken)
        failLoad(s);
        continue;
      }
      path(s, ".mle", name_);
      fromTmp_ = false;
      if (!card_->exists(name_)) {
        path(s, ".tmp", name_);  // a save interrupted between removing the old file and the rename
        fromTmp_ = card_->exists(name_);
        if (!fromTmp_) {
          if (startImport(s)) return true;
          continue;
        }
      }
      ioStore_ = false;
      fromStore_ = false;
      if (!card_->openRead(name_) || load_.start(*pool_, *runs_, *manifests_, *history_, *res_, *ids_, s, source_,
                                                 loadScratch_) != Load::Status::Running) {
        card_->closeRead();
        if (fromTmp_) {
          if (startImport(s)) return true;
          continue;
        }
        failLoad(s);
        continue;
      }
      loading_ = s;
      return true;
    }
    return false;
  }

  const Stats& stats() const { return stats_; }
  bool savedCurrent(uint8_t slot) const { return seen_[slot] == saved_[slot]; }
  /// Diagnostics, tests: run `id`'s record's first store sector, or `kNoSector` when it is not on the card.
  uint32_t runRecordSector(RunId id) const {
    const runstore::CardRun& c = cardRun_[id];
    return c.serial != 0 && c.serial == runs_->run(id).serial ? kRunAreaBase + c.sector : runstore::kNoSector;
  }
  /// The run area's clusters held (written this session, or listed by a valid state record at boot).
  uint32_t runAreaClusters() const { return clusters_.usedClusters(); }

 private:
  static constexpr uint8_t kNone = 0xFF;
  struct Fingerprint {
    uint32_t identity = 0;  // the current state (0: empty or no history)
    uint8_t undo = 0, redo = 0;
    bool any = false;       // the slot has a history at all
    bool operator==(const Fingerprint& o) const {
      return identity == o.identity && undo == o.undo && redo == o.redo && any == o.any;
    }
  };

  Fingerprint fingerprint(uint8_t s) const {
    Fingerprint f;
    const ManifestId m = history_->current(s);
    f.identity = m == kNoManifest ? 0 : manifests_->manifest(m).identity;
    f.undo = history_->undoDepth(s);
    f.redo = history_->redoDepth(s);
    f.any = m != kNoManifest || f.redo != 0;
    return f;
  }
  static constexpr const char* kStateFile = "/MidiLooper/engine/surface.mls";
  static constexpr uint32_t kStateMagic = 0x31534C4Du;   // "MLS1": the state file of before the store
  static constexpr uint32_t kStateMagic2 = 0x32534C4Du;  // "MLS2": a store state sector
  static constexpr uint32_t kCopyMagic = 0x31434C4Du;   // "MLC1": a store copy header
  static constexpr uint32_t kNoState = 0xFFFFFFFFu;
  /// The state packed in 27 bits (persistence state is a fixed PSRAM line): the track, then each track's slot index.
  static uint32_t pack(const uint8_t* s) {
    uint32_t v = s[0] & 7u;
    for (uint8_t t = 0; t < kTracks; ++t) v |= static_cast<uint32_t>(s[1 + t] & 7u) << (3 + 3 * t);
    return v;
  }
  static constexpr uint8_t kTracks = 8;
  /// The muted tracks' word after the state's four: "MU", the mask, its complement (a check of its own, so a
  /// state sector of before — zeros there — reads as none muted and stays valid).
  static uint32_t muteWord(uint8_t m) { return 0x4D550000u | static_cast<uint32_t>(m) << 8 | static_cast<uint8_t>(~m); }
  static uint8_t mutesOf(const uint8_t* b) {
    const uint32_t w = b[16] | static_cast<uint32_t>(b[17]) << 8 | static_cast<uint32_t>(b[18]) << 16 |
                       static_cast<uint32_t>(b[19]) << 24;
    const uint8_t m = static_cast<uint8_t>(w >> 8);
    return w == muteWord(m) ? m : 0;
  }
  static uint32_t settingsWord(uint8_t c) { return 0x53450000u | static_cast<uint32_t>(c) << 8 | static_cast<uint8_t>(~c); }
  static uint8_t settingsOf(const uint8_t* b) {  // bytes 20–23 ("SE"): zeros read as none
    const uint32_t w = b[20] | static_cast<uint32_t>(b[21]) << 8 | static_cast<uint32_t>(b[22]) << 16 |
                       static_cast<uint32_t>(b[23]) << 24;
    const uint8_t c = static_cast<uint8_t>(w >> 8);
    return w == settingsWord(c) ? c : 0;
  }
  static uint32_t stateSum(uint32_t v) { return (v * 2654435761u) ^ 0xA5A5A5A5u; }
  // The store's state sectors 0 and 1, alternately (one sector write is the whole save): magic "MLS2", sequence,
  // state, check word; the valid one with the later sequence counts.
  uint8_t stateSector() const { return stateHave_ ? static_cast<uint8_t>(stateCopy_ ^ 1) : 0; }
  void writeState() {
    stateDue_ = false;
    if (!store_) return;
    const uint8_t c = stateSector();
    uint8_t* b = sectorBuf();
    for (uint32_t i = 0; i < kSector; ++i) b[i] = 0;
    const uint32_t seq = stateSeq_ + 1;
    putWords(b, kStateMagic2, seq, state_, stateSum(state_ ^ seq));
    const uint32_t mw = muteWord(mutes_);
    for (uint8_t i = 0; i < 4; ++i) b[16 + i] = static_cast<uint8_t>(mw >> (8 * i));
    if (settings_ != 0) {
      const uint32_t sw = settingsWord(settings_);
      for (uint8_t i = 0; i < 4; ++i) b[20 + i] = static_cast<uint8_t>(sw >> (8 * i));
    }
    if (!card_->writeSector(c, b)) return;
    stateSeq_ = seq;
    stateCopy_ = c;
    stateHave_ = true;
    stateSaved_ = state_;
    mutesSaved_ = mutes_;
    settingsSaved_ = settings_;
  }
  void readState() {
    if (store_) {
      for (uint8_t c = 0; c < 2; ++c) {
        uint32_t w[4];
        if (!card_->readSector(c, importScratch_)) continue;
        getWords(importScratch_, w);
        if (w[0] != kStateMagic2 || w[3] != stateSum(w[2] ^ w[1]) || w[2] >> 27 != 0) continue;
        if (stateHave_ && static_cast<int32_t>(w[1] - stateSeq_) <= 0) continue;
        stateHave_ = true;
        stateSeq_ = w[1];
        stateCopy_ = c;
        stateRead_ = stateSaved_ = w[2];
        mutesRead_ = mutesSaved_ = mutesOf(importScratch_);
        settingsRead_ = settingsSaved_ = settingsOf(importScratch_);
      }
      if (stateHave_) return;
    }
    readStateFile();
  }
  /// The state file of before the store ("MLS1"): restored, and written into the store when the store is there.
  void readStateFile() {
    uint8_t b[12];
    if (!card_->exists(kStateFile) || !card_->openRead(kStateFile)) return;
    const bool ok = card_->read(b, sizeof b);
    card_->closeRead();
    uint32_t w[3] = {};
    for (uint8_t i = 0; i < 12; ++i) w[i / 4] |= static_cast<uint32_t>(b[i]) << (8 * (i % 4));
    if (!ok || w[0] != kStateMagic || w[2] != stateSum(w[1]) || w[1] >> 27 != 0) return;
    stateRead_ = w[1];
    stateSaved_ = kNoState;  // not in the store yet
  }
  static void putWords(uint8_t* b, uint32_t a, uint32_t c, uint32_t d, uint32_t e) {
    const uint32_t w[4] = {a, c, d, e};
    for (uint8_t i = 0; i < 16; ++i) b[i] = static_cast<uint8_t>(w[i / 4] >> (8 * (i % 4)));
  }
  static void getWords(const uint8_t* b, uint32_t* w) {
    for (uint8_t k = 0; k < 4; ++k)
      w[k] = b[4 * k] | static_cast<uint32_t>(b[4 * k + 1]) << 8 | static_cast<uint32_t>(b[4 * k + 2]) << 16 |
             static_cast<uint32_t>(b[4 * k + 3]) << 24;
  }

  // ---- The store -------------------------------------------------------------------------------------------------------
  // Sectors 0–1: the surface state. Slot s, copy c (A = 0, B = 1): `kCopySectors` from `copyBase(s, c)` — a header
  // sector (magic "MLC1", slot, sequence, body bytes, check word), then the body (the slot file, `SlotFile.h`). A save writes
  // the copy that is not current: the body, then the header with the next sequence — an interrupted save leaves the
  // current copy whole. Boot loads the valid copy with the later sequence (the other
  // when it fails its CRC).
  uint8_t* sectorBuf() { return saveScratch_ + kSaveChunk; }  // the sector after the save chunk
  static uint16_t stamp(uint32_t us) { return static_cast<uint16_t>(us >> 10); }
  bool finishBody() {  // the last, partial sector
    if (ioFill_ == 0) return true;
    uint8_t* buf = sectorBuf();
    for (uint32_t i = ioFill_; i < kSector; ++i) buf[i] = 0;
    if (!card_->writeSector(ioNext_, buf)) return false;  // the buffer stays partial: the save is not sealed
    ++ioNext_;
    ioFill_ = 0;
    return true;
  }
  uint32_t copyBase(uint8_t s, uint8_t c) const {
    return 2 + (2u * s + c) * (legacy_ ? kOldCopySectors : kCopySectors);
  }
  static uint32_t copySum(uint32_t slot, uint32_t seq, uint32_t bytes) {
    return ((slot * 0x9E3779B1u) ^ (seq * 2654435761u) ^ (bytes * 0x85EBCA77u)) + 0x5A5A5A5Au;
  }
  /// Opens the run-once store (creating it when missing; a new store's headers are cleared: its sectors hold old card
  /// data). `boot`: when the run-once store is new and the two-copy store is there, that one is opened instead and read
  /// (`legacy_`); the boot's slots are then written into the run-once store (`finishMigration`).
  void openStore(uint8_t* buf, bool boot) {
    if (boot) nameStores();
    bool created = false;
    legacy_ = false;
    clusters_.clear();
    clusters_.reserve(kColdBase, 2u * Slots * kColdSectors);
    store_ = card_->openStore(kStoreFile, kStoreSectors, true, created);
    if (store_ && created) {
      for (uint32_t i = 0; i < kSector; ++i) buf[i] = 0;
      for (uint32_t i = 0; i < 2 && store_; ++i) store_ = card_->writeSector(i, buf);
      for (uint8_t s = 0; s < Slots && store_; ++s)
        for (uint8_t c = 0; c < 2 && store_; ++c) store_ = card_->writeSector(copyBase(s, c), buf);
      closing_ = true;  // once, at creation: these writes may wait for the card
      bool old = false;
      if (store_ && boot && card_->openStore(kOldStoreFile, kOldStoreSectors, false, old)) {
        legacy_ = migrate_ = true;  // read the two-copy store; the run-once store is written after the boot
      } else if (store_) {
        store_ = card_->openStore(kStoreFile, kStoreSectors, false, created);
      }
    }
    stats_.store = store_ ? 1 : 2;
  }
  /// At boot, before the store opens: the two-copy store becomes `store.bak` and the run-once store `store.bin`. A card
  /// with the interim `store2.bin`: `store.bin` (the two-copy store) is renamed `.bak`, then `store2.bin` takes its
  /// name. A card with only the two-copy store under `store.bin` (shorter than the run-once store): it is renamed
  /// `.bak`, so the boot migrates from it. Nothing is renamed over an existing file; nothing is deleted.
  void nameStores() {
    if (card_->exists(kInterimStoreFile)) {
      if (card_->exists(kStoreFile) && !card_->exists(kOldStoreFile)) card_->rename(kStoreFile, kOldStoreFile);
      if (!card_->exists(kStoreFile) && card_->rename(kInterimStoreFile, kStoreFile)) ++stats_.storeRenamed;
      return;
    }
    const uint32_t n = card_->fileSectors(kStoreFile);
    if (n != 0 && n < kStoreSectors && !card_->exists(kOldStoreFile) && card_->rename(kStoreFile, kOldStoreFile))
      ++stats_.storeRenamed;
  }
  /// After a boot that read the two-copy store: the run-once store, and every slot with a history saved into it.
  void finishMigration() {
    migrate_ = false;
    bool created = false;
    legacy_ = false;
    store_ = card_->openStore(kStoreFile, kStoreSectors, false, created);
    stats_.store = store_ ? 1 : 2;
    haveCopy_ = copyB_ = 0;  // the run-once store holds no copy yet
    for (uint8_t s = 0; s < Slots; ++s)
      if (seen_[s].any) {
        saved_[s] = Fingerprint{};
        ++stats_.migratedSlots;
      }
    if (stateRead_ != kNoState) stateSaved_ = kNoState;  // the surface state too
  }
  /// The run-area extents slot `s`'s copy `c` lists, held (the boot's cluster map, reclaimed at boot).
  void markCopy(uint8_t s, uint8_t c, uint32_t bytes) {
    uint8_t* b = importScratch_;
    uint32_t next = copyBase(s, c) + 1, at = kSector, pos = 0, count = 0, seen = 0, got = 0;
    uint8_t e[24];
    bool head = true;
    while (pos < bytes && (head || seen < count)) {
      if (at == kSector) {
        if (!card_->readSector(next++, b)) return;
        at = 0;
      }
      e[got++] = b[at++];
      ++pos;
      if (head && got == 24) {  // the slot stream's header: its run count
        count = static_cast<uint32_t>(e[12] | (e[13] << 8));
        head = false;
        got = 0;
      } else if (!head && got == 12) {  // a directory entry: kind, rows, record sector
        clusters_.mark(runstore::get32(e + 8), runstore::get32(e + 4));
        got = 0;
        ++seen;
      }
    }
  }
  // ---- saving into the run-once store, one sector a grain --------------------------------------------------
  // `SaveJob` writes the slot stream into a FIFO (`Sink`); `splitStep` reads it: the header, the directory (each run
  // with its record's sector — a run on the card keeps its record, a new one gets an extent) and the states go to the
  // state record's body; a new run's rows go to its record (its header last), a kept run's rows nowhere. A sector ready
  // to write waits in `pend*_` until the card is ready for it.
  static constexpr uint32_t kFifo = 1024;
  uint8_t* runBuf() { return saveScratch_ + 2 * kSaveChunk; }
  uint8_t* hdrBuf() { return saveScratch_ + 3 * kSaveChunk; }
  uint8_t* fifoBuf() { return saveScratch_ + 4 * kSaveChunk; }
  bool fifoPush(const uint8_t* d, uint32_t n);
  void pend(uint32_t sector, uint8_t* buf);
  void bodyByte(uint8_t v);
  void out(const uint8_t* d, uint8_t n);
  uint32_t runBytes(uint16_t r) const { return runRows_[r] * kSetFileRowBytes; }
  /// Run `r` is about to stream: a run without rows ends at once.
  void beginRun();
  /// False: no room in the run area (the save fails by name).
  bool splitStep();
  void startRecordSave();
  bool stepRecordSave();
  // ---- the cold tier -------------------------------------------------------------------------------
 public:
  bool demote(uint8_t slot, ManifestId m) override;
  /// Run `id`'s record sector: known, or one of the slot being loaded from its records — its own oldest
  /// states may go cold while it loads, before the load maps its runs. kNoSector: not on the card.
  uint32_t cardSectorOf(RunId id) const;
  uint8_t coldCount(uint8_t slot) const override { return coldTier_.count(slot); }
  void dropOldestCold(uint8_t slot) override { coldTier_.dropOldest(slot); }
  void clearCold(uint8_t slot) override { coldTier_.clearSlot(slot); }
  /// An undo at the slot's cold floor — the next cold state below; a press during the fetch reaches one deeper
  /// (only the final target is switched to).
  void requestFetch(uint8_t slot) override;
  const cold::ColdTier<Slots>& coldTier() const { return coldTier_; }

 private:
  void coldBegin(uint8_t s);
  void item32(uint32_t v);
  /// The cold record's bytes until a sector is to be written: 1 done, 0 more to come, -1 the slot's cold states
  /// changed.
  int8_t coldStep(uint8_t s);
  /// Slot `s`'s copy `c` cold record (sequence `seq`): `mark` holds its runs' extents (boot), else its states go below
  /// the slot's (checked first: a record failing its check loads nothing).
  void readCold(uint8_t s, uint8_t c, uint32_t seq, bool mark);

  // ---- the fetch -----------------------------------------------------------------------------------
  static constexpr uint8_t kFetchSectors = 4;  // a grain reads at most this many (the boot's 32 took 31 ms)
  /// A run in PSRAM whose record is at area sector `sector` (kNoRun: none).
  RunId cardRunAt(uint32_t sector) const;
  /// Gives back the state in progress (its first `n` runs) and everything fetched so far; the slot stays as it is.
  void dropFetch(uint8_t n);
  bool refuseFetch(uint8_t n);
  bool stepFetch();

  bool recordSaveFailed(uint8_t s);
  // ---- loading from the run-once store ------------------------------------------------------------------
  // The slot stream rebuilt for `LoadJob`: the header and directory from the state record (each entry's record sector
  // kept), each run's rows from its record (its header checked first, its rows' CRC at its end), the states and CRC
  // from the state record again.
  bool bodyRead(uint8_t* d, uint32_t n);
  bool openRun(uint16_t r);
  bool closeRun();
  bool recordRead(uint8_t* d, uint32_t n);
  /// Slot `s`'s copies: true when one is valid (`copyB_` names the later; `altValid_` the other).
  bool pickCopy(uint8_t s);
  bool startStoreLoad(uint8_t s, bool b);
  bool writeHeader(uint8_t s, uint8_t c, uint32_t seq, uint32_t bytes);

  static void path(uint8_t slot, const char* ext, char* out);

  void startSave(uint8_t s);
  /// A save grain: one sector write at most, and only once the card is ready for it — a body chunk, then the last,
  /// partial sector, then the header.
  bool stepSave();
  bool stepLoad();
  static void v1Path(uint8_t slot, char* out);
  bool startImport(uint8_t s);
  bool stepImport();

  void failLoad(uint8_t s);

  Card* card_ = nullptr;
  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Manifests* manifests_ = nullptr;
  HistoryT* history_ = nullptr;
  Reservations* res_ = nullptr;
  NoteIdSource* ids_ = nullptr;
  uint8_t* saveScratch_ = nullptr;
  uint8_t* loadScratch_ = nullptr;
  Sink sink_{};
  Source source_{};
  Save save_;
  Load load_;
  bool approved_ = false, fromTmp_ = false;
  bool store_ = false, sealing_ = false, fromStore_ = false, altValid_ = false, stateHave_ = false;
  bool closing_ = false;  // the card is to be left idle (`CardIo::kIdle`) before the next grain's work
  bool closingSave_ = false;  // ... after a slot's save (the display's dots; not after a state write)
  uint8_t savingCopy_ = 0, loadingCopy_ = 0, stateCopy_ = 0;
  uint64_t haveCopy_ = 0, copyB_ = 0;  // per slot: a valid copy in the store, the current one is B
  uint32_t stateSeq_ = 0, savingSeq_ = 0, nextSeq_ = 1;  // `nextSeq_`: the next save's sequence (any slot)
  uint32_t ioNext_ = 0, ioBytes_ = 0;  // the cursor: the next sector, the bytes written (save) or left (load)
  uint16_t ioFill_ = 0;                // bytes used of the sector buffer
  bool ioStore_ = false;               // the load reads the store (else a file)
  bool stateDue_ = false, stateKnown_ = false;
  uint8_t mutes_ = 0, mutesSaved_ = 0, mutesRead_ = 0;  // bit t, track t muted
  uint8_t settings_ = 0, settingsSaved_ = 0, settingsRead_ = 0;  // the GROOVE cycle's bars (0: none)
  uint32_t state_ = 0, stateSaved_ = 0, stateRead_ = 0xFFFFFFFFu, stateChangedUs_ = 0;
  uint8_t saving_ = kNone, loading_ = kNone, bootNext_ = Slots;
  Fingerprint saved_[Slots], seen_[Slots], savingPrint_{};
  uint16_t changedAt_[Slots] = {};  // when each slot last changed (`stamp`: 1.024 ms units, 67 s around)
  // The file in use: an engine slot file ("/MidiLooper/engine/slot_NN.tmp", 30 characters) or, while a V1 slot is
  // imported, its V1 file ("/MidiLooper/current/slots/loop_TT_SS.bin", 40) — one at a time: boot loads and imports one
  // slot after another and no save runs while booting.
  char name_[44] = {};
  uint8_t* importScratch_ = nullptr;
  MidiEventLayout v1Layout_ = kTeensyMidiEvent;
  uint8_t importing_ = kNone;
  // The run-once store.
  enum class Split : uint8_t { Head, Dir, Rows, Tail };
  bool legacy_ = false;   // the two-copy store is open (read at boot to migrate)
  bool migrate_ = false;  // ... and the run-once store is to take its slots after the boot
  bool saveDone_ = false;
  uint8_t sealStage_ = 0, runEnd_ = 0, got_ = 0, outLen_ = 0, outPos_ = 0;
  Split split_ = Split::Head, ld_ = Split::Head;
  uint16_t dirCount_ = 0, dirAt_ = 0, r_ = 0, runFill_ = 0, ldCount_ = 0;
  uint32_t rowByte_ = 0, runOut_ = 0, runCrc_ = 0, wantCrc_ = 0, fifoHead_ = 0, fifoUsed_ = 0, bytes_[2] = {};
  uint32_t pendSector_ = runstore::kNoSector;
  uint8_t* pendBuf_ = nullptr;
  uint8_t head_[24] = {}, entry_[8] = {}, outq_[32] = {};
  uint32_t runSector_[Runs::kRunCount] = {}, runRows_[Runs::kRunCount] = {}, runSerial_[Runs::kRunCount] = {};
  uint8_t runKind_[Runs::kRunCount] = {};
  bool runWrite_[Runs::kRunCount] = {};
  runstore::CardRun cardRun_[Runs::kRunCount];
  runstore::ClusterMap clusters_;
  // The cold tier and the cold record being written.
  cold::ColdTier<Slots> coldTier_;
  uint16_t coldSt_ = cold::kNone, coldEpoch_ = 0;
  uint8_t coldLeft_ = 0, coldPhase_ = 0, coldRun_ = 0, itemLen_ = 0, itemPos_ = 0;
  uint8_t item_[16] = {};
  uint16_t coldFill_ = 0;
  uint32_t coldOut_ = 0, coldCrc_ = 0, copySeq_[2] = {};
  // The fetch.
  uint8_t fetchSlot_ = kNone, fWant_ = 0, fGot_ = 0, fPhase_ = 0, fN_ = 0, fR_ = 0, fetchCold_ = 0;
  bool fetchArrived_ = false, fHead_ = false;
  ManifestId fetchFloor_ = kNoManifest, fetchOldest_ = kNoManifest;
  uint16_t fSt_ = cold::kNone;
  uint32_t fRow_ = 0, fSec_ = 0, fCrc_ = 0, fWantCrc_ = 0;
  RunId fRun_[Manifests::kMaxRuns] = {};
  bool fNew_[Manifests::kMaxRuns] = {};
  ManifestId fMade_[99] = {};
  Import import_;
  Reader reader_;
  Stats stats_{};
};

}  // namespace ContentEngine

#include "ContentEngine/Storage/PersistenceSaving.h"
#include "ContentEngine/Storage/PersistenceLoading.h"
