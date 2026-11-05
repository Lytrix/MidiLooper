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
//   persistence-class grains (`SaveJob`, ≤ 16 KiB a write). The file is written to `slot_NN.tmp` and renamed over
//   `slot_NN.mle` when complete, so an interrupted save leaves the previous file whole. A slot that changes during its
//   save is saved again later.
// * Boot load: every slot with an engine file is loaded (`LoadJob`: admission, CRC, full release on failure); a slot
//   whose file fails stays empty and is counted by name (`Stats::loadFailed`); loading never writes. A slot without
//   one whose V1 file (`/MidiLooper/current/slots/loop_TT_SS.bin`, slot = TT x 8 + SS) holds an LSV1 snapshot is
//   imported from it (E9.7b, `Lsv1Reader`: exact current content), as is one holding a pass stream (E9.7c: its Active
//   passes as runs); a loop-start-shifted, empty or edited (until E9.7c-2) V1 slot is skipped and named
//   (`v1SkippedMask`), a failed one named too (`v1FailedMask`). Imported slots are saved in the new
//   format once approved; V1 files are never written.
// `Card` (one file open for reading and one for writing at a time): exists(path), makeDir(path), openRead(path),
// read(data, n), closeRead(), openWrite(path), write(data, n), closeWrite(), rename(from, to) — rename replaces `to`.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/SetFile.h"
#include "ContentEngine/V1Import.h"

namespace ContentEngine {

/// The card, as the integration provides it (SD on the Teensy, a fake on the host).
struct CardIo {
  virtual bool exists(const char* path) = 0;
  virtual bool makeDir(const char* path) = 0;
  virtual bool openRead(const char* path) = 0;
  virtual bool read(uint8_t* data, uint32_t n) = 0;
  virtual void closeRead() = 0;
  virtual bool openWrite(const char* path) = 0;
  virtual bool write(const uint8_t* data, uint32_t n) = 0;
  virtual bool closeWrite() = 0;
  virtual bool rename(const char* from, const char* to) = 0;  // replaces `to`
  virtual ~CardIo() = default;
};

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
class Persistence {
 public:
  static constexpr const char* kFolder = "/MidiLooper/engine";
  static constexpr uint32_t kSettleUs = 2000000;  // 2 s without a change
  static constexpr uint32_t kChunk = 16384;
  using Reservations = RowReservations<Pool, Runs>;

  struct Sink {
    Card* card;
    bool write(const uint8_t* d, uint32_t n) { return card->write(d, n); }
  };
  struct Source {
    Card* card;
    bool read(uint8_t* d, uint32_t n) { return card->read(d, n); }
  };
  using Save = SaveJob<Pool, Runs, Manifests, HistoryT, Sink, kChunk>;
  using Load = LoadJob<Pool, Runs, Manifests, HistoryT, Source, kChunk>;

  struct Stats {
    uint32_t saves;          // slot files completed
    uint32_t saveFailed;     // a write or rename failed (the previous file stays)
    uint32_t loads;          // slots loaded at boot
    uint32_t loadFailed;     // engine files that failed to load (slot left empty)
    uint64_t loadFailedMask; // which slots
    uint32_t v1Imported;     // slots imported from V1 files
    uint64_t v1SkippedMask;  // V1 slots skipped (owner: reported): a shifted loop start, empty, or edit passes
    uint64_t v1OffsetMask;   // ... of them, skipped for a shifted loop start
    uint64_t v1EmptyMask;    // ... of them, without notes or kept events
    uint64_t v1EditedMask;   // ... of them, a pass stream with edit passes (E9.7c-2)
    uint8_t v1Outcome[Slots];  // per slot: the reader's status (and the importer's in the high nibble when refused)
    uint64_t v1FailedMask;   // V1 slots that failed to read or import
    uint32_t v1HeldWhole;    // imported notes that had no NoteOff in their pass: held for the whole loop
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
    sink_ = Sink{&card};
    source_ = Source{&card};
    save_ = Save{};
    load_ = Load{};
    approved_ = false;
    saving_ = loading_ = kNone;
    bootNext_ = Slots;
    for (uint8_t s = 0; s < Slots; ++s) {
      saved_[s] = seen_[s] = Fingerprint{};
      changedUs_[s] = 0;
    }
    stats_ = Stats{};
  }

