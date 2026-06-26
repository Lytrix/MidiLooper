//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "CurrentSetStorage.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <SD.h>
#endif

namespace CurrentSetStorage {

namespace {

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

}  // namespace

bool formatLoopSlotPath(char* out, size_t outSize, uint8_t trackIndex, uint8_t slotIndex) {
  if (out == nullptr || outSize < 32) {
    return false;
  }
  const int written =
      std::snprintf(out, outSize, "%s/loop_%02u_%02u.bin", kCurrentSetDir,
                    static_cast<unsigned>(trackIndex), static_cast<unsigned>(slotIndex));
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool formatLoopSlotTempPath(char* out, size_t outSize, uint8_t trackIndex, uint8_t slotIndex) {
  if (out == nullptr || outSize < 36) {
    return false;
  }
  const int written = std::snprintf(out, outSize, "%s/loop_%02u_%02u.bin.tmp", kCurrentSetDir,
                                    static_cast<unsigned>(trackIndex),
                                    static_cast<unsigned>(slotIndex));
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool writeMetaHeader(const StorageIo& io, const MetaHeader& header) {
  if (!ioWrite(io, &header.containerVersion, sizeof(header.containerVersion))) {
    return false;
  }
  if (!ioWrite(io, &header.lastActiveUnix, sizeof(header.lastActiveUnix))) {
    return false;
  }
  if (!ioWrite(io, &header.anchor.loadedFromSequence, sizeof(header.anchor.loadedFromSequence))) {
    return false;
  }
  if (!ioWrite(io, &header.anchor.lastAnchoredSequence,
               sizeof(header.anchor.lastAnchoredSequence))) {
    return false;
  }
  if (!ioWrite(io, &header.anchor.lastMaterialChangeUnix,
               sizeof(header.anchor.lastMaterialChangeUnix))) {
    return false;
  }
  return ioWrite(io, &header.anchor.hasMaterialChangesSinceAnchor,
                 sizeof(header.anchor.hasMaterialChangesSinceAnchor)) &&
         ioWrite(io, header.anchor.reserved, sizeof(header.anchor.reserved));
}

bool readMetaHeader(const StorageIo& io, MetaHeader& header) {
  if (!ioRead(io, &header.containerVersion, sizeof(header.containerVersion))) {
    return false;
  }
  if (!ioRead(io, &header.lastActiveUnix, sizeof(header.lastActiveUnix))) {
    return false;
  }
  if (!ioRead(io, &header.anchor.loadedFromSequence, sizeof(header.anchor.loadedFromSequence))) {
    return false;
  }
  if (!ioRead(io, &header.anchor.lastAnchoredSequence,
              sizeof(header.anchor.lastAnchoredSequence))) {
    return false;
  }
  if (!ioRead(io, &header.anchor.lastMaterialChangeUnix,
              sizeof(header.anchor.lastMaterialChangeUnix))) {
    return false;
  }
  if (!ioRead(io, &header.anchor.hasMaterialChangesSinceAnchor,
              sizeof(header.anchor.hasMaterialChangesSinceAnchor))) {
    return false;
  }
  if (!ioRead(io, header.anchor.reserved, sizeof(header.anchor.reserved))) {
    return false;
  }
  return true;
}

bool writeCompleteMagic(const StorageIo& io) {
  const uint32_t magic = COMPLETE_MAGIC;
  return ioWrite(io, &magic, sizeof(magic));
}

bool verifyCompleteMagicAtEnd(const uint8_t* fileBytes, size_t fileSize) {
  if (fileBytes == nullptr || fileSize < sizeof(COMPLETE_MAGIC)) {
    return false;
  }
  uint32_t magic = 0;
  std::memcpy(&magic, fileBytes + fileSize - sizeof(COMPLETE_MAGIC), sizeof(magic));
  return magic == COMPLETE_MAGIC;
}

#if defined(ARDUINO)

bool ensureDirectory(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (SD.exists(path)) {
    return true;
  }
  return SD.mkdir(path);
}

bool atomicRenameTempFile(const char* tempPath, const char* finalPath) {
  if (tempPath == nullptr || finalPath == nullptr) {
    return false;
  }
  if (!SD.exists(tempPath)) {
    return false;
  }
  if (SD.exists(finalPath)) {
    SD.remove(finalPath);
  }
  return SD.rename(tempPath, finalPath);
}

bool verifyFileCompleteMagic(const char* path) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const size_t fileSize = file.size();
  if (fileSize < sizeof(COMPLETE_MAGIC)) {
    file.close();
    return false;
  }
  if (!file.seek(fileSize - sizeof(COMPLETE_MAGIC))) {
    file.close();
    return false;
  }
  uint32_t magic = 0;
  const bool ok = file.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) == sizeof(magic) &&
                  magic == COMPLETE_MAGIC;
  file.close();
  return ok;
}

bool patchLastActiveUnix(const char* metaPath, uint32_t lastActiveUnix) {
  File file = SD.open(metaPath, FILE_WRITE);
  if (!file) {
    return false;
  }
  if (!file.seek(kLastActiveUnixOffset)) {
    file.close();
    return false;
  }
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&lastActiveUnix),
                             sizeof(lastActiveUnix)) == sizeof(lastActiveUnix);
  file.close();
  return ok;
}

bool patchAnchorFields(const char* metaPath, const AnchorFields& anchor) {
  File file = SD.open(metaPath, FILE_WRITE);
  if (!file) {
    return false;
  }
  if (!file.seek(kAnchorFieldsOffset)) {
    file.close();
    return false;
  }
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&anchor), sizeof(anchor)) ==
                  sizeof(anchor);
  file.close();
  return ok;
}

#endif

}  // namespace CurrentSetStorage
