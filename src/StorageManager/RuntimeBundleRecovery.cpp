#include "RuntimeBundleRecovery.h"
#include "StorageManager.h"
#include "StorageManagerInternal.h"
#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "StorageLoopIo.h"
#include "GlobalUndoStack.h"
#include "TrackState.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace StorageManagerInternal {
template<typename T>
STORAGE_PERSIST_MEM bool writeRecoveryValue(File& file, const T& value) {
  return StorageManagerInternal::writeRaw(file, &value, sizeof(value));
}

STORAGE_PERSIST_MEM bool writeRecoveryRuntimeBundle(File& file, const RuntimeBundleRecoveryDefaults& defaults) {
  for (auto slot : defaults.selected) if (slot >= Config::MAX_LOOPS_PER_TRACK) return false;
  if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(file, defaults.epoch)) return false;
  StorageIo io{[&](const void* data, size_t n) {
    return file.write(static_cast<const uint8_t*>(data), n) == n;
  }, nullptr};
  CurrentSetStorage::MetaHeader meta{};
  if (!CurrentSetStorage::writeMetaHeader(io, meta)) return false;
  if (!writeRecoveryValue(file,120.0f) || !writeRecoveryValue(file,uint32_t(LOOPER_IDLE)) ||
      !writeRecoveryValue(file,uint32_t(0)) || !writeRecoveryValue(file,uint8_t(Config::NUM_TRACKS))) return false;
  for (uint8_t t=0;t<Config::NUM_TRACKS;++t) {
    if (!writeRecoveryValue(file,uint32_t(TRACK_STOPPED)) || !writeRecoveryValue(file,false)) return false;
    for (uint8_t s=0;s<Config::MAX_LOOPS_PER_TRACK;++s) {
      if (!writeRecoveryValue(file,s==defaults.selected[t]) || !writeRecoveryValue(file,false) ||
          !writeRecoveryValue(file,uint32_t(s))) return false;
    }
  }
  if (!writeRecoveryValue(file,uint8_t(0))) return false;
  for (auto s:defaults.selected) if (!writeRecoveryValue(file,s)) return false;
  if (!writeRecoveryValue(file,kFooterSelectedSlotExtensionToken)) return false;
  for (auto s:defaults.selected) if (!writeRecoveryValue(file,s)) return false;
  if (!writeRecoveryValue(file,kGlobalUndoStackToken)) return false;
  for (unsigned t=0;t<Config::NUM_TRACKS;++t) {
    GlobalUndoStack empty;
    if (!StorageManagerInternal::writeGlobalUndoStackToFile(file,empty)) return false;
  }
  return writeRecoveryValue(file,CurrentSetStorage::kSaveFileToken);
}

