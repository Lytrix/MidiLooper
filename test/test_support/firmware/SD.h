#pragma once
// Native SD boundary: an in-memory card, enough for the StorageManager load path.
//
// This was two lines (`struct File {};`) and blocked the whole load path from native coverage --
// `SD.exists(...)` did not even compile. It is now a small emulation so the real load queue,
// selection and admission code can be exercised; the physical card is the only thing replaced.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifndef FILE_READ
#define FILE_READ 0
#endif
#ifndef FILE_WRITE
#define FILE_WRITE 1
#endif
#ifndef FILE_WRITE_BEGIN
#define FILE_WRITE_BEGIN 2
#endif

/// POSIX open flags the persistence path passes to the SD boundary. The device's SD/FsFat layer
/// accepts them; the in-memory card ignores them because `open()` is mode-driven.
#ifndef O_CREAT
#define O_CREAT 0100
#endif
#ifndef O_RDWR
#define O_RDWR 02
#endif
#ifndef O_AT_END
#define O_AT_END 0100000
#endif

class File {
 public:
  std::vector<uint8_t>* bytes = nullptr;
  size_t pos = 0;
  bool isOpen = false;

  explicit operator bool() const { return isOpen; }
  size_t size() const { return bytes != nullptr ? bytes->size() : 0; }
  bool seek(size_t where) {
    if (bytes == nullptr || where > bytes->size()) return false;
    pos = where;
    return true;
  }
  int read(void* dst, size_t count) {
    if (bytes == nullptr || !isOpen) return -1;
    const size_t available = bytes->size() - pos;
    const size_t take = count < available ? count : available;
    if (take != 0) std::memcpy(dst, bytes->data() + pos, take);
    pos += take;
    return static_cast<int>(take);
  }
  int position() const { return static_cast<int>(pos); }
  int available() const {
    if (bytes == nullptr) return 0;
    return static_cast<int>(bytes->size() - pos);
  }
  size_t write(const uint8_t* src, size_t count) {
    if (bytes == nullptr || !isOpen || !writable) return 0;
    if (pos + count > bytes->size()) bytes->resize(pos + count);
    std::memcpy(bytes->data() + pos, src, count);
    pos += count;
    return count;
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  /// The device's `File::flush` pushes buffered bytes to the card. The in-memory card writes
  /// synchronously, so there is nothing to push; the method exists because production persistence
  /// code calls it and would otherwise not compile natively.
  void flush() {}
  void close() { isOpen = false; }

  bool writable = false;
};

class SDClass {
 public:
  std::map<std::string, std::vector<uint8_t>> files;

  void reset() { files.clear(); }
  bool exists(const char* path) const { return files.find(path) != files.end(); }
  bool mkdir(const char*) { return true; }
  bool remove(const char* path) { return files.erase(path) != 0; }

  File open(const char* path, int mode) {
    auto found = files.find(path);
    if (mode == FILE_WRITE || mode == FILE_WRITE_BEGIN) {
      // Device-faithful write modes, and the reason this emulation exists.
      //
      // The Teensy SD class maps `FILE_WRITE` to `O_RDWR | O_CREAT | O_AT_END` (SD.h) -- it
      // **appends**, it does not truncate -- and `FILE_WRITE_BEGIN` to `O_RDWR | O_CREAT`, which
      // only *starts at offset 0*: `O_TRUNC` is never set (SdFat `common/FsApiConstants.h` defines
      // it, and `SD.h` does not pass it), so a stale tail survives as well.
      //
      // This stub used to create-or-truncate on `FILE_WRITE`, which is what the write path
      // *expected* rather than what the device *does*. That single divergence hid the whole
      // stale-temp corruption class from native coverage (see
      // `docs/Plans/sd_temp_write_truncation_refinement.md`), so the modes are modelled here.
      if (found == files.end()) {
        found = files.insert_or_assign(path, std::vector<uint8_t>{}).first;
      }
      File handle;
      handle.bytes = &found->second;
      handle.pos = mode == FILE_WRITE ? found->second.size() : 0;
      handle.isOpen = true;
      handle.writable = true;
      return handle;
    }
    if (found == files.end()) return File{};
    File handle;
    handle.bytes = &found->second;
    handle.pos = 0;
    handle.isOpen = true;
    return handle;
  }

  bool rename(const char* from, const char* to) {
    auto found = files.find(from);
    if (found == files.end()) return false;
    files[to] = std::move(found->second);
    files.erase(found);
    return true;
  }

  /// Test helper: create or replace a file's bytes.
  void writeFile(const char* path, const std::vector<uint8_t>& body) { files[path] = body; }
};

extern SDClass SD;
