//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The card for the engine (fixed-memory-engine E9.8a): `CardIo` over the Teensy SD library (SdFat underneath).
//
// The adapter writes only what `Persistence` asks, and `Persistence` asks nothing before the owner approves saving on
// the device (the engine folder; owner 2026-11-05). Files are only read (engine files of before the store, V1 files).
// The store (E9.8g) is one file preallocated in one contiguous run; its sectors are then read and written on the card
// directly — no directory lookup, allocation or flush, so a sector costs the card's write and nothing else (every
// file-system operation took 4–8 ms on the device). SdFat's cache never holds a store sector: the store is never
// read or written through the file system after it is opened.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <SD.h>

#include "ContentEngine/Persistence.h"

namespace EngineFirmware {

class SdCardIo : public ContentEngine::CardIo {
 public:
  /// Diagnostics: the worst time (µs) of each card operation since `clearTimes`.
  struct Times {
    uint32_t exists, openRead, sectorRead, sectorWrite, store;
  };
  const Times& times() const { return t_; }
  void clearTimes() { t_ = Times{}; }
  bool begin() { return ready_ = SD.begin(BUILTIN_SDCARD); }
  bool ready() const { return ready_; }
  /// The store's first sector on the card (0: none).
  uint32_t storeSector() const { return base_; }
  /// Why the store did not open: 0 opened, 1 no card, 2 not there (and not to be created), 3 could not be created,
  /// 4 could not be preallocated (no contiguous room), 5 not one contiguous run.
  uint8_t storeError() const { return storeError_; }

  bool exists(const char* path) override {
    const uint32_t t = micros();
    const bool e = ready_ && SD.exists(path);
    worst(t_.exists, t);
    return e;
  }
  bool makeDir(const char* path) override { return ready_ && SD.mkdir(path); }
  bool openRead(const char* path) override {
    const uint32_t t = micros();
    reader_ = SD.open(path, FILE_READ);
    worst(t_.openRead, t);
    return static_cast<bool>(reader_);
  }
  bool read(uint8_t* data, uint32_t n) override {
    return reader_ && reader_.read(data, n) == static_cast<int>(n);
  }
  void closeRead() override {
    if (reader_) reader_.close();
  }
  bool openStore(const char* path, uint32_t sectors, bool create, bool& created) override {
    created = false;
    base_ = count_ = 0;
    storeError_ = 1;
    if (!ready_) return false;
    const uint32_t t = micros();
    FsFile f = SD.sdfs.open(path, O_RDONLY);
    storeError_ = 2;
    if (!f && create) {
      f = SD.sdfs.open(path, O_RDWR | O_CREAT);
      storeError_ = 3;
      if (!f) return false;
      if (!f.preAllocate(static_cast<uint64_t>(sectors) * 512u)) {
        f.close();
        SD.sdfs.remove(path);
        storeError_ = 4;
        return false;
      }
      created = true;
    }
    if (!f) return false;
    uint32_t bgn = 0, end = 0;
    const bool ok = f.contiguousRange(&bgn, &end) && end >= bgn && end - bgn + 1 >= sectors;
    f.close();
    worst(t_.store, t);
    storeError_ = 5;
    if (!ok) return false;
    storeError_ = 0;
    base_ = bgn;
    count_ = sectors;
    return true;
  }
  bool readSector(uint32_t i, uint8_t* data) override {
    if (i >= count_) return false;
    const uint32_t t = micros();
    const bool ok = SD.sdfs.card()->readSector(base_ + i, data);
    worst(t_.sectorRead, t);
    return ok;
  }
  bool writeSector(uint32_t i, const uint8_t* data) override {
    if (i >= count_) return false;
    const uint32_t t = micros();
    const bool ok = SD.sdfs.card()->writeSector(base_ + i, data);
    worst(t_.sectorWrite, t);
    return ok;
  }

 private:
  static void worst(uint32_t& w, uint32_t since) {
    const uint32_t d = micros() - since;
    if (d > w) w = d;
  }
  Times t_{};
  File reader_;
  uint32_t base_ = 0, count_ = 0;
  uint8_t storeError_ = 0;
  bool ready_ = false;
};

}  // namespace EngineFirmware

#endif  // ENGINE_FIRMWARE
