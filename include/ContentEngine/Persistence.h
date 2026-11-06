//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Saving and loading the engine's slots on the card (fixed-memory-engine E9.7a; owner decisions 2026-11-05).
//
// * New format, new folder (`kFolder`); V1 files are never written (their import is E9.7b).
// * Approval (owner: once, remembered): nothing is written until the owner approves saving on the device; `approve`
//   creates the folder, and the folder's existence is the remembered approval (`probe` at boot). Without it the engine
//   only reads.
// * Autosave (owner: when settled): a slot whose history changed (current state identity, undo / redo depth) is saved
//   once it has not changed for `kSettleUs` and no take or NOTE_EDIT session is open on it, one slot at a time, as
//   persistence-class grains. A slot that changes during its save is saved again later.
// * The store (E9.8g, owner 2026-11-06: a save must not hold the main loop — every file-system operation took 4–8 ms on
//   the device, a save ~30 ms): one file, `store.bin`, created and preallocated in one contiguous run when saving is
//   approved (or at the first boot after), then only read and written sector by sector, with no file-system work. Each
//   slot has two copies (A / B, `kCopySectors` each: a slot may hold the whole content pool); a save writes the copy
//   that is not current, one sector write a grain (one took up to 8 ms on the device) — the body (`SaveJob`), its last
//   partial sector, then the header with the next sequence (one counter past every copy the boot found, so a save
//   reads nothing) — so an interrupted save leaves the current copy whole. The surface state alternates between two
//   sectors.
// * Boot load: every slot's valid copy with the later sequence is loaded (`LoadJob`: admission, CRC, full release on
//   failure), the other copy when it fails; a slot whose copies fail stays empty and is counted by name
//   (`Stats::loadFailed`); loading never writes. A slot with no copy in the store is taken from before the store: its
//   engine file `slot_NN.mle` (or the complete `slot_NN.tmp` an interrupted rename left), then written into the store
//   (`Stats::migrated`; the files stay on the card, never written again). A slot with neither whose V1 file
//   (`/MidiLooper/current/slots/loop_TT_SS.bin`, slot = TT x 8 + SS) holds an LSV1 snapshot is imported from it (E9.7b,
//   `Lsv1Reader`: exact current content), as is one holding a pass stream (E9.7c: its Active passes as runs, legacy
//   note edits discarded and named: `v1EditedMask`); a loop-start-shifted or empty V1 slot is skipped and named
//   (`v1SkippedMask`), a failed one named too (`v1FailedMask`). Imported slots are saved once approved; V1 files are
//   never written.
// * Surface state (owner 2026-11-06, E9.8): the selected track and each track's selected slot, as `kStateBytes` bytes
//   from the surface (`setSurfaceState`), saved like a slot — after approval, `kSettleUs` after its last change, as a
//   persistence grain — into the store's state sectors ("MLS2", sequence, the state packed in 27 bits, a check word;
//   before the store: `surface.mls`, read once); read at boot (`restoredSurfaceState`), none valid meaning none.
// `Card`: the files read only (exists, makeDir, openRead, read, closeRead) and the store (openStore, readSector,
// writeSector) — see `CardIo`.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/SetFile.h"
#include "ContentEngine/V1Import.h"

namespace ContentEngine {

/// The card, as the integration provides it (SD on the Teensy, a fake on the host).
struct CardIo {
  // Files, read only: engine files written before the store (E9.8g), V1 files.
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
  virtual ~CardIo() = default;
};

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
class Persistence {
 public:
  static constexpr const char* kFolder = "/MidiLooper/engine";
  static constexpr uint32_t kSettleUs = 2000000;  // 2 s without a change
  static constexpr uint8_t kStateBytes = 9;        // the selected track, then each track's selected slot
  static constexpr uint32_t kChunk = 16384;
  // A save grain produces at most one sector (the CRC runs across chunks, so the file is the same whatever the chunk).
  static constexpr uint32_t kSaveChunk = 512;
  static constexpr uint32_t kSector = 512;
  static constexpr const char* kStoreFile = "/MidiLooper/engine/store.bin";
  // The largest slot file: one slot may hold every content row (the pool is shared), each run once, every state.
  static constexpr uint32_t kMaxFileBytes = 24 + Config::kRunCount * 8u + Config::kContentRows * kSetFileRowBytes +
                                            Config::kHistoryDepth * (9u + 2u * Config::kRunsPerManifest) + 4;
  static constexpr uint32_t kCopySectors = 1 + (kMaxFileBytes + kSector - 1) / kSector;  // header + body
  static constexpr uint32_t kStoreSectors = 2 + 2u * Slots * kCopySectors;  // the surface state's two, two per slot
  using Reservations = RowReservations<Pool, Runs>;

