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

class SDClass;
/// The card the directory handles enumerate. Declared before `File` so a directory handle can read it
/// without `File` needing the full `SDClass` definition (which is what created a cycle in the first
/// attempt at this change).
extern SDClass SD;

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

  // --- Directory handles -----------------------------------------------------
  // On the device a `File` doubles as a directory handle: `SD.open(dir)` then repeated
  // `openNextFile()`. The saved-set persistence scan (`scanHighestSavedSetSequenceOnSd`) reads the sets
  // archive that way, so the in-memory card models it. The card is a flat path map, so a path "is a
  // directory" exactly when other paths live beneath it.
  bool isDir = false;
  /// Child paths of this directory, in the map's (sorted) order, and how far the cursor has advanced.
  std::vector<std::string> dirEntries;
  size_t dirCursor = 0;
  /// The last entry returned by `openNextFile`, for `name()`.
  std::string entryName;
  bool isDirectory() const { return isDir; }
  const char* name() const { return entryName.c_str(); }
  File openNextFile();
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
    // Classify by the **flags that carry the meaning**, not by the whole integer. The stub used to
    // compare the raw argument, so a caller passing `FILE_WRITE | O_CREAT` -- the only express-create
    // site, `MidPassChunkPersist`'s seal journal -- matched neither branch, fell through to the
    // existing-file path, and got an **invalid handle** on a card where the journal did not exist yet.
    // Masking to `FILE_WRITE_BEGIN` does not work either: this stub's fallback macros (1 and 2) and the
    // native Arduino core's (`0x100` / a `0x02` flag) disagree, and the guard is `#ifndef`-based, so the
    // numeric value depends on include order. `FILE_WRITE` means append (`O_AT_END`); a create/truncate
    // open means start at 0.
    const bool appendMode = (mode & O_AT_END) != 0;
    const bool createMode = !appendMode && (mode & O_CREAT) != 0;
    if (appendMode || createMode || mode == FILE_WRITE || mode == FILE_WRITE_BEGIN) {
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
      handle.pos = appendMode ? found->second.size() : 0;
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

  /// True when other file paths live beneath this path, which is what makes it a directory here.
  static bool pathIsDirectory(const std::map<std::string, std::vector<uint8_t>>& files,
                              const char* path) {
    std::string prefix(path);
    while (!prefix.empty() && prefix.back() == '/') {
      prefix.pop_back();
    }
    prefix += '/';
    for (const auto& entry : files) {
      if (entry.first.compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
  }

  /// Directory-handle form: `SD.open(dir)` with no mode. Returns an open directory `File` when other
  /// paths live beneath `path`, otherwise an invalid handle (the device returns false for a path that is
  /// neither a file nor a directory).
  File open(const char* path) {
    if (!pathIsDirectory(files, path)) return File{};
    File handle;
    handle.isDir = true;
    handle.isOpen = true;
    std::string prefix(path);
    while (!prefix.empty() && prefix.back() == '/') {
      prefix.pop_back();
    }
    prefix += '/';
    for (const auto& entry : files) {
      if (entry.first.compare(0, prefix.size(), prefix) == 0) {
        handle.dirEntries.push_back(entry.first);
      }
    }
    return handle;
  }

  /// Test helper: create or replace a file's bytes.
  void writeFile(const char* path, const std::vector<uint8_t>& body) { files[path] = body; }
};

/// One directory entry per call, mirroring the device: the returned handle is a normal open file (or a
/// directory, when that entry itself has children) and the caller closes it.
inline File File::openNextFile() {
  if (!isDir || dirCursor >= dirEntries.size()) return File{};
  const std::string child = dirEntries[dirCursor++];
  entryName = child;
  if (SDClass::pathIsDirectory(SD.files, child.c_str())) {
    File nested;
    nested.isDir = true;
    nested.isOpen = true;
    const std::string prefix = child + '/';
    for (const auto& entry : SD.files) {
      if (entry.first.compare(0, prefix.size(), prefix) == 0) {
        nested.dirEntries.push_back(entry.first);
      }
    }
    return nested;
  }
  return SD.open(child.c_str(), FILE_READ);
}

extern SDClass SD;
