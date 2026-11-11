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
// read or written through the file system after it is opened. Writes do not wait for the card either (E9.8h): see
// `writeSector` and `ready`. The milestone library (DEC-258) is a second such file, written through the same stream.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <SD.h>

#include "ContentEngine/Storage/Persistence.h"

namespace EngineFirmware {

class SdCardIo : public ContentEngine::CardIo {
 public:
  /// Diagnostics: the worst time (µs) of each card operation since `clearTimes`.
  struct Times {
    uint32_t exists, openRead, sectorRead, sectorWrite, store;
    uint32_t nextWrite, jumpWrite;  // a write continuing the open stream, and one starting a stream
    uint32_t notReady, stops;       // `ready` said wait; streams stopped without waiting
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
    quiesce();
    const uint32_t t = micros();
    const bool e = ready_ && SD.exists(path);
    worst(t_.exists, t);
    return e;
  }
  bool rename(const char* from, const char* to) override {  // boot only (DEC-254: the store's names)
    quiesce();
    return ready_ && SD.exists(from) && !SD.exists(to) && SD.sdfs.rename(from, to);
  }
  uint32_t fileSectors(const char* path) override {
    quiesce();
    if (!ready_) return 0;
    FsFile f = SD.sdfs.open(path, O_RDONLY);
    if (!f) return 0;
    const uint64_t n = f.fileSize() / 512u;
    f.close();
    return n > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(n);
  }
  bool makeDir(const char* path) override {
    quiesce();
    return ready_ && SD.mkdir(path);
  }
  bool openRead(const char* path) override {
    quiesce();
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
    const uint32_t t = micros();
    const bool ok = openContiguous(path, sectors, create, created, base_, count_, storeError_);
    worst(t_.store, t);
    return ok;
  }
  bool readSector(uint32_t i, uint8_t* data) override {
    return i < count_ && readAt(base_ + i, data);
  }
  /// The library's first sector on the card (0: none), why it did not open (as `storeError`), and how long the last
  /// open took (µs; creating it preallocates 1 GiB).
  uint32_t librarySector() const { return libBase_; }
  uint8_t libraryError() const { return libError_; }
  uint32_t libraryOpenUs() const { return libOpenUs_; }
  bool openLibrary(const char* path, uint32_t sectors, bool create, bool& created) override {
    const uint32_t t = micros();
    const bool ok = openContiguous(path, sectors, create, created, libBase_, libCount_, libError_);
    libOpenUs_ = micros() - t;
    return ok;
  }
  bool readLibrarySector(uint32_t i, uint8_t* data) override {
    return i < libCount_ && readAt(libBase_ + i, data);
  }
  bool writeLibrarySector(uint32_t i, const uint8_t* data) override {
    return i < libCount_ && writeAt(libBase_ + i, data);
  }
  bool libraryReady(uint32_t i) override {
    return readyAt(i == ContentEngine::CardIo::kIdle ? i : libBase_ + i);
  }
  // Store writes are SdFat's multi-block stream (`writeStart`, `writeData`), the adapter's own: SdFat's `writeSector`
  // stops an open stream and waits while the card programs it (1.6–8.2 ms on the owner's card), so it is not used.
  // The stream stays open while sectors follow; `ready` stops it without waiting when another sector or the idle card
  // is asked for, then reports busy (DAT0) until the card has programmed it. SdFat's own state stays idle throughout;
  // its transfer flag is cleared (`syncDevice` on an idle SdFat: no wait once the block is done) before a stop or a
  // new stream, because a command clears the transfer-complete status that flag would wait for.
  bool writeSector(uint32_t i, const uint8_t* data) override {
    return i < count_ && writeAt(base_ + i, data);
  }
  /// False while writing sector `i` would wait: the card still takes the last block or programs a stopped stream; or
  /// the open stream is not at `i` (`kIdle`: the store is to be left idle) — it is stopped now, without waiting.
  bool ready(uint32_t i) override {
    return readyAt(i == ContentEngine::CardIo::kIdle ? i : base_ + i);
  }
  /// Stops an open store stream and waits until the card has programmed it: before anything else reaches the card
  /// (SdFat believes it idle).
  void quiesce() {
    if (!streaming_) return;
    sdio()->syncDevice();
    sdio()->stopTransmission(true);
    streaming_ = false;
  }

 private:
  /// Opens (or creates and preallocates) `path` as one contiguous run of at least `sectors`: its first card sector in
  /// `base`. `error` as `storeError`.
  bool openContiguous(const char* path, uint32_t sectors, bool create, bool& created, uint32_t& base, uint32_t& count,
                      uint8_t& error) {
    quiesce();
    created = false;
    base = count = 0;
    error = 1;
    if (!ready_) return false;
    FsFile f = SD.sdfs.open(path, O_RDONLY);
    error = 2;
    if (!f && create) {
      f = SD.sdfs.open(path, O_RDWR | O_CREAT);
      error = 3;
      if (!f) return false;
      if (!f.preAllocate(static_cast<uint64_t>(sectors) * 512u)) {
        f.close();
        SD.sdfs.remove(path);
        error = 4;
        return false;
      }
      created = true;
    }
    if (!f) return false;
    uint32_t bgn = 0, end = 0;
    const bool ok = f.contiguousRange(&bgn, &end) && end >= bgn && end - bgn + 1 >= sectors;
    f.close();
    error = 5;
    if (!ok) return false;
    error = 0;
    base = bgn;
    count = sectors;
    return true;
  }
  bool readAt(uint32_t sector, uint8_t* data) {
    quiesce();
    const uint32_t t = micros();
    const bool ok = SD.sdfs.card()->readSector(sector, data);
    worst(t_.sectorRead, t);
    return ok;
  }
  bool writeAt(uint32_t sector, const uint8_t* data) {
    const uint32_t t = micros();
    const bool next = streaming_ && sector == next_;
    if (!next) {
      quiesce();  // waits only when `ready` was not asked first
      if (!sdio()->syncDevice() || !sdio()->writeStart(sector)) return false;
      streaming_ = true;
    }
    const bool ok = sdio()->writeData(data);
    next_ = sector + 1;
    worst(t_.sectorWrite, t);
    worst(next ? t_.nextWrite : t_.jumpWrite, t);
    return ok;
  }
  /// `ready` on a card sector (`kIdle`: the card to be left idle).
  bool readyAt(uint32_t sector) {
    SdioCard* c = sdio();
    bool r = !c->isBusy();
    if (r && streaming_ && (sector == ContentEngine::CardIo::kIdle || sector != next_)) {
      c->syncDevice();
      c->stopTransmission(false);
      streaming_ = false;
      ++t_.stops;
      r = false;
    }
    if (!r) ++t_.notReady;
    return r;
  }
  static void worst(uint32_t& w, uint32_t since) {
    const uint32_t d = micros() - since;
    if (d > w) w = d;
  }
  Times t_{};
  File reader_;
  // The built-in slot is SDIO (`begin`), so the card is an `SdioCard`.
  static SdioCard* sdio() { return static_cast<SdioCard*>(SD.sdfs.card()); }
  uint32_t base_ = 0, count_ = 0, next_ = 0;  // `next_`: the open stream's next card sector
  bool streaming_ = false;
  uint8_t storeError_ = 0;
  uint32_t libBase_ = 0, libCount_ = 0, libOpenUs_ = 0;
  uint8_t libError_ = 0;
  bool ready_ = false;
};

}  // namespace EngineFirmware

#endif  // ENGINE_FIRMWARE
