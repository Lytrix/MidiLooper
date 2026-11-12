//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `Persistence`'s saving into the run-once store, the cold tier and the fetch: the member function bodies,
// declared (and documented) in `Persistence.h`, which includes this file after the class.

#pragma once

#include "ContentEngine/Storage/Persistence.h"

namespace ContentEngine {

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::fifoPush(const uint8_t* d, uint32_t n) -> bool {
  if (n > kFifo - fifoUsed_) return false;
  for (uint32_t i = 0; i < n; ++i) fifoBuf()[(fifoHead_ + fifoUsed_ + i) % kFifo] = d[i];
  fifoUsed_ += n;
  return true;
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::pend(uint32_t sector, uint8_t* buf) -> void {
  pendSector_ = sector;
  pendBuf_ = buf;
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::bodyByte(uint8_t v) -> void {
  uint8_t* b = sectorBuf();
  b[ioFill_++] = v;
  ++ioBytes_;
  if (ioFill_ == kSector) {
    pend(ioNext_++, b);
    ioFill_ = 0;
  }
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::out(const uint8_t* d, uint8_t n) -> void {
  for (uint8_t i = 0; i < n; ++i) outq_[outLen_++] = d[i];
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::beginRun() -> void {
  rowByte_ = 0;
  runOut_ = 0;
  runFill_ = 0;
  runCrc_ = 0;
  if (r_ < dirCount_ && runBytes(r_) == 0) runEnd_ = runWrite_[r_] ? 2 : 3;
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::splitStep() -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::startRecordSave() -> void {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::stepRecordSave() -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::demote(uint8_t slot, ManifestId m) -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::cardSectorOf(RunId id) const -> uint32_t {
  const runstore::CardRun& c = cardRun_[id];
  if (c.serial != 0 && c.serial == runs_->run(id).serial) return c.sector;
  if (loading_ != kNone && fromStore_ && !legacy_)
    for (uint16_t i = 0; i < ldCount_; ++i)
      if (load_.directoryRun(i) == id) return runSector_[i];
  return runstore::kNoSector;
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::requestFetch(uint8_t slot) -> void {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::coldBegin(uint8_t s) -> void {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::item32(uint32_t v) -> void {
  runstore::put32(item_ + itemLen_, v);
  itemLen_ = static_cast<uint8_t>(itemLen_ + 4);
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::coldStep(uint8_t s) -> int8_t {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::readCold(uint8_t s, uint8_t c, uint32_t seq, bool mark) -> void {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::cardRunAt(uint32_t sector) const -> RunId {
  for (RunId id = 0; id < Runs::kRunCount; ++id) {
    const runstore::CardRun& c = cardRun_[id];
    if (c.sector == sector && c.serial != 0 && c.serial == runs_->run(id).serial) return id;
  }
  return kNoRun;
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::dropFetch(uint8_t n) -> void {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::refuseFetch(uint8_t n) -> bool {
  ++stats_.fetchRefused;
  dropFetch(n);
  return false;
}

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::stepFetch() -> bool {
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

template <typename Card, typename Pool, typename Runs, typename Manifests, typename HistoryT, uint8_t Slots>
inline auto Persistence<Card, Pool, Runs, Manifests, HistoryT, Slots>::recordSaveFailed(uint8_t s) -> bool {
  save_.cancel();
  saving_ = kNone;
  pendSector_ = runstore::kNoSector;
  closing_ = closingSave_ = true;
  ++stats_.saveFailed;  // the current copy stays; extents taken stay held until the next boot
  saved_[s] = savingPrint_;
  return false;
}

}  // namespace ContentEngine
