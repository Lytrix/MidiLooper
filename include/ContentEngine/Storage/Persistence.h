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
  bool fifoPush(const uint8_t* d, uint32_t n) {
    if (n > kFifo - fifoUsed_) return false;
    for (uint32_t i = 0; i < n; ++i) fifoBuf()[(fifoHead_ + fifoUsed_ + i) % kFifo] = d[i];
    fifoUsed_ += n;
    return true;
  }
  void pend(uint32_t sector, uint8_t* buf) {
    pendSector_ = sector;
    pendBuf_ = buf;
  }
  void bodyByte(uint8_t v) {
    uint8_t* b = sectorBuf();
    b[ioFill_++] = v;
    ++ioBytes_;
    if (ioFill_ == kSector) {
      pend(ioNext_++, b);
      ioFill_ = 0;
    }
  }
  void out(const uint8_t* d, uint8_t n) {
    for (uint8_t i = 0; i < n; ++i) outq_[outLen_++] = d[i];
  }
  uint32_t runBytes(uint16_t r) const { return runRows_[r] * kSetFileRowBytes; }
  /// Run `r` is about to stream: a run without rows ends at once.
  void beginRun() {
    rowByte_ = 0;
    runOut_ = 0;
    runFill_ = 0;
    runCrc_ = 0;
    if (r_ < dirCount_ && runBytes(r_) == 0) runEnd_ = runWrite_[r_] ? 2 : 3;
  }
  /// False: no room in the run area (the save fails by name).
  bool splitStep() {
    while (pendSector_ == runstore::kNoSector) {
      if (outPos_ < outLen_) {
        bodyByte(outq_[outPos_++]);
        if (outPos_ == outLen_) outPos_ = outLen_ = 0;
        continue;
      }
      if (runEnd_ == 1) {  // the run's last, partial rows sector
        for (uint32_t i = runFill_; i < kSector; ++i) runBuf()[i] = 0;
        pend(kRunAreaBase + runSector_[r_] + 1 + runOut_++, runBuf());
        runFill_ = 0;
        runEnd_ = 2;
        continue;
      }
      if (runEnd_ == 2) {  // its header, last: the record is whole once it is written
        runstore::writeHeader(hdrBuf(), saving_, runKind_[r_], runRows_[r_], runCrc_);
        pend(kRunAreaBase + runSector_[r_], hdrBuf());
        ++stats_.runsWritten;
        stats_.runSectors += runstore::sectorsFor(runRows_[r_]);
        runEnd_ = 3;
        continue;
      }
      if (runEnd_ == 3) {
        runEnd_ = 0;
        ++r_;
        if (r_ >= dirCount_) {
          split_ = Split::Tail;
        } else {
          beginRun();
        }
        continue;
      }
      if (fifoUsed_ == 0) return true;
      const uint8_t v = fifoBuf()[fifoHead_];
      fifoHead_ = (fifoHead_ + 1) % kFifo;
      --fifoUsed_;
      switch (split_) {
        case Split::Head:
          head_[got_++] = v;
          if (got_ == 24) {
            out(head_, 24);
            dirCount_ = static_cast<uint16_t>(head_[12] | (head_[13] << 8));
            dirAt_ = 0;
            got_ = 0;
            split_ = dirCount_ ? Split::Dir : Split::Tail;
          }
          break;
        case Split::Dir:
          entry_[got_++] = v;
          if (got_ == 8) {
            got_ = 0;
            const uint16_t i = dirAt_++;
            const RunId id = save_.directoryRun(i);
            runKind_[i] = entry_[0];
            runRows_[i] = runstore::get32(entry_ + 4);
            runSerial_[i] = runs_->run(id).serial;
            const runstore::CardRun& c = cardRun_[id];
            runWrite_[i] = !(c.serial != 0 && c.serial == runSerial_[i]);
            if (runWrite_[i]) {
              runSector_[i] = clusters_.take(runRows_[i]);
              if (runSector_[i] == runstore::kNoSector) return false;
            } else {
              runSector_[i] = c.sector;
              ++stats_.runsKept;
            }
            uint8_t a[4];
            runstore::put32(a, runSector_[i]);
            out(entry_, 8);
            out(a, 4);
            if (dirAt_ == dirCount_) {
              split_ = Split::Rows;
              r_ = 0;
              beginRun();
            }
          }
          break;
        case Split::Rows:
          if (r_ >= dirCount_) {
            split_ = Split::Tail;
            out(&v, 1);
            break;
          }
          if (runWrite_[r_]) {
            runBuf()[runFill_++] = v;
            runCrc_ = crc32Update(runCrc_, &v, 1);
            if (runFill_ == kSector) {
              pend(kRunAreaBase + runSector_[r_] + 1 + runOut_++, runBuf());
              runFill_ = 0;
            }
          }
          if (++rowByte_ == runBytes(r_)) runEnd_ = runWrite_[r_] ? (runFill_ != 0 ? 1 : 2) : 3;
          break;
        case Split::Tail:
          out(&v, 1);
          break;
      }
    }
    return true;
  }
  void startRecordSave() {
    split_ = Split::Head;
    got_ = 0;
    dirCount_ = dirAt_ = 0;
    r_ = 0;
    runEnd_ = 0;
    outLen_ = outPos_ = 0;
    fifoHead_ = fifoUsed_ = 0;
    pendSector_ = runstore::kNoSector;
    saveDone_ = false;
    sealStage_ = 0;
  }
  bool stepRecordSave() {
    const uint8_t s = saving_;
    if (pendSector_ != runstore::kNoSector) {  // one sector a grain, once the card is ready for it
      if (!card_->ready(pendSector_)) return true;
      const uint32_t at = pendSector_;
      pendSector_ = runstore::kNoSector;
      if (!card_->writeSector(at, pendBuf_)) return recordSaveFailed(s);
      if (sealStage_ == 2) {  // the state record's header: the save is whole
        saving_ = kNone;
        closing_ = closingSave_ = true;
        haveCopy_ |= 1ull << s;
        if (savingCopy_) copyB_ |= 1ull << s; else copyB_ &= ~(1ull << s);
        for (uint16_t i = 0; i < dirCount_; ++i)
          if (runWrite_[i]) cardRun_[save_.directoryRun(i)] = runstore::CardRun{runSector_[i], runSerial_[i]};
        ++stats_.saves;
        saved_[s] = savingPrint_;
        return false;
      }
      return true;
    }
    if (!splitStep()) {
      ++stats_.storeFull;
      return recordSaveFailed(s);
    }
    if (pendSector_ != runstore::kNoSector || outLen_ != 0) return true;
    if (!saveDone_) {
      const typename Save::Status st = save_.step();  // into the FIFO (empty here): at most one chunk
      if (st == Save::Status::Done) saveDone_ = true;
      else if (st != Save::Status::Running) return recordSaveFailed(s);
      return true;
    }
    if (runEnd_ != 0 || fifoUsed_ != 0) return true;
    if (sealStage_ == 0) {  // the body's last, partial sector
      sealStage_ = 1;
      if (ioFill_ != 0) {
        uint8_t* b = sectorBuf();
        for (uint32_t i = ioFill_; i < kSector; ++i) b[i] = 0;
        pend(ioNext_++, b);
        ioFill_ = 0;
      }
      return true;
    }
    if (sealStage_ == 1) {  // the copy's cold record, before its header seals the copy
      coldBegin(s);
      sealStage_ = 3;
      return true;
    }
    if (sealStage_ == 3) {
      const int8_t r = coldStep(s);
      if (r < 0) {
        ++stats_.coldRestarts;
        return recordSaveFailed(s);
      }
      if (r > 0) sealStage_ = 4;
      return true;
    }
    // The header, last, with the next sequence: the copy is the current one once it is written.
    uint8_t* b = sectorBuf();
    for (uint32_t i = 0; i < kSector; ++i) b[i] = 0;
    putWords(b, kCopyMagic, s, savingSeq_, ioBytes_);
    const uint32_t check = copySum(s, savingSeq_, ioBytes_);
    for (uint8_t i = 0; i < 4; ++i) b[16 + i] = static_cast<uint8_t>(check >> (8 * i));
    pend(copyBase(s, savingCopy_), b);
    sealStage_ = 2;
    return true;
  }
  // ---- the cold tier -------------------------------------------------------------------------------
 public:
  bool demote(uint8_t slot, ManifestId m) override {
    if (!store_ || legacy_) return false;
    const auto& d = manifests_->manifest(m);
    cold::CardRunRef refs[Manifests::kMaxRuns];
    for (uint8_t i = 0; i < d.runCount; ++i) {
      const RunId id = d.runs[i];
      const uint32_t at = cardSectorOf(id);
      if (at == runstore::kNoSector) return false;  // not on the card yet: forgotten
      refs[i].sector = at;
      refs[i].rows = runs_->run(id).rowCount;
      refs[i].kind = runs_->run(id).kind;
    }
    return coldTier_.push(slot, d.revision, d.loopLengthTicks, refs, d.runCount);
  }
  /// Run `id`'s record sector: known, or one of the slot being loaded from its records — its own oldest
  /// states may go cold while it loads, before the load maps its runs. kNoSector: not on the card.
  uint32_t cardSectorOf(RunId id) const {
    const runstore::CardRun& c = cardRun_[id];
    if (c.serial != 0 && c.serial == runs_->run(id).serial) return c.sector;
    if (loading_ != kNone && fromStore_ && !legacy_)
      for (uint16_t i = 0; i < ldCount_; ++i)
        if (load_.directoryRun(i) == id) return runSector_[i];
    return runstore::kNoSector;
  }
  uint8_t coldCount(uint8_t slot) const override { return coldTier_.count(slot); }
  void dropOldestCold(uint8_t slot) override { coldTier_.dropOldest(slot); }
  void clearCold(uint8_t slot) override { coldTier_.clearSlot(slot); }
  /// An undo at the slot's cold floor — the next cold state below; a press during the fetch reaches one deeper
  /// (only the final target is switched to).
  void requestFetch(uint8_t slot) override {
    if (!store_ || legacy_ || slot >= Slots) return;
    if (fetchSlot_ == kNone) {
      fetchSlot_ = slot;
      fWant_ = 1;
      fGot_ = 0;
      fPhase_ = 0;
      fetchFloor_ = history_->current(slot);
      fetchOldest_ = history_->oldestHot(slot);
      fetchCold_ = coldTier_.count(slot);
      fetchArrived_ = false;
      return;
    }
    if (slot == fetchSlot_ && !fetchArrived_) {
      if (fWant_ < fetchCold_) ++fWant_;
      return;
    }
    ++stats_.fetchBusy;
  }
  const cold::ColdTier<Slots>& coldTier() const { return coldTier_; }

 private:
  void coldBegin(uint8_t s) {
    coldSt_ = coldTier_.first(s);
    coldLeft_ = coldTier_.count(s);
    coldEpoch_ = coldTier_.epoch(s);
    coldPhase_ = 0;
    coldRun_ = 0;
    itemLen_ = itemPos_ = 0;
    coldFill_ = 0;
    coldOut_ = 0;
    coldCrc_ = 0;
  }
  void item32(uint32_t v) {
    runstore::put32(item_ + itemLen_, v);
    itemLen_ = static_cast<uint8_t>(itemLen_ + 4);
  }
  /// The cold record's bytes until a sector is to be written: 1 done, 0 more to come, -1 the slot's cold states
  /// changed.
  int8_t coldStep(uint8_t s) {
    while (pendSector_ == runstore::kNoSector) {
      if (itemPos_ == itemLen_) {
        if (coldTier_.epoch(s) != coldEpoch_) return -1;
        itemLen_ = itemPos_ = 0;
        if (coldPhase_ == 0) {
          item32(kColdMagic);
          item32(savingSeq_);
          item_[itemLen_++] = coldLeft_;
          item_[itemLen_++] = 0;
          coldPhase_ = 1;
        } else if (coldPhase_ == 1) {
          if (coldLeft_ == 0 || coldSt_ == cold::kNone) {
            item32(coldCrc_);
            coldPhase_ = 3;
          } else {
            item32(coldTier_.revision(coldSt_));
            item32(coldTier_.loop(coldSt_));
            item_[itemLen_++] = coldTier_.runCount(coldSt_);
            coldRun_ = 0;
            coldPhase_ = 2;
          }
        } else if (coldPhase_ == 2) {
          if (coldRun_ < coldTier_.runCount(coldSt_)) {
            const cold::CardRunRef r = coldTier_.run(coldSt_, coldRun_++);
            item32(r.sector);
            item32(r.rows);
            item_[itemLen_++] = r.kind;
          } else {
            coldSt_ = coldTier_.next(coldSt_);
            --coldLeft_;
            coldPhase_ = 1;
            continue;
          }
        } else {  // done: the last, partial sector
          if (coldFill_ != 0) {
            for (uint32_t i = coldFill_; i < kSector; ++i) runBuf()[i] = 0;
            pend(coldSector(s, savingCopy_) + coldOut_++, runBuf());
            coldFill_ = 0;
            return 0;
          }
          return 1;
        }
      }
      const uint8_t v = item_[itemPos_++];
      if (coldPhase_ != 3) coldCrc_ = crc32Update(coldCrc_, &v, 1);  // the CRC item is not in its own check
      runBuf()[coldFill_++] = v;
      if (coldFill_ == kSector) {
        if (coldOut_ >= kColdSectors) return -1;
        pend(coldSector(s, savingCopy_) + coldOut_++, runBuf());
        coldFill_ = 0;
      }
    }
    return 0;
  }
  /// Slot `s`'s copy `c` cold record (sequence `seq`): `mark` holds its runs' extents (boot), else its states go below
  /// the slot's (checked first: a record failing its check loads nothing).
  void readCold(uint8_t s, uint8_t c, uint32_t seq, bool mark) {
    uint8_t* b = importScratch_;
    for (uint8_t pass = mark ? 1 : 0; pass < 2; ++pass) {
      uint32_t next = coldSector(s, c), at = kSector, crc = 0;
      const auto get = [&](uint8_t* d, uint8_t n, bool sum) {
        for (uint8_t i = 0; i < n; ++i) {
          if (at == kSector) {
            if (next >= coldSector(s, c) + kColdSectors || !card_->readSector(next++, b)) return false;
            at = 0;
          }
          d[i] = b[at++];
          if (sum) crc = crc32Update(crc, d + i, 1);
        }
        return true;
      };
      uint8_t h[10];
      if (!get(h, 10, true) || runstore::get32(h) != kColdMagic || runstore::get32(h + 4) != seq) return;
      const uint8_t count = h[8];
      if (count > 99) return;
      uint16_t after = cold::kNone;
      for (uint8_t k = 0; k < count; ++k) {
        uint8_t st[9];
        if (!get(st, 9, true) || st[8] > Manifests::kMaxRuns) return;
        cold::CardRunRef refs[Manifests::kMaxRuns];
        for (uint8_t i = 0; i < st[8]; ++i) {
          uint8_t r[9];
          if (!get(r, 9, true)) return;
          refs[i].sector = runstore::get32(r);
          refs[i].rows = runstore::get32(r + 4);
          refs[i].kind = r[8];
          if (mark) clusters_.mark(refs[i].sector, refs[i].rows);
        }
        if (pass == 1 && !mark) {
          after = coldTier_.insertAfter(s, after, runstore::get32(st), runstore::get32(st + 4), refs, st[8]);
          if (after == cold::kNone) return;
          ++stats_.coldLoaded;
        }
      }
      if (pass == 0) {  // the check, before anything is taken
        uint8_t t[4];
        const uint32_t want = crc;
        if (!get(t, 4, false) || runstore::get32(t) != want) {
          ++stats_.coldBad;
          return;
        }
      }
    }
  }

  // ---- the fetch -----------------------------------------------------------------------------------
  static constexpr uint8_t kFetchSectors = 4;  // a grain reads at most this many (the boot's 32 took 31 ms)
  /// A run in PSRAM whose record is at area sector `sector` (kNoRun: none).
  RunId cardRunAt(uint32_t sector) const {
    for (RunId id = 0; id < Runs::kRunCount; ++id) {
      const runstore::CardRun& c = cardRun_[id];
      if (c.sector == sector && c.serial != 0 && c.serial == runs_->run(id).serial) return id;
    }
    return kNoRun;
  }
  /// Gives back the state in progress (its first `n` runs) and everything fetched so far; the slot stays as it is.
  void dropFetch(uint8_t n) {
    for (uint8_t i = 0; i < n; ++i) {
      if (fNew_[i]) {
        res_->forgive(fRun_[i]);
        runs_->seal(fRun_[i]);
      }
      runs_->release(fRun_[i]);
    }
    for (uint8_t k = 0; k < fGot_; ++k) manifests_->release(fMade_[k], *runs_);
    fGot_ = 0;
    fetchSlot_ = kNone;
    fetchArrived_ = false;
  }
  bool refuseFetch(uint8_t n) {
    ++stats_.fetchRefused;
    dropFetch(n);
    return false;
  }
  bool stepFetch() {
    const uint8_t s = fetchSlot_;
    if (history_->oldestHot(s) != fetchOldest_ || coldTier_.count(s) != fetchCold_) {  // the slot changed: cancelled
      ++stats_.fetchCancelled;
      dropFetch(fPhase_ == 0 ? 0 : fN_);
      return false;
    }
    switch (fPhase_) {
      case 0: {  // the next cold state (from the newest down): reuse the runs in PSRAM, admit the others
        if (fGot_ >= fetchCold_) return refuseFetch(0);
        fSt_ = coldTier_.at(s, static_cast<uint8_t>(fetchCold_ - 1 - fGot_));
        fN_ = coldTier_.runCount(fSt_);
        for (uint8_t i = 0; i < fN_; ++i) {
          const cold::CardRunRef ref = coldTier_.run(fSt_, i);
          const RunId have = cardRunAt(ref.sector);
          if (have != kNoRun) {
            runs_->addRef(have);
            fRun_[i] = have;
            fNew_[i] = false;
            continue;
          }
          const RunId id = runs_->allocate(s, ref.kind);
          if (id == kNoRun) return refuseFetch(i);
          fRun_[i] = id;
          fNew_[i] = true;
          if (res_->reserve(id, ref.rows) != Admission::Ok) return refuseFetch(static_cast<uint8_t>(i + 1));
        }
        fR_ = 0;
        fRow_ = 0;
        fHead_ = false;
        fPhase_ = 1;
        return true;
      }
      case 1: {  // the new runs' records: a header, then at most `kFetchSectors` sectors a grain
        while (fR_ < fN_ && !fNew_[fR_]) ++fR_;
        if (fR_ == fN_) {
          fPhase_ = 2;
          return true;
        }
        const cold::CardRunRef ref = coldTier_.run(fSt_, fR_);
        uint8_t* b = importScratch_;
        if (!fHead_) {
          ++stats_.fetchSectors;
          if (!card_->readSector(kRunAreaBase + ref.sector, b) || !runstore::readHeader(b, s, ref.kind, ref.rows, fWantCrc_)) {
            ++stats_.badRun;
            return refuseFetch(fN_);
          }
          fHead_ = true;
          fCrc_ = 0;
          fSec_ = 0;
          return true;
        }
        for (uint8_t k = 0; k < kFetchSectors && fRow_ < ref.rows; ++k) {
          ++stats_.fetchSectors;
          if (!card_->readSector(kRunAreaBase + ref.sector + 1 + fSec_++, b)) return refuseFetch(fN_);
          const uint32_t m = ref.rows - fRow_ < runstore::kRowsPerSector ? ref.rows - fRow_ : runstore::kRowsPerSector;
          fCrc_ = crc32Update(fCrc_, b, m * kSetFileRowBytes);
          for (uint32_t j = 0; j < m; ++j) {
            const uint8_t* r = b + j * kSetFileRowBytes;
            PassEvent e{};
            e.tick = runstore::get32(r);
            e.noteId = runstore::get32(r + 4);
            e.seq = runstore::get32(r + 8);
            e.status = r[12];
            e.data1 = r[13];
            e.data2 = r[14];
            e.origin = r[15];
            res_->writeOwed(fRun_[fR_], e);
          }
          fRow_ += m;
        }
        if (fRow_ == ref.rows) {  // the run is in: checked, sealed, known to be on the card
          if (fCrc_ != fWantCrc_) {
            ++stats_.badRun;
            return refuseFetch(fN_);
          }
          res_->forgive(fRun_[fR_]);
          runs_->seal(fRun_[fR_]);
          cardRun_[fRun_[fR_]] = runstore::CardRun{ref.sector, runs_->run(fRun_[fR_]).serial};
          fNew_[fR_] = false;  // sealed: given back as any reused run
          ++fR_;
          fRow_ = 0;
          fHead_ = false;
        }
        return true;
      }
      default: {  // the state: kept aside until the target is in
        const ManifestId m = manifests_->create(s, coldTier_.revision(fSt_), coldTier_.loop(fSt_), fRun_, fN_, *runs_);
        for (uint8_t i = 0; i < fN_; ++i) runs_->release(fRun_[i]);  // the manifest holds them now
        if (m == kNoManifest) {
          fN_ = 0;
          return refuseFetch(0);
        }
        fMade_[fGot_++] = m;
        fPhase_ = 0;
        if (fGot_ < fWant_) return true;
        // The target is in: every fetched state hot (the newest cold first, each below the last), then the switch.
        for (uint8_t k = 0; k < fGot_; ++k) {
          if (!history_->promote(s, fMade_[k])) {
            for (uint8_t j = k; j < fGot_; ++j) manifests_->release(fMade_[j], *runs_);
            ++stats_.fetchRefused;
            fGot_ = 0;
            fetchArrived_ = true;  // what was promoted stays; the switch finds the slot as it is
            return false;
          }
          coldTier_.dropNewest(s);
          ++stats_.fetchedStates;
        }
        fGot_ = 0;
        fetchArrived_ = true;
        return false;
      }
    }
  }

  bool recordSaveFailed(uint8_t s) {
    save_.cancel();
    saving_ = kNone;
    pendSector_ = runstore::kNoSector;
    closing_ = closingSave_ = true;
    ++stats_.saveFailed;  // the current copy stays; extents taken stay held until the next boot
    saved_[s] = savingPrint_;
    return false;
  }
  // ---- loading from the run-once store ------------------------------------------------------------------
  // The slot stream rebuilt for `LoadJob`: the header and directory from the state record (each entry's record sector
  // kept), each run's rows from its record (its header checked first, its rows' CRC at its end), the states and CRC
  // from the state record again.
  bool bodyRead(uint8_t* d, uint32_t n) {
    if (n > ioBytes_) return false;
    ioBytes_ -= n;
    uint8_t* buf = importScratch_;
    while (n != 0) {
      if (ioFill_ == kSector) {
        if (!card_->readSector(ioNext_++, buf)) return false;
        ioFill_ = 0;
      }
      const uint32_t k = n < kSector - ioFill_ ? n : kSector - ioFill_;
      for (uint32_t i = 0; i < k; ++i) d[i] = buf[ioFill_ + i];
      ioFill_ = static_cast<uint16_t>(ioFill_ + k);
      d += k;
      n -= k;
    }
    return true;
  }
  bool openRun(uint16_t r) {
    rowByte_ = 0;
    runCrc_ = 0;
    runOut_ = 0;
    runFill_ = kSector;
    if (!card_->readSector(kRunAreaBase + runSector_[r], runBuf())) return false;
    uint32_t crc = 0;
    if (!runstore::readHeader(runBuf(), loading_, runKind_[r], runRows_[r], crc)) {
      ++stats_.badRun;
      return false;
    }
    wantCrc_ = crc;
    return true;
  }
  bool closeRun() {
    if (runCrc_ != wantCrc_) {
      ++stats_.badRun;
      return false;
    }
    return true;
  }
  bool recordRead(uint8_t* d, uint32_t n) {
    while (n != 0) {
      switch (ld_) {
        case Split::Head: {
          if (n < 24 || !bodyRead(d, 24)) return false;
          dirCount_ = static_cast<uint16_t>(d[12] | (d[13] << 8));
          if (dirCount_ > Runs::kRunCount) return false;
          ldCount_ = dirCount_;
          dirAt_ = 0;
          ld_ = dirCount_ ? Split::Dir : Split::Tail;
          d += 24;
          n -= 24;
          break;
        }
        case Split::Dir: {
          uint8_t e[12];
          if (n < 8 || !bodyRead(e, 12)) return false;
          for (uint8_t i = 0; i < 8; ++i) d[i] = e[i];
          runKind_[dirAt_] = e[0];
          runRows_[dirAt_] = runstore::get32(e + 4);
          runSector_[dirAt_] = runstore::get32(e + 8);
          if (runSector_[dirAt_] >= runstore::kAreaSectors) return false;
          if (++dirAt_ == dirCount_) {
            ld_ = Split::Rows;
            r_ = 0;
            if (!openRun(0)) return false;
          }
          d += 8;
          n -= 8;
          break;
        }
        case Split::Rows: {
          while (r_ < dirCount_ && rowByte_ == runBytes(r_)) {  // a run done (or without rows): checked, the next
            if (!closeRun()) return false;
            if (++r_ < dirCount_ && !openRun(r_)) return false;
          }
          if (r_ >= dirCount_) {
            ld_ = Split::Tail;
            break;
          }
          const uint32_t left = runBytes(r_) - rowByte_;
          uint32_t k = n < left ? n : left;
          while (k != 0) {
            if (runFill_ == kSector) {
              if (!card_->readSector(kRunAreaBase + runSector_[r_] + 1 + runOut_++, runBuf())) return false;
              runFill_ = 0;
            }
            const uint32_t m = k < kSector - runFill_ ? k : kSector - runFill_;
            for (uint32_t i = 0; i < m; ++i) d[i] = runBuf()[runFill_ + i];
            runCrc_ = crc32Update(runCrc_, d, m);
            runFill_ = static_cast<uint16_t>(runFill_ + m);
            rowByte_ += m;
            d += m;
            n -= m;
            k -= m;
          }
          break;
        }
        case Split::Tail:
          return bodyRead(d, n);
      }
    }
    return true;
  }
  /// Slot `s`'s copies: true when one is valid (`copyB_` names the later; `altValid_` the other).
  bool pickCopy(uint8_t s) {
    bool valid[2] = {false, false};
    uint32_t seq[2] = {0, 0};
    for (uint8_t c = 0; c < 2; ++c) {
      uint32_t w[4];
      if (!card_->readSector(copyBase(s, c), importScratch_)) continue;
      getWords(importScratch_, w);
      const uint32_t bytes = w[3];
      uint32_t check = 0;
      for (uint8_t i = 0; i < 4; ++i) check |= static_cast<uint32_t>(importScratch_[16 + i]) << (8 * i);
      valid[c] = w[0] == kCopyMagic && w[1] == s && bytes != 0 &&
                 bytes <= (legacy_ ? kMaxFileBytes : kRecordBodyBytes) && check == copySum(s, w[2], bytes);
      seq[c] = w[2];
      bytes_[c] = bytes;
    }
    if (!legacy_)  // what either valid copy lists stays held (a boot may fall back to the other)
      for (uint8_t c = 0; c < 2; ++c)
        if (valid[c]) {
          copySeq_[c] = seq[c];
          markCopy(s, c, bytes_[c]);
          readCold(s, c, seq[c], true);
        }
    if (!valid[0] && !valid[1]) return false;
    for (uint8_t c = 0; c < 2; ++c)
      if (valid[c] && static_cast<int32_t>(seq[c] - nextSeq_) >= 0) nextSeq_ = seq[c] + 1;
    const uint8_t later = !valid[0] ? 1 : !valid[1] ? 0 : static_cast<int32_t>(seq[1] - seq[0]) > 0 ? 1 : 0;
    haveCopy_ |= 1ull << s;
    if (later) copyB_ |= 1ull << s; else copyB_ &= ~(1ull << s);
    altValid_ = valid[later ^ 1];
    return true;
  }
  bool startStoreLoad(uint8_t s, bool b) {
    const uint8_t c = b ? 1 : 0;
    uint32_t w[4];
    if (!card_->readSector(copyBase(s, c), importScratch_)) return false;
    getWords(importScratch_, w);  // checked by `pickCopy`
    ioStore_ = fromStore_ = true;
    ioNext_ = copyBase(s, c) + 1;
    ioBytes_ = w[3];
    ioFill_ = kSector;
    ld_ = Split::Head;  // the record source from its start
    ldCount_ = 0;
    loading_ = s;  // `openRun` checks the records' slot
    if (load_.start(*pool_, *runs_, *manifests_, *history_, *res_, *ids_, s, source_, loadScratch_) !=
        Load::Status::Running) {
      loading_ = kNone;
      return false;
    }
    loading_ = s;
    loadingCopy_ = c;
    return true;
  }
  bool writeHeader(uint8_t s, uint8_t c, uint32_t seq, uint32_t bytes) {
    uint8_t* b = sectorBuf();
    for (uint32_t i = 0; i < kSector; ++i) b[i] = 0;
    putWords(b, kCopyMagic, s, seq, bytes);
    const uint32_t check = copySum(s, seq, bytes);
    for (uint8_t i = 0; i < 4; ++i) b[16 + i] = static_cast<uint8_t>(check >> (8 * i));
    return card_->writeSector(copyBase(s, c), b);
  }

  static void path(uint8_t slot, const char* ext, char* out) {
    const char* f = kFolder;
    uint8_t n = 0;
    while (*f) out[n++] = *f++;
    const char* p = "/slot_";
    while (*p) out[n++] = *p++;
    out[n++] = static_cast<char>('0' + slot / 10);
    out[n++] = static_cast<char>('0' + slot % 10);
    while (*ext) out[n++] = *ext++;
    out[n] = 0;
  }

  void startSave(uint8_t s) {
    // Nothing to keep (never recorded) — unless the store holds a copy: a slot a load left without history (DEC-259)
    // is written empty, else the next boot would bring the old content back.
    if (!seen_[s].any && !((haveCopy_ >> s) & 1)) {
      saved_[s] = seen_[s];
      return;
    }
    // The copy that is not current (an interrupted save leaves its old header over a damaged body: the current copy
    // has the later sequence, and the damaged one fails its CRC should it ever be tried).
    const uint8_t c = (haveCopy_ >> s & 1) ? static_cast<uint8_t>(((copyB_ >> s) & 1) ^ 1) : 0;
    // The next sequence: one counter for every slot, past every copy the boot found (`pickCopy`) — later than the
    // current copy without reading its header (a read took 10 ms on the device) or holding a sequence per slot.
    savingSeq_ = nextSeq_++;
    if (!store_) {
      ++stats_.saveFailed;
      saved_[s] = seen_[s];  // not retried until the slot changes again
      return;
    }
    ioNext_ = copyBase(s, c) + 1;
    ioBytes_ = 0;
    ioFill_ = 0;
    saving_ = s;  // the run records name their slot
    if (!legacy_) startRecordSave();
    if (save_.start(*pool_, *runs_, *manifests_, *history_, s, sink_, ids_->peek(), saveScratch_) !=
        Save::Status::Running) {
      ++stats_.saveFailed;
      saved_[s] = seen_[s];
      saving_ = kNone;
      return;
    }
    saving_ = s;
    savingCopy_ = c;
    savingPrint_ = seen_[s];
  }
  /// A save grain: one sector write at most, and only once the card is ready for it — a body chunk, then the last,
  /// partial sector, then the header.
  bool stepSave() {
    if (!legacy_) return stepRecordSave();
    const uint8_t s = saving_;
    // The sector this grain may write: a body chunk fills at most one (`ioNext_`), then the last one, then the header.
    if (!card_->ready(!sealing_ || ioFill_ != 0 ? ioNext_ : copyBase(s, savingCopy_))) return true;
    if (!sealing_) {
      const typename Save::Status st = save_.step();
      if (st == Save::Status::Running) return true;
      if (st == Save::Status::Done) {
        sealing_ = true;
        return true;
      }
      saving_ = kNone;
      closing_ = closingSave_ = true;
      ++stats_.saveFailed;
      saved_[s] = savingPrint_;
      return false;
    }
    if (ioFill_ != 0 && finishBody()) return true;  // `finishBody` empties the buffer: the header next
    sealing_ = false;
    saving_ = kNone;
    closing_ = closingSave_ = true;
    if (ioFill_ == 0 && writeHeader(s, savingCopy_, savingSeq_, ioBytes_)) {
      haveCopy_ |= 1ull << s;
      if (savingCopy_) copyB_ |= 1ull << s; else copyB_ &= ~(1ull << s);
      ++stats_.saves;
    } else {
      ++stats_.saveFailed;  // the current copy stays (this one's header is cleared)
    }
    saved_[s] = savingPrint_;  // a change during the save differs from it: saved again later
    return false;
  }
  bool stepLoad() {
    const typename Load::Status st = load_.step();
    if (st == Load::Status::Running) return true;
    const uint8_t s = loading_;
    loading_ = kNone;
    if (fromStore_) {
      if (st == Load::Status::Done) {
        if (!legacy_) {  // the loaded runs are on the card, in the records the state record names
          for (uint16_t i = 0; i < ldCount_; ++i) {
            const RunId id = load_.directoryRun(i);
            cardRun_[id] = runstore::CardRun{runSector_[i], runs_->run(id).serial};
          }
          readCold(s, loadingCopy_, copySeq_[loadingCopy_], false);  // below the hot states
        }
        ++stats_.loads;
        saved_[s] = seen_[s] = fingerprint(s);  // what the store holds
      } else if (altValid_) {  // the later copy failed its CRC: the earlier one
        altValid_ = false;
        const uint8_t c = loadingCopy_ ^ 1;
        if (c) copyB_ |= 1ull << s; else copyB_ &= ~(1ull << s);
        if (startStoreLoad(s, c != 0)) return true;
        failLoad(s);
      } else {
        failLoad(s);
      }
      return true;
    }
    card_->closeRead();
    if (st == Load::Status::Done) {
      ++stats_.loads;
      // An engine file of before the store: written into the store once it is there (with approval).
      saved_[s] = seen_[s] = fingerprint(s);
      ++stats_.migrated;
      if (store_) saved_[s] = Fingerprint{};
    } else if (fromTmp_) {
      startImport(s);  // an incomplete first save: the slot's V1 file, if any
    } else {
      failLoad(s);
    }
    return true;
  }
  static void v1Path(uint8_t slot, char* out) {
    const char* p = "/MidiLooper/current/slots/loop_";
    uint8_t n = 0;
    while (*p) out[n++] = *p++;
    out[n++] = '0';
    out[n++] = static_cast<char>('0' + slot / 8);
    out[n++] = '_';
    out[n++] = '0';
    out[n++] = static_cast<char>('0' + slot % 8);
    const char* e = ".bin";
    while (*e) out[n++] = *e++;
    out[n] = 0;
  }
  bool startImport(uint8_t s) {
    v1Path(s, name_);  // the reader keeps this path while it imports (no load or save runs meanwhile)
    const typename Reader::Status st = reader_.start(*card_, name_, import_, *pool_, *runs_, *manifests_, *history_,
                                                      *res_, *ids_, s, importScratch_, kChunk, v1Layout_);
    if (st == Reader::Status::Running) {
      importing_ = s;
      return true;
    }
    if (st != Reader::Status::NoFile) stats_.v1FailedMask |= 1ull << s;
    return false;
  }
  bool stepImport() {
    const typename Reader::Status st = reader_.step();
    if (st == Reader::Status::Running) return true;
    const uint8_t s = importing_;
    importing_ = kNone;
    stats_.v1Outcome[s] = static_cast<uint8_t>(static_cast<uint8_t>(st) |
                                               (st == Reader::Status::ImportRefused ? reader_.importStatus() << 4 : 0));
    if (st == Reader::Status::Done) {
      ++stats_.v1Imported;
      stats_.v1HeldWhole += reader_.heldWhole();
      if (reader_.editsDiscarded()) stats_.v1EditedMask |= 1ull << s;
      seen_[s] = fingerprint(s);  // saved in the new format once approved and settled; saved_ stays "nothing"
      changedAt_[s] = 0;
    } else if (st == Reader::Status::StartOffset || st == Reader::Status::EmptySnapshot) {
      stats_.v1SkippedMask |= 1ull << s;
      if (st == Reader::Status::StartOffset) stats_.v1OffsetMask |= 1ull << s;
      if (st == Reader::Status::EmptySnapshot) stats_.v1EmptyMask |= 1ull << s;
    } else {
      stats_.v1FailedMask |= 1ull << s;
    }
    return true;
  }

  void failLoad(uint8_t s) {
    ++stats_.loadFailed;
    stats_.loadFailedMask |= 1ull << s;
  }

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
