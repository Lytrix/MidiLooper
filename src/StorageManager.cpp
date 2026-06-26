//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "TrackManager.h"
#include "Loop.h"
#include "Slot.h"
#include "StorageLoopIo.h"
#include "CurrentSetStorage.h"
#include "SavedSetCatalog.h"
#include "RtcTime.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MemoryMonitor.h"
#include <SD.h>
#include <Arduino.h>
#include "TrackUndo.h"
#include "Utils/MemoryPool.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#define STORAGE_FILENAME CurrentSetStorage::kLegacyMonolithPath
#define STORAGE_VERSION 5
static constexpr uint32_t GLOBAL_UNDO_MAGIC = 0x33535547UL;  // "GUS3"
static constexpr uint32_t STORAGE_COMPLETE_MAGIC = CurrentSetStorage::COMPLETE_MAGIC;

// Helper to write raw data
static bool writeRaw(File &file, const void *data, size_t size) {
    return file.write((const uint8_t*)data, size) == size;
}
// Helper to read raw data
static bool readRaw(File &file, void *data, size_t size) {
    // Check if enough bytes remain
    if ((file.size() - file.position()) < size) {
        Serial.print("[StorageManager] readRaw: Not enough bytes left in file. Needed: ");
        Serial.print(size);
        Serial.print(", available: ");
        Serial.println(file.size() - file.position());
        return false;
    }
    int bytesRead = file.read((uint8_t*)data, size);
    if (bytesRead != (int)size) {
        Serial.print("[StorageManager] readRaw: expected ");
        Serial.print(size);
        Serial.print(" bytes, got ");
        Serial.println(bytesRead);
        return false;
    }
    return true;
}

static LooperState sanitizeLoadedLooperState(LooperState state) {
    switch (state) {
        case LOOPER_RECORDING:
        case LOOPER_PLAYING:
        case LOOPER_OVERDUBBING:
            return LOOPER_IDLE;
        default:
            return state;
    }
}

static uint32_t persistedLooperStateRaw(LooperState state) {
    return static_cast<uint32_t>(sanitizeLoadedLooperState(state));
}

static StorageIo storageIoFromFileWrite(File& file) {
    return StorageIo{
        [&file](const void* data, size_t size) { return writeRaw(file, data, size); },
        nullptr,
    };
}

static StorageIo storageIoFromFileRead(File& file) {
    return StorageIo{
        nullptr,
        [&file](void* data, size_t size) { return readRaw(file, data, size); },
    };
}

static bool writeLoopSnapshot(File& file, const LoopSnapshotRef& snapshot) {
    bool hasSnapshot = snapshot != nullptr;
    if (!writeRaw(file, &hasSnapshot, sizeof(hasSnapshot))) return false;
    if (!hasSnapshot) return true;
    const StorageIo io = storageIoFromFileWrite(file);
    return writePersistedLoopSnapshot(io, *snapshot);
}

static bool readLoopSnapshot(File& file, LoopSnapshotRef& snapshot) {
    bool hasSnapshot = false;
    if (!readRaw(file, &hasSnapshot, sizeof(hasSnapshot))) return false;
    if (!hasSnapshot) {
        snapshot.reset();
        return true;
    }
    auto loaded = std::make_shared<PersistedLoopSnapshot>();
    const StorageIo io = storageIoFromFileRead(file);
    if (!readPersistedLoopSnapshot(io, *loaded)) {
        return false;
    }
    snapshot = std::move(loaded);
    return true;
}

[[maybe_unused]] static bool writeGlobalUndoStack(File& file, const GlobalUndoStack& stack) {
    uint32_t entryCount = static_cast<uint32_t>(stack.entries.size());
    uint32_t cursor = static_cast<uint32_t>(stack.cursor);
    uint32_t nextEntryId = stack.nextEntryId;
    if (!writeRaw(file, &entryCount, sizeof(entryCount))) return false;
    if (!writeRaw(file, &cursor, sizeof(cursor))) return false;
    if (!writeRaw(file, &nextEntryId, sizeof(nextEntryId))) return false;

    for (const UndoEntry& entry : stack.entries) {
        uint8_t kind = static_cast<uint8_t>(entry.kind);
        if (!writeRaw(file, &entry.id, sizeof(entry.id))) return false;
        if (!writeRaw(file, &kind, sizeof(kind))) return false;
        if (!writeRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) return false;
        if (!writeRaw(file, &entry.loopId, sizeof(entry.loopId))) return false;
        if (!writeRaw(file, &entry.passId, sizeof(entry.passId))) return false;

        if (!writeLoopSnapshot(file, entry.beforeSnapshot)) return false;
        if (!writeLoopSnapshot(file, entry.afterSnapshot)) return false;

        if (!writeRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) return false;
        if (!writeRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) return false;
        if (!writeRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) return false;
        if (!writeRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) return false;
        if (!writeRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) return false;
        if (!writeRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) return false;

        uint32_t beforeTrackState = static_cast<uint32_t>(entry.beforeTrackState);
        uint32_t afterTrackState = static_cast<uint32_t>(entry.afterTrackState);
        if (!writeRaw(file, &beforeTrackState, sizeof(beforeTrackState))) return false;
        if (!writeRaw(file, &afterTrackState, sizeof(afterTrackState))) return false;
        if (!writeRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) return false;
        if (!writeRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload))) return false;
    }

    return true;
}

static bool readGlobalUndoStack(File& file, GlobalUndoStack& stack) {
    uint32_t entryCount = 0;
    uint32_t cursor = 0;
    uint32_t nextEntryId = 1;
    if (!readRaw(file, &entryCount, sizeof(entryCount))) return false;
    if (!readRaw(file, &cursor, sizeof(cursor))) return false;
    if (!readRaw(file, &nextEntryId, sizeof(nextEntryId))) return false;

    stack.clear();
    stack.nextEntryId = nextEntryId;
    stack.entries.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        UndoEntry entry;
        uint8_t kindRaw = 0;
        uint32_t beforeTrackStateRaw = 0;
        uint32_t afterTrackStateRaw = 0;
        if (!readRaw(file, &entry.id, sizeof(entry.id))) return false;
        if (!readRaw(file, &kindRaw, sizeof(kindRaw))) return false;
        entry.kind = static_cast<UndoEntryKind>(kindRaw);
        if (!readRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) return false;
        if (!readRaw(file, &entry.loopId, sizeof(entry.loopId))) return false;
        if (!readRaw(file, &entry.passId, sizeof(entry.passId))) return false;

        if (!readLoopSnapshot(file, entry.beforeSnapshot)) return false;
        if (!readLoopSnapshot(file, entry.afterSnapshot)) return false;

        if (!readRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) return false;
        if (!readRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) return false;
        if (!readRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) return false;
        if (!readRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) return false;
        if (!readRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) return false;
        if (!readRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) return false;
        if (!readRaw(file, &beforeTrackStateRaw, sizeof(beforeTrackStateRaw))) return false;
        if (!readRaw(file, &afterTrackStateRaw, sizeof(afterTrackStateRaw))) return false;
        if (!readRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) return false;
        if (!readRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload))) return false;

        entry.beforeTrackState = static_cast<TrackState>(beforeTrackStateRaw);
        entry.afterTrackState = static_cast<TrackState>(afterTrackStateRaw);
        stack.entries.push_back(std::move(entry));
    }

    stack.cursor = (cursor <= stack.entries.size()) ? cursor : stack.entries.size();
    if (stack.nextEntryId == 0) {
        stack.nextEntryId = 1;
    }
    return true;
}

namespace {
bool deferredSavePending = false;
bool urgentEditSavePending = false;
uint32_t lastEditAutosaveMs = 0;
bool clearEditDirtyAfterDeferredSave = false;
bool quarantineLegacyMonolithAfterSave = false;

enum class DeferredSaveStage : uint8_t {
    Idle = 0,
    CurrentSetMeta,
    TrackHeaderAndSlots,
    CurrentSetLoopSlot,
    Footer,
    UndoStacks,
    CurrentSetCompletion,
};

enum class DeferredGlobalHeaderStage : uint8_t {
    Version = 0,
    Bpm,
    LooperState,
    MasterLoopLength,
    TrackCount,
};

enum class DeferredTrackWriteStage : uint8_t {
    TrackState = 0,
    Muted,
};

enum class DeferredSlotWriteStage : uint8_t {
    SlotEnabled = 0,
    SlotMuted,
    SlotLoopId,
};

enum class DeferredFooterWriteStage : uint8_t {
    SelectedTrack = 0,
    ActiveLoopIndex,
    UndoMagic,
};

enum class DeferredLoopWriteStage : uint8_t {
    Header = 0,
    CapturePassHeader,
    CapturePassChunk,
    EditTail,
};

enum class DeferredUndoWriteStage : uint8_t {
    Header = 0,
    EntryHeader,
    BeforeSnapshotPresence,
    BeforeSnapshotLoop,
    AfterSnapshotPresence,
    AfterSnapshotLoop,
    EntryTail,
};

bool deferredSaveInProgress = false;
bool deferredSaveSdIoActive = false;
bool deferredSaveUrgentRequested = false;
DeferredSaveStage deferredSaveStage = DeferredSaveStage::Idle;
DeferredGlobalHeaderStage deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Version;
DeferredTrackWriteStage deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
DeferredSlotWriteStage deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
DeferredFooterWriteStage deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
DeferredLoopWriteStage deferredLoopWriteStage = DeferredLoopWriteStage::Header;
DeferredUndoWriteStage deferredUndoWriteStage = DeferredUndoWriteStage::Header;
LooperState deferredSaveStateSnapshot = LOOPER_IDLE;
File deferredSaveFile;
File deferredSaveLoopFile;
bool deferredSaveLoopFileOpen = false;
CurrentSetStorage::AnchorFields currentSetAnchorFields{};
uint8_t deferredSaveNumTracks = 0;
uint8_t deferredSaveTrackCursor = 0;
uint8_t deferredSaveSlotCursor = 0;
uint8_t deferredSavePoolCursor = 0;
uint8_t deferredSaveUndoTrackCursor = 0;
uint16_t deferredSaveCapturePassCursor = 0;
uint16_t deferredSaveChunkCursor = 0;
uint32_t deferredSaveUndoEntryCursor = 0;
bool deferredSaveTrackHeaderWritten = false;
uint8_t deferredSaveFooterTrackCursor = 0;
uint32_t deferredSaveStartedAtUs = 0;
uint32_t deferredSaveHeapBefore = 0;
uint32_t deferredSaveAdmissionHeap = 0;
bool deferredSaveHeapFloorDeferred = false;
bool deferredSaveLastCompletedOk = false;
uint32_t deferredSaveCompletedAtMs = 0;
uint32_t deferredSaveFailedAtMs = 0;
std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> deferredSaveMidiBatch;
uint16_t deferredSaveLoopSlotsWritten = 0;
uint16_t deferredSaveLoopSlotsSkipped = 0;
uint32_t deferredSaveDisplayBlockUs = 0;
bool forceCurrentSetFullLoopWrite = true;
std::array<std::array<bool, Config::MAX_LOOPS_PER_TRACK>, Config::NUM_TRACKS>
    currentSetLoopSlotDirty{};
uint32_t lastSavedSetFailsafeCheckAtMs = 0;

constexpr size_t kSavedSetPathCapacity = 64;
constexpr uint32_t kSavedSetFailsafeCheckIntervalMs = 1000;

void markCurrentSetMaterialChange() {
    currentSetAnchorFields.hasMaterialChangesSinceAnchor = 1;
    const uint32_t nowUnix = RtcTime::getUnixTime();
    if (nowUnix != 0) {
        currentSetAnchorFields.lastMaterialChangeUnix = nowUnix;
    }
}

void markCurrentSetLoopSlotDirtyInternal(uint8_t trackIndex, uint8_t slotIndex,
                                         bool markMaterialChange = true) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    currentSetLoopSlotDirty[trackIndex][slotIndex] = true;
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void markCurrentSetTrackDirtyInternal(uint8_t trackIndex,
                                      bool markMaterialChange = true) {
    if (trackIndex >= Config::NUM_TRACKS) {
        return;
    }
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        currentSetLoopSlotDirty[trackIndex][slot] = true;
    }
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void markAllCurrentSetLoopSlotsDirtyInternal(bool markMaterialChange = true) {
    for (uint8_t track = 0; track < Config::NUM_TRACKS; ++track) {
        markCurrentSetTrackDirtyInternal(track, false);
    }
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void clearCurrentSetLoopSlotDirtyInternal(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    currentSetLoopSlotDirty[trackIndex][slotIndex] = false;
}

void syncCurrentSetDirtyTrackingFromLoadedState() {
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            markCurrentSetTrackDirtyInternal(t, false);
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            currentSetLoopSlotDirty[t][s] = false;
        }
    }
}

