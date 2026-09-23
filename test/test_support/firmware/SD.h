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
  int available() const {
    if (bytes == nullptr) return 0;
    return static_cast<int>(bytes->size() - pos);
  }
  void close() { isOpen = false; }
};

class SDClass {
 public:
  std::map<std::string, std::vector<uint8_t>> files;

  void reset() { files.clear(); }
  bool exists(const char* path) const { return files.find(path) != files.end(); }
  bool mkdir(const char*) { return true; }
  bool remove(const char* path) { return files.erase(path) != 0; }

  File open(const char* path, int /*mode*/) {
    auto found = files.find(path);
    if (found == files.end()) return File{};
    File handle;
    handle.bytes = &found->second;
    handle.pos = 0;
    handle.isOpen = true;
    return handle;
  }

  /// Test helper: create or replace a file's bytes.
  void writeFile(const char* path, const std::vector<uint8_t>& body) { files[path] = body; }
};

extern SDClass SD;