  // A save's body into the store, and a load's bytes out of it (or out of a file of before the store), through one
  // cursor (`io*_`: a save and a load never run together; the persistence state is a fixed PSRAM line).
  struct Sink {
    Persistence* p;
    bool write(const uint8_t* d, uint32_t n) {
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
    uint64_t v1SkippedMask;  // V1 slots skipped (owner: reported): a shifted loop start, or empty
    uint64_t v1OffsetMask;   // ... of them, skipped for a shifted loop start
    uint64_t v1EmptyMask;    // ... of them, without notes or kept events
    uint8_t v1Outcome[Slots];  // per slot: the reader's status (and the importer's in the high nibble when refused)
    uint64_t v1FailedMask;   // V1 slots that failed to read or import
    uint64_t v1EditedMask;   // imported slots whose legacy note edits were discarded (owner 2026-11-05)
    uint32_t v1HeldWhole;    // imported notes that had no NoteOff in their pass: held for the whole loop
    uint32_t migrated;       // slots loaded from an engine file of before the store (written into the store)
    uint8_t store;           // 0: not opened (no approval yet), 1: ready, 2: could not be opened or created
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
    store_ = sealing_ = fromStore_ = stateHave_ = false;
    haveCopy_ = copyB_ = 0;
    stateSeq_ = savingSeq_ = 0;
    nextSeq_ = 1;
    stateCopy_ = 0;
    for (uint8_t s = 0; s < Slots; ++s) {
      saved_[s] = seen_[s] = Fingerprint{};
      changedAt_[s] = 0;
    }
    state_ = stateSaved_ = 0;
    stateRead_ = kNoState;
    stateDue_ = stateKnown_ = false;
    stateChangedUs_ = 0;
    stats_ = Stats{};
  }