bool anyAllocatedLoopEditStateDirty() {
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (track.getLoop(s).isEditStateDirty()) {
                return true;
            }
        }
    }
    return false;
}

void clearAllocatedLoopEditStateDirty() {
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            track.getLoop(s).clearEditStateDirty();
        }
    }
}

static void quarantineStorageFile() {
#if defined(ARDUINO)
    if (!SD.exists(STORAGE_FILENAME)) {
        return;
    }
    char quarantineName[48];
    snprintf(quarantineName, sizeof(quarantineName), "/state.bad.%lu",
             static_cast<unsigned long>(millis()));
    if (SD.rename(STORAGE_FILENAME, quarantineName)) {
        Serial.print("[StorageManager] Quarantined storage file as ");
        Serial.println(quarantineName);
    }
#endif
}

static void stabilizeBootMemoryAfterLoad() {
    uint32_t freeHeap = MemoryMonitor::getInternalHeapFreeBytes();
    if (freeHeap < Config::HEAP_RESERVE_BYTES) {
        Serial.print("[StorageManager] Boot heap below reserve after load (");
        Serial.print(freeHeap);
        Serial.println(" B); clearing undo stacks");
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            trackManager.getTrack(t).getGlobalUndoStack().clear();
        }
        freeHeap = MemoryMonitor::getInternalHeapFreeBytes();
    }

    if (freeHeap < Config::HEAP_RESERVE_BYTES) {
        Serial.print("[StorageManager] WARN: boot heap still low (");
        Serial.print(freeHeap);
        Serial.println(" B); skipping playback prewarm");
        return;
    }

    trackManager.prewarmPlaybackRuntime();
}

void resetTracksAfterFailedLoad() {
    trackManager.setMasterLoopLength(0);
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        track.ensureLoopsAllocated();
        track.forceSetState(TRACK_EMPTY);
        track.getGlobalUndoStack().clear();
        track.setActiveLoopIndex(0);
        trackManager.setSelectedSlotIndex(t, 0);
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            trackManager.setSlotEnabled(t, s, false);
            trackManager.setSlotMuted(t, s, false);
            Loop& loop = track.getLoop(s);
            loop.discardPendingCapturePass();
            loop.discardCapture();
            loop.resetPassTimeline();
            loop.loopId = static_cast<LoopId>(s);
            loop.startLoopTick = 0;
            loop.loopLengthTicks = 0;
            loop.loopStartTick = 0;
            loop.nextPassId_ = 1;
            loop.nextMergeSequence_ = 0;
            loop.lastPublishedPassId_ = kInvalidPassId;
            loop.lastTickInLoop = 0;
            loop.nextEventIndex = 0;
            loop.clearEditStateDirty();
            loop.visualCache.clear();
            loop.capturePreview.clear();
            loop.pendingVisualDelta.clear();
            loop.invalidateCaches();
        }
    }
    trackManager.setSelectedTrack(0);
}

bool writeSetIndexToSd(const SavedSetCatalog::SetIndex& index) {
    File file = SD.open(CurrentSetStorage::kSetIndexTempPath, FILE_WRITE);
    if (!file) {
        return false;
    }
    const StorageIo io = storageIoFromFileWrite(file);
    if (!SavedSetCatalog::writeSetIndex(io, index) ||
        !writeRaw(file, &STORAGE_COMPLETE_MAGIC, sizeof(STORAGE_COMPLETE_MAGIC))) {
        file.close();
        return false;
    }
    file.close();
    if (!CurrentSetStorage::verifyFileCompleteMagic(CurrentSetStorage::kSetIndexTempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(CurrentSetStorage::kSetIndexTempPath,
                                                   CurrentSetStorage::kSetIndexPath);
}

bool readSetIndexFromSd(SavedSetCatalog::SetIndex& index) {
    if (!SD.exists(CurrentSetStorage::kSetIndexPath)) {
        index.nextSequence = 1;
        return true;
    }

    File file = SD.open(CurrentSetStorage::kSetIndexPath, FILE_READ);
    if (!file) {
        return false;
    }
    const StorageIo io = storageIoFromFileRead(file);
    if (!SavedSetCatalog::readSetIndex(io, index)) {
        file.close();
        return false;
    }
    uint32_t completeMagic = 0;
    const bool ok = readRaw(file, &completeMagic, sizeof(completeMagic)) &&
                    completeMagic == STORAGE_COMPLETE_MAGIC;
    file.close();
    return ok;
}

bool parseSavedSetSequence(const char* folderName, uint32_t& sequence) {
    return SavedSetCatalog::parseSavedSetFolderName(folderName, sequence, nullptr);
}

bool formatSavedSetDirectoryPath(const char* folderName, char* out, size_t outSize) {
    if (folderName == nullptr || folderName[0] == '\0' || out == nullptr || outSize == 0) {
        return false;
    }
    const int written = std::snprintf(out, outSize, "%s/%s", CurrentSetStorage::kSetsRoot,
                                      folderName);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

uint32_t scanHighestSavedSetSequenceOnSd() {
    if (!SD.exists(CurrentSetStorage::kSetsRoot)) {
        return 0;
    }
    File dir = SD.open(CurrentSetStorage::kSetsRoot);
    if (!dir) {
        return 0;
    }
    uint32_t highest = 0;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        uint32_t sequence = 0;
        if (isDirectory && parseSavedSetSequence(name, sequence) && sequence > highest) {
            highest = sequence;
        }
        entry.close();
    }
    dir.close();
    return highest;
}

bool reconcileSetIndexOnSd(SavedSetCatalog::SetIndex& index) {
    if (!readSetIndexFromSd(index)) {
        return false;
    }
    const uint32_t reconciled =
        SavedSetCatalog::reconcileNextSequence(index.nextSequence,
                                               scanHighestSavedSetSequenceOnSd());
    if (reconciled != index.nextSequence || !SD.exists(CurrentSetStorage::kSetIndexPath)) {
        index.nextSequence = reconciled;
        if (!writeSetIndexToSd(index)) {
            return false;
        }
    }
    return true;
}

bool copyFileBinary(const char* sourcePath, const char* destinationPath) {
    File source = SD.open(sourcePath, FILE_READ);
    if (!source) {
        return false;
    }
    File destination = SD.open(destinationPath, FILE_WRITE);
    if (!destination) {
        source.close();
        return false;
    }

    uint8_t buffer[512];
    bool ok = true;
    while (source.available() > 0) {
        const int bytesRead = source.read(buffer, sizeof(buffer));
        if (bytesRead <= 0) {
            ok = false;
            break;
        }
        if (destination.write(buffer, static_cast<size_t>(bytesRead)) !=
            static_cast<size_t>(bytesRead)) {
            ok = false;
            break;
        }
    }
    source.close();
    destination.close();
    return ok;
}

bool buildSavedSetMetadata(uint32_t sequence, SavedSetCatalog::FolderNamingMode namingMode,
                           uint32_t createdAtUnix,
                           SavedSetCatalog::SavedSetMetadata& metadata) {
    metadata = {};
    metadata.sequence = sequence;
    metadata.folderNamingMode = namingMode;
    metadata.createdAtUnix = createdAtUnix;
    metadata.masterLoopBars = static_cast<uint16_t>(
        trackManager.getMasterLoopLength() / Config::TICKS_PER_BAR);

    uint16_t filledTotal = 0;
    uint8_t filledTracks = 0;
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        Track& track = trackManager.getTrack(trackIndex);
        uint8_t filledSlots = 0;
        if (track.loopsAllocated()) {
            for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
                if (track.hasDataInSlot(slotIndex)) {
                    ++filledSlots;
                }
            }
        }
        metadata.perTrackFilledSlots[trackIndex] = filledSlots;
        if (filledSlots > 0) {
            ++filledTracks;
            filledTotal += filledSlots;
        }
    }
    metadata.trackCount = filledTracks;
    metadata.filledSlotCount =
        static_cast<uint8_t>(filledTotal > 255 ? 255 : filledTotal);
    metadata.userLabel[0] = '\0';
    return true;
}

bool copyCurrentSetMetaToSavedSet(const char* savedSetDir,
                                  const SavedSetCatalog::SavedSetMetadata& metadata) {
    if (!CurrentSetStorage::verifyFileCompleteMagic(CurrentSetStorage::kCurrentMetaPath)) {
        return false;
    }
    File source = SD.open(CurrentSetStorage::kCurrentMetaPath, FILE_READ);
    if (!source) {
        return false;
    }
    const size_t sourceSize = source.size();
    if (sourceSize < sizeof(STORAGE_COMPLETE_MAGIC)) {
        source.close();
        return false;
    }
    const size_t payloadSize = sourceSize - sizeof(STORAGE_COMPLETE_MAGIC);

    char destinationTempPath[kSavedSetPathCapacity];
    char destinationPath[kSavedSetPathCapacity];
    if (std::snprintf(destinationTempPath, sizeof(destinationTempPath), "%s/meta.bin.tmp",
                      savedSetDir) <= 0 ||
        std::snprintf(destinationPath, sizeof(destinationPath), "%s/meta.bin", savedSetDir) <= 0) {
        source.close();
        return false;
    }

    File destination = SD.open(destinationTempPath, FILE_WRITE);
    if (!destination) {
        source.close();
        return false;
    }

    uint8_t buffer[512];
    size_t remaining = payloadSize;
    bool ok = true;
    while (remaining > 0) {
        const size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        const int bytesRead = source.read(buffer, chunk);
        if (bytesRead != static_cast<int>(chunk) ||
            destination.write(buffer, chunk) != chunk) {
            ok = false;
            break;
        }
        remaining -= chunk;
    }

    if (ok) {
        const StorageIo destinationIo = storageIoFromFileWrite(destination);
        ok = SavedSetCatalog::writeSavedSetMetadataTrailer(destinationIo, metadata) &&
             writeRaw(destination, &STORAGE_COMPLETE_MAGIC, sizeof(STORAGE_COMPLETE_MAGIC));
    }
    source.close();
    destination.close();
    if (!ok) {
        return false;
    }
    if (!CurrentSetStorage::verifyFileCompleteMagic(destinationTempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(destinationTempPath, destinationPath);
}

bool copyCurrentSetLoopsToSavedSet(const char* savedSetDir) {
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            char sourcePath[64];
            char destinationPath[kSavedSetPathCapacity];
            if (!CurrentSetStorage::formatLoopSlotPath(sourcePath, sizeof(sourcePath), trackIndex,
                                                       slotIndex) ||
                std::snprintf(destinationPath, sizeof(destinationPath), "%s/loop_%02u_%02u.bin",
                              savedSetDir, static_cast<unsigned>(trackIndex),
                              static_cast<unsigned>(slotIndex)) <= 0) {
                return false;
            }
            if (!copyFileBinary(sourcePath, destinationPath)) {
                return false;
            }
        }
    }
    return true;
}

bool patchCurrentSetAnchor() {
    return CurrentSetStorage::patchAnchorFields(CurrentSetStorage::kCurrentMetaPath,
                                                currentSetAnchorFields);
}