  // ---- Approval -------------------------------------------------------------------------------------------------------
  /// Boot: the folder is the remembered approval.
  void probe() { approved_ = card_->exists(kFolder); }
  /// The owner approved saving on the device: the folder is created (the one write approval permits first).
  bool approve() {
    if (!approved_) approved_ = card_->exists(kFolder) || card_->makeDir(kFolder);
    return approved_;
  }
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
        changedUs_[s] = nowUs;
      }
    }
    if (!approved_ || booting() || saving_ != kNone) return;
    for (uint8_t s = 0; s < Slots; ++s) {
      if (seen_[s] == saved_[s] || s == busySlotA || s == busySlotB) continue;
      if (nowUs - changedUs_[s] < kSettleUs) continue;
      startSave(s);
      return;
    }
  }
  bool pending() const { return saving_ != kNone || booting(); }
  /// One persistence grain: a save chunk, or a load grain, or the next boot file opened.
  bool step() {
    if (saving_ != kNone) return stepSave();
    if (loading_ != kNone) return stepLoad();
    if (importing_ != kNone) return stepImport();
    while (bootNext_ < Slots) {
      const uint8_t s = bootNext_++;
      path(s, ".mle", name_);
      if (!card_->exists(name_)) {
        if (startImport(s)) return true;
        continue;
      }
      if (!card_->openRead(name_) || load_.start(*pool_, *runs_, *manifests_, *history_, *res_, *ids_, s, source_,
                                                 loadScratch_) != Load::Status::Running) {
        card_->closeRead();
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
    path(s, ".tmp", name_);
    if (!card_->openWrite(name_)) {
      ++stats_.saveFailed;
      saved_[s] = seen_[s];  // not retried until the slot changes again
      return;
    }
    if (save_.start(*pool_, *runs_, *manifests_, *history_, s, sink_, ids_->peek(), saveScratch_) !=
        Save::Status::Running) {
      card_->closeWrite();
      ++stats_.saveFailed;
      saved_[s] = seen_[s];
      return;
    }
    saving_ = s;
    savingPrint_ = seen_[s];
  }
  bool stepSave() {
    const typename Save::Status st = save_.step();
    if (st == Save::Status::Running) return true;
    const uint8_t s = saving_;
    saving_ = kNone;
    const bool closed = card_->closeWrite();
    char tmp[40];
    path(s, ".tmp", tmp);
    path(s, ".mle", name_);
    if (st == Save::Status::Done && closed && card_->rename(tmp, name_)) {
      ++stats_.saves;
      saved_[s] = savingPrint_;  // a change during the save differs from it: saved again later
    } else {
      ++stats_.saveFailed;
      saved_[s] = savingPrint_;
    }
    return false;
  }
  bool stepLoad() {
    const typename Load::Status st = load_.step();
    if (st == Load::Status::Running) return true;
    const uint8_t s = loading_;
    loading_ = kNone;
    card_->closeRead();
    if (st == Load::Status::Done) {
      ++stats_.loads;
      saved_[s] = seen_[s] = fingerprint(s);  // what the card holds
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
    v1Path(s, v1Name_);
    const typename Reader::Status st = reader_.start(*card_, v1Name_, import_, *pool_, *runs_, *manifests_, *history_,
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
      seen_[s] = fingerprint(s);  // saved in the new format once approved and settled; saved_ stays "nothing"
      changedUs_[s] = 0;
    } else if (st == Reader::Status::StartOffset || st == Reader::Status::EmptySnapshot ||
               st == Reader::Status::Edited) {
      stats_.v1SkippedMask |= 1ull << s;
      if (st == Reader::Status::StartOffset) stats_.v1OffsetMask |= 1ull << s;
      if (st == Reader::Status::EmptySnapshot) stats_.v1EmptyMask |= 1ull << s;
      if (st == Reader::Status::Edited) stats_.v1EditedMask |= 1ull << s;
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
  bool approved_ = false;
  uint8_t saving_ = kNone, loading_ = kNone, bootNext_ = Slots;
  Fingerprint saved_[Slots], seen_[Slots], savingPrint_{};
  uint32_t changedUs_[Slots] = {};
  char name_[32] = {};    // "/MidiLooper/engine/slot_NN.tmp": 30 characters
  char v1Name_[44] = {};  // "/MidiLooper/current/slots/loop_TT_SS.bin": 40 characters
  uint8_t* importScratch_ = nullptr;
  MidiEventLayout v1Layout_ = kTeensyMidiEvent;
  uint8_t importing_ = kNone;
  Import import_;
  Reader reader_;
  Stats stats_{};
};

}  // namespace ContentEngine
