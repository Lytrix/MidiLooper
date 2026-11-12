//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `Persistence`'s loading from the run-once store and the boot's migration: the member function bodies,
// declared (and documented) in `Persistence.h`, which includes this file after the class.

#pragma once

#include "ContentEngine/Storage/Persistence.h"

namespace ContentEngine {

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::bodyRead(uint8_t* d, uint32_t n) -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::openRun(uint16_t r) -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::closeRun() -> bool {
  if (runCrc_ != wantCrc_) {
    ++stats_.badRun;
    return false;
  }
  return true;
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::recordRead(uint8_t* d, uint32_t n) -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::pickCopy(uint8_t s) -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::startStoreLoad(uint8_t s, bool b) -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::writeHeader(uint8_t s, uint8_t c, uint32_t seq, uint32_t bytes) -> bool {
  uint8_t* b = sectorBuf();
  for (uint32_t i = 0; i < kSector; ++i) b[i] = 0;
  putWords(b, kCopyMagic, s, seq, bytes);
  const uint32_t check = copySum(s, seq, bytes);
  for (uint8_t i = 0; i < 4; ++i) b[16 + i] = static_cast<uint8_t>(check >> (8 * i));
  return card_->writeSector(copyBase(s, c), b);
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::path(uint8_t slot, const char* ext, char* out) -> void {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::startSave(uint8_t s) -> void {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::stepSave() -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::stepLoad() -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::v1Path(uint8_t slot, char* out) -> void {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::startImport(uint8_t s) -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::stepImport() -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::failLoad(uint8_t s) -> void {
  ++stats_.loadFailed;
  stats_.loadFailedMask |= 1ull << s;
}

}  // namespace ContentEngine