  // ---- Approval -------------------------------------------------------------------------------------------------------
  /// Boot: the folder is the remembered approval.
  void probe() { approved_ = card_->exists(kFolder); }
  /// The owner approved saving on the device: the folder is created (the one write approval permits first).
  bool approve() {
    if (!approved_) approved_ = card_->exists(kFolder) || card_->makeDir(kFolder);
    if (approved_ && !store_ && saving_ == kNone) {
      openStore(sectorBuf());
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
    for (uint8_t s = 0; s < Slots; ++s) {
      const Fingerprint f = fingerprint(s);
      if (!(f == seen_[s])) {
        seen_[s] = f;
        changedAt_[s] = stamp(nowUs);
      }
    }
    if (!approved_ || booting() || saving_ != kNone || stateDue_) return;
    for (uint8_t s = 0; s < Slots; ++s) {
      if (seen_[s] == saved_[s] || s == busySlotA || s == busySlotB) continue;
      if (static_cast<uint32_t>(static_cast<uint16_t>(stamp(nowUs) - changedAt_[s])) * 1024u < kSettleUs) continue;
      startSave(s);
      return;
    }
    if (stateKnown_ && nowUs - stateChangedUs_ >= kSettleUs && state_ != stateSaved_)
      stateDue_ = true;  // written by the next persistence grain
  }
  bool pending() const { return saving_ != kNone || booting() || stateDue_; }

  // ---- Surface state ----------------------------------------------------------------------------------------------------
  /// The surface's state now (`kStateBytes`); saved once settled and approved.
  void setSurfaceState(const uint8_t* s, uint32_t nowUs) {
    const uint32_t packed = pack(s);
    if (stateKnown_ && packed == state_) return;
    state_ = packed;
    stateKnown_ = true;
    stateChangedUs_ = nowUs;
  }
  /// The state read at boot. False: none (no file, or a bad one).
  bool restoredSurfaceState(uint8_t* out) const {
    if (stateRead_ == kNoState) return false;
    out[0] = stateRead_ & 7;  // the selected track; then track t's slot, t x 8 + its index
    for (uint8_t t = 0; t < kStateBytes - 1; ++t) out[1 + t] = static_cast<uint8_t>(t * 8 + ((stateRead_ >> (3 + 3 * t)) & 7));
    return true;
  }
  /// One persistence grain: a save chunk, or a load grain, or the next boot file opened.
  bool step() {
    if (saving_ != kNone) return stepSave();
    if (loading_ != kNone) return stepLoad();
    if (importing_ != kNone) return stepImport();
    if (stateDue_) {
      writeState();
      return false;
    }
    if (bootNext_ == 0) {  // the boot's first grain: the store (created on the first boot after approval), the state
      if (approved_) openStore(importScratch_);
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
    for (uint8_t t = 0; t < kStateBytes - 1; ++t) v |= static_cast<uint32_t>(s[1 + t] & 7u) << (3 + 3 * t);
    return v;
  }
  static uint32_t stateSum(uint32_t v) { return (v * 2654435761u) ^ 0xA5A5A5A5u; }
  // The store's state sectors 0 and 1, alternately (one sector write is the whole save): magic "MLS2", sequence,
  // state, check word; the valid one with the later sequence counts.
  void writeState() {
    stateDue_ = false;
    if (!store_) return;
    const uint8_t c = stateHave_ ? static_cast<uint8_t>(stateCopy_ ^ 1) : 0;
    uint8_t* b = sectorBuf();
    for (uint32_t i = 0; i < kSector; ++i) b[i] = 0;
    const uint32_t seq = stateSeq_ + 1;
    putWords(b, kStateMagic2, seq, state_, stateSum(state_ ^ seq));
    if (!card_->writeSector(c, b)) return;
    stateSeq_ = seq;
    stateCopy_ = c;
    stateHave_ = true;
    stateSaved_ = state_;
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
  // sector (magic "MLC1", slot, sequence, body bytes, check word), then the body (the slot file of E8.1). A save writes
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
  static uint32_t copyBase(uint8_t s, uint8_t c) { return 2 + (2u * s + c) * kCopySectors; }
  static uint32_t copySum(uint32_t slot, uint32_t seq, uint32_t bytes) {
    return ((slot * 0x9E3779B1u) ^ (seq * 2654435761u) ^ (bytes * 0x85EBCA77u)) + 0x5A5A5A5Au;
  }
  /// Opens the store (creating it when missing; a new store's headers are cleared: its sectors hold old card data).
  void openStore(uint8_t* buf) {
    bool created = false;
    store_ = card_->openStore(kStoreFile, kStoreSectors, true, created);
    if (store_ && created) {
      for (uint32_t i = 0; i < kSector; ++i) buf[i] = 0;
      for (uint32_t i = 0; i < 2 && store_; ++i) store_ = card_->writeSector(i, buf);
      for (uint8_t s = 0; s < Slots && store_; ++s)
        for (uint8_t c = 0; c < 2 && store_; ++c) store_ = card_->writeSector(copyBase(s, c), buf);
    }
    stats_.store = store_ ? 1 : 2;
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
      valid[c] = w[0] == kCopyMagic && w[1] == s && bytes != 0 && bytes <= kMaxFileBytes &&
                 check == copySum(s, w[2], bytes);
      seq[c] = w[2];
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
    if (load_.start(*pool_, *runs_, *manifests_, *history_, *res_, *ids_, s, source_, loadScratch_) !=
        Load::Status::Running)
      return false;
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
    if (!seen_[s].any) {  // nothing to keep (never recorded)
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
    if (save_.start(*pool_, *runs_, *manifests_, *history_, s, sink_, ids_->peek(), saveScratch_) !=
        Save::Status::Running) {
      ++stats_.saveFailed;
      saved_[s] = seen_[s];
      return;
    }
    saving_ = s;
    savingCopy_ = c;
    savingPrint_ = seen_[s];
  }
  /// A save grain: one sector write at most (one took up to 8 ms on the device) — a body chunk, then the last,
  /// partial sector, then the header.
  bool stepSave() {
    const uint8_t s = saving_;
    if (!sealing_) {
      const typename Save::Status st = save_.step();
      if (st == Save::Status::Running) return true;
      if (st == Save::Status::Done) {
        sealing_ = true;
        return true;
      }
      saving_ = kNone;
      ++stats_.saveFailed;
      saved_[s] = savingPrint_;
      return false;
    }
    if (ioFill_ != 0 && finishBody()) return true;  // `finishBody` empties the buffer: the header next
    sealing_ = false;
    saving_ = kNone;
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
  uint8_t savingCopy_ = 0, loadingCopy_ = 0, stateCopy_ = 0;
  uint64_t haveCopy_ = 0, copyB_ = 0;  // per slot: a valid copy in the store, the current one is B
  uint32_t stateSeq_ = 0, savingSeq_ = 0, nextSeq_ = 1;  // `nextSeq_`: the next save's sequence (any slot)
  uint32_t ioNext_ = 0, ioBytes_ = 0;  // the cursor: the next sector, the bytes written (save) or left (load)
  uint16_t ioFill_ = 0;                // bytes used of the sector buffer
  bool ioStore_ = false;               // the load reads the store (else a file)
  bool stateDue_ = false, stateKnown_ = false;
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
  Import import_;
  Reader reader_;
  Stats stats_{};
};

}  // namespace ContentEngine