bool saveNewSetInternal(const LooperState& state, char* savedSetFolderOut, size_t outSize) {
    if (!CurrentSetStorage::ensureDirectory(CurrentSetStorage::kSetsRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir)) {
        return false;
    }
    if (!SD.exists(CurrentSetStorage::kCurrentMetaPath)) {
        return false;
    }
    if (StorageManager::hasDeferredSaveWork() && !StorageManager::saveState(state)) {
        return false;
    }

    SavedSetCatalog::SetIndex index{};
    if (!reconcileSetIndexOnSd(index)) {
        return false;
    }

    const uint32_t createdAtUnix = RtcTime::getUnixTime();
    const bool hasValidDateFolder = RtcTime::hasValidDateForFolderNaming();
    const uint32_t sequence = SavedSetCatalog::allocateNextSequence(index);
    SavedSetCatalog::FolderNamingMode namingMode = SavedSetCatalog::FolderNamingMode::Unknown;
    char folderName[16];
    if (!SavedSetCatalog::formatSavedSetFolderName(sequence, createdAtUnix, hasValidDateFolder,
                                                   folderName, sizeof(folderName), &namingMode)) {
        return false;
    }
    char savedSetDir[kSavedSetPathCapacity];
    if (!formatSavedSetDirectoryPath(folderName, savedSetDir, sizeof(savedSetDir))) {
        return false;
    }
    if (SD.exists(savedSetDir) || !CurrentSetStorage::ensureDirectory(savedSetDir)) {
        return false;
    }

    SavedSetCatalog::SavedSetMetadata metadata{};
    if (!buildSavedSetMetadata(sequence, namingMode, createdAtUnix, metadata) ||
        !copyCurrentSetMetaToSavedSet(savedSetDir, metadata) ||
        !copyCurrentSetLoopsToSavedSet(savedSetDir) ||
        !writeSetIndexToSd(index)) {
        return false;
    }

    currentSetAnchorFields.lastAnchoredSequence = sequence;
    currentSetAnchorFields.hasMaterialChangesSinceAnchor = 0;
    if (!patchCurrentSetAnchor()) {
        return false;
    }

    if (savedSetFolderOut != nullptr && outSize > 0) {
        std::snprintf(savedSetFolderOut, outSize, "%s", folderName);
    }
    return true;
}

bool copySavedSetIntoCurrent(const char* sourceSetDir) {
    if (!CurrentSetStorage::ensureDirectory(CurrentSetStorage::kSetsRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir)) {
        return false;
    }

    char sourceMetaPath[kSavedSetPathCapacity];
    if (std::snprintf(sourceMetaPath, sizeof(sourceMetaPath), "%s/meta.bin", sourceSetDir) <= 0) {
        return false;
    }
    if (!copyFileBinary(sourceMetaPath, CurrentSetStorage::kCurrentMetaTempPath)) {
        return false;
    }
    if (!CurrentSetStorage::verifyFileCompleteMagic(CurrentSetStorage::kCurrentMetaTempPath) ||
        !CurrentSetStorage::atomicRenameTempFile(CurrentSetStorage::kCurrentMetaTempPath,
                                                 CurrentSetStorage::kCurrentMetaPath)) {
        return false;
    }

    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            char sourceLoopPath[kSavedSetPathCapacity];
            char destinationLoopPath[64];
            char destinationTempPath[68];
            if (std::snprintf(sourceLoopPath, sizeof(sourceLoopPath), "%s/loop_%02u_%02u.bin",
                              sourceSetDir, static_cast<unsigned>(trackIndex),
                              static_cast<unsigned>(slotIndex)) <= 0 ||
                !CurrentSetStorage::formatLoopSlotPath(destinationLoopPath,
                                                       sizeof(destinationLoopPath), trackIndex,
                                                       slotIndex) ||
                !CurrentSetStorage::formatLoopSlotTempPath(destinationTempPath,
                                                           sizeof(destinationTempPath), trackIndex,
                                                           slotIndex)) {
                return false;
            }
            if (!copyFileBinary(sourceLoopPath, destinationTempPath) ||
                !CurrentSetStorage::verifyFileCompleteMagic(destinationTempPath) ||
                !CurrentSetStorage::atomicRenameTempFile(destinationTempPath,
                                                         destinationLoopPath)) {
                return false;
            }
        }
    }
    return true;
}

const char* deferredSaveStageName(DeferredSaveStage stage) {
    switch (stage) {
        case DeferredSaveStage::Idle: return "idle";
        case DeferredSaveStage::CurrentSetMeta: return "current_set_meta";
        case DeferredSaveStage::TrackHeaderAndSlots: return "track_header_slots";
        case DeferredSaveStage::CurrentSetLoopSlot: return "current_set_loop_slot";
        case DeferredSaveStage::Footer: return "footer";
        case DeferredSaveStage::UndoStacks: return "undo_stacks";
        case DeferredSaveStage::CurrentSetCompletion: return "current_set_completion";
    }
    return "unknown";
}

const char* deferredGlobalHeaderStageName(DeferredGlobalHeaderStage stage) {
    switch (stage) {
        case DeferredGlobalHeaderStage::Version: return "version";
        case DeferredGlobalHeaderStage::Bpm: return "bpm";
        case DeferredGlobalHeaderStage::LooperState: return "looper_state";
        case DeferredGlobalHeaderStage::MasterLoopLength: return "master_loop_length";
        case DeferredGlobalHeaderStage::TrackCount: return "track_count";
    }
    return "unknown";
}

const char* deferredTrackWriteStageName(DeferredTrackWriteStage stage) {
    switch (stage) {
        case DeferredTrackWriteStage::TrackState: return "track_state";
        case DeferredTrackWriteStage::Muted: return "muted";
    }
    return "unknown";
}

const char* deferredSlotWriteStageName(DeferredSlotWriteStage stage) {
    switch (stage) {
        case DeferredSlotWriteStage::SlotEnabled: return "slot_enabled";
        case DeferredSlotWriteStage::SlotMuted: return "slot_muted";
        case DeferredSlotWriteStage::SlotLoopId: return "slot_loop_id";
    }
    return "unknown";
}

const char* deferredFooterWriteStageName(DeferredFooterWriteStage stage) {
    switch (stage) {
        case DeferredFooterWriteStage::SelectedTrack: return "selected_track";
        case DeferredFooterWriteStage::ActiveLoopIndex: return "active_loop_index";
        case DeferredFooterWriteStage::UndoMagic: return "undo_magic";
    }
    return "unknown";
}

const char* deferredLoopWriteStageName(DeferredLoopWriteStage stage) {
    switch (stage) {
        case DeferredLoopWriteStage::Header: return "loop_header";
        case DeferredLoopWriteStage::CapturePassHeader: return "capture_pass_header";
        case DeferredLoopWriteStage::CapturePassChunk: return "capture_pass_chunk";
        case DeferredLoopWriteStage::EditTail: return "edit_tail";
    }
    return "unknown";
}

const char* deferredUndoWriteStageName(DeferredUndoWriteStage stage) {
    switch (stage) {
        case DeferredUndoWriteStage::Header: return "undo_header";
        case DeferredUndoWriteStage::EntryHeader: return "entry_header";
        case DeferredUndoWriteStage::BeforeSnapshotPresence: return "before_snapshot_presence";
        case DeferredUndoWriteStage::BeforeSnapshotLoop: return "before_snapshot_loop";
        case DeferredUndoWriteStage::AfterSnapshotPresence: return "after_snapshot_presence";
        case DeferredUndoWriteStage::AfterSnapshotLoop: return "after_snapshot_loop";
        case DeferredUndoWriteStage::EntryTail: return "entry_tail";
    }
    return "unknown";
}

void emitDeferredSaveSliceTelemetry(const char* phase) {
    char outcome[96];
    if (deferredSaveStage == DeferredSaveStage::CurrentSetLoopSlot) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:p%u:c%u:k%u", phase,
                 deferredLoopWriteStageName(deferredLoopWriteStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSavePoolCursor),
                 static_cast<unsigned>(deferredSaveCapturePassCursor),
                 static_cast<unsigned>(deferredSaveChunkCursor));
    } else if (deferredSaveStage == DeferredSaveStage::UndoStacks) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:e%lu:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveUndoTrackCursor),
                 static_cast<unsigned long>(deferredSaveUndoEntryCursor),
                 deferredUndoWriteStageName(deferredUndoWriteStage));
    } else if (deferredSaveStage == DeferredSaveStage::CurrentSetMeta) {
        snprintf(outcome, sizeof(outcome), "%s:%s:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 deferredGlobalHeaderStageName(deferredGlobalHeaderStage));
    } else if (deferredSaveStage == DeferredSaveStage::TrackHeaderAndSlots) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:s%u:%s:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSaveSlotCursor),
                 deferredSaveTrackHeaderWritten ? "slot" : "track",
                 deferredSaveTrackHeaderWritten
                     ? deferredSlotWriteStageName(deferredSlotWriteStage)
                     : deferredTrackWriteStageName(deferredTrackWriteStage));
    } else if (deferredSaveStage == DeferredSaveStage::Footer) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveFooterTrackCursor),
                 deferredFooterWriteStageName(deferredFooterWriteStage));
    } else {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:s%u:p%u", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSaveSlotCursor),
                 static_cast<unsigned>(deferredSavePoolCursor));
    }
    SC_PERSIST("slice", micros() - deferredSaveStartedAtUs, deferredSaveHeapBefore,
               deferredSaveHeapBefore, outcome);
}

void resetDeferredLoopWriteState() {
    deferredLoopWriteStage = DeferredLoopWriteStage::Header;
    deferredSaveCapturePassCursor = 0;
    deferredSaveChunkCursor = 0;
    deferredSaveMidiBatch.clear();
}

void resetDeferredUndoWriteState() {
    deferredUndoWriteStage = DeferredUndoWriteStage::Header;
    deferredSaveUndoEntryCursor = 0;
    resetDeferredLoopWriteState();
}

void resetDeferredSaveJobState() {
    if (deferredSaveFile) {
        deferredSaveFile.close();
    }
    if (deferredSaveLoopFile) {
        deferredSaveLoopFile.close();
    }
    deferredSaveLoopFileOpen = false;
    deferredSaveInProgress = false;
    deferredSaveSdIoActive = false;
    deferredSaveStage = DeferredSaveStage::Idle;
    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Bpm;
    deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
    deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
    deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
    deferredSaveNumTracks = 0;
    deferredSaveTrackCursor = 0;
    deferredSaveSlotCursor = 0;
    deferredSavePoolCursor = 0;
    deferredSaveUndoTrackCursor = 0;
    deferredSaveFooterTrackCursor = 0;
    resetDeferredLoopWriteState();
    resetDeferredUndoWriteState();
    deferredSaveTrackHeaderWritten = false;
    deferredSaveStartedAtUs = 0;
    deferredSaveHeapBefore = 0;
    deferredSaveAdmissionHeap = 0;
    deferredSaveHeapFloorDeferred = false;
    deferredSaveUrgentRequested = false;
    deferredSaveLoopSlotsWritten = 0;
    deferredSaveLoopSlotsSkipped = 0;
    deferredSaveDisplayBlockUs = 0;
    deferredSaveStateSnapshot = LOOPER_IDLE;
}

bool writeCurrentSetMetaHeaderToOpenFile(File& file) {
    CurrentSetStorage::MetaHeader header{};
    header.containerVersion = CurrentSetStorage::CONTAINER_VERSION;
    header.lastActiveUnix = 0;
    header.anchor = currentSetAnchorFields;
    const StorageIo io = storageIoFromFileWrite(file);
    return CurrentSetStorage::writeMetaHeader(io, header);
}

bool finalizeDeferredMetaTempFile() {
    if (!writeRaw(deferredSaveFile, &STORAGE_COMPLETE_MAGIC, sizeof(STORAGE_COMPLETE_MAGIC))) {
        return false;
    }
    deferredSaveFile.close();
    if (!CurrentSetStorage::verifyFileCompleteMagic(CurrentSetStorage::kCurrentMetaTempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(CurrentSetStorage::kCurrentMetaTempPath,
                                                   CurrentSetStorage::kCurrentMetaPath);
}

bool closeDeferredMetaTempForLoopWrites() {
    if (deferredSaveFile) {
        deferredSaveFile.close();
    }
    return true;
}

bool reopenDeferredMetaTempForAppend() {
    deferredSaveFile = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
    if (!deferredSaveFile) {
        Serial.println("[StorageManager] ERROR: Could not reopen CurrentSet meta temp for append");
        return false;
    }
    if (!deferredSaveFile.seek(deferredSaveFile.size())) {
        deferredSaveFile.close();
        return false;
    }
    return true;
}

bool openDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (deferredSaveLoopFileOpen) {
        deferredSaveLoopFile.close();
        deferredSaveLoopFileOpen = false;
    }
    char tempPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex)) {
        return false;
    }
    deferredSaveLoopFile = SD.open(tempPath, FILE_WRITE);
    if (!deferredSaveLoopFile) {
        Serial.print("[StorageManager] ERROR: Could not open loop temp file: ");
        Serial.println(tempPath);
        return false;
    }
    deferredSaveLoopFile.seek(0);
    deferredSaveLoopFileOpen = true;
    return true;
}