namespace {
template<typename T>
STORAGE_PERSIST_MEM bool readRecoveryValue(File& file, T& value) {
  return readRaw(file, &value, sizeof(value));
}
STORAGE_PERSIST_MEM bool readRecoveryIndex(File& file, uint8_t maximum) {
  uint8_t value=0;
  return readRecoveryValue(file,value) && value<maximum;
}
STORAGE_PERSIST_MEM bool readRecoveryEpoch(File& file, uint32_t& epoch) {
  epoch=0;
  if (!file.seek(0)) return false;
  if (!CurrentWorkspaceStorage::fileStartsWithEpochHeader(file)) return file.seek(0);
  CurrentWorkspaceStorage::EpochFileHeader header{};
  if (!CurrentWorkspaceStorage::readEpochFileHeader(storageIoFromFileRead(file),header)) return false;
  epoch=header.epoch;
  return epoch!=0;
}
// No destination is ever reused: even an interrupted candidate is preserved.
STORAGE_PERSIST_MEM bool unusedRecoveryPath(char* path, size_t capacity, const char* kind) {
  for (unsigned n=0;n<64;++n) {
    const int length=std::snprintf(path,capacity,"%s/runtime.%s.%02u.bin",
                                 CurrentWorkspaceStorage::kCurrentTempDir,kind,n);
    if (length<=0 || static_cast<size_t>(length)>=capacity) return false;
    if (!SD.exists(path)) return true;
  }
  return false;
}

STORAGE_PERSIST_MEM bool validateRecoverySlot(const char* path, uint32_t maximumEpoch,
                                             uint8_t slot, bool& nonempty) {
  File file=SD.open(path,FILE_READ);
  uint32_t epoch=0;
  if (!file || !readRecoveryEpoch(file,epoch) || epoch==0 || epoch>maximumEpoch) return false;
  const size_t offset=file.position();
  if (!CurrentWorkspaceStorage::validateEpochFileOnSd(path,epoch) || file.size()<offset+4)
    return false;
  const size_t size=file.size()-offset-4;
  // Non-aborting allocator for the one-slot input buffer. Parsers retain their
  // existing resource checks; no parsed state survives this validation iteration.
  CommittedChunkIdAllocator<uint8_t> allocator;
  uint8_t* bytes=allocator.allocate(size);
  if (!bytes) return false;
  struct ReleaseRecoveryBuffer {
    CommittedChunkIdAllocator<uint8_t>& allocator; uint8_t* bytes; size_t size;
    ~ReleaseRecoveryBuffer() { allocator.deallocate(bytes,size); }
  } release{allocator,bytes,size};
  if (file.read(bytes,size)!=static_cast<int>(size)) return false;
  file.close();
  bool ok=false;
  if (size>=4 && std::memcmp(bytes,"LSV1",4)==0) {
    LoopSnapshotBodyFormatV1ReadResult parsed{};
    ok=readLoopSnapshotBodyFormatV1(bytes,size,parsed) && parsed.ok &&
       parsed.metadata.loopId==slot && parsed.loopSnapshot;
    nonempty=ok && parsed.metadata.loopLengthTicks!=0;
    releasePersistedLoopSnapshotChunks(parsed.persistedOracle);
  } else {
    PersistedLoopSnapshot parsed{};
    size_t position=0;
    StorageIo io{nullptr,[&](void* dest,size_t n) {
      if (n>size-position) return false;
      std::memcpy(dest,bytes+position,n); position+=n; return true;
    }};
    ok=readPersistedLoopSnapshot(io,parsed) && position==size && parsed.loopId==slot;
    nonempty=ok && parsed.loopLengthTicks!=0;
    releasePersistedLoopSnapshotChunks(parsed);
  }
  return ok;
}
} // namespace

STORAGE_PERSIST_MEM bool validateRuntimeBundleForBoot(const char* path) {
  File file=SD.open(path,FILE_READ);
  uint32_t epoch=0;
  if (!file || !readRecoveryEpoch(file,epoch) ||
      !CurrentWorkspaceStorage::validateEpochFileOnSd(path,epoch)) return false;
  CurrentSetStorage::MetaHeader meta{};
  if (!CurrentSetStorage::readMetaHeader(storageIoFromFileRead(file),meta) ||
      meta.containerVersion!=CurrentSetStorage::CONTAINER_VERSION) return false;
  float tempo=0; uint32_t state=0,master=0; uint8_t tracks=0;
  if (!readRecoveryValue(file,tempo) || !std::isfinite(tempo) || tempo<20 || tempo>300 ||
      !readRecoveryValue(file,state) || state>LOOPER_SETTINGS ||
      !readRecoveryValue(file,master) || !readRecoveryValue(file,tracks) ||
      tracks!=Config::NUM_TRACKS) return false;
  for (uint8_t t=0;t<tracks;++t) {
    if (!readRecoveryValue(file,state) || state>=NUM_TRACK_STATES || !readRecoveryIndex(file,2))
      return false;
    for (uint8_t s=0;s<Config::MAX_LOOPS_PER_TRACK;++s) {
      LoopId identity=0;
      if (!readRecoveryIndex(file,2) || !readRecoveryIndex(file,2) ||
          !readRecoveryValue(file,identity)) return false;
      // Legacy bundle readers sanitize unassigned loop identities; retain that compatibility.
    }
  }
  if (!readRecoveryIndex(file,tracks)) return false;
  for (uint8_t t=0;t<tracks;++t)
    if (!readRecoveryIndex(file,Config::MAX_LOOPS_PER_TRACK)) return false;
  uint32_t token=0;
  if (!readRecoveryValue(file,token)) return false;
  if (token==kFooterSelectedSlotExtensionToken) {
    for (uint8_t t=0;t<tracks;++t)
      if (!readRecoveryIndex(file,Config::MAX_LOOPS_PER_TRACK)) return false;
    if (!readRecoveryValue(file,token)) return false;
  }
  if (token!=kGlobalUndoStackToken) return false;
  for (uint8_t t=0;t<tracks;++t) {
    GlobalUndoStack ignored;
    if (!readGlobalUndoStackMetadataFromFile(file,ignored)) return false;
  }
  if (!readRecoveryValue(file,token)) return false;
  if (token==SavedSetCatalog::kSavedSetMetaTrailerMagic) {
    if (!file.seek(file.position()-4)) return false;
    SavedSetCatalog::SavedSetMetadata ignored{};
    if (!SavedSetCatalog::readSavedSetMetadataTrailer(storageIoFromFileRead(file),ignored) ||
        !readRecoveryValue(file,token)) return false;
  }
  return token==CurrentSetStorage::kSaveFileToken && file.position()==file.size();
}

