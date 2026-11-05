//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The card for the engine (fixed-memory-engine E9.8a): `CardIo` over the Teensy SD library (SdFat underneath).
//
// The adapter writes whatever `Persistence` asks, and `Persistence` asks nothing before the owner approves saving on
// the device (the engine folder; owner 2026-11-05). V1 files are only ever read. SdFat's rename refuses an existing
// target, so `rename` removes it first; a save interrupted between the two leaves the complete `.tmp`, which boot load
// takes (`Persistence`). `openWrite` starts the file empty (a stale `.tmp` from an interrupted save is replaced).

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <SD.h>

#include "ContentEngine/Persistence.h"

namespace EngineFirmware {

class SdCardIo : public ContentEngine::CardIo {
 public:
  bool begin() { return ready_ = SD.begin(BUILTIN_SDCARD); }
  bool ready() const { return ready_; }

  bool exists(const char* path) override { return ready_ && SD.exists(path); }
  bool makeDir(const char* path) override { return ready_ && SD.mkdir(path); }
  bool openRead(const char* path) override {
    reader_ = SD.open(path, FILE_READ);
    return static_cast<bool>(reader_);
  }
  bool read(uint8_t* data, uint32_t n) override {
    return reader_ && reader_.read(data, n) == static_cast<int>(n);
  }
  void closeRead() override {
    if (reader_) reader_.close();
  }
  bool openWrite(const char* path) override {
    if (!ready_) return false;
    if (SD.exists(path) && !SD.remove(path)) return false;
    writer_ = SD.open(path, FILE_WRITE_BEGIN);
    return static_cast<bool>(writer_);
  }
  bool write(const uint8_t* data, uint32_t n) override {
    return writer_ && writer_.write(data, n) == n;
  }
  bool closeWrite() override {
    if (!writer_) return false;
    writer_.flush();
    writer_.close();
    return true;
  }
  bool rename(const char* from, const char* to) override {
    if (!ready_) return false;
    if (SD.exists(to) && !SD.remove(to)) return false;
    return SD.rename(from, to);
  }

 private:
  File reader_, writer_;
  bool ready_ = false;
};

}  // namespace EngineFirmware

#endif  // ENGINE_FIRMWARE