bool finalizeDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (!deferredSaveLoopFileOpen) {
        return false;
    }
    if (!writeRaw(deferredSaveLoopFile, &STORAGE_COMPLETE_MAGIC, sizeof(STORAGE_COMPLETE_MAGIC))) {
        deferredSaveLoopFile.close();
        deferredSaveLoopFileOpen = false;
        return false;
    }
    deferredSaveLoopFile.close();
    deferredSaveLoopFileOpen = false;

    char tempPath[48];
    char finalPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex) ||
        !CurrentSetStorage::formatLoopSlotPath(finalPath, sizeof(finalPath), trackIndex,
                                               slotIndex)) {
        return false;
    }
    if (!CurrentSetStorage::verifyFileCompleteMagic(tempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(tempPath, finalPath);
}

bool shouldWriteCurrentSetLoopSlot(uint8_t trackIndex, uint8_t slotIndex) {
    if (forceCurrentSetFullLoopWrite) {
        return true;
    }
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return true;
    }
    return currentSetLoopSlotDirty[trackIndex][slotIndex];
}

bool trackHasCurrentSetDirtyLoopSlot(uint8_t trackIndex) {
    if (forceCurrentSetFullLoopWrite) {
        return true;
    }
    if (trackIndex >= Config::NUM_TRACKS) {
        return false;
    }
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        if (currentSetLoopSlotDirty[trackIndex][slot]) {
            return true;
        }
    }
    return false;
}

bool beginDeferredSaveJob(const LooperState& state) {
    if (!CurrentSetStorage::ensureDirectory(CurrentSetStorage::kSetsRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir)) {
        Serial.println("[StorageManager] ERROR: Could not create Sets/_current directory");
        return false;
    }

    deferredSaveFile = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
    if (!deferredSaveFile) {
        Serial.println("[StorageManager] ERROR: Could not open CurrentSet meta temp file");
        return false;
    }
    deferredSaveFile.seek(0);

    if (!writeCurrentSetMetaHeaderToOpenFile(deferredSaveFile)) {
        Serial.println("[StorageManager] ERROR: Deferred save failed writing CurrentSet meta header");
        deferredSaveFile.close();
        return false;
    }

    deferredSaveStateSnapshot = state;
    deferredSaveNumTracks = Config::NUM_TRACKS;
    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Bpm;
    deferredSaveTrackCursor = 0;
    deferredSaveSlotCursor = 0;
    deferredSavePoolCursor = 0;
    deferredSaveUndoTrackCursor = 0;
    deferredSaveFooterTrackCursor = 0;
    deferredSaveTrackHeaderWritten = false;
    deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
    deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
    deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
    resetDeferredLoopWriteState();
    resetDeferredUndoWriteState();
    deferredSaveStage = DeferredSaveStage::CurrentSetMeta;
    return true;
}

bool selectDeferredCapturePass(const LoopPasses& passes, uint16_t cursor,
                               PersistedCapturePassWire& wire,
                               const ChunkIdList*& chunkRefs) {
    uint16_t overdubIndex = cursor;
    if (passes.hasRecordPass()) {
        if (cursor == 0) {
            wire.id = passes.recordPass.id;
            wire.mergeSequence = 0;
            wire.stateRaw = static_cast<uint8_t>(passes.recordPass.state);
            wire.typeRaw = 0;
            wire.sealedAtTick = passes.recordPass.sealedAtTick;
            chunkRefs = &passes.recordPass.chunkRefs;
            return true;
        }
        overdubIndex = cursor - 1;
    }

    if (overdubIndex >= passes.overdubPasses.size()) {
        chunkRefs = nullptr;
        return false;
    }

    const OverdubPass& pass = passes.overdubPasses[overdubIndex];
    wire.id = pass.id;
    wire.mergeSequence = pass.mergeSequence;
    wire.stateRaw = static_cast<uint8_t>(pass.state);
    wire.typeRaw = 1;
    wire.sealedAtTick = pass.sealedAtTick;
    chunkRefs = &pass.chunkRefs;
    return true;
}

bool writeDeferredLoopHeader(File& file, LoopId loopId, uint32_t startLoopTick,
                             uint32_t loopLengthTicks, uint32_t loopStartTick,
                             PassId nextPassId, uint32_t nextMergeSequence,
                             PassId lastPublishedPassId, const LoopPasses& passes) {
    if (!writeRaw(file, &loopId, sizeof(loopId))) return false;
    if (!writeRaw(file, &startLoopTick, sizeof(startLoopTick))) return false;
    if (!writeRaw(file, &loopLengthTicks, sizeof(loopLengthTicks))) return false;
    if (!writeRaw(file, &loopStartTick, sizeof(loopStartTick))) return false;
    if (!writeRaw(file, &nextPassId, sizeof(nextPassId))) return false;
    if (!writeRaw(file, &nextMergeSequence, sizeof(nextMergeSequence))) return false;
    if (!writeRaw(file, &lastPublishedPassId, sizeof(lastPublishedPassId))) {
        return false;
    }

    const uint32_t persistedCount = static_cast<uint32_t>(passes.capturePassCount());
    return writeRaw(file, &persistedCount, sizeof(persistedCount));
}

bool writeDeferredCapturePassHeader(File& file, const PersistedCapturePassWire& wire,
                                    const ChunkIdList& chunkRefs) {
    if (!writeRaw(file, &wire.id, sizeof(wire.id))) return false;
    if (!writeRaw(file, &wire.mergeSequence, sizeof(wire.mergeSequence))) return false;
    if (!writeRaw(file, &wire.stateRaw, sizeof(wire.stateRaw))) return false;
    if (!writeRaw(file, &wire.typeRaw, sizeof(wire.typeRaw))) return false;
    if (!writeRaw(file, &wire.sealedAtTick, sizeof(wire.sealedAtTick))) return false;

    const uint32_t midiCount =
        static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(chunkRefs));
    return writeRaw(file, &midiCount, sizeof(midiCount));
}

bool writeDeferredCapturePassChunk(File& file, uint16_t chunkId) {
    deferredSaveMidiBatch.clear();
    LoopEventStore::appendChunkRefEvent(chunkId, deferredSaveMidiBatch);
    if (deferredSaveMidiBatch.empty()) {
        return true;
    }
    return writeRaw(file, deferredSaveMidiBatch.data(),
                    deferredSaveMidiBatch.size() * sizeof(MidiEvent));
}

bool stepDeferredLoopPersist(File& file, const Loop& loop, bool& loopDone) {
    loopDone = false;

    switch (deferredLoopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loop.loopId, loop.startLoopTick, loop.loopLengthTicks,
                                         loop.loopStartTick, loop.nextPassId_,
                                         loop.nextMergeSequence_, loop.lastPublishedPassId_,
                                         loop.passes)) {
                return false;
            }
            deferredSaveCapturePassCursor = 0;
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;

        case DeferredLoopWriteStage::CapturePassHeader: {
            if (deferredSaveCapturePassCursor >= loop.passes.capturePassCount()) {
                deferredLoopWriteStage = DeferredLoopWriteStage::EditTail;
                return true;
            }

            PersistedCapturePassWire wire{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, deferredSaveCapturePassCursor, wire, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, wire, *chunkRefs)) {
                return false;
            }
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            PersistedCapturePassWire wire{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, deferredSaveCapturePassCursor, wire, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)wire;

            if (deferredSaveChunkCursor < chunkRefs->size()) {
                const uint16_t chunkId = (*chunkRefs)[deferredSaveChunkCursor++];
                return writeDeferredCapturePassChunk(file, chunkId);
            }

            ++deferredSaveCapturePassCursor;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;
        }

        case DeferredLoopWriteStage::EditTail: {
            const StorageIo io = storageIoFromFileWrite(file);
            if (!writePersistedEditsTail(io, loop.nextPassId_, loop.passes.editPasses)) {
                return false;
            }
            resetDeferredLoopWriteState();
            loopDone = true;
            return true;
        }
    }

    return false;
}

bool stepDeferredEmptyLoopPersist(File& file, LoopId loopId, bool& loopDone) {
    loopDone = false;
    const LoopPasses emptyPasses{};

    switch (deferredLoopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loopId, 0, 0, 0, 1, 0, kInvalidPassId,
                                         emptyPasses)) {
                return false;
            }
            deferredLoopWriteStage = DeferredLoopWriteStage::EditTail;
            return true;

        case DeferredLoopWriteStage::CapturePassHeader:
        case DeferredLoopWriteStage::CapturePassChunk:
            return false;

        case DeferredLoopWriteStage::EditTail: {
            const StorageIo io = storageIoFromFileWrite(file);
            if (!writePersistedEditsTail(io, 1, emptyPasses.editPasses)) {
                return false;
            }
            resetDeferredLoopWriteState();
            loopDone = true;
            return true;
        }
    }

    return false;
}

bool stepDeferredLoopSnapshotPersist(File& file, const PersistedLoopSnapshot& snapshot,
                                     bool& loopDone) {
    loopDone = false;

    switch (deferredLoopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, snapshot.loopId, snapshot.startLoopTick,
                                         snapshot.loopLengthTicks, snapshot.loopStartTick,
                                         snapshot.nextPassId, snapshot.nextMergeSequence,
                                         snapshot.lastPublishedPassId, snapshot.passes)) {
                return false;
            }
            deferredSaveCapturePassCursor = 0;
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;

        case DeferredLoopWriteStage::CapturePassHeader: {
            if (deferredSaveCapturePassCursor >= snapshot.passes.capturePassCount()) {
                deferredLoopWriteStage = DeferredLoopWriteStage::EditTail;
                return true;
            }

            PersistedCapturePassWire wire{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, deferredSaveCapturePassCursor, wire,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, wire, *chunkRefs)) {
                return false;
            }
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            PersistedCapturePassWire wire{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, deferredSaveCapturePassCursor, wire,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)wire;

            if (deferredSaveChunkCursor < chunkRefs->size()) {
                const uint16_t chunkId = (*chunkRefs)[deferredSaveChunkCursor++];
                return writeDeferredCapturePassChunk(file, chunkId);
            }

            ++deferredSaveCapturePassCursor;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;
        }

        case DeferredLoopWriteStage::EditTail: {
            const StorageIo io = storageIoFromFileWrite(file);
            if (!writePersistedEditsTail(io, snapshot.nextPassId, snapshot.passes.editPasses)) {
                return false;
            }
            resetDeferredLoopWriteState();
            loopDone = true;
            return true;
        }
    }

    return false;
}

bool writeDeferredUndoEntryHeader(File& file, const UndoEntry& entry) {
    const uint8_t kind = static_cast<uint8_t>(entry.kind);
    if (!writeRaw(file, &entry.id, sizeof(entry.id))) return false;
    if (!writeRaw(file, &kind, sizeof(kind))) return false;
    if (!writeRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) return false;
    if (!writeRaw(file, &entry.loopId, sizeof(entry.loopId))) return false;
    return writeRaw(file, &entry.passId, sizeof(entry.passId));
}

bool writeDeferredUndoEntryTail(File& file, const UndoEntry& entry) {
    if (!writeRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) return false;
    if (!writeRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) return false;
    if (!writeRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) return false;
    if (!writeRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) return false;
    if (!writeRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) return false;
    if (!writeRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) return false;

    const uint32_t beforeTrackState = static_cast<uint32_t>(entry.beforeTrackState);
    const uint32_t afterTrackState = static_cast<uint32_t>(entry.afterTrackState);
    if (!writeRaw(file, &beforeTrackState, sizeof(beforeTrackState))) return false;
    if (!writeRaw(file, &afterTrackState, sizeof(afterTrackState))) return false;
    if (!writeRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) return false;
    return writeRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload));
}

bool writeDeferredSnapshotPresence(File& file, const LoopSnapshotRef& snapshot) {
    const bool hasSnapshot = snapshot != nullptr;
    return writeRaw(file, &hasSnapshot, sizeof(hasSnapshot));
}