STORAGE_PERSIST_MEM BootBundleRecovery prepareCurrentWorkspaceForBoot() {
  const char* finalPath=CurrentSetStorage::kCurrentRuntimeBundlePath;
  if (validateRuntimeBundleForBoot(finalPath)) return BootBundleRecovery::Ready;
  bool evidence=SD.exists(finalPath) || SD.exists(CurrentWorkspaceStorage::kWorkspaceMetaPath) ||
                SD.exists(CurrentSetStorage::kCurrentRuntimeBundleTempPath);
  for (uint8_t t=0;t<Config::NUM_TRACKS && !evidence;++t)
    for (uint8_t s=0;s<Config::MAX_LOOPS_PER_TRACK && !evidence;++s) {
      char path[80];
      if (CurrentSetStorage::formatLoopSlotPath(path,sizeof(path),t,s) && SD.exists(path)) evidence=true;
    }
  if (!evidence) return BootBundleRecovery::NoWorkspace;
  storageSession.bootRecovery.writesBlocked=true;
  CurrentWorkspaceStorage::WorkspaceMetaRecord workspace{};
  File workspaceFile=SD.open(CurrentWorkspaceStorage::kWorkspaceMetaPath,FILE_READ);
  const bool workspaceSizeOk=workspaceFile && workspaceFile.size()==CurrentWorkspaceStorage::kWorkspaceMetaByteSize;
  workspaceFile.close();
  if (!workspaceSizeOk || !CurrentWorkspaceStorage::readWorkspaceMetaFile(workspace) ||
      workspace.currentEpoch==0 || workspace.slotCount!=Config::MAX_LOOPS_PER_TRACK)
    return BootBundleRecovery::Blocked;
  RuntimeBundleRecoveryDefaults defaults;
  defaults.epoch=workspace.currentEpoch;
  for (uint8_t t=0;t<Config::NUM_TRACKS;++t) {
    bool selected=false;
    for (uint8_t s=0;s<Config::MAX_LOOPS_PER_TRACK;++s) {
      char path[80]; bool nonempty=false;
      if (!CurrentSetStorage::formatLoopSlotPath(path,sizeof(path),t,s) ||
          !validateRecoverySlot(path,defaults.epoch,s,nonempty)) return BootBundleRecovery::Blocked;
      if (!selected && nonempty) { defaults.selected[t]=s; selected=true; }
    }
  }
  char candidate[96],backup[96];
  if (!unusedRecoveryPath(candidate,sizeof(candidate),"repair") ||
      (SD.exists(finalPath) && !unusedRecoveryPath(backup,sizeof(backup),"before-repair")) ||
      !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir))
    return BootBundleRecovery::Blocked;
  // Exclusive by precondition: only boot owns SD here, and this path did not exist.
  File file=SD.open(candidate,FILE_WRITE_BEGIN);
  if (!file) return BootBundleRecovery::Blocked;
  const bool written=writeRecoveryRuntimeBundle(file,defaults);
  file.flush(); file.close();
  if (!written || !CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(candidate) ||
      !validateRuntimeBundleForBoot(candidate)) return BootBundleRecovery::Blocked;
  if (SD.exists(finalPath) && !SD.rename(finalPath,backup)) return BootBundleRecovery::Blocked;
  if (!SD.rename(candidate,finalPath) || !validateRuntimeBundleForBoot(finalPath))
    return BootBundleRecovery::Blocked;
  storageSession.bootRecovery.writesBlocked=false;
  return BootBundleRecovery::Repaired;
}
} // namespace StorageManagerInternal