bool stepDeferredUndoStackPersist(File& file, const GlobalUndoStack& stack, bool& stackDone) {
    stackDone = false;

    switch (deferredUndoWriteStage) {
        case DeferredUndoWriteStage::Header: {
            const uint32_t entryCount = static_cast<uint32_t>(stack.entries.size());
            const uint32_t cursor = static_cast<uint32_t>(stack.cursor);
            const uint32_t nextEntryId = stack.nextEntryId;
            if (!writeRaw(file, &entryCount, sizeof(entryCount))) return false;
            if (!writeRaw(file, &cursor, sizeof(cursor))) return false;
            if (!writeRaw(file, &nextEntryId, sizeof(nextEntryId))) return false;
            deferredSaveUndoEntryCursor = 0;
            deferredUndoWriteStage = DeferredUndoWriteStage::EntryHeader;
            return true;
        }

        case DeferredUndoWriteStage::EntryHeader: {
            if (deferredSaveUndoEntryCursor >= stack.entries.size()) {
                resetDeferredUndoWriteState();
                stackDone = true;
                return true;
            }
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!writeDeferredUndoEntryHeader(file, entry)) {
                return false;
            }
            deferredUndoWriteStage = DeferredUndoWriteStage::BeforeSnapshotPresence;
            return true;
        }

        case DeferredUndoWriteStage::BeforeSnapshotPresence: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!writeDeferredSnapshotPresence(file, entry.beforeSnapshot)) {
                return false;
            }
            if (entry.beforeSnapshot) {
                resetDeferredLoopWriteState();
                deferredUndoWriteStage = DeferredUndoWriteStage::BeforeSnapshotLoop;
            } else {
                deferredUndoWriteStage = DeferredUndoWriteStage::AfterSnapshotPresence;
            }
            return true;
        }

        case DeferredUndoWriteStage::BeforeSnapshotLoop: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!entry.beforeSnapshot) {
                return false;
            }
            bool snapshotDone = false;
            if (!stepDeferredLoopSnapshotPersist(file, *entry.beforeSnapshot, snapshotDone)) {
                return false;
            }
            if (snapshotDone) {
                deferredUndoWriteStage = DeferredUndoWriteStage::AfterSnapshotPresence;
            }
            return true;
        }

        case DeferredUndoWriteStage::AfterSnapshotPresence: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!writeDeferredSnapshotPresence(file, entry.afterSnapshot)) {
                return false;
            }
            if (entry.afterSnapshot) {
                resetDeferredLoopWriteState();
                deferredUndoWriteStage = DeferredUndoWriteStage::AfterSnapshotLoop;
            } else {
                deferredUndoWriteStage = DeferredUndoWriteStage::EntryTail;
            }
            return true;
        }

        case DeferredUndoWriteStage::AfterSnapshotLoop: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!entry.afterSnapshot) {
                return false;
            }
            bool snapshotDone = false;
            if (!stepDeferredLoopSnapshotPersist(file, *entry.afterSnapshot, snapshotDone)) {
                return false;
            }
            if (snapshotDone) {
                deferredUndoWriteStage = DeferredUndoWriteStage::EntryTail;
            }
            return true;
        }

        case DeferredUndoWriteStage::EntryTail: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!writeDeferredUndoEntryTail(file, entry)) {
                return false;
            }
            ++deferredSaveUndoEntryCursor;
            deferredUndoWriteStage = DeferredUndoWriteStage::EntryHeader;
            return true;
        }
    }

    return false;
}

bool stepDeferredSaveJob() {
    switch (deferredSaveStage) {
        case DeferredSaveStage::CurrentSetMeta:
            switch (deferredGlobalHeaderStage) {
                case DeferredGlobalHeaderStage::Version:
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Bpm;
                    return true;

                case DeferredGlobalHeaderStage::Bpm: {
                    const float savedBpm = bpm;
                    if (!writeRaw(deferredSaveFile, &savedBpm, sizeof(savedBpm))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing BPM");
                        return false;
                    }
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::LooperState;
                    return true;
                }

                case DeferredGlobalHeaderStage::LooperState: {
                    const uint32_t looperStateVal =
                        persistedLooperStateRaw(deferredSaveStateSnapshot);
                    if (!writeRaw(deferredSaveFile, &looperStateVal, sizeof(looperStateVal))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing looper state");
                        return false;
                    }
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::MasterLoopLength;
                    return true;
                }

                case DeferredGlobalHeaderStage::MasterLoopLength: {
                    const uint32_t masterLoopLength = trackManager.getMasterLoopLength();
                    if (!writeRaw(deferredSaveFile, &masterLoopLength, sizeof(masterLoopLength))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing master loop length");
                        return false;
                    }
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::TrackCount;
                    return true;
                }

                case DeferredGlobalHeaderStage::TrackCount:
                    if (!writeRaw(deferredSaveFile, &deferredSaveNumTracks,
                                  sizeof(deferredSaveNumTracks))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing track count");
                        return false;
                    }
                    deferredSaveTrackCursor = 0;
                    deferredSaveSlotCursor = 0;
                    deferredSavePoolCursor = 0;
                    deferredSaveTrackHeaderWritten = false;
                    deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                    deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                    deferredSaveStage = DeferredSaveStage::TrackHeaderAndSlots;
                    return true;
            }
            return false;

        case DeferredSaveStage::TrackHeaderAndSlots: {
            Track& track = trackManager.getTrack(deferredSaveTrackCursor);
            if (!deferredSaveTrackHeaderWritten) {
                switch (deferredTrackWriteStage) {
                    case DeferredTrackWriteStage::TrackState: {
                        TrackState stateToSave = track.getState();
                        if (stateToSave == TRACK_OVERDUBBING) {
                            stateToSave = TRACK_PLAYING;
                        }
                        const uint32_t trackState = static_cast<uint32_t>(stateToSave);
                        if (!writeRaw(deferredSaveFile, &trackState, sizeof(trackState))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing trackState for track ");
                            Serial.println(deferredSaveTrackCursor);
                            return false;
                        }
                        deferredTrackWriteStage = DeferredTrackWriteStage::Muted;
                        return true;
                    }

                    case DeferredTrackWriteStage::Muted: {
                        const bool muted = track.isMuted();
                        if (!writeRaw(deferredSaveFile, &muted, sizeof(muted))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing muted for track ");
                            Serial.println(deferredSaveTrackCursor);
                            return false;
                        }
                        deferredSaveTrackHeaderWritten = true;
                        deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                        deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                        return true;
                    }
                }
                return false;
            }

            if (deferredSaveSlotCursor < Config::MAX_LOOPS_PER_TRACK) {
                const uint8_t slot = deferredSaveSlotCursor;
                switch (deferredSlotWriteStage) {
                    case DeferredSlotWriteStage::SlotEnabled: {
                        const bool slotEnabled =
                            trackManager.isSlotEnabled(deferredSaveTrackCursor, slot);
                        if (!writeRaw(deferredSaveFile, &slotEnabled, sizeof(slotEnabled))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotEnabled for track ");
                            Serial.print(deferredSaveTrackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        deferredSlotWriteStage = DeferredSlotWriteStage::SlotMuted;
                        return true;
                    }

                    case DeferredSlotWriteStage::SlotMuted: {
                        const bool slotMuted =
                            trackManager.isSlotMuted(deferredSaveTrackCursor, slot);
                        if (!writeRaw(deferredSaveFile, &slotMuted, sizeof(slotMuted))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotMuted for track ");
                            Serial.print(deferredSaveTrackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        deferredSlotWriteStage = DeferredSlotWriteStage::SlotLoopId;
                        return true;
                    }

                    case DeferredSlotWriteStage::SlotLoopId: {
                        const LoopId slotLoopId = track.slotRef(slot).loopId;
                        if (!writeRaw(deferredSaveFile, &slotLoopId, sizeof(slotLoopId))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotLoopId for track ");
                            Serial.print(deferredSaveTrackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        deferredSaveSlotCursor++;
                        deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                        return true;
                    }
                }
                return false;
            }

            if (!trackHasCurrentSetDirtyLoopSlot(deferredSaveTrackCursor)) {
                deferredSaveTrackCursor++;
                deferredSaveSlotCursor = 0;
                deferredSavePoolCursor = 0;
                deferredSaveTrackHeaderWritten = false;
                deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                if (deferredSaveTrackCursor < deferredSaveNumTracks) {
                    return true;
                }
                deferredSaveStage = DeferredSaveStage::Footer;
                deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
                deferredSaveFooterTrackCursor = 0;
                return true;
            }

            deferredSavePoolCursor = 0;
            resetDeferredLoopWriteState();
            if (!closeDeferredMetaTempForLoopWrites()) {
                return false;
            }
            deferredSaveStage = DeferredSaveStage::CurrentSetLoopSlot;
            return true;
        }

        case DeferredSaveStage::CurrentSetLoopSlot: {
            const uint8_t trackIndex = deferredSaveTrackCursor;
            const uint8_t slotIndex = deferredSavePoolCursor;
            if (!shouldWriteCurrentSetLoopSlot(trackIndex, slotIndex)) {
                ++deferredSaveLoopSlotsSkipped;
                deferredSavePoolCursor++;
                if (deferredSavePoolCursor < Config::MAX_LOOPS_PER_TRACK) {
                    return true;
                }
                deferredSaveTrackCursor++;
                if (deferredSaveTrackCursor < deferredSaveNumTracks) {
                    deferredSaveSlotCursor = 0;
                    deferredSavePoolCursor = 0;
                    resetDeferredLoopWriteState();
                    deferredSaveTrackHeaderWritten = false;
                    deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                    deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                    if (!reopenDeferredMetaTempForAppend()) {
                        return false;
                    }
                    deferredSaveStage = DeferredSaveStage::TrackHeaderAndSlots;
                    return true;
                }
                if (!reopenDeferredMetaTempForAppend()) {
                    return false;
                }
                deferredSaveStage = DeferredSaveStage::Footer;
                deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
                deferredSaveFooterTrackCursor = 0;
                return true;
            }

            if (!deferredSaveLoopFileOpen &&
                !openDeferredLoopSlotTemp(trackIndex, slotIndex)) {
                return false;
            }
            Track& track = trackManager.getTrack(deferredSaveTrackCursor);
            bool loopDone = false;
            const bool loopWriteOk = track.loopsAllocated()
                                         ? stepDeferredLoopPersist(
                                               deferredSaveLoopFile,
                                               track.getLoop(deferredSavePoolCursor), loopDone)
                                         : stepDeferredEmptyLoopPersist(
                                               deferredSaveLoopFile,
                                               static_cast<LoopId>(deferredSavePoolCursor),
                                               loopDone);
            if (!loopWriteOk) {
                Serial.print("[StorageManager] ERROR: Deferred save failed writing loop pool entry track ");
                Serial.print(deferredSaveTrackCursor);
                Serial.print(" pool ");
                Serial.println(deferredSavePoolCursor);
                return false;
            }
            if (!loopDone) {
                return true;
            }

            if (!finalizeDeferredLoopSlotTemp(trackIndex, slotIndex)) {
                Serial.print("[StorageManager] ERROR: Deferred save failed finalizing loop slot track ");
                Serial.print(trackIndex);
                Serial.print(" slot ");
                Serial.println(slotIndex);
                return false;
            }
            ++deferredSaveLoopSlotsWritten;
            clearCurrentSetLoopSlotDirtyInternal(trackIndex, slotIndex);

            deferredSavePoolCursor++;
            if (deferredSavePoolCursor < Config::MAX_LOOPS_PER_TRACK) {
                resetDeferredLoopWriteState();
                return true;
            }

            deferredSaveTrackCursor++;
            if (deferredSaveTrackCursor < deferredSaveNumTracks) {
                deferredSaveSlotCursor = 0;
                deferredSavePoolCursor = 0;
                resetDeferredLoopWriteState();
                deferredSaveTrackHeaderWritten = false;
                deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                if (!reopenDeferredMetaTempForAppend()) {
                    return false;
                }
                deferredSaveStage = DeferredSaveStage::TrackHeaderAndSlots;
                return true;
            }

            if (!reopenDeferredMetaTempForAppend()) {
                return false;
            }
            deferredSaveStage = DeferredSaveStage::Footer;
            deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
            deferredSaveFooterTrackCursor = 0;
            return true;
        }

        case DeferredSaveStage::Footer: {
            switch (deferredFooterWriteStage) {
                case DeferredFooterWriteStage::SelectedTrack: {
                    const uint8_t selectedTrackIdx = trackManager.getSelectedTrackIndex();
                    if (!writeRaw(deferredSaveFile, &selectedTrackIdx,
                                  sizeof(selectedTrackIdx))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing selected track index");
                        return false;
                    }
                    deferredSaveFooterTrackCursor = 0;
                    deferredFooterWriteStage = DeferredFooterWriteStage::ActiveLoopIndex;
                    return true;
                }

                case DeferredFooterWriteStage::ActiveLoopIndex:
                    if (deferredSaveFooterTrackCursor < deferredSaveNumTracks) {
                        const uint8_t activeIdx =
                            trackManager.getActiveLoopIndex(deferredSaveFooterTrackCursor);
                        if (!writeRaw(deferredSaveFile, &activeIdx, sizeof(activeIdx))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing activeLoopIndex for track ");
                            Serial.println(deferredSaveFooterTrackCursor);
                            return false;
                        }
                        ++deferredSaveFooterTrackCursor;
                        return true;
                    }
                    deferredFooterWriteStage = DeferredFooterWriteStage::UndoMagic;
                    return true;

                case DeferredFooterWriteStage::UndoMagic:
                    if (!writeRaw(deferredSaveFile, &GLOBAL_UNDO_MAGIC,
                                  sizeof(GLOBAL_UNDO_MAGIC))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing global undo magic");
                        return false;
                    }
                    deferredSaveUndoTrackCursor = 0;
                    resetDeferredUndoWriteState();
                    deferredSaveStage = DeferredSaveStage::UndoStacks;
                    return true;
            }
            return false;
        }

        case DeferredSaveStage::UndoStacks: {
            if (deferredSaveUndoTrackCursor < deferredSaveNumTracks) {
                const Track& track = trackManager.getTrack(deferredSaveUndoTrackCursor);
                bool stackDone = false;
                if (!stepDeferredUndoStackPersist(deferredSaveFile, track.getGlobalUndoStack(),
                                                  stackDone)) {
                    Serial.print("[StorageManager] ERROR: Deferred save failed writing global undo stack for track ");
                    Serial.println(deferredSaveUndoTrackCursor);
                    return false;
                }
                if (!stackDone) {
                    return true;
                }
                deferredSaveUndoTrackCursor++;
                resetDeferredUndoWriteState();
                return true;
            }

            if (!finalizeDeferredMetaTempFile()) {
                Serial.println("[StorageManager] ERROR: Deferred save failed finalizing CurrentSet meta");
                return false;
            }
            deferredSaveStage = DeferredSaveStage::CurrentSetCompletion;
            return true;
        }

        case DeferredSaveStage::CurrentSetCompletion: {
            const uint32_t lastActiveUnix = RtcTime::getUnixTime();
            if (!CurrentSetStorage::patchLastActiveUnix(CurrentSetStorage::kCurrentMetaPath,
                                                        lastActiveUnix)) {
                Serial.println("[StorageManager] ERROR: Deferred save failed patching lastActiveUnix");
                return false;
            }
            if (quarantineLegacyMonolithAfterSave) {
                quarantineStorageFile();
                quarantineLegacyMonolithAfterSave = false;
                Serial.println("[StorageManager] v5 monolith quarantined after CurrentSet save.");
            }
            forceCurrentSetFullLoopWrite = false;
            Serial.println("[StorageManager] CurrentSet saved successfully (v6 deferred slices).");
            deferredSaveInProgress = false;
            deferredSaveStage = DeferredSaveStage::Idle;
            return true;
        }

        case DeferredSaveStage::Idle:
        default:
            return true;
    }
}
}  // namespace

void StorageManager::requestUrgentEditSave() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    urgentEditSavePending = true;
}

bool StorageManager::saveNewSet(char* savedSetFolderOut, size_t outSize) {
    return saveNewSetInternal(looperState.getLooperState(), savedSetFolderOut, outSize);
}

bool StorageManager::loadSetIntoCurrent(const char* savedSetFolderName) {
    uint32_t sourceSequence = 0;
    if (!SavedSetCatalog::parseSavedSetFolderName(savedSetFolderName, sourceSequence, nullptr)) {
        return false;
    }
    char sourceSetDir[kSavedSetPathCapacity];
    if (!formatSavedSetDirectoryPath(savedSetFolderName, sourceSetDir,
                                     sizeof(sourceSetDir)) ||
        !SD.exists(sourceSetDir)) {
        return false;
    }

    if (currentSetAnchorFields.hasMaterialChangesSinceAnchor != 0 &&
        !saveNewSetInternal(looperState.getLooperState(), nullptr, 0)) {
        return false;
    }

    if (!copySavedSetIntoCurrent(sourceSetDir) ||
        !loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir,
                                     looperState.getLooperState())) {
        return false;
    }

    forceCurrentSetFullLoopWrite = false;
    syncCurrentSetDirtyTrackingFromLoadedState();
    currentSetAnchorFields.loadedFromSequence = sourceSequence;
    currentSetAnchorFields.lastAnchoredSequence = sourceSequence;
    currentSetAnchorFields.hasMaterialChangesSinceAnchor = 0;
    currentSetAnchorFields.lastMaterialChangeUnix = 0;
    return patchCurrentSetAnchor();
}

void StorageManager::processSavedSetFailsafe(const LooperState& state) {
    const uint32_t nowMs = millis();
    if (nowMs - lastSavedSetFailsafeCheckAtMs < kSavedSetFailsafeCheckIntervalMs) {
        return;
    }
    lastSavedSetFailsafeCheckAtMs = nowMs;

    bool captureActive = false;
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        const Track& track = trackManager.getTrack(trackIndex);
        if (track.isRecording() || track.isOverdubbing()) {
            captureActive = true;
            break;
        }
    }

    if (!SavedSetCatalog::shouldRunEightHourFailsafe(
            currentSetAnchorFields.hasMaterialChangesSinceAnchor != 0,
            currentSetAnchorFields.lastMaterialChangeUnix, RtcTime::getUnixTime(),
            captureActive)) {
        return;
    }

    char folderName[16];
    if (saveNewSetInternal(state, folderName, sizeof(folderName))) {
        Serial.print("[StorageManager] Eight-hour failsafe SavedSet created: ");
        Serial.println(folderName);
    } else {
        Serial.println("[StorageManager] ERROR: Eight-hour failsafe saveNewSet failed");
    }
}

void StorageManager::markCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#endif
    markCurrentSetLoopSlotDirtyInternal(trackIndex, slotIndex);
}

void StorageManager::markCurrentSetTrackDirty(uint8_t trackIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    return;
#endif
    markCurrentSetTrackDirtyInternal(trackIndex);
}

void StorageManager::markAllCurrentSetLoopSlotsDirty() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    markAllCurrentSetLoopSlotsDirtyInternal();
}

void StorageManager::processEditAutosave(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    urgentEditSavePending = false;
    return;
#endif
    const uint32_t nowMs = millis();
    if (urgentEditSavePending) {
        urgentEditSavePending = false;
        clearEditDirtyAfterDeferredSave = true;
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            Track& track = trackManager.getTrack(t);
            if (!track.loopsAllocated()) {
                continue;
            }
            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                if (track.getLoop(s).isEditStateDirty()) {
                    markCurrentSetLoopSlotDirtyInternal(t, s);
                }
            }
        }
        requestDeferredSaveState(state, UINT32_MAX, true);
        // Runtime policy: urgent NOTE_EDIT save requests stay deferred to avoid
        // blocking playback timing on synchronous SD drain.
        lastEditAutosaveMs = nowMs;
        return;
    }

    bool captureActive = false;
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (track.isRecording() || track.isOverdubbing()) {
            captureActive = true;
        }
    }
    const bool anyDirty = anyAllocatedLoopEditStateDirty();
    if (!anyDirty || captureActive) {
        return;
    }
    if (nowMs - lastEditAutosaveMs < Config::autosaveIntervalMs) {
        return;
    }
    clearEditDirtyAfterDeferredSave = true;
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (track.getLoop(s).isEditStateDirty()) {
                markCurrentSetLoopSlotDirtyInternal(t, s);
            }
        }
    }
    requestDeferredSaveState(state);
    lastEditAutosaveMs = nowMs;
}

void StorageManager::requestDeferredSaveState(const LooperState& /*state*/, uint32_t admissionHeap,
                                              bool isUrgentRequest) {
#if BYPASS_STOP_UNDO_SAVE
    (void)isUrgentRequest;
    return;
#endif
    const bool alreadyPending = deferredSavePending;
    if (admissionHeap != UINT32_MAX || deferredSaveAdmissionHeap == 0) {
        deferredSaveAdmissionHeap = admissionHeap;
    }
    const uint32_t reportedHeap = deferredSaveAdmissionHeap == UINT32_MAX
                                      ? 0
                                      : deferredSaveAdmissionHeap;
    deferredSaveUrgentRequested = deferredSaveUrgentRequested || isUrgentRequest;
    deferredSavePending = true;
    SC_PERSIST("request", 0, reportedHeap, reportedHeap,
               alreadyPending ? "already_pending" : "queued");
}

bool StorageManager::isDeferredSaveActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    // Active only during the SD-write section of a save slice.
    return deferredSaveSdIoActive;
#endif
}

bool StorageManager::hasDeferredSaveWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return deferredSavePending || deferredSaveInProgress;
#endif
}

void StorageManager::processDeferredSaveState(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    deferredSavePending = false;
    resetDeferredSaveJobState();
    return;
#endif
    deferredSaveSdIoActive = false;
    if (!deferredSavePending && !deferredSaveInProgress) {
        return;
    }

    // Do not start or continue deferred save while capture is active.
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        const Track& track = trackManager.getTrack(t);
        if (track.isRecording() || track.isOverdubbing()) {
            return;
        }
    }

    // Admission uses a caller-provided heap sample when available; do not sample
    // getInternalHeapFreeBytes() here.
    // Once dispatch has started, run every slice to completion without re-gating.
    if (!deferredSaveInProgress) {
        if (deferredSaveAdmissionHeap != UINT32_MAX &&
            !LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(deferredSaveAdmissionHeap)) {
            if (!deferredSaveHeapFloorDeferred) {
                SC_PERSIST("defer", 0, deferredSaveAdmissionHeap, deferredSaveAdmissionHeap,
                           "heap_floor");
                deferredSaveHeapFloorDeferred = true;
            }
            return;
        }
        deferredSaveHeapFloorDeferred = false;
    }

    if (!deferredSaveInProgress && deferredSavePending) {
        const uint32_t dispatchHeap = deferredSaveAdmissionHeap == UINT32_MAX
                                          ? 0
                                          : deferredSaveAdmissionHeap;
        SC_PERSIST("dispatch", 0, dispatchHeap, dispatchHeap, "run");
        deferredSavePending = false;
        deferredSaveStartedAtUs = micros();
        deferredSaveHeapBefore = dispatchHeap;
        deferredSaveLoopSlotsWritten = 0;
        deferredSaveLoopSlotsSkipped = 0;
        deferredSaveDisplayBlockUs = 0;
        deferredSaveInProgress = true;
        const uint32_t ioStartUs = micros();
        deferredSaveSdIoActive = true;
        const bool beginOk = beginDeferredSaveJob(state);
        deferredSaveDisplayBlockUs += micros() - ioStartUs;
        deferredSaveSdIoActive = false;
        if (!beginOk) {
            const uint32_t saveDurationUs = micros() - deferredSaveStartedAtUs;
            SC_PERSIST("result", saveDurationUs, deferredSaveHeapBefore, deferredSaveHeapBefore,
                       "failed");
            deferredSaveLastCompletedOk = false;
            deferredSaveFailedAtMs = millis();
            deferredSaveCompletedAtMs = 0;
            resetDeferredSaveJobState();
            return;
        }
        deferredSaveUrgentRequested = false;
        return;
    }

    if (!deferredSaveInProgress) {
        return;
    }

    emitDeferredSaveSliceTelemetry("start");
    const uint32_t ioStartUs = micros();
    deferredSaveSdIoActive = true;
    const bool stepOk = stepDeferredSaveJob();
    deferredSaveDisplayBlockUs += micros() - ioStartUs;
    deferredSaveSdIoActive = false;
    emitDeferredSaveSliceTelemetry(stepOk ? "done" : "failed");
    if (!stepOk) {
        deferredSaveInProgress = false;
    }
    if (deferredSaveInProgress) {
        return;
    }

    const uint32_t saveDurationUs = micros() - deferredSaveStartedAtUs;
    const char* resultOutcome = stepOk ? "ok" : "failed";
    SC_PERSIST("result", saveDurationUs, deferredSaveHeapBefore, deferredSaveHeapBefore,
               resultOutcome);
    char resultStats[72];
    std::snprintf(resultStats, sizeof(resultStats), "w%u_s%u_db%lu",
                  static_cast<unsigned>(deferredSaveLoopSlotsWritten),
                  static_cast<unsigned>(deferredSaveLoopSlotsSkipped),
                  static_cast<unsigned long>(deferredSaveDisplayBlockUs));
    SC_PERSIST("result_stats", saveDurationUs, deferredSaveHeapBefore, deferredSaveHeapBefore,
               resultStats);
    deferredSaveLastCompletedOk = stepOk;
    const uint32_t resultAtMs = millis();
    if (stepOk) {
        deferredSaveCompletedAtMs = resultAtMs;
        deferredSaveFailedAtMs = 0;
    } else {
        deferredSaveFailedAtMs = resultAtMs;
        deferredSaveCompletedAtMs = 0;
    }
    if (stepOk && clearEditDirtyAfterDeferredSave) {
        clearAllocatedLoopEditStateDirty();
        clearEditDirtyAfterDeferredSave = false;
    }
    deferredSaveUrgentRequested = false;
    if (!stepOk) {
        resetDeferredSaveJobState();
    }
}

DeferredSaveDisplayStatus StorageManager::getDeferredSaveDisplayStatus(uint32_t nowMs) {
#if BYPASS_STOP_UNDO_SAVE
    (void)nowMs;
    return {};
#else
    DeferredSaveDisplayInputs inputs{};
    inputs.savePending = deferredSavePending;
    inputs.saveInProgress = deferredSaveInProgress;
    inputs.completedAtMs = deferredSaveCompletedAtMs;
    inputs.failedAtMs = deferredSaveFailedAtMs;
    return resolveDeferredSaveDisplayStatus(nowMs, inputs);
#endif
}

bool StorageManager::saveState(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    Serial.println("[StorageManager] BYPASS_STOP_UNDO_SAVE: skip saveState");
    return true;
#endif
    HotPathTelemetry::ScopedSaveState telemetryScope;
    Serial.println("[StorageManager] Draining deferred save to SD card...");

    if (!deferredSaveInProgress) {
        deferredSaveAdmissionHeap = UINT32_MAX;
        deferredSaveUrgentRequested = true;
        const bool alreadyPending = deferredSavePending;
        deferredSavePending = true;
        SC_PERSIST("request", 0, 0, 0,
                   alreadyPending ? "sync_drain_already_pending" : "sync_drain");
    }

    deferredSaveLastCompletedOk = false;
    constexpr uint32_t kMaxDrainSteps = 200000u;
    uint32_t steps = 0;
    while ((deferredSavePending || deferredSaveInProgress) && steps < kMaxDrainSteps) {
        processDeferredSaveState(state);
        yield();
        steps++;
    }

    const bool completed = !deferredSavePending && !deferredSaveInProgress;
    if (!completed) {
        Serial.println("[StorageManager] ERROR: Deferred save drain exceeded step limit");
        return false;
    }
    if (!deferredSaveLastCompletedOk) {
        Serial.println("[StorageManager] ERROR: Deferred save drain failed");
        return false;
    }
    Serial.println("[StorageManager] State saved successfully (v4).");
    telemetryScope.setOk(true);
    return true;
}

static bool readLoopFromCurrentSetFile(File& file, Loop& loop) {
    const size_t fileSize = file.size();
    if (fileSize < sizeof(STORAGE_COMPLETE_MAGIC)) {
        return false;
    }
    const size_t payloadSize = fileSize - sizeof(STORAGE_COMPLETE_MAGIC);
    if (!file.seek(0)) {
        return false;
    }

    class BoundedFileIo {
     public:
        BoundedFileIo(File& f, size_t limit) : file_(f), limit_(limit), pos_(0) {}

        StorageIo io() {
            return StorageIo{
                [this](const void*, size_t) { return false; },
                [this](void* data, size_t size) { return read(data, size); },
            };
        }

     private:
        bool read(void* data, size_t size) {
            if (pos_ + size > limit_) {
                return false;
            }
            const int bytesRead = file_.read(static_cast<uint8_t*>(data), size);
            if (bytesRead != static_cast<int>(size)) {
                return false;
            }
            pos_ += size;
            return true;
        }

        File& file_;
        size_t limit_;
        size_t pos_;
    };

    BoundedFileIo bounded(file, payloadSize);
    if (!readLoopPersisted(bounded.io(), loop)) {
        return false;
    }
    uint32_t magic = 0;
    return readRaw(file, &magic, sizeof(magic)) && magic == STORAGE_COMPLETE_MAGIC;
}

static bool applyLoadedTransportFooter(uint8_t numTracks, const std::vector<uint8_t>& activeLoopIndex,
                                       uint8_t selectedTrackIdx, LooperState& state,
                                       LooperState loadedLooperState, uint32_t masterLoopLength) {
    state = loadedLooperState;
    trackManager.setMasterLoopLength(masterLoopLength);
    for (uint8_t t = 0; t < numTracks; ++t) {
        trackManager.getTrack(t).setActiveLoopIndex(activeLoopIndex[t]);
        trackManager.setSelectedSlotIndex(t, activeLoopIndex[t]);
    }
    if (selectedTrackIdx < Config::NUM_TRACKS) {
        trackManager.setSelectedTrack(selectedTrackIdx);
    } else {
        trackManager.setSelectedTrack(0);
    }
    stabilizeBootMemoryAfterLoad();
    return true;
}

bool StorageManager::loadCurrentSetMetaAndTracks(File& file, const char* setDir, LooperState& state,
                                        std::vector<uint8_t>& activeLoopIndex,
                                        uint8_t& selectedTrackIdx) {
    CurrentSetStorage::MetaHeader metaHeader{};
    const StorageIo metaIo = storageIoFromFileRead(file);
    if (!CurrentSetStorage::readMetaHeader(metaIo, metaHeader)) {
        Serial.println("[StorageManager] ERROR: Failed to read CurrentSet meta header");
        return false;
    }
    if (metaHeader.containerVersion != CurrentSetStorage::CONTAINER_VERSION) {
        Serial.print("[StorageManager] ERROR: Unsupported CurrentSet version ");
        Serial.println(metaHeader.containerVersion);
        return false;
    }
    currentSetAnchorFields = metaHeader.anchor;

    float savedBpm = 0;
    if (!readRaw(file, &savedBpm, sizeof(savedBpm))) {
        return false;
    }
    if (savedBpm >= 20.0f && savedBpm <= 300.0f) {
        bpm = savedBpm;
    }

    uint32_t looperStateVal = 0;
    if (!readRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        return false;
    }
    const LooperState loadedLooperState = sanitizeLoadedLooperState(static_cast<LooperState>(looperStateVal));

    uint32_t masterLoopLength = 0;
    if (!readRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        return false;
    }

    uint8_t numTracks = 0;
    if (!readRaw(file, &numTracks, sizeof(numTracks)) || numTracks != Config::NUM_TRACKS) {
        return false;
    }

    activeLoopIndex.assign(numTracks, 0);
    for (uint8_t t = 0; t < numTracks; ++t) {
        Track& track = trackManager.getTrack(t);
        track.ensureLoopsAllocated();

        uint32_t trackStateRaw = 0;
        if (!readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
            return false;
        }
        TrackState loadedTrackState = static_cast<TrackState>(trackStateRaw);

        bool muted = false;
        if (!readRaw(file, &muted, sizeof(muted))) {
            return false;
        }

        bool anySlotHasEvents = false;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            bool slotEnabled = false;
            bool slotMuted = false;
            LoopId slotLoopId = kInvalidLoopId;
            if (!readRaw(file, &slotEnabled, sizeof(slotEnabled)) ||
                !readRaw(file, &slotMuted, sizeof(slotMuted)) ||
                !readRaw(file, &slotLoopId, sizeof(slotLoopId))) {
                return false;
            }
            if (slotLoopId == kInvalidLoopId || slotLoopId >= Config::MAX_LOOPS_PER_TRACK) {
                slotLoopId = static_cast<LoopId>(s);
            }
            trackManager.setSlotEnabled(t, s, slotEnabled);
            trackManager.setSlotMuted(t, s, slotMuted);
            track.slots_[s].loopId = slotLoopId;
        }

        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            char loopPath[64];
            const int written = std::snprintf(loopPath, sizeof(loopPath), "%s/loop_%02u_%02u.bin",
                                              setDir, static_cast<unsigned>(t),
                                              static_cast<unsigned>(s));
            if (written <= 0 || static_cast<size_t>(written) >= sizeof(loopPath)) {
                return false;
            }
            if (!SD.exists(loopPath)) {
                Serial.print("[StorageManager] ERROR: Missing CurrentSet loop file ");
                Serial.println(loopPath);
                return false;
            }
            if (!CurrentSetStorage::verifyFileCompleteMagic(loopPath)) {
                Serial.print("[StorageManager] ERROR: CurrentSet loop file missing completion marker ");
                Serial.println(loopPath);
                return false;
            }
            File loopFile = SD.open(loopPath, FILE_READ);
            if (!loopFile) {
                return false;
            }
            Loop& loop = track.getLoop(s);
            if (!readLoopFromCurrentSetFile(loopFile, loop)) {
                loopFile.close();
                return false;
            }
            loopFile.close();
            if (loop.hasPublishedEvents()) {
                anySlotHasEvents = true;
            }
        }

        if (loadedTrackState == TRACK_RECORDING || loadedTrackState == TRACK_ARMED ||
            loadedTrackState == TRACK_STOPPED_RECORDING || loadedTrackState == TRACK_PLAYING ||
            loadedTrackState == TRACK_OVERDUBBING) {
            loadedTrackState = anySlotHasEvents ? TRACK_STOPPED : TRACK_EMPTY;
        }
        if (loadedTrackState == TRACK_EMPTY && anySlotHasEvents) {
            loadedTrackState = TRACK_STOPPED;
        }
        track.forceSetState(loadedTrackState);
        if (muted != track.isMuted()) {
            track.toggleMuteTrack();
        }
    }

    if (!readRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
        return false;
    }
    for (uint8_t t = 0; t < numTracks; ++t) {
        if (!readRaw(file, &activeLoopIndex[t], sizeof(activeLoopIndex[t]))) {
            return false;
        }
    }

    uint32_t undoMagic = 0;
    if (!readRaw(file, &undoMagic, sizeof(undoMagic)) || undoMagic != GLOBAL_UNDO_MAGIC) {
        return false;
    }
    for (uint8_t t = 0; t < numTracks; ++t) {
        if (!readGlobalUndoStack(file, trackManager.getTrack(t).getGlobalUndoStack())) {
            return false;
        }
    }

    uint32_t tailMarker = 0;
    if (!readRaw(file, &tailMarker, sizeof(tailMarker))) {
        return false;
    }
    if (tailMarker == SavedSetCatalog::kSavedSetMetaTrailerMagic) {
        if (!file.seek(file.position() - sizeof(uint32_t))) {
            return false;
        }
        SavedSetCatalog::SavedSetMetadata ignoredMetadata{};
        const StorageIo trailerIo = storageIoFromFileRead(file);
        if (!SavedSetCatalog::readSavedSetMetadataTrailer(trailerIo, ignoredMetadata)) {
            return false;
        }
        if (!readRaw(file, &tailMarker, sizeof(tailMarker))) {
            return false;
        }
    }
    if (tailMarker != STORAGE_COMPLETE_MAGIC) {
        Serial.println("[StorageManager] ERROR: CurrentSet meta completion marker mismatch");
        return false;
    }

  return applyLoadedTransportFooter(numTracks, activeLoopIndex, selectedTrackIdx, state,
                                    loadedLooperState, masterLoopLength);
}

bool StorageManager::loadCurrentSetFromDirectory(const char* setDir, LooperState& state) {
    char metaPath[64];
    const int written = std::snprintf(metaPath, sizeof(metaPath), "%s/meta.bin", setDir);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(metaPath)) {
        return false;
    }
    File file = SD.open(metaPath, FILE_READ);
    if (!file) {
        return false;
    }
    std::vector<uint8_t> activeLoopIndex;
    uint8_t selectedTrackIdx = 0;
    const bool ok = loadCurrentSetMetaAndTracks(file, setDir, state, activeLoopIndex, selectedTrackIdx);
    file.close();
    return ok;
}

bool StorageManager::loadCurrentSetFromSd(LooperState& state) {
    Serial.println("[StorageManager] Loading CurrentSet from SD...");
    const bool ok = loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir, state);
    if (ok) {
        Serial.println("[StorageManager] CurrentSet loaded successfully (v6).");
    }
    return ok;
}

bool StorageManager::loadV5MonolithIntoRam(LooperState& state) {
    Serial.println("[StorageManager] Loading state from SD card...");
    File file = SD.open(STORAGE_FILENAME, FILE_READ);
    if (!file) {
        Serial.print("[StorageManager] ERROR: Could not open file for reading: ");
        Serial.println(STORAGE_FILENAME);
        return false;
    }
    // Use temporary variables to avoid corrupting current state if file is bad
    uint32_t version = 0;
    if (!readRaw(file, &version, sizeof(version))) {
        Serial.println("[StorageManager] ERROR: Failed to read version");
        file.close();
        return false;
    }
    Serial.println("[StorageManager] Version read OK");
    if (version != 5) {
        Serial.print("[StorageManager] ERROR: Unsupported legacy storage version. Found: ");
        Serial.println(version);
        file.close();
        return false;
    }

    float savedBpm = 0;
    if (!readRaw(file, &savedBpm, sizeof(savedBpm))) {
        Serial.println("[StorageManager] ERROR: Failed to read BPM");
        file.close();
        return false;
    }
    if (savedBpm >= 20.0f && savedBpm <= 300.0f) {
        bpm = savedBpm;
        Serial.print("[StorageManager] Restored BPM: ");
        Serial.println(savedBpm);
    }

    // Looper state
    uint32_t looperStateVal = 0;
    if (!readRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        Serial.println("[StorageManager] ERROR: Failed to read looper state");
        file.close();
        return false;
    }
    LooperState loadedLooperState = sanitizeLoadedLooperState((LooperState)looperStateVal);

    // Master loop length
    uint32_t masterLoopLength = 0;
    if (!readRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        Serial.println("[StorageManager] ERROR: Failed to read master loop length");
        file.close();
        return false;
    }

    // Tracks
    uint8_t numTracks = 0;
    if (!readRaw(file, &numTracks, sizeof(numTracks))) {
        Serial.println("[StorageManager] ERROR: Failed to read numTracks");
        file.close();
        return false;
    }
    if (numTracks != Config::NUM_TRACKS) {
        Serial.print("[StorageManager] ERROR: numTracks mismatch. Found: ");
        Serial.println(numTracks);
        file.close();
        return false;
    }

    std::vector<uint8_t> activeLoopIndex(numTracks, 0);
        uint8_t selectedTrackIdx = 0;
        auto failAfterPartialLoad = [&file]() {
            file.close();
            quarantineStorageFile();
            resetTracksAfterFailedLoad();
            return false;
        };

        for (uint8_t t = 0; t < numTracks; ++t) {
            Track& track = trackManager.getTrack(t);
            track.ensureLoopsAllocated();

            uint32_t trackStateRaw = 0;
            if (!readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
                Serial.print("[StorageManager] ERROR: Failed to read trackState for track "); Serial.println(t);
                return failAfterPartialLoad();
            }
            TrackState loadedTrackState = static_cast<TrackState>(trackStateRaw);

            bool muted = false;
            if (!readRaw(file, &muted, sizeof(muted))) {
                Serial.print("[StorageManager] ERROR: Failed to read muted for track "); Serial.println(t);
                return failAfterPartialLoad();
            }

            bool anySlotHasEvents = false;

            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                bool slotEnabled = false;
                bool slotMuted = false;
                LoopId slotLoopId = kInvalidLoopId;
                if (!readRaw(file, &slotEnabled, sizeof(slotEnabled))) { Serial.print("[StorageManager] ERROR: Failed to read slotEnabled for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }
                if (!readRaw(file, &slotMuted, sizeof(slotMuted))) { Serial.print("[StorageManager] ERROR: Failed to read slotMuted for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }
                if (!readRaw(file, &slotLoopId, sizeof(slotLoopId))) { Serial.print("[StorageManager] ERROR: Failed to read slotLoopId for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }

                if (slotLoopId == kInvalidLoopId || slotLoopId >= Config::MAX_LOOPS_PER_TRACK) {
                    Serial.print("[StorageManager] WARNING: Invalid slotLoopId ");
                    Serial.print(static_cast<unsigned long>(slotLoopId));
                    Serial.print(" for track ");
                    Serial.print(t);
                    Serial.print(" slot ");
                    Serial.print(s);
                    Serial.print(" — repairing to ");
                    Serial.println(s);
                    slotLoopId = static_cast<LoopId>(s);
                }

                trackManager.setSlotEnabled(t, s, slotEnabled);
                trackManager.setSlotMuted(t, s, slotMuted);
                track.slots_[s].loopId = slotLoopId;
            }

            const StorageIo loopIo = storageIoFromFileRead(file);
            for (uint8_t p = 0; p < Config::MAX_LOOPS_PER_TRACK; ++p) {
                Loop& loop = track.loopPool_.at(p);
                if (!readLoopPersisted(loopIo, loop)) {
                    Serial.print("[StorageManager] ERROR: Failed to read loop pool entry track ");
                    Serial.print(t);
                    Serial.print(" pool ");
                    Serial.println(p);
                    return failAfterPartialLoad();
                }
                if (loop.hasPublishedEvents()) {
                    anySlotHasEvents = true;
                }
            }

            if (loadedTrackState == TRACK_RECORDING || loadedTrackState == TRACK_ARMED ||
                loadedTrackState == TRACK_STOPPED_RECORDING || loadedTrackState == TRACK_PLAYING ||
                loadedTrackState == TRACK_OVERDUBBING) {
                loadedTrackState = anySlotHasEvents ? TRACK_STOPPED : TRACK_EMPTY;
            }
            if (loadedTrackState == TRACK_EMPTY && anySlotHasEvents) {
                loadedTrackState = TRACK_STOPPED;
            }

            track.forceSetState(loadedTrackState);
            if (muted != track.isMuted()) track.toggleMuteTrack();
        }

        if (!readRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
            Serial.println("[StorageManager] ERROR: Failed to read selectedTrackIdx for v4");
            return failAfterPartialLoad();
        }

        for (uint8_t t = 0; t < numTracks; ++t) {
            if (!readRaw(file, &activeLoopIndex[t], sizeof(activeLoopIndex[t]))) {
                Serial.println("[StorageManager] ERROR: Failed to read activeLoopIndex for v4");
                return failAfterPartialLoad();
            }
        }

        uint32_t undoMagic = 0;
        if (!readRaw(file, &undoMagic, sizeof(undoMagic))) {
            Serial.println("[StorageManager] ERROR: Failed to read global undo magic for v4");
            return failAfterPartialLoad();
        }
        if (undoMagic != GLOBAL_UNDO_MAGIC) {
            Serial.println("[StorageManager] ERROR: Global undo magic mismatch for v4");
            return failAfterPartialLoad();
        }
        for (uint8_t t = 0; t < numTracks; ++t) {
            Track& track = trackManager.getTrack(t);
            if (!readGlobalUndoStack(file, track.getGlobalUndoStack())) {
                Serial.print("[StorageManager] ERROR: Failed to read global undo stack for track ");
                Serial.println(t);
                return failAfterPartialLoad();
            }
        }

        uint32_t completeMagic = 0;
        if (!readRaw(file, &completeMagic, sizeof(completeMagic))) {
            Serial.println("[StorageManager] ERROR: Failed to read storage completion marker for v4");
            return failAfterPartialLoad();
        }
        if (completeMagic != STORAGE_COMPLETE_MAGIC) {
            Serial.println("[StorageManager] ERROR: Storage completion marker mismatch for v4");
            return failAfterPartialLoad();
        }

        file.close();
        Serial.println("[StorageManager] State loaded successfully (v4).");

        state = loadedLooperState;
        trackManager.setMasterLoopLength(masterLoopLength);
        for (uint8_t t = 0; t < numTracks; ++t) {
            trackManager.getTrack(t).setActiveLoopIndex(activeLoopIndex[t]);
            trackManager.setSelectedSlotIndex(t, activeLoopIndex[t]);
        }
        if (selectedTrackIdx < Config::NUM_TRACKS) {
            trackManager.setSelectedTrack(selectedTrackIdx);
        } else {
            trackManager.setSelectedTrack(0);
        }
        stabilizeBootMemoryAfterLoad();
    return true;
}

bool StorageManager::migrateV5MonolithToCurrentSet(LooperState& state) {
    Serial.println("[StorageManager] Migrating v5 monolith to CurrentSet (deferred SD write)...");
    if (!loadV5MonolithIntoRam(state)) {
        return false;
    }
    currentSetAnchorFields = {};
    quarantineLegacyMonolithAfterSave = true;
    forceCurrentSetFullLoopWrite = true;
    markAllCurrentSetLoopSlotsDirtyInternal(false);
    requestDeferredSaveState(state);
    Serial.println("[StorageManager] v5 state loaded to RAM; CurrentSet write queued.");
    return true;
}

bool StorageManager::tryLoadLatestRecoveryPoint(LooperState& state) {
    if (!SD.exists(CurrentSetStorage::kCheckpointsDir)) {
        return false;
    }
    File dir = SD.open(CurrentSetStorage::kCheckpointsDir);
    if (!dir) {
        return false;
    }
    char latestName[32] = {};
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const char* name = entry.name();
        entry.close();
        if (name == nullptr || name[0] != '_') {
            continue;
        }
        if (latestName[0] == '\0' || std::strcmp(name, latestName) > 0) {
            std::snprintf(latestName, sizeof(latestName), "%s", name);
        }
    }
    dir.close();
    if (latestName[0] == '\0') {
        return false;
    }
    char recoveryDir[72];
    std::snprintf(recoveryDir, sizeof(recoveryDir), "%s/%s",
                  CurrentSetStorage::kCheckpointsDir, latestName);
    Serial.print("[StorageManager] Boot recovery: trying RecoveryPoint ");
    Serial.println(recoveryDir);
    if (!loadCurrentSetFromDirectory(recoveryDir, state)) {
        return false;
    }
    forceCurrentSetFullLoopWrite = true;
    markAllCurrentSetLoopSlotsDirtyInternal(false);
    requestDeferredSaveState(state);
    return true;
}

bool StorageManager::tryLoadNewestSavedSet(LooperState& state) {
    if (!SD.exists(CurrentSetStorage::kSetsRoot)) {
        return false;
    }
    File dir = SD.open(CurrentSetStorage::kSetsRoot);
    if (!dir) {
        return false;
    }
    char newestName[32] = {};
    uint32_t newestSequence = 0;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        entry.close();
        uint32_t sequence = 0;
        if (!isDirectory || !SavedSetCatalog::parseSavedSetFolderName(name, sequence, nullptr)) {
            continue;
        }
        if (newestName[0] == '\0' || sequence > newestSequence) {
            newestSequence = sequence;
            std::snprintf(newestName, sizeof(newestName), "%s", name);
        }
    }
    dir.close();
    if (newestName[0] == '\0') {
        return false;
    }
    char savedSetDir[48];
    std::snprintf(savedSetDir, sizeof(savedSetDir), "%s/%s", CurrentSetStorage::kSetsRoot,
                  newestName);
    Serial.print("[StorageManager] Boot recovery: trying SavedSet ");
    Serial.println(savedSetDir);
    if (!loadCurrentSetFromDirectory(savedSetDir, state)) {
        return false;
    }
    forceCurrentSetFullLoopWrite = true;
    markAllCurrentSetLoopSlotsDirtyInternal(false);
    requestDeferredSaveState(state);
    return true;
}

bool StorageManager::attemptBootRecoveryChain(LooperState& state) {
    if (tryLoadLatestRecoveryPoint(state)) {
        Serial.println("[StorageManager] Boot recovered from RecoveryPoint.");
        return true;
    }
    if (tryLoadNewestSavedSet(state)) {
        Serial.println("[StorageManager] Boot recovered from newest SavedSet.");
        return true;
    }
    Serial.println("[StorageManager] Boot recovery chain exhausted; starting empty.");
    return false;
}

bool StorageManager::loadState(LooperState& state) {
    RtcTime::init();
    if (SD.exists(CurrentSetStorage::kCurrentMetaPath)) {
        if (loadCurrentSetFromSd(state)) {
            SavedSetCatalog::SetIndex index{};
            if (!reconcileSetIndexOnSd(index)) {
                Serial.println("[StorageManager] WARN: could not reconcile Sets/index.bin");
            }
            forceCurrentSetFullLoopWrite = false;
            syncCurrentSetDirtyTrackingFromLoadedState();
            return true;
        }
        Serial.println("[StorageManager] CurrentSet load failed; attempting boot recovery chain.");
        resetTracksAfterFailedLoad();
        if (attemptBootRecoveryChain(state)) {
            return true;
        }
        return false;
    }
    if (SD.exists(STORAGE_FILENAME)) {
        return migrateV5MonolithToCurrentSet(state);
    }
    return attemptBootRecoveryChain(state);
}
