//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "TrackManager.h"
#include "Loop.h"
#include "Slot.h"
#include "StorageLoopIo.h"
#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "PersistenceBudget.h"
#include "SetRevisionCatalog.h"
#include "RevisionPackedBlob.h"
#include "RevisionCommitPolicy.h"
#include "RevisionLoadPolicy.h"
#include "SetBrowserOverlayPolicy.h"
#include "BootRecoveryPolicy.h"
#include "PersistenceSchema.h"
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

static bool writeGlobalUndoStack(File& file, const GlobalUndoStack& stack) {
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

static void applyLoadedTrackStateAfterLoopSlots(Track& track, TrackState loadedTrackState,
                                                bool anySlotHasEvents, bool muted);
static bool loadLoopSlotFromCurrentSetSd(uint8_t trackIndex, uint8_t slotIndex, Track& track,
                                         bool& anySlotHasEventsOut);
static bool readCurrentSetTrackSlotMetadata(File& file, uint8_t trackIndex, Track& track,
                                            TrackState& loadedTrackStateOut, bool& mutedOut);
static bool readCurrentSetFilePreamble(File& file, LooperState& loadedLooperStateOut,
                                       uint32_t& masterLoopLengthOut, uint8_t& numTracksOut);
static bool readCurrentSetFileEpilogue(File& file, uint8_t numTracks,
                                       std::vector<uint8_t>& activeLoopIndex,
                                       uint8_t& selectedTrackIdxOut);
static bool applyLoadedTransportFooter(uint8_t numTracks, const std::vector<uint8_t>& activeLoopIndex,
                                       uint8_t selectedTrackIdx, LooperState& state,
                                       LooperState loadedLooperState, uint32_t masterLoopLength);

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

enum class LoopPersistPayloadCrc : uint8_t {
    None = 0,
    RevisionCommit,
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
// Full payload rewrite only for explicit migration/recovery/repair paths:
// migrateV5MonolithToCurrentSet, tryLoadLatestRecoveryPoint, tryLoadNewestSavedSet.
// Normal runtime uses per-slot dirty bitmap + shouldWriteCurrentSetLoopSlot().
bool forceCurrentSetFullLoopWrite = true;
std::array<std::array<bool, Config::MAX_LOOPS_PER_TRACK>, Config::NUM_TRACKS>
    currentSetLoopSlotDirty{};
uint32_t lastSavedSetFailsafeCheckAtMs = 0;
uint32_t currentSetLastActiveUnix = 0;
char currentSetLoadedFromFolder[16] = {};
uint32_t currentWorkspaceEpoch = 0;
uint32_t lastCommittedWorkspaceEpoch = 0;
uint16_t workspaceDerivedFromSetId = 0;
uint16_t workspaceDerivedFromRevisionId = 0;
uint16_t workspaceLastCommittedRevisionId = 0;
uint32_t deferredSaveWorkspaceEpoch = 0;
char autoSaveBeforeLoadFolderPending[16] = {};
bool autoSaveBeforeLoadFolderPendingValid = false;

enum class RevisionCommitStage : uint8_t {
    Idle = 0,
    Snapshot,
    Write,
    Validate,
    CatalogUpdate,
    Complete,
};

static_assert(!RevisionCommitPolicy::kWritePathUsesLoopPassesMaterialize,
              "revision commit WRITE must not call LoopPasses::materialize");
static_assert(RevisionCommitPolicy::kLoopSlotBodyUsesStorageLoopIoStream,
              "revision commit LoopSlot bodies must stream via StorageLoopIo");

enum class RevisionWriteStage : uint8_t {
    PrepareLayout = 0,
    OpenTempFile,
    WriteHeader,
    WriteTransportChunk,
    WriteLoopSlotChunks,
    WriteSlotIndexChunk,
    WriteFooter,
};

bool revisionCommitPending = false;
bool revisionCommitInProgress = false;
bool revisionCommitSdIoActive = false;
RevisionCommitStage revisionCommitStage = RevisionCommitStage::Idle;
RevisionWriteStage revisionWriteStage = RevisionWriteStage::PrepareLayout;
uint32_t revisionCommitSourceEpoch = 0;
uint32_t revisionCommitWorkspaceEpochBeforeSnapshot = 0;
uint16_t revisionCommitSetId = 0;
uint16_t revisionCommitPendingRevisionId = 0;
char revisionCommitTempPath[80] = {};
char revisionCommitFinalPath[80] = {};
File revisionCommitFile;
SetRevisionCatalog::SetCatalogIndex revisionCommitCatalogIndex{};
SetRevisionCatalog::SetMetaRecord revisionCommitSetMeta{};
RevisionPackedBlob::RevisionHeader revisionCommitHeader{};
constexpr uint16_t kMaxRevisionLoopIndexEntries =
    static_cast<uint16_t>(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK);
RevisionPackedBlob::RevisionLoopSlotDirectoryEntry revisionCommitSlotEntries[kMaxRevisionLoopIndexEntries];
RevisionPackedBlob::RevisionLoopSlotDirectoryEntry
    revisionLoadSlotDirectoryEntries[kMaxRevisionLoopIndexEntries];
uint16_t revisionCommitSlotIndexCount = 0;
uint16_t revisionCommitSlotIndexWriteCursor = 0;
uint32_t revisionCommitPayloadWriteOffset = 0;
uint16_t revisionCommitChunkCount = 0;
uint8_t revisionCommitCopyTrackCursor = 0;
uint8_t revisionCommitCopySlotCursor = 0;
uint32_t revisionCommitRuntimeBundleSize = 0;
uint32_t revisionCommitRuntimeBundleReadPos = 0;
uint32_t revisionCommitSlotReadPos = 0;
uint32_t revisionCommitSlotBodyRemaining = 0;
bool revisionCommitLoopSlotBodyActive = false;
File revisionCommitSourceFile;
bool revisionCommitSourceFileOpen = false;
std::array<uint8_t, 512> revisionCommitCopyBuffer{};
uint32_t revisionCommitPayloadCrc = 0;
bool revisionCommitPayloadCrcSeeded = false;
bool revisionCommitAllocatedNewSet = false;
bool revisionCommitSlotIndexChunkWritten = false;
uint32_t lastRevisionCommitBlockedLogAtMs = 0;

enum class RevisionLoadStage : uint8_t {
    Idle = 0,
    Validate,
    Write,
    ReloadRam,
    Complete,
};

enum class RevisionLoadWriteStage : uint8_t {
    PrepareEpoch = 0,
    OpenMetaTemp,
    CopyTransportBody,
    FinalizeMetaTemp,
    WriteLoopSlots,
};

enum class RevisionLoadReloadRamStage : uint8_t {
    WriteWorkspaceMeta = 0,
    ReadMetaHeaders,
    LoadLoopSlot,
    ReadFooter,
};

bool revisionLoadPending = false;
bool revisionLoadInProgress = false;
bool revisionLoadSdIoActive = false;
RevisionLoadStage revisionLoadStage = RevisionLoadStage::Idle;
RevisionLoadWriteStage revisionLoadWriteStage = RevisionLoadWriteStage::PrepareEpoch;
uint16_t revisionLoadSetId = 0;
uint16_t revisionLoadRevisionId = 0;
char revisionLoadSourcePath[80] = {};
RevisionPackedBlob::RevisionHeader revisionLoadHeader{};
uint16_t revisionLoadSlotIndexCount = 0;
uint32_t revisionLoadWorkspaceEpoch = 0;
uint32_t revisionLoadTransportFileOffset = 0;
uint32_t revisionLoadTransportBodySize = 0;
uint32_t revisionLoadTransportReadPos = 0;
uint8_t revisionLoadCopyTrackCursor = 0;
uint8_t revisionLoadCopySlotCursor = 0;
uint32_t revisionLoadSlotBodyRemaining = 0;
uint32_t revisionLoadSlotReadPos = 0;
File revisionLoadSourceFile;
bool revisionLoadSourceFileOpen = false;
File revisionLoadDestFile;
bool revisionLoadDestFileOpen = false;
bool revisionLoadWritingEmptySlot = false;
DeferredLoopWriteStage revisionLoadLoopWriteStage = DeferredLoopWriteStage::Header;
uint32_t lastRevisionLoadBlockedLogAtMs = 0;
bool revisionLoadUsedDefaultTransport = false;
bool revisionLoadDisplayRefreshPending = false;
bool bootRevisionRecoveryPending = false;
uint16_t bootRevisionRecoverySetId = 0;
uint16_t bootRevisionRecoveryRevisionId = 0;
bool revisionLoadRequestStaged = false;
uint16_t revisionLoadStagedSetId = 0;
uint16_t revisionLoadStagedRevisionId = 0;
bool revisionLoadDirtyPromptActive = false;
uint8_t revisionLoadDirtyPromptSelection = 0;
bool revisionLoadPipelineActive = false;
bool revisionLoadSaveThenLoadPipeline = false;
SetBrowserOverlayPolicy::NavigationState setBrowserOverlayNavigation{};
RevisionLoadReloadRamStage revisionLoadReloadRamStage = RevisionLoadReloadRamStage::WriteWorkspaceMeta;
File revisionLoadReloadMetaFile;
bool revisionLoadReloadMetaFileOpen = false;
uint8_t revisionLoadReloadTrackCursor = 0;
uint8_t revisionLoadReloadSlotCursor = 0;
uint8_t revisionLoadReloadNumTracks = 0;
bool revisionLoadReloadAnySlotHasEvents[Config::NUM_TRACKS] = {};
TrackState revisionLoadReloadLoadedTrackState[Config::NUM_TRACKS] = {};
bool revisionLoadReloadMuted[Config::NUM_TRACKS] = {};
std::vector<uint8_t> revisionLoadReloadActiveLoopIndex;
uint8_t revisionLoadReloadSelectedTrackIdx = 0;
LooperState revisionLoadReloadLooperState = LOOPER_IDLE;
uint32_t revisionLoadReloadMasterLoopLength = 0;

#if defined(SESSION_CAPTURE)
struct HitlRevisionCommitBackup {
    bool armed = false;
    bool hadIndexOnSd = false;
    SetRevisionCatalog::SetCatalogIndex catalogIndex{};
    bool hadSetMetaOnSd = false;
    SetRevisionCatalog::SetMetaRecord setMeta{};
    uint32_t workspaceCurrentEpoch = 0;
    uint32_t workspaceLastCommittedEpoch = 0;
    uint16_t workspaceDerivedFromSetId = 0;
    uint16_t workspaceDerivedFromRevisionId = 0;
    uint16_t workspaceLastCommittedRevisionId = 0;
    uint16_t committedSetId = 0;
    uint16_t committedRevisionId = 0;
    bool createdNewSetFolder = false;
    char revisionFinalPath[80] = {};
    char setFolderPath[48] = {};
};

HitlRevisionCommitBackup hitlRevisionCommitBackup{};
#endif

constexpr size_t kSavedSetPathCapacity = 64;
constexpr uint32_t kSavedSetFailsafeCheckIntervalMs = 1000;

void clearCurrentSetLoadedFromFolder() {
    currentSetLoadedFromFolder[0] = '\0';
}

bool setCurrentSetLoadedFromFolder(const char* folderName) {
    if (folderName == nullptr || folderName[0] == '\0') {
        clearCurrentSetLoadedFromFolder();
        return false;
    }
    const int written = std::snprintf(currentSetLoadedFromFolder,
                                      sizeof(currentSetLoadedFromFolder), "%s", folderName);
    if (written <= 0 ||
        static_cast<size_t>(written) >= sizeof(currentSetLoadedFromFolder)) {
        clearCurrentSetLoadedFromFolder();
        return false;
    }
    return true;
}

void clearAutoSaveBeforeLoadFolderPending() {
    autoSaveBeforeLoadFolderPending[0] = '\0';
    autoSaveBeforeLoadFolderPendingValid = false;
}

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

static void resetLoopSlotToEmpty(Loop& loop, uint8_t slotIndex) {
    loop.discardPendingCapturePass();
    loop.discardCapture();
    loop.resetPassTimeline();
    loop.loopId = static_cast<LoopId>(slotIndex);
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
            resetLoopSlotToEmpty(track.getLoop(s), s);
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
        !writeRaw(file, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        file.close();
        return false;
    }
    file.close();
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kSetIndexTempPath)) {
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
    uint32_t svokToken = 0;
    const bool ok = readRaw(file, &svokToken, sizeof(svokToken)) &&
                    svokToken == CurrentSetStorage::kSaveFileToken;
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
    const int written = std::snprintf(out, outSize, "%s/%s", CurrentSetStorage::kSetsArchiveDir,
                                      folderName);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

bool resolveSavedSetFolderNameBySequence(uint32_t sequence, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || sequence == 0 ||
        !SD.exists(CurrentSetStorage::kSetsArchiveDir)) {
        return false;
    }
    File dir = SD.open(CurrentSetStorage::kSetsArchiveDir);
    if (!dir) {
        return false;
    }
    bool found = false;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        entry.close();
        uint32_t parsedSequence = 0;
        if (!isDirectory ||
            !SavedSetCatalog::parseSavedSetFolderName(name, parsedSequence, nullptr) ||
            parsedSequence != sequence) {
            continue;
        }
        const char* baseName = std::strrchr(name, '/');
        if (baseName != nullptr) {
            baseName += 1;
        } else {
            baseName = name;
        }
        const int written = std::snprintf(out, outSize, "%s", baseName);
        found = written > 0 && static_cast<size_t>(written) < outSize;
        break;
    }
    dir.close();
    return found;
}

uint32_t scanHighestSavedSetSequenceOnSd() {
    if (!SD.exists(CurrentSetStorage::kSetsArchiveDir)) {
        return 0;
    }
    File dir = SD.open(CurrentSetStorage::kSetsArchiveDir);
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
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kCurrentMetaPath)) {
        return false;
    }
    File source = SD.open(CurrentSetStorage::kCurrentMetaPath, FILE_READ);
    if (!source) {
        return false;
    }
    const size_t sourceSize = source.size();
    if (sourceSize < sizeof(CurrentSetStorage::kSaveFileToken)) {
        source.close();
        return false;
    }
    const size_t payloadSize = sourceSize - sizeof(CurrentSetStorage::kSaveFileToken);

    char destinationTempPath[kSavedSetPathCapacity];
    char destinationPath[kSavedSetPathCapacity];
    if (std::snprintf(destinationTempPath, sizeof(destinationTempPath), "%s/%s",
                      savedSetDir, CurrentSetStorage::kSetBinTempFileName) <= 0 ||
        std::snprintf(destinationPath, sizeof(destinationPath), "%s/%s", savedSetDir,
                      CurrentSetStorage::kSetBinFileName) <= 0) {
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
             writeRaw(destination, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken));
    }
    source.close();
    destination.close();
    if (!ok) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(destinationTempPath)) {
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
    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kSetsRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kSetsArchiveDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir)) {
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
    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir)) {
        return false;
    }

    char sourceMetaPath[kSavedSetPathCapacity];
    if (std::snprintf(sourceMetaPath, sizeof(sourceMetaPath), "%s/%s", sourceSetDir,
                      CurrentSetStorage::kSetBinFileName) <= 0) {
        return false;
    }
    if (!copyFileBinary(sourceMetaPath, CurrentSetStorage::kCurrentMetaTempPath)) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kCurrentMetaTempPath) ||
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
                !CurrentSetStorage::verifySaveFileTokenAtPath(destinationTempPath) ||
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

void fillSlotSummariesForTrack(uint8_t trackIndex, const Track& track,
                               CurrentWorkspaceStorage::SlotSummary* summaries,
                               size_t summaryCount) {
    if (summaries == nullptr || summaryCount == 0) {
        return;
    }
    const uint32_t ticksPerBar = Track::getTicksPerBar();
    const uint8_t slotLimit = static_cast<uint8_t>(
        summaryCount < Config::MAX_LOOPS_PER_TRACK ? summaryCount : Config::MAX_LOOPS_PER_TRACK);
    for (uint8_t slot = 0; slot < slotLimit; ++slot) {
        const Loop& loop = track.getLoop(slot);
        CurrentWorkspaceStorage::SlotSummary& summary = summaries[slot];
        summary = CurrentWorkspaceStorage::SlotSummary{};
        summary.muted = trackManager.isSlotMuted(trackIndex, slot) ? 1 : 0;
        const bool occupied = loop.passes.hasRecordPass() || !loop.passes.overdubPasses.empty() ||
                              loop.hasPendingCapturePass();
        summary.occupied = occupied ? 1 : 0;
        if (!occupied || loop.loopLengthTicks == 0 || ticksPerBar == 0) {
            continue;
        }
        size_t eventCount = 0;
        if (loop.passes.hasRecordPass()) {
            eventCount +=
                LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.chunkRefs);
        }
        for (const OverdubPass& pass : loop.passes.overdubPasses) {
            eventCount += LoopEventStore::countEventsInChunkIds(pass.chunkRefs);
        }
        summary.noteCount =
            static_cast<uint16_t>(eventCount > UINT16_MAX ? UINT16_MAX : eventCount / 2U);
        summary.bars = static_cast<uint16_t>(loop.loopLengthTicks / ticksPerBar);
    }
}

void fillSlotSummariesForTrack(uint8_t trackIndex, const Track& track,
                               CurrentWorkspaceStorage::SlotSummary* summaries,
                               size_t summaryCount);

bool writeWorkspaceMetaAfterDeferredSave();
bool writeCurrentSetMetaHeaderToOpenFile(File& file);

bool stepDeferredLoopPersist(File& file, const Loop& loop, bool& loopDone,
                             LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None);

void clearRevisionLoadPromptAndPipelineState();
void dispatchStagedRevisionLoad();

void resetRevisionCommitJobState() {
    if (revisionCommitWorkspaceEpochBeforeSnapshot != 0) {
        currentWorkspaceEpoch = revisionCommitWorkspaceEpochBeforeSnapshot;
        revisionCommitWorkspaceEpochBeforeSnapshot = 0;
    }
    if (revisionCommitFile) {
        revisionCommitFile.close();
    }
    if (revisionCommitSourceFileOpen) {
        revisionCommitSourceFile.close();
        revisionCommitSourceFileOpen = false;
    }
    revisionCommitInProgress = false;
    revisionCommitSdIoActive = false;
    revisionCommitStage = RevisionCommitStage::Idle;
    revisionWriteStage = RevisionWriteStage::PrepareLayout;
    revisionCommitSourceEpoch = 0;
    revisionCommitSetId = 0;
    revisionCommitPendingRevisionId = 0;
    revisionCommitTempPath[0] = '\0';
    revisionCommitFinalPath[0] = '\0';
    revisionCommitSlotIndexCount = 0;
    revisionCommitSlotIndexWriteCursor = 0;
    revisionCommitPayloadWriteOffset = 0;
    revisionCommitChunkCount = 0;
    revisionCommitCopyTrackCursor = 0;
    revisionCommitCopySlotCursor = 0;
    revisionCommitRuntimeBundleSize = 0;
    revisionCommitRuntimeBundleReadPos = 0;
    revisionCommitSlotReadPos = 0;
    revisionCommitSlotBodyRemaining = 0;
    revisionCommitLoopSlotBodyActive = false;
    revisionCommitPayloadCrc = 0;
    revisionCommitPayloadCrcSeeded = false;
    revisionCommitAllocatedNewSet = false;
    revisionCommitSlotIndexChunkWritten = false;
    lastRevisionCommitBlockedLogAtMs = 0;
    resetDeferredLoopWriteState();
    revisionCommitHeader = RevisionPackedBlob::RevisionHeader{};
    revisionCommitCatalogIndex = SetRevisionCatalog::SetCatalogIndex{};
    revisionCommitSetMeta = SetRevisionCatalog::SetMetaRecord{};
    for (uint16_t i = 0; i < kMaxRevisionLoopIndexEntries; ++i) {
        revisionCommitSlotEntries[i] = RevisionPackedBlob::RevisionLoopSlotDirectoryEntry{};
    }
    if (revisionLoadSaveThenLoadPipeline) {
        clearRevisionLoadPromptAndPipelineState();
    }
}

bool isCaptureActiveForPersistence() {
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        const Track& track = trackManager.getTrack(trackIndex);
        if (track.isRecording() || track.isOverdubbing()) {
            return true;
        }
    }
    return false;
}

uint32_t resolveMaxPersistenceMicros(const LooperState& state) {
    return PersistenceBudget::resolveMaxPersistenceMicros(
        isCaptureActiveForPersistence(),
        state == LOOPER_PLAYING || state == LOOPER_OVERDUBBING || state == LOOPER_RECORDING);
}

uint32_t resolvePersistenceSliceBudgetUs(const LooperState& state) {
    return PersistenceBudget::resolvePersistenceSliceBudgetUs(
        isCaptureActiveForPersistence(),
        state == LOOPER_PLAYING || state == LOOPER_OVERDUBBING || state == LOOPER_RECORDING);
}

uint32_t resolveRevisionCommitSourceEpoch() {
    return CurrentWorkspaceStorage::resolveCompletedWorkspaceEpochForRevisionSnapshot(
        currentWorkspaceEpoch, deferredSaveWorkspaceEpoch, deferredSaveInProgress);
}

/// Loop slots are written only when dirty; unchanged slots keep an older epoch on SD but still
/// belong to the workspace snapshot at sourceEpoch. Include any non-empty slot with epoch <= max.
bool slotSourceFileReadableForRevisionCommit(const char* path, uint32_t maxEpoch,
                                             uint32_t& bodySizeOut) {
    bodySizeOut = 0;
    if (path == nullptr || !SD.exists(path)) {
        return false;
    }
    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    size_t payloadOffset = 0;
    if (CurrentWorkspaceStorage::fileStartsWithEpochHeader(file)) {
        CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
        const StorageIo epochIo = storageIoFromFileRead(file);
        if (!CurrentWorkspaceStorage::readEpochFileHeader(epochIo, epochHeader)) {
            file.close();
            return false;
        }
        if (epochHeader.epoch > maxEpoch) {
            file.close();
            return false;
        }
        payloadOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
    } else if (maxEpoch != 0) {
        file.close();
        return false;
    }
    if (fileSize < payloadOffset + sizeof(CurrentSetStorage::kSaveFileToken)) {
        file.close();
        return false;
    }
    bodySizeOut = static_cast<uint32_t>(fileSize - payloadOffset - sizeof(CurrentSetStorage::kSaveFileToken));
    file.close();
    if (bodySizeOut == 0) {
        return false;
    }
    return CurrentSetStorage::verifySaveFileTokenAtPath(path);
}

bool prepareRevisionCommitLayout() {
    revisionCommitSlotIndexCount = 0;
    revisionCommitRuntimeBundleSize = 0;
    uint32_t bundleBodySize = 0;
    if (slotSourceFileReadableForRevisionCommit(CurrentSetStorage::kCurrentMetaPath,
                                                revisionCommitSourceEpoch, bundleBodySize)) {
        revisionCommitRuntimeBundleSize = bundleBodySize;
    }

    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            const Loop& loop = trackManager.getTrack(trackIndex).getLoop(slotIndex);
            if (!loop.hasPublishedEvents() && !loop.passes.hasRecordPass() &&
                loop.passes.overdubPasses.empty()) {
                continue;
            }
            const uint32_t bodySize = measureLoopSlotFileBytes(loop);
            if (bodySize == 0) {
                continue;
            }
            if (revisionCommitSlotIndexCount >= kMaxRevisionLoopIndexEntries) {
                return false;
            }
            RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
                revisionCommitSlotEntries[revisionCommitSlotIndexCount];
            entry = RevisionPackedBlob::RevisionLoopSlotDirectoryEntry{};
            entry.trackIndex = trackIndex;
            entry.slotIndex = slotIndex;
            entry.occupied = 1;
            entry.bodyLength = bodySize;
            entry.loopLengthTicks = loop.loopLengthTicks;
            const uint32_t ticksPerBar = Track::getTicksPerBar();
            if (loop.loopLengthTicks > 0 && ticksPerBar > 0) {
                entry.bars = static_cast<uint16_t>(loop.loopLengthTicks / ticksPerBar);
            }
            size_t eventCount = 0;
            if (loop.passes.hasRecordPass()) {
                eventCount +=
                    LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.chunkRefs);
            }
            for (const OverdubPass& pass : loop.passes.overdubPasses) {
                eventCount += LoopEventStore::countEventsInChunkIds(pass.chunkRefs);
            }
            entry.noteCount =
                static_cast<uint16_t>(eventCount > UINT16_MAX ? UINT16_MAX : eventCount / 2U);
            ++revisionCommitSlotIndexCount;
        }
    }
    return true;
}

bool beginRevisionCommitSnapshot() {
    revisionCommitSourceEpoch = resolveRevisionCommitSourceEpoch();
    if (revisionCommitSourceEpoch == 0) {
        Serial.println("[StorageManager] ERROR: Revision commit has no completed source epoch");
        return false;
    }

    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(SetRevisionCatalog::kSetsRoot)) {
        return false;
    }

    File indexFile = SD.open(SetRevisionCatalog::kSetIndexPath, FILE_READ);
    if (indexFile) {
        const StorageIo indexIo = storageIoFromFileRead(indexFile);
        (void)SetRevisionCatalog::readSetCatalogIndex(indexIo, revisionCommitCatalogIndex);
        indexFile.close();
    }

    revisionCommitSetId = workspaceDerivedFromSetId;
    revisionCommitAllocatedNewSet = false;
    if (revisionCommitSetId == 0) {
        revisionCommitSetId = SetRevisionCatalog::allocateNextSetId(revisionCommitCatalogIndex);
        revisionCommitAllocatedNewSet = true;
        revisionCommitSetMeta = SetRevisionCatalog::SetMetaRecord{};
        revisionCommitSetMeta.setId = revisionCommitSetId;
    } else {
        char setMetaPath[64];
        if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                                   revisionCommitSetId)) {
            return false;
        }
        File setMetaFile = SD.open(setMetaPath, FILE_READ);
        if (!setMetaFile) {
            Serial.println(
                "[StorageManager] WARN: Derived set missing on SD; allocating new set for commit");
            revisionCommitSetId = SetRevisionCatalog::allocateNextSetId(revisionCommitCatalogIndex);
            revisionCommitAllocatedNewSet = true;
            revisionCommitSetMeta = SetRevisionCatalog::SetMetaRecord{};
            revisionCommitSetMeta.setId = revisionCommitSetId;
        } else {
            const StorageIo setMetaIo = storageIoFromFileRead(setMetaFile);
            const bool metaOk =
                SetRevisionCatalog::readSetMetaRecord(setMetaIo, revisionCommitSetMeta);
            setMetaFile.close();
            if (!metaOk) {
                Serial.println(
                    "[StorageManager] WARN: Derived set meta invalid; allocating new set for commit");
                revisionCommitSetId =
                    SetRevisionCatalog::allocateNextSetId(revisionCommitCatalogIndex);
                revisionCommitAllocatedNewSet = true;
                revisionCommitSetMeta = SetRevisionCatalog::SetMetaRecord{};
                revisionCommitSetMeta.setId = revisionCommitSetId;
            }
        }
    }

    revisionCommitPendingRevisionId =
        SetRevisionCatalog::peekNextRevisionId(revisionCommitSetMeta);

    char setFolderPath[48];
    char revisionsDirPath[56];
    if (!SetRevisionCatalog::formatSetFolderPath(setFolderPath, sizeof(setFolderPath),
                                                 revisionCommitSetId) ||
        !SetRevisionCatalog::formatRevisionPath(revisionCommitTempPath,
                                                sizeof(revisionCommitTempPath),
                                                revisionCommitSetId,
                                                revisionCommitPendingRevisionId, true) ||
        !SetRevisionCatalog::formatRevisionPath(revisionCommitFinalPath,
                                                sizeof(revisionCommitFinalPath),
                                                revisionCommitSetId,
                                                revisionCommitPendingRevisionId, false)) {
        return false;
    }
    const int revisionsWritten = std::snprintf(
        revisionsDirPath, sizeof(revisionsDirPath), "%s/revisions", setFolderPath);
    if (revisionsWritten <= 0 ||
        static_cast<size_t>(revisionsWritten) >= sizeof(revisionsDirPath)) {
        return false;
    }
    if (!CurrentSetStorage::ensureDirectory(setFolderPath) ||
        !CurrentSetStorage::ensureDirectory(revisionsDirPath)) {
        return false;
    }

    revisionCommitHeader = RevisionPackedBlob::RevisionHeader{};
    std::memcpy(revisionCommitHeader.magic, RevisionPackedBlob::kRevisionMagic,
                sizeof(revisionCommitHeader.magic));
    revisionCommitHeader.revisionId = 0;
    revisionCommitHeader.setId = revisionCommitSetId;
    revisionCommitHeader.sourceEpoch = revisionCommitSourceEpoch;
    revisionCommitHeader.createdUnix = static_cast<uint64_t>(RtcTime::getUnixTime());

    if (!prepareRevisionCommitLayout()) {
        return false;
    }

    revisionCommitWorkspaceEpochBeforeSnapshot = currentWorkspaceEpoch;
    currentWorkspaceEpoch = CurrentWorkspaceStorage::workspaceEpochAfterRevisionSnapshot(
        revisionCommitSourceEpoch);

    revisionWriteStage = RevisionWriteStage::OpenTempFile;
    revisionCommitStage = RevisionCommitStage::Write;
    return true;
}

bool appendRevisionCommitPayloadCrc(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return true;
    }
    if (!revisionCommitPayloadCrcSeeded) {
        revisionCommitPayloadCrc = PersistenceSchema::crc32(data, size);
        revisionCommitPayloadCrcSeeded = true;
    } else {
        revisionCommitPayloadCrc =
            PersistenceSchema::crc32Continue(revisionCommitPayloadCrc, data, size);
    }
    return true;
}

bool persistenceWriteRaw(File& file, const void* data, size_t size,
                           LoopPersistPayloadCrc crcMode) {
    if (!writeRaw(file, data, size)) {
        return false;
    }
    if (crcMode == LoopPersistPayloadCrc::RevisionCommit) {
        if (!appendRevisionCommitPayloadCrc(static_cast<const uint8_t*>(data), size)) {
            return false;
        }
        revisionCommitPayloadWriteOffset += static_cast<uint32_t>(size);
        return true;
    }
    return true;
}

StorageIo storageIoFromFileWriteWithRevisionPayloadCrc(File& file) {
    return StorageIo{
        [&file](const void* data, size_t size) -> bool {
            return persistenceWriteRaw(file, data, size, LoopPersistPayloadCrc::RevisionCommit);
        },
        nullptr,
    };
}

bool computeRevisionCommitPayloadCrcFromFile(File& file, uint32_t payloadOffset,
                                             uint32_t payloadSize, uint32_t& crcOut) {
    if (payloadSize == 0) {
        crcOut = 0;
        return true;
    }
    if (!file.seek(payloadOffset)) {
        return false;
    }
    uint32_t remaining = payloadSize;
    uint32_t crc = 0;
    bool seeded = false;
    while (remaining > 0) {
        const size_t chunkSize =
            remaining > revisionCommitCopyBuffer.size() ? revisionCommitCopyBuffer.size() : remaining;
        const int bytesRead =
            file.read(revisionCommitCopyBuffer.data(), static_cast<size_t>(chunkSize));
        if (bytesRead <= 0) {
            return false;
        }
        const size_t written = static_cast<size_t>(bytesRead);
        if (!seeded) {
            crc = PersistenceSchema::crc32(revisionCommitCopyBuffer.data(), written);
            seeded = true;
        } else {
            crc = PersistenceSchema::crc32Continue(crc, revisionCommitCopyBuffer.data(), written);
        }
        remaining -= static_cast<uint32_t>(written);
    }
    crcOut = crc;
    return true;
}

bool writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType type, uint8_t trackIndex,
                                    uint8_t slotIndex, uint32_t bodyLength) {
    RevisionPackedBlob::ChunkHeader chunkHeader{};
    chunkHeader.type = static_cast<uint8_t>(type);
    chunkHeader.trackIndex = trackIndex;
    chunkHeader.slotIndex = slotIndex;
    chunkHeader.bodyLength = bodyLength;
    const StorageIo io = storageIoFromFileWrite(revisionCommitFile);
    if (!RevisionPackedBlob::writeChunkHeader(io, chunkHeader)) {
        return false;
    }
    uint8_t chunkHeaderBytes[RevisionPackedBlob::kChunkHeaderByteSize];
    if (!RevisionPackedBlob::revisionChunkHeaderFileBytes(chunkHeader, chunkHeaderBytes, sizeof(chunkHeaderBytes))) {
        return false;
    }
    if (!appendRevisionCommitPayloadCrc(chunkHeaderBytes, sizeof(chunkHeaderBytes))) {
        return false;
    }
    revisionCommitPayloadWriteOffset +=
        static_cast<uint32_t>(RevisionPackedBlob::kChunkHeaderByteSize);
    return true;
}

bool writeRevisionCommitSlotIndexChunkFromFooter() {
    if (revisionCommitSlotIndexChunkWritten) {
        return true;
    }
    if (!revisionCommitFile) {
        return false;
    }
    const uint32_t slotIndexBodySize = static_cast<uint32_t>(
        RevisionPackedBlob::slotIndexChunkBodySize(revisionCommitSlotIndexCount));
    if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                        slotIndexBodySize)) {
        return false;
    }
    ++revisionCommitChunkCount;
    const uint16_t entryCount = revisionCommitSlotIndexCount;
    const uint16_t reservedPrefix = 0;
    if (revisionCommitFile.write(reinterpret_cast<const uint8_t*>(&entryCount),
                                 sizeof(entryCount)) != sizeof(entryCount) ||
        revisionCommitFile.write(reinterpret_cast<const uint8_t*>(&reservedPrefix),
                                 sizeof(reservedPrefix)) != sizeof(reservedPrefix)) {
        return false;
    }
    uint8_t slotIndexPrefixBytes[RevisionPackedBlob::kSlotIndexBodyPrefixByteSize];
    std::memcpy(slotIndexPrefixBytes, &entryCount, sizeof(entryCount));
    std::memcpy(slotIndexPrefixBytes + sizeof(entryCount), &reservedPrefix, sizeof(reservedPrefix));
    if (!appendRevisionCommitPayloadCrc(slotIndexPrefixBytes, sizeof(slotIndexPrefixBytes))) {
        return false;
    }
    revisionCommitPayloadWriteOffset +=
        static_cast<uint32_t>(RevisionPackedBlob::kSlotIndexBodyPrefixByteSize);
    const StorageIo io = storageIoFromFileWrite(revisionCommitFile);
    for (uint16_t i = 0; i < revisionCommitSlotIndexCount; ++i) {
        if (!RevisionPackedBlob::writeRevisionLoopSlotDirectoryEntry(io, revisionCommitSlotEntries[i])) {
            return false;
        }
        uint8_t directoryEntryBytes[RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize];
        if (!RevisionPackedBlob::revisionLoopSlotDirectoryEntryFileBytes(revisionCommitSlotEntries[i], directoryEntryBytes,
                                                         sizeof(directoryEntryBytes))) {
            return false;
        }
        if (!appendRevisionCommitPayloadCrc(directoryEntryBytes, sizeof(directoryEntryBytes))) {
            return false;
        }
        revisionCommitPayloadWriteOffset +=
            static_cast<uint32_t>(RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize);
    }
    revisionCommitSlotIndexWriteCursor = revisionCommitSlotIndexCount;
    revisionCommitSlotIndexChunkWritten = true;
    return true;
}

bool copyRevisionCommitChunk(File& dest, File& src, uint32_t& readPos, uint32_t& bytesRemaining,
                             uint32_t chunkSize) {
    if (bytesRemaining == 0) {
        return true;
    }
    const uint32_t toRead = bytesRemaining < chunkSize ? bytesRemaining : chunkSize;
    if (!src.seek(readPos)) {
        return false;
    }
    const int bytesRead = src.read(revisionCommitCopyBuffer.data(), toRead);
    if (bytesRead <= 0) {
        return false;
    }
    const auto written = static_cast<size_t>(bytesRead);
    if (dest.write(revisionCommitCopyBuffer.data(), written) != bytesRead) {
        return false;
    }
    readPos += written;
    bytesRemaining -= static_cast<uint32_t>(written);
    if (written > 0 && revisionCommitInProgress) {
        if (!appendRevisionCommitPayloadCrc(revisionCommitCopyBuffer.data(), written)) {
            return false;
        }
        revisionCommitPayloadWriteOffset += static_cast<uint32_t>(written);
    }
    return true;
}

bool stepRevisionCommitWrite() {
    switch (revisionWriteStage) {
        case RevisionWriteStage::PrepareLayout:
            return false;

        case RevisionWriteStage::OpenTempFile:
            revisionCommitFile = SD.open(revisionCommitTempPath, FILE_WRITE);
            if (!revisionCommitFile) {
                Serial.println("[StorageManager] ERROR: Could not open revision temp file");
                return false;
            }
            revisionCommitFile.seek(0);
            revisionWriteStage = RevisionWriteStage::WriteHeader;
            return true;

        case RevisionWriteStage::WriteHeader: {
            revisionCommitPayloadCrc = 0;
            revisionCommitPayloadCrcSeeded = false;
            revisionCommitPayloadWriteOffset = 0;
            revisionCommitChunkCount = 0;
            const StorageIo io = storageIoFromFileWrite(revisionCommitFile);
            if (!RevisionPackedBlob::writeRevisionHeader(io, revisionCommitHeader)) {
                return false;
            }
            revisionWriteStage = RevisionWriteStage::WriteTransportChunk;
            return true;
        }

        case RevisionWriteStage::WriteTransportChunk:
            if (revisionCommitRuntimeBundleSize == 0) {
                Serial.println("[StorageManager] ERROR: Revision commit missing runtime bundle body");
                return false;
            }
            if (revisionCommitRuntimeBundleReadPos == 0) {
                if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::Transport, 0, 0,
                                                    revisionCommitRuntimeBundleSize)) {
                    return false;
                }
                ++revisionCommitChunkCount;
                revisionCommitRuntimeBundleReadPos =
                    CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
            }
            if (!revisionCommitSourceFileOpen) {
                revisionCommitSourceFile = SD.open(CurrentSetStorage::kCurrentMetaPath, FILE_READ);
                if (!revisionCommitSourceFile) {
                    return false;
                }
                revisionCommitSourceFileOpen = true;
            }
            {
                const uint32_t endPos = CurrentWorkspaceStorage::kEpochFileHeaderByteSize +
                                        revisionCommitRuntimeBundleSize;
                uint32_t remaining = endPos - revisionCommitRuntimeBundleReadPos;
                if (remaining > 0) {
                    if (!copyRevisionCommitChunk(
                            revisionCommitFile, revisionCommitSourceFile,
                            revisionCommitRuntimeBundleReadPos, remaining,
                            static_cast<uint32_t>(revisionCommitCopyBuffer.size()))) {
                        return false;
                    }
                    if (remaining > 0) {
                        return true;
                    }
                }
            }
            if (revisionCommitSourceFileOpen) {
                revisionCommitSourceFile.close();
                revisionCommitSourceFileOpen = false;
            }
            revisionCommitCopyTrackCursor = 0;
            revisionCommitCopySlotCursor = 0;
            revisionWriteStage = RevisionWriteStage::WriteLoopSlotChunks;
            return true;

        case RevisionWriteStage::WriteLoopSlotChunks:
            while (revisionCommitCopyTrackCursor < Config::NUM_TRACKS) {
                while (revisionCommitCopySlotCursor < Config::MAX_LOOPS_PER_TRACK) {
                    const uint8_t trackIndex = revisionCommitCopyTrackCursor;
                    const uint8_t slotIndex = revisionCommitCopySlotCursor;
                    uint16_t slotEntryIndex = 0;
                    bool foundEntry = false;
                    for (uint16_t i = 0; i < revisionCommitSlotIndexCount; ++i) {
                        if (revisionCommitSlotEntries[i].trackIndex == trackIndex &&
                            revisionCommitSlotEntries[i].slotIndex == slotIndex) {
                            slotEntryIndex = i;
                            foundEntry = true;
                            break;
                        }
                    }
                    if (!foundEntry) {
                        ++revisionCommitCopySlotCursor;
                        continue;
                    }
                    const Loop& loop = trackManager.getTrack(trackIndex).getLoop(slotIndex);
                    if (!revisionCommitLoopSlotBodyActive) {
                        revisionCommitSlotEntries[slotEntryIndex].chunkOffset =
                            revisionCommitPayloadWriteOffset;
                        if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::LoopSlot,
                                                            trackIndex, slotIndex,
                                                            revisionCommitSlotEntries[slotEntryIndex]
                                                                .bodyLength)) {
                            return false;
                        }
                        ++revisionCommitChunkCount;
                        resetDeferredLoopWriteState();
                        revisionCommitLoopSlotBodyActive = true;
                    }
                    bool loopDone = false;
                    if (!stepDeferredLoopPersist(revisionCommitFile, loop, loopDone,
                                                 LoopPersistPayloadCrc::RevisionCommit)) {
                        return false;
                    }
                    if (!loopDone) {
                        return true;
                    }
                    revisionCommitLoopSlotBodyActive = false;
                    ++revisionCommitCopySlotCursor;
                }
                revisionCommitCopySlotCursor = 0;
                ++revisionCommitCopyTrackCursor;
            }
            revisionCommitSlotIndexWriteCursor = 0;
            revisionWriteStage = RevisionWriteStage::WriteSlotIndexChunk;
            return true;

        case RevisionWriteStage::WriteSlotIndexChunk: {
            if (revisionCommitSlotIndexWriteCursor == 0) {
                const uint32_t slotIndexBodySize = static_cast<uint32_t>(
                    RevisionPackedBlob::slotIndexChunkBodySize(revisionCommitSlotIndexCount));
                if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                                    slotIndexBodySize)) {
                    return false;
                }
                ++revisionCommitChunkCount;
                const uint16_t entryCount = revisionCommitSlotIndexCount;
                const uint16_t reservedPrefix = 0;
                if (revisionCommitFile.write(reinterpret_cast<const uint8_t*>(&entryCount),
                                             sizeof(entryCount)) != sizeof(entryCount) ||
                    revisionCommitFile.write(reinterpret_cast<const uint8_t*>(&reservedPrefix),
                                             sizeof(reservedPrefix)) != sizeof(reservedPrefix)) {
                    return false;
                }
                uint8_t slotIndexPrefixBytes[RevisionPackedBlob::kSlotIndexBodyPrefixByteSize];
                std::memcpy(slotIndexPrefixBytes, &entryCount, sizeof(entryCount));
                std::memcpy(slotIndexPrefixBytes + sizeof(entryCount), &reservedPrefix,
                            sizeof(reservedPrefix));
                if (!appendRevisionCommitPayloadCrc(slotIndexPrefixBytes, sizeof(slotIndexPrefixBytes))) {
                    return false;
                }
                revisionCommitPayloadWriteOffset +=
                    static_cast<uint32_t>(RevisionPackedBlob::kSlotIndexBodyPrefixByteSize);
                revisionCommitSlotIndexChunkWritten = true;
            }
            if (revisionCommitSlotIndexWriteCursor < revisionCommitSlotIndexCount) {
                const StorageIo io = storageIoFromFileWrite(revisionCommitFile);
                if (!RevisionPackedBlob::writeRevisionLoopSlotDirectoryEntry(
                        io, revisionCommitSlotEntries[revisionCommitSlotIndexWriteCursor])) {
                    return false;
                }
                const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
                    revisionCommitSlotEntries[revisionCommitSlotIndexWriteCursor];
                uint8_t directoryEntryBytes[RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize];
                if (!RevisionPackedBlob::revisionLoopSlotDirectoryEntryFileBytes(entry, directoryEntryBytes,
                                                                 sizeof(directoryEntryBytes))) {
                    return false;
                }
                if (!appendRevisionCommitPayloadCrc(directoryEntryBytes, sizeof(directoryEntryBytes))) {
                    return false;
                }
                revisionCommitPayloadWriteOffset +=
                    static_cast<uint32_t>(RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize);
                ++revisionCommitSlotIndexWriteCursor;
                return true;
            }
            revisionWriteStage = RevisionWriteStage::WriteFooter;
            return true;
        }

        case RevisionWriteStage::WriteFooter: {
            if (!revisionCommitFile) {
                return false;
            }
            if (!writeRevisionCommitSlotIndexChunkFromFooter()) {
                return false;
            }
            revisionCommitHeader.chunkCount = revisionCommitChunkCount;
            revisionCommitHeader.payloadSize = revisionCommitPayloadWriteOffset;
            uint32_t payloadCrc = 0;
            if (!computeRevisionCommitPayloadCrcFromFile(
                    revisionCommitFile, static_cast<uint32_t>(RevisionPackedBlob::kRevisionHeaderByteSize),
                    revisionCommitHeader.payloadSize, payloadCrc)) {
                return false;
            }
            RevisionPackedBlob::RevisionFooter footer{};
            footer.svokToken = RevisionPackedBlob::kRevisionSvokFileToken;
            footer.payloadCrc32 =
                revisionCommitHeader.payloadSize == 0U ? 0U : payloadCrc;
            footer.fileSize =
                static_cast<uint32_t>(revisionCommitFile.size()) +
                static_cast<uint32_t>(RevisionPackedBlob::kRevisionFooterByteSize);
            const StorageIo footerIo = storageIoFromFileWrite(revisionCommitFile);
            if (!RevisionPackedBlob::writeRevisionFooter(footerIo, footer)) {
                return false;
            }
            revisionCommitFile.flush();
            revisionCommitFile.close();
            revisionCommitHeader.revisionId = revisionCommitPendingRevisionId;
            revisionCommitHeader.headerCrc32 =
                RevisionPackedBlob::computeRevisionHeaderChecksum(revisionCommitHeader);
            revisionCommitStage = RevisionCommitStage::Validate;
            return true;
        }
    }
    return false;
}

bool stepRevisionCommitValidate() {
    File file = SD.open(revisionCommitTempPath, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    if (fileSize < RevisionPackedBlob::kRevisionHeaderByteSize +
                        RevisionPackedBlob::kRevisionFooterByteSize) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }

    uint8_t headerBytes[RevisionPackedBlob::kRevisionHeaderByteSize];
    if (file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes))) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }

    RevisionPackedBlob::RevisionHeader header{};
    if (!RevisionPackedBlob::parseRevisionHeaderFromBytes(headerBytes, sizeof(headerBytes),
                                                          header)) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }

    const size_t payloadOffset = RevisionPackedBlob::kRevisionHeaderByteSize;
    const size_t payloadSize =
        fileSize - payloadOffset - RevisionPackedBlob::kRevisionFooterByteSize;
    if (payloadSize > UINT32_MAX) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }
    if (payloadOffset + payloadSize + RevisionPackedBlob::kRevisionFooterByteSize > fileSize) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }

    uint32_t payloadCrc = 0;
    bool payloadCrcSeeded = false;
    if (payloadSize > 0) {
        if (!file.seek(payloadOffset)) {
            file.close();
            SD.remove(revisionCommitTempPath);
            return false;
        }
        uint32_t remaining = static_cast<uint32_t>(payloadSize);
        while (remaining > 0) {
            const size_t chunkSize =
                remaining > revisionCommitCopyBuffer.size() ? revisionCommitCopyBuffer.size()
                                                            : remaining;
            const int bytesRead =
                file.read(revisionCommitCopyBuffer.data(), static_cast<size_t>(chunkSize));
            if (bytesRead <= 0) {
                file.close();
                SD.remove(revisionCommitTempPath);
                return false;
            }
            const size_t written = static_cast<size_t>(bytesRead);
            if (!payloadCrcSeeded) {
                payloadCrc = PersistenceSchema::crc32(revisionCommitCopyBuffer.data(), written);
                payloadCrcSeeded = true;
            } else {
                payloadCrc = PersistenceSchema::crc32Continue(payloadCrc,
                                                              revisionCommitCopyBuffer.data(),
                                                              written);
            }
            remaining -= static_cast<uint32_t>(written);
        }
    }

    RevisionPackedBlob::RevisionFooter footer{};
    if (!file.seek(fileSize - RevisionPackedBlob::kRevisionFooterByteSize) ||
        !readRaw(file, &footer.svokToken, sizeof(footer.svokToken)) ||
        !readRaw(file, &footer.payloadCrc32, sizeof(footer.payloadCrc32)) ||
        !readRaw(file, &footer.fileSize, sizeof(footer.fileSize))) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }
    file.close();

    if (footer.svokToken != RevisionPackedBlob::kRevisionSvokFileToken ||
        footer.fileSize != static_cast<uint32_t>(fileSize) ||
        footer.payloadCrc32 != payloadCrc) {
        Serial.print("[StorageManager] ERROR: Revision validate footer magic=");
        Serial.print(footer.svokToken, HEX);
        Serial.print(" fileSize=");
        Serial.print(footer.fileSize);
        Serial.print(" expectedCrc=");
        Serial.print(payloadCrc);
        Serial.print(" footerCrc=");
        Serial.println(footer.payloadCrc32);
        SD.remove(revisionCommitTempPath);
        return false;
    }

    header.revisionId = revisionCommitPendingRevisionId;
    header.chunkCount = revisionCommitChunkCount;
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.headerCrc32 = RevisionPackedBlob::computeRevisionHeaderChecksum(header);
    revisionCommitFile = SD.open(revisionCommitTempPath, FILE_WRITE);
    if (!revisionCommitFile) {
        return false;
    }
    revisionCommitFile.seek(0);
    const StorageIo headerIo = storageIoFromFileWrite(revisionCommitFile);
    if (!RevisionPackedBlob::writeRevisionHeader(headerIo, header)) {
        revisionCommitFile.close();
        return false;
    }
    revisionCommitFile.close();

    if (!CurrentSetStorage::atomicRenameTempFile(revisionCommitTempPath,
                                                 revisionCommitFinalPath)) {
        return false;
    }
    revisionCommitHeader = header;
    revisionCommitStage = RevisionCommitStage::CatalogUpdate;
    return true;
}

bool stepRevisionCommitCatalogUpdate() {
    const uint64_t updatedUnix = static_cast<uint64_t>(RtcTime::getUnixTime());
    SetRevisionCatalog::applyValidatedRevisionToSetMeta(revisionCommitSetMeta,
                                                       revisionCommitPendingRevisionId,
                                                       updatedUnix);

    char setMetaPath[64];
    char setMetaTempPath[72];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                               revisionCommitSetId)) {
        return false;
    }
    const int tempWritten = std::snprintf(setMetaTempPath, sizeof(setMetaTempPath), "%s.tmp",
                                          setMetaPath);
    if (tempWritten <= 0 || static_cast<size_t>(tempWritten) >= sizeof(setMetaTempPath)) {
        return false;
    }

    File setMetaTemp = SD.open(setMetaTempPath, FILE_WRITE);
    if (!setMetaTemp) {
        return false;
    }
    const StorageIo setMetaIo = storageIoFromFileWrite(setMetaTemp);
    if (!SetRevisionCatalog::writeSetMetaRecord(setMetaIo, revisionCommitSetMeta)) {
        setMetaTemp.close();
        return false;
    }
    setMetaTemp.close();
    if (!CurrentSetStorage::atomicRenameTempFile(setMetaTempPath, setMetaPath)) {
        return false;
    }

    revisionCommitCatalogIndex.catalogChecksum =
        SetRevisionCatalog::computeSetCatalogIndexChecksum(revisionCommitCatalogIndex);
    File indexTemp = SD.open(SetRevisionCatalog::kSetIndexTempPath, FILE_WRITE);
    if (!indexTemp) {
        return false;
    }
    const StorageIo indexIo = storageIoFromFileWrite(indexTemp);
    if (!SetRevisionCatalog::writeSetCatalogIndex(indexIo, revisionCommitCatalogIndex)) {
        indexTemp.close();
        return false;
    }
    indexTemp.close();
    if (!CurrentSetStorage::atomicRenameTempFile(SetRevisionCatalog::kSetIndexTempPath,
                                                 SetRevisionCatalog::kSetIndexPath)) {
        return false;
    }

    revisionCommitStage = RevisionCommitStage::Complete;
    return true;
}

bool writeSetCatalogIndexFile(const SetRevisionCatalog::SetCatalogIndex& index) {
    File indexTemp = SD.open(SetRevisionCatalog::kSetIndexTempPath, FILE_WRITE);
    if (!indexTemp) {
        return false;
    }
    const StorageIo indexIo = storageIoFromFileWrite(indexTemp);
    if (!SetRevisionCatalog::writeSetCatalogIndex(indexIo, index)) {
        indexTemp.close();
        return false;
    }
    indexTemp.close();
    return CurrentSetStorage::atomicRenameTempFile(SetRevisionCatalog::kSetIndexTempPath,
                                                   SetRevisionCatalog::kSetIndexPath);
}

bool writeSetMetaRecordFile(uint16_t setId, const SetRevisionCatalog::SetMetaRecord& record) {
    char setMetaPath[64];
    char setMetaTempPath[72];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId)) {
        return false;
    }
    const int tempWritten = std::snprintf(setMetaTempPath, sizeof(setMetaTempPath), "%s.tmp",
                                          setMetaPath);
    if (tempWritten <= 0 || static_cast<size_t>(tempWritten) >= sizeof(setMetaTempPath)) {
        return false;
    }
    File setMetaTemp = SD.open(setMetaTempPath, FILE_WRITE);
    if (!setMetaTemp) {
        return false;
    }
    const StorageIo setMetaIo = storageIoFromFileWrite(setMetaTemp);
    if (!SetRevisionCatalog::writeSetMetaRecord(setMetaIo, record)) {
        setMetaTemp.close();
        return false;
    }
    setMetaTemp.close();
    return CurrentSetStorage::atomicRenameTempFile(setMetaTempPath, setMetaPath);
}

bool stepRevisionCommitComplete() {
    lastCommittedWorkspaceEpoch =
        CurrentWorkspaceStorage::syncLastCommittedEpochAfterRevisionCommitComplete(
            currentWorkspaceEpoch);
    revisionCommitWorkspaceEpochBeforeSnapshot = 0;
    workspaceDerivedFromSetId = revisionCommitSetId;
    workspaceDerivedFromRevisionId = revisionCommitPendingRevisionId;
    workspaceLastCommittedRevisionId = revisionCommitPendingRevisionId;
    if (!writeWorkspaceMetaAfterDeferredSave()) {
        return false;
    }
    Serial.print("[StorageManager] Revision commit complete S");
    Serial.print(revisionCommitSetId);
    Serial.print(" v");
    Serial.println(revisionCommitPendingRevisionId);
#if defined(SESSION_CAPTURE)
    if (hitlRevisionCommitBackup.armed) {
        hitlRevisionCommitBackup.committedSetId = revisionCommitSetId;
        hitlRevisionCommitBackup.committedRevisionId = revisionCommitPendingRevisionId;
        hitlRevisionCommitBackup.createdNewSetFolder = revisionCommitAllocatedNewSet;
        const int folderWritten = std::snprintf(hitlRevisionCommitBackup.setFolderPath,
                                                sizeof(hitlRevisionCommitBackup.setFolderPath),
                                                "%s/S%04u", SetRevisionCatalog::kSetsRoot,
                                                revisionCommitSetId);
        const int revisionWritten = std::snprintf(
            hitlRevisionCommitBackup.revisionFinalPath,
            sizeof(hitlRevisionCommitBackup.revisionFinalPath), "%s",
            revisionCommitFinalPath);
        if (folderWritten <= 0 ||
            static_cast<size_t>(folderWritten) >= sizeof(hitlRevisionCommitBackup.setFolderPath) ||
            revisionWritten <= 0 ||
            static_cast<size_t>(revisionWritten) >=
                sizeof(hitlRevisionCommitBackup.revisionFinalPath)) {
            return false;
        }
        char revCompleteDetail[24];
        std::snprintf(revCompleteDetail, sizeof(revCompleteDetail), "S%04u_v%04u",
                        revisionCommitSetId, revisionCommitPendingRevisionId);
        SC_PERSIST("rev_complete", 0, revisionCommitSetId, revisionCommitPendingRevisionId,
                   revCompleteDetail);
    }
#endif
    revisionCommitStage = RevisionCommitStage::Idle;
    revisionCommitInProgress = false;
    return true;
}

bool stepRevisionCommitJob() {
    switch (revisionCommitStage) {
        case RevisionCommitStage::Idle:
            return true;

        case RevisionCommitStage::Snapshot:
            return beginRevisionCommitSnapshot();

        case RevisionCommitStage::Write:
            return stepRevisionCommitWrite();

        case RevisionCommitStage::Validate:
            return stepRevisionCommitValidate();

        case RevisionCommitStage::CatalogUpdate:
            return stepRevisionCommitCatalogUpdate();

        case RevisionCommitStage::Complete:
            return stepRevisionCommitComplete();
    }
    return false;
}

void clearRevisionLoadPromptAndPipelineState() {
    revisionLoadRequestStaged = false;
    revisionLoadStagedSetId = 0;
    revisionLoadStagedRevisionId = 0;
    revisionLoadDirtyPromptActive = false;
    revisionLoadDirtyPromptSelection = 0;
    revisionLoadPipelineActive = false;
    revisionLoadSaveThenLoadPipeline = false;
}

void resetRevisionLoadReloadRamState() {
    if (revisionLoadReloadMetaFileOpen) {
        revisionLoadReloadMetaFile.close();
        revisionLoadReloadMetaFileOpen = false;
    }
    revisionLoadReloadRamStage = RevisionLoadReloadRamStage::WriteWorkspaceMeta;
    revisionLoadReloadTrackCursor = 0;
    revisionLoadReloadSlotCursor = 0;
    revisionLoadReloadNumTracks = 0;
    revisionLoadReloadActiveLoopIndex.clear();
    revisionLoadReloadSelectedTrackIdx = 0;
    revisionLoadReloadLooperState = LOOPER_IDLE;
    revisionLoadReloadMasterLoopLength = 0;
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
        revisionLoadReloadAnySlotHasEvents[t] = false;
        revisionLoadReloadLoadedTrackState[t] = TRACK_EMPTY;
        revisionLoadReloadMuted[t] = false;
    }
}

void dispatchStagedRevisionLoad() {
    if (!revisionLoadRequestStaged) {
        return;
    }
    revisionLoadSetId = revisionLoadStagedSetId;
    revisionLoadRevisionId = revisionLoadStagedRevisionId;
    revisionLoadRequestStaged = false;
    revisionLoadDirtyPromptActive = false;
    revisionLoadPending = true;
    revisionLoadPipelineActive = true;
    SC_PERSIST("rev_load_request", 0, revisionLoadSetId, revisionLoadRevisionId, "queued");
}

void resetRevisionLoadJobState() {
    if (revisionLoadSourceFileOpen) {
        revisionLoadSourceFile.close();
        revisionLoadSourceFileOpen = false;
    }
    if (revisionLoadDestFileOpen) {
        revisionLoadDestFile.close();
        revisionLoadDestFileOpen = false;
    }
    revisionLoadInProgress = false;
    revisionLoadSdIoActive = false;
    revisionLoadStage = RevisionLoadStage::Idle;
    revisionLoadWriteStage = RevisionLoadWriteStage::PrepareEpoch;
    revisionLoadSetId = 0;
    revisionLoadRevisionId = 0;
    revisionLoadSourcePath[0] = '\0';
    revisionLoadSlotIndexCount = 0;
    revisionLoadWorkspaceEpoch = 0;
    revisionLoadTransportFileOffset = 0;
    revisionLoadTransportBodySize = 0;
    revisionLoadTransportReadPos = 0;
    revisionLoadCopyTrackCursor = 0;
    revisionLoadCopySlotCursor = 0;
    revisionLoadSlotBodyRemaining = 0;
    revisionLoadSlotReadPos = 0;
    revisionLoadWritingEmptySlot = false;
    revisionLoadLoopWriteStage = DeferredLoopWriteStage::Header;
    lastRevisionLoadBlockedLogAtMs = 0;
    revisionLoadUsedDefaultTransport = false;
    revisionLoadDisplayRefreshPending = false;
    revisionLoadHeader = RevisionPackedBlob::RevisionHeader{};
    revisionLoadSlotIndexCount = 0;
    for (uint16_t i = 0; i < kMaxRevisionLoopIndexEntries; ++i) {
        revisionLoadSlotDirectoryEntries[i] = RevisionPackedBlob::RevisionLoopSlotDirectoryEntry{};
    }
    resetRevisionLoadReloadRamState();
    clearRevisionLoadPromptAndPipelineState();
}

bool findRevisionLoadSlotEntry(uint8_t trackIndex, uint8_t slotIndex,
                               RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entryOut) {
    for (uint16_t i = 0; i < revisionLoadSlotIndexCount; ++i) {
        if (revisionLoadSlotDirectoryEntries[i].trackIndex == trackIndex &&
            revisionLoadSlotDirectoryEntries[i].slotIndex == slotIndex) {
            entryOut = revisionLoadSlotDirectoryEntries[i];
            return true;
        }
    }
    return false;
}

uint32_t revisionLoadLoopSlotBodyFileOffset(const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry) {
    return RevisionPackedBlob::kRevisionHeaderByteSize + entry.chunkOffset +
           static_cast<uint32_t>(RevisionPackedBlob::kChunkHeaderByteSize);
}

bool revisionLoadRevisionLoopSlotDirectoryEntryOccupied(uint8_t trackIndex, uint8_t slotIndex) {
    for (uint16_t index = 0; index < revisionLoadSlotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            revisionLoadSlotDirectoryEntries[index];
        if (entry.trackIndex == trackIndex && entry.slotIndex == slotIndex) {
            return entry.occupied != 0;
        }
    }
    return false;
}

uint32_t revisionLoadDefaultMasterLoopLength() {
    uint32_t maxLength = 0;
    for (uint16_t index = 0; index < revisionLoadSlotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            revisionLoadSlotDirectoryEntries[index];
        if (entry.occupied != 0 && entry.loopLengthTicks > maxLength) {
            maxLength = entry.loopLengthTicks;
        }
    }
    return maxLength;
}

uint8_t revisionLoadDefaultActiveLoopIndex(uint8_t trackIndex) {
    for (uint16_t index = 0; index < revisionLoadSlotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            revisionLoadSlotDirectoryEntries[index];
        if (entry.trackIndex == trackIndex && entry.occupied != 0) {
            return entry.slotIndex;
        }
    }
    return 0;
}

bool writeDefaultRevisionLoadTransportBody(File& file) {
    if (!writeCurrentSetMetaHeaderToOpenFile(file)) {
        return false;
    }

    const float savedBpm = bpm;
    if (!writeRaw(file, &savedBpm, sizeof(savedBpm))) {
        return false;
    }

    const uint32_t looperStateVal = persistedLooperStateRaw(LOOPER_IDLE);
    if (!writeRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        return false;
    }

    const uint32_t masterLoopLength = revisionLoadDefaultMasterLoopLength();
    if (!writeRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        return false;
    }

    const uint8_t numTracks = Config::NUM_TRACKS;
    if (!writeRaw(file, &numTracks, sizeof(numTracks))) {
        return false;
    }

    for (uint8_t trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        uint32_t trackState = static_cast<uint32_t>(TRACK_EMPTY);
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            if (revisionLoadRevisionLoopSlotDirectoryEntryOccupied(trackIndex, slotIndex)) {
                trackState = static_cast<uint32_t>(TRACK_STOPPED);
                break;
            }
        }
        const bool muted = false;
        if (!writeRaw(file, &trackState, sizeof(trackState)) || !writeRaw(file, &muted, sizeof(muted))) {
            return false;
        }

        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            const bool slotEnabled = revisionLoadRevisionLoopSlotDirectoryEntryOccupied(trackIndex, slotIndex);
            const bool slotMuted = false;
            const LoopId slotLoopId = static_cast<LoopId>(slotIndex);
            if (!writeRaw(file, &slotEnabled, sizeof(slotEnabled)) ||
                !writeRaw(file, &slotMuted, sizeof(slotMuted)) ||
                !writeRaw(file, &slotLoopId, sizeof(slotLoopId))) {
                return false;
            }
        }
    }

    const uint8_t selectedTrackIdx = 0;
    if (!writeRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
        return false;
    }
    for (uint8_t trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        const uint8_t activeLoopIndex = revisionLoadDefaultActiveLoopIndex(trackIndex);
        if (!writeRaw(file, &activeLoopIndex, sizeof(activeLoopIndex))) {
            return false;
        }
    }

    if (!writeRaw(file, &GLOBAL_UNDO_MAGIC, sizeof(GLOBAL_UNDO_MAGIC))) {
        return false;
    }

    GlobalUndoStack emptyUndoStack{};
    emptyUndoStack.clear();
    for (uint8_t trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        if (!writeGlobalUndoStack(file, emptyUndoStack)) {
            return false;
        }
    }
    return true;
}

bool stepDeferredEmptyLoopPersist(File& file, LoopId loopId, bool& loopDone);

bool readRevisionChunkHeaderFromFile(File& file, uint32_t fileOffset,
                                     RevisionPackedBlob::ChunkHeader& chunkHeaderOut) {
    if (!file.seek(fileOffset)) {
        return false;
    }
    const StorageIo io = storageIoFromFileRead(file);
    return RevisionPackedBlob::readChunkHeader(io, chunkHeaderOut);
}

bool findChunkBodyInRevisionFile(File& file, size_t fileSize,
                                 const RevisionPackedBlob::RevisionHeader& header,
                                 RevisionPackedBlob::ChunkType type, uint8_t trackIndex,
                                 uint8_t slotIndex, uint32_t& bodyOffsetInFileOut,
                                 uint32_t& bodyLengthOut) {
    bodyOffsetInFileOut = 0;
    bodyLengthOut = 0;
    if (header.payloadSize == 0) {
        return false;
    }
    const size_t payloadOffset = RevisionPackedBlob::kRevisionHeaderByteSize;
    if (payloadOffset + header.payloadSize > fileSize) {
        return false;
    }

    size_t cursor = 0;
    while (cursor + RevisionPackedBlob::kChunkHeaderByteSize <= header.payloadSize) {
        RevisionPackedBlob::ChunkHeader chunkHeader{};
        if (!readRevisionChunkHeaderFromFile(file,
                                             static_cast<uint32_t>(payloadOffset + cursor),
                                             chunkHeader)) {
            return false;
        }
        const size_t bodyStart = cursor + RevisionPackedBlob::kChunkHeaderByteSize;
        const size_t bodyEnd = bodyStart + static_cast<size_t>(chunkHeader.bodyLength);
        if (bodyEnd > header.payloadSize) {
            return false;
        }
        if (chunkHeader.type == static_cast<uint8_t>(type) &&
            chunkHeader.trackIndex == trackIndex && chunkHeader.slotIndex == slotIndex) {
            bodyOffsetInFileOut = static_cast<uint32_t>(payloadOffset + bodyStart);
            bodyLengthOut = chunkHeader.bodyLength;
            return true;
        }
        cursor = bodyEnd;
    }
    return false;
}

bool readRevisionLoopSlotDirectoryEntryCountFromRevisionFile(File& file, size_t fileSize,
                                             const RevisionPackedBlob::RevisionHeader& header,
                                             uint16_t& entryCountOut) {
    entryCountOut = 0;
    uint32_t bodyOffsetInFile = 0;
    uint32_t bodyLength = 0;
    if (!findChunkBodyInRevisionFile(file, fileSize, header,
                                     RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                     bodyOffsetInFile, bodyLength)) {
        return false;
    }
    if (bodyLength < RevisionPackedBlob::kSlotIndexBodyPrefixByteSize) {
        return false;
    }
    if (!file.seek(bodyOffsetInFile) ||
        file.read(reinterpret_cast<uint8_t*>(&entryCountOut), sizeof(entryCountOut)) !=
            static_cast<int>(sizeof(entryCountOut))) {
        return false;
    }
    const size_t expectedBodySize =
        RevisionPackedBlob::kSlotIndexBodyPrefixByteSize +
        static_cast<size_t>(entryCountOut) * RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize;
    return bodyLength >= expectedBodySize;
}

bool readSlotIndexEntriesFromRevisionFile(
    File& file, size_t fileSize, const RevisionPackedBlob::RevisionHeader& header,
    RevisionPackedBlob::RevisionLoopSlotDirectoryEntry* entriesOut, uint16_t maxEntries,
    uint16_t& entryCountOut) {
    entryCountOut = 0;
    if (entriesOut == nullptr || maxEntries == 0) {
        return false;
    }

    uint16_t totalEntries = 0;
    if (!readRevisionLoopSlotDirectoryEntryCountFromRevisionFile(file, fileSize, header, totalEntries)) {
        return false;
    }

    uint32_t bodyOffsetInFile = 0;
    uint32_t bodyLength = 0;
    if (!findChunkBodyInRevisionFile(file, fileSize, header,
                                     RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                     bodyOffsetInFile, bodyLength)) {
        return false;
    }

    for (uint16_t index = 0; index < totalEntries; ++index) {
        if (index >= maxEntries) {
            return false;
        }
        const uint32_t entryOffset =
            bodyOffsetInFile + RevisionPackedBlob::kSlotIndexBodyPrefixByteSize +
            static_cast<uint32_t>(index) * RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize;
        if (entryOffset + RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize >
            bodyOffsetInFile + bodyLength) {
            return false;
        }
        if (!file.seek(entryOffset)) {
            return false;
        }
        const StorageIo io = storageIoFromFileRead(file);
        if (!RevisionPackedBlob::readRevisionLoopSlotDirectoryEntry(io, entriesOut[index])) {
            return false;
        }
        ++entryCountOut;
    }
    return true;
}

bool validateRevisionLoadFileFooterFromSd(File& file, size_t fileSize,
                                          RevisionPackedBlob::RevisionHeader& headerOut) {
    if (fileSize < RevisionPackedBlob::kRevisionHeaderByteSize +
                        RevisionPackedBlob::kRevisionFooterByteSize) {
        return false;
    }
    uint8_t headerBytes[RevisionPackedBlob::kRevisionHeaderByteSize];
    if (!file.seek(0) ||
        file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes)) ||
        !RevisionPackedBlob::parseRevisionHeaderFromBytes(headerBytes, sizeof(headerBytes),
                                                          headerOut)) {
        return false;
    }

    const size_t payloadOffset = RevisionPackedBlob::kRevisionHeaderByteSize;
    const size_t payloadSize =
        fileSize - payloadOffset - RevisionPackedBlob::kRevisionFooterByteSize;
    if (payloadSize > UINT32_MAX) {
        return false;
    }

    uint32_t payloadCrc = 0;
    if (!computeRevisionCommitPayloadCrcFromFile(
            file, static_cast<uint32_t>(payloadOffset), static_cast<uint32_t>(payloadSize),
            payloadCrc)) {
        return false;
    }

    RevisionPackedBlob::RevisionFooter footer{};
    if (!file.seek(fileSize - RevisionPackedBlob::kRevisionFooterByteSize) ||
        !readRaw(file, &footer.svokToken, sizeof(footer.svokToken)) ||
        !readRaw(file, &footer.payloadCrc32, sizeof(footer.payloadCrc32)) ||
        !readRaw(file, &footer.fileSize, sizeof(footer.fileSize))) {
        return false;
    }

    if (footer.svokToken != RevisionPackedBlob::kRevisionSvokFileToken ||
        footer.fileSize != static_cast<uint32_t>(fileSize) ||
        footer.payloadCrc32 != payloadCrc) {
        Serial.print("[StorageManager] ERROR: Revision load SD footer mismatch footerCrc=");
        Serial.print(footer.payloadCrc32);
        Serial.print(" expectedCrc=");
        Serial.print(payloadCrc);
        Serial.print(" payloadSize=");
        Serial.println(static_cast<uint32_t>(payloadSize));
        return false;
    }

    if (headerOut.payloadSize != static_cast<uint32_t>(payloadSize)) {
        headerOut.payloadSize = static_cast<uint32_t>(payloadSize);
    }
    return true;
}

bool beginRevisionLoadValidate() {
    if (!SetRevisionCatalog::formatRevisionPath(revisionLoadSourcePath,
                                                sizeof(revisionLoadSourcePath),
                                                revisionLoadSetId, revisionLoadRevisionId,
                                                false)) {
        return false;
    }
    if (!SD.exists(revisionLoadSourcePath)) {
        Serial.print("[StorageManager] ERROR: Revision file missing ");
        Serial.println(revisionLoadSourcePath);
        return false;
    }

    File file = SD.open(revisionLoadSourcePath, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    if (fileSize < RevisionPackedBlob::kRevisionHeaderByteSize +
                        RevisionPackedBlob::kRevisionFooterByteSize) {
        file.close();
        return false;
    }

    if (!validateRevisionLoadFileFooterFromSd(file, fileSize, revisionLoadHeader)) {
        file.close();
        return false;
    }

    revisionLoadSlotIndexCount = 0;
    revisionLoadTransportFileOffset = 0;
    revisionLoadTransportBodySize = 0;
    revisionLoadUsedDefaultTransport = false;

    if (!findChunkBodyInRevisionFile(file, fileSize, revisionLoadHeader,
                                     RevisionPackedBlob::ChunkType::Transport, 0, 0,
                                     revisionLoadTransportFileOffset,
                                     revisionLoadTransportBodySize) ||
        revisionLoadTransportBodySize == 0) {
        revisionLoadTransportFileOffset = 0;
        revisionLoadTransportBodySize = 0;
        revisionLoadUsedDefaultTransport = true;
        Serial.println(
            "[StorageManager] Revision load missing Transport chunk; using default transport");
    }

    if (!readSlotIndexEntriesFromRevisionFile(file, fileSize, revisionLoadHeader,
                                              revisionLoadSlotDirectoryEntries,
                                              kMaxRevisionLoopIndexEntries,
                                              revisionLoadSlotIndexCount)) {
        uint16_t slotIndexEntryCount = 0;
        const bool hasSlotIndexChunk = readRevisionLoopSlotDirectoryEntryCountFromRevisionFile(
            file, fileSize, revisionLoadHeader, slotIndexEntryCount);
        file.close();
        if (!hasSlotIndexChunk) {
            revisionLoadSlotIndexCount = 0;
        } else {
            Serial.print("[StorageManager] ERROR: Revision load slot index parse failed fileSize=");
            Serial.print(static_cast<uint32_t>(fileSize));
            Serial.print(" hdrChunkCount=");
            Serial.print(revisionLoadHeader.chunkCount);
            Serial.print(" hdrPayloadSize=");
            Serial.print(revisionLoadHeader.payloadSize);
            Serial.print(" slotIndexEntryCount=");
            Serial.println(slotIndexEntryCount);
            return false;
        }
    } else {
        file.close();
    }

    revisionLoadWriteStage = RevisionLoadWriteStage::PrepareEpoch;
    revisionLoadStage = RevisionLoadStage::Write;
    return true;
}

bool openRevisionLoadLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (revisionLoadDestFileOpen) {
        revisionLoadDestFile.close();
        revisionLoadDestFileOpen = false;
    }
    char tempPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex)) {
        return false;
    }
    revisionLoadDestFile = SD.open(tempPath, FILE_WRITE);
    if (!revisionLoadDestFile) {
        return false;
    }
    revisionLoadDestFile.seek(0);
    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(revisionLoadDestFile,
                                                              revisionLoadWorkspaceEpoch)) {
        revisionLoadDestFile.close();
        return false;
    }
    revisionLoadDestFileOpen = true;
    return true;
}

bool finalizeRevisionLoadLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (!revisionLoadDestFileOpen) {
        return false;
    }
    if (!writeRaw(revisionLoadDestFile, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        revisionLoadDestFile.close();
        revisionLoadDestFileOpen = false;
        return false;
    }
    revisionLoadDestFile.close();
    revisionLoadDestFileOpen = false;

    char tempPath[48];
    char finalPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex) ||
        !CurrentSetStorage::formatLoopSlotPath(finalPath, sizeof(finalPath), trackIndex,
                                               slotIndex)) {
        return false;
    }
    if (!CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(tempPath)) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(tempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(tempPath, finalPath);
}

bool stepRevisionLoadWrite() {
    switch (revisionLoadWriteStage) {
        case RevisionLoadWriteStage::PrepareEpoch:
            ++currentWorkspaceEpoch;
            revisionLoadWorkspaceEpoch = currentWorkspaceEpoch;
            if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
                !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
                !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
                !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir)) {
                return false;
            }
            revisionLoadWriteStage = RevisionLoadWriteStage::OpenMetaTemp;
            return true;

        case RevisionLoadWriteStage::OpenMetaTemp:
            revisionLoadDestFile = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
            if (!revisionLoadDestFile) {
                return false;
            }
            revisionLoadDestFile.seek(0);
            if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(revisionLoadDestFile,
                                                                      revisionLoadWorkspaceEpoch)) {
                revisionLoadDestFile.close();
                return false;
            }
            revisionLoadDestFileOpen = true;
            revisionLoadTransportReadPos = 0;
            revisionLoadWriteStage = RevisionLoadWriteStage::CopyTransportBody;
            return true;

        case RevisionLoadWriteStage::CopyTransportBody:
            if (revisionLoadTransportBodySize == 0) {
                if (!writeDefaultRevisionLoadTransportBody(revisionLoadDestFile)) {
                    Serial.println(
                        "[StorageManager] ERROR: Revision load failed writing default transport");
                    revisionLoadDestFile.close();
                    revisionLoadDestFileOpen = false;
                    return false;
                }
                revisionLoadWriteStage = RevisionLoadWriteStage::FinalizeMetaTemp;
                return true;
            }
            if (!revisionLoadSourceFileOpen) {
                revisionLoadSourceFile = SD.open(revisionLoadSourcePath, FILE_READ);
                if (!revisionLoadSourceFile) {
                    return false;
                }
                revisionLoadSourceFileOpen = true;
            }
            if (revisionLoadTransportReadPos == 0) {
                revisionLoadTransportReadPos = revisionLoadTransportFileOffset;
            }
            {
                const uint32_t endPos =
                    revisionLoadTransportFileOffset + revisionLoadTransportBodySize;
                uint32_t remaining = endPos - revisionLoadTransportReadPos;
                if (remaining > 0) {
                    if (!copyRevisionCommitChunk(
                            revisionLoadDestFile, revisionLoadSourceFile,
                            revisionLoadTransportReadPos, remaining,
                            static_cast<uint32_t>(revisionCommitCopyBuffer.size()))) {
                        return false;
                    }
                    if (remaining > 0) {
                        return true;
                    }
                }
            }
            if (revisionLoadSourceFileOpen) {
                revisionLoadSourceFile.close();
                revisionLoadSourceFileOpen = false;
            }
            revisionLoadWriteStage = RevisionLoadWriteStage::FinalizeMetaTemp;
            return true;

        case RevisionLoadWriteStage::FinalizeMetaTemp: {
            if (!revisionLoadDestFileOpen) {
                return false;
            }
            if (!writeRaw(revisionLoadDestFile, &CurrentSetStorage::kSaveFileToken,
                          sizeof(CurrentSetStorage::kSaveFileToken))) {
                revisionLoadDestFile.close();
                revisionLoadDestFileOpen = false;
                return false;
            }
            revisionLoadDestFile.close();
            revisionLoadDestFileOpen = false;
            if (!CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(
                    CurrentSetStorage::kCurrentMetaTempPath)) {
                return false;
            }
            if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kCurrentMetaTempPath)) {
                return false;
            }
            if (!CurrentSetStorage::atomicRenameTempFile(CurrentSetStorage::kCurrentMetaTempPath,
                                                           CurrentSetStorage::kCurrentMetaPath)) {
                return false;
            }
            revisionLoadCopyTrackCursor = 0;
            revisionLoadCopySlotCursor = 0;
            revisionLoadSlotBodyRemaining = 0;
            revisionLoadWriteStage = RevisionLoadWriteStage::WriteLoopSlots;
            return true;
        }

        case RevisionLoadWriteStage::WriteLoopSlots:
            while (revisionLoadCopyTrackCursor < Config::NUM_TRACKS) {
                while (revisionLoadCopySlotCursor < Config::MAX_LOOPS_PER_TRACK) {
                    const uint8_t trackIndex = revisionLoadCopyTrackCursor;
                    const uint8_t slotIndex = revisionLoadCopySlotCursor;
                    RevisionPackedBlob::RevisionLoopSlotDirectoryEntry slotEntry{};
                    const bool hasSlotEntry =
                        findRevisionLoadSlotEntry(trackIndex, slotIndex, slotEntry);

                    if (revisionLoadSlotBodyRemaining == 0 && !revisionLoadDestFileOpen &&
                        !revisionLoadWritingEmptySlot) {
                        if (!openRevisionLoadLoopSlotTemp(trackIndex, slotIndex)) {
                            return false;
                        }
                        if (hasSlotEntry) {
                            revisionLoadWritingEmptySlot = false;
                            revisionLoadSlotReadPos =
                                revisionLoadLoopSlotBodyFileOffset(slotEntry);
                            revisionLoadSlotBodyRemaining = slotEntry.bodyLength;
                            revisionLoadLoopWriteStage = DeferredLoopWriteStage::Header;
                        } else {
                            revisionLoadWritingEmptySlot = true;
                            revisionLoadLoopWriteStage = DeferredLoopWriteStage::Header;
                        }
                    }

                    if (revisionLoadWritingEmptySlot) {
                        bool loopDone = false;
                        if (!stepDeferredEmptyLoopPersist(
                                revisionLoadDestFile, static_cast<LoopId>(slotIndex), loopDone)) {
                            return false;
                        }
                        if (!loopDone) {
                            return true;
                        }
                    } else if (revisionLoadSlotBodyRemaining > 0) {
                        if (!revisionLoadSourceFileOpen) {
                            revisionLoadSourceFile = SD.open(revisionLoadSourcePath, FILE_READ);
                            if (!revisionLoadSourceFile) {
                                return false;
                            }
                            revisionLoadSourceFileOpen = true;
                        }
                        if (!copyRevisionCommitChunk(
                                revisionLoadDestFile, revisionLoadSourceFile,
                                revisionLoadSlotReadPos, revisionLoadSlotBodyRemaining,
                                static_cast<uint32_t>(revisionCommitCopyBuffer.size()))) {
                            return false;
                        }
                        if (revisionLoadSlotBodyRemaining > 0) {
                            return true;
                        }
                        if (revisionLoadSourceFileOpen) {
                            revisionLoadSourceFile.close();
                            revisionLoadSourceFileOpen = false;
                        }
                    }

                    if (!finalizeRevisionLoadLoopSlotTemp(trackIndex, slotIndex)) {
                        return false;
                    }
                    revisionLoadWritingEmptySlot = false;
                    revisionLoadSlotBodyRemaining = 0;
                    ++revisionLoadCopySlotCursor;
                }
                revisionLoadCopySlotCursor = 0;
                ++revisionLoadCopyTrackCursor;
            }
            revisionLoadStage = RevisionLoadStage::ReloadRam;
            return true;
    }
    return false;
}

bool stepRevisionLoadReloadRam(LooperState& state) {
    switch (revisionLoadReloadRamStage) {
        case RevisionLoadReloadRamStage::WriteWorkspaceMeta:
            currentWorkspaceEpoch = revisionLoadWorkspaceEpoch;
            lastCommittedWorkspaceEpoch = revisionLoadWorkspaceEpoch;
            workspaceDerivedFromSetId = revisionLoadHeader.setId;
            workspaceDerivedFromRevisionId = revisionLoadHeader.revisionId;
            if (!writeWorkspaceMetaAfterDeferredSave()) {
                Serial.println("[StorageManager] ERROR: Revision load failed writing workspace.bin");
                return false;
            }
            resetTracksAfterFailedLoad();
            revisionLoadReloadMetaFile =
                SD.open(CurrentSetStorage::kCurrentRuntimeBundlePath, FILE_READ);
            if (!revisionLoadReloadMetaFile) {
                Serial.println("[StorageManager] ERROR: Revision load failed opening runtime bundle");
                return false;
            }
            revisionLoadReloadMetaFileOpen = true;
            revisionLoadReloadRamStage = RevisionLoadReloadRamStage::ReadMetaHeaders;
            return true;

        case RevisionLoadReloadRamStage::ReadMetaHeaders: {
            if (!readCurrentSetFilePreamble(revisionLoadReloadMetaFile, revisionLoadReloadLooperState,
                                            revisionLoadReloadMasterLoopLength,
                                            revisionLoadReloadNumTracks)) {
                return false;
            }
            revisionLoadReloadActiveLoopIndex.assign(revisionLoadReloadNumTracks, 0);
            for (uint8_t t = 0; t < revisionLoadReloadNumTracks; ++t) {
                Track& track = trackManager.getTrack(t);
                if (!readCurrentSetTrackSlotMetadata(revisionLoadReloadMetaFile, t, track,
                                                     revisionLoadReloadLoadedTrackState[t],
                                                     revisionLoadReloadMuted[t])) {
                    return false;
                }
                revisionLoadReloadAnySlotHasEvents[t] = false;
            }
            revisionLoadReloadTrackCursor = 0;
            revisionLoadReloadSlotCursor = 0;
            revisionLoadReloadRamStage = RevisionLoadReloadRamStage::LoadLoopSlot;
            return true;
        }

        case RevisionLoadReloadRamStage::LoadLoopSlot: {
            if (revisionLoadReloadTrackCursor >= revisionLoadReloadNumTracks) {
                revisionLoadReloadRamStage = RevisionLoadReloadRamStage::ReadFooter;
                return true;
            }
            Track& track = trackManager.getTrack(revisionLoadReloadTrackCursor);
            if (!loadLoopSlotFromCurrentSetSd(revisionLoadReloadTrackCursor,
                                              revisionLoadReloadSlotCursor, track,
                                              revisionLoadReloadAnySlotHasEvents
                                                  [revisionLoadReloadTrackCursor])) {
                return false;
            }
            ++revisionLoadReloadSlotCursor;
            if (revisionLoadReloadSlotCursor >= Config::MAX_LOOPS_PER_TRACK) {
                applyLoadedTrackStateAfterLoopSlots(
                    track, revisionLoadReloadLoadedTrackState[revisionLoadReloadTrackCursor],
                    revisionLoadReloadAnySlotHasEvents[revisionLoadReloadTrackCursor],
                    revisionLoadReloadMuted[revisionLoadReloadTrackCursor]);
                ++revisionLoadReloadTrackCursor;
                revisionLoadReloadSlotCursor = 0;
            }
            return true;
        }

        case RevisionLoadReloadRamStage::ReadFooter: {
            if (!readCurrentSetFileEpilogue(revisionLoadReloadMetaFile, revisionLoadReloadNumTracks,
                                            revisionLoadReloadActiveLoopIndex,
                                            revisionLoadReloadSelectedTrackIdx)) {
                return false;
            }
            if (revisionLoadReloadMetaFileOpen) {
                revisionLoadReloadMetaFile.close();
                revisionLoadReloadMetaFileOpen = false;
            }
            if (!applyLoadedTransportFooter(revisionLoadReloadNumTracks,
                                            revisionLoadReloadActiveLoopIndex,
                                            revisionLoadReloadSelectedTrackIdx, state,
                                            revisionLoadReloadLooperState,
                                            revisionLoadReloadMasterLoopLength)) {
                return false;
            }
            forceCurrentSetFullLoopWrite = false;
            syncCurrentSetDirtyTrackingFromLoadedState();
            clearCurrentSetLoadedFromFolder();
            currentSetAnchorFields = CurrentSetStorage::AnchorFields{};
            resetRevisionLoadReloadRamState();
            revisionLoadStage = RevisionLoadStage::Complete;
            return true;
        }
    }
    return false;
}

bool stepRevisionLoadComplete() {
    Serial.print("[StorageManager] Revision load complete S");
    Serial.print(revisionLoadSetId);
    Serial.print(" v");
    Serial.println(revisionLoadRevisionId);
#if defined(SESSION_CAPTURE)
    char revLoadDetail[40];
    if (revisionLoadUsedDefaultTransport) {
        std::snprintf(revLoadDetail, sizeof(revLoadDetail), "S%04u_v%04u,default_transport",
                      revisionLoadSetId, revisionLoadRevisionId);
    } else {
        std::snprintf(revLoadDetail, sizeof(revLoadDetail), "S%04u_v%04u", revisionLoadSetId,
                      revisionLoadRevisionId);
    }
    SC_PERSIST("rev_load_complete", 0, revisionLoadSetId, revisionLoadRevisionId, revLoadDetail);
#endif
    revisionLoadDisplayRefreshPending = true;
    revisionLoadStage = RevisionLoadStage::Idle;
    revisionLoadInProgress = false;
    revisionLoadPipelineActive = false;
    return true;
}

bool stepRevisionLoadJob(LooperState& state) {
    switch (revisionLoadStage) {
        case RevisionLoadStage::Idle:
            return true;

        case RevisionLoadStage::Validate:
            return beginRevisionLoadValidate();

        case RevisionLoadStage::Write:
            return stepRevisionLoadWrite();

        case RevisionLoadStage::ReloadRam:
            return stepRevisionLoadReloadRam(state);

        case RevisionLoadStage::Complete:
            return stepRevisionLoadComplete();
    }
    return false;
}

bool writeWorkspaceMetaAfterDeferredSave() {
    CurrentWorkspaceStorage::WorkspaceMetaRecord record{};
    record.currentEpoch = currentWorkspaceEpoch;
    record.lastCommittedEpoch = lastCommittedWorkspaceEpoch;
    record.derivedFromSetId = workspaceDerivedFromSetId;
    record.derivedFromRevisionId = workspaceDerivedFromRevisionId;
    record.lastCommittedRevisionId = workspaceLastCommittedRevisionId;
    record.updatedUnix = static_cast<uint64_t>(RtcTime::getUnixTime());
    const uint8_t selectedTrack = trackManager.getSelectedTrackIndex();
    if (selectedTrack < trackManager.getTrackCount()) {
        fillSlotSummariesForTrack(selectedTrack, trackManager.getTrack(selectedTrack),
                                  record.slotSummary, CurrentWorkspaceStorage::kSlotSummaryCount);
    }
    return CurrentWorkspaceStorage::writeWorkspaceMetaFile(record);
}

void loadWorkspaceMetaCountersFromSd() {
    CurrentWorkspaceStorage::WorkspaceMetaRecord record{};
    if (!CurrentWorkspaceStorage::readWorkspaceMetaFile(record)) {
        return;
    }
    currentWorkspaceEpoch = record.currentEpoch;
    lastCommittedWorkspaceEpoch = record.lastCommittedEpoch;
    workspaceDerivedFromSetId = record.derivedFromSetId;
    workspaceDerivedFromRevisionId = record.derivedFromRevisionId;
    workspaceLastCommittedRevisionId = record.lastCommittedRevisionId;
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
    if (!writeRaw(deferredSaveFile, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        return false;
    }
    deferredSaveFile.close();
    if (!CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(
            CurrentSetStorage::kCurrentMetaTempPath)) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kCurrentMetaTempPath)) {
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
    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(deferredSaveLoopFile,
                                                              deferredSaveWorkspaceEpoch)) {
        deferredSaveLoopFile.close();
        deferredSaveLoopFileOpen = false;
        return false;
    }
    deferredSaveLoopFileOpen = true;
    return true;
}

bool finalizeDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (!deferredSaveLoopFileOpen) {
        return false;
    }
    if (!writeRaw(deferredSaveLoopFile, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
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
    if (!CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(tempPath)) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(tempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(tempPath, finalPath);
}

bool shouldWriteCurrentSetLoopSlot(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return true;
    }
    return CurrentSetStorage::shouldWriteLoopPayloadForSlot(
        forceCurrentSetFullLoopWrite, currentSetLoopSlotDirty[trackIndex][slotIndex]);
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
    ++currentWorkspaceEpoch;
    deferredSaveWorkspaceEpoch = currentWorkspaceEpoch;

    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir) ||
        !CurrentSetStorage::ensureDirectory(PersistenceLayout::kRecoveryRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCheckpointsDir)) {
        Serial.println("[StorageManager] ERROR: Could not create MidiLooper/current directories");
        return false;
    }

    deferredSaveFile = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
    if (!deferredSaveFile) {
        Serial.println("[StorageManager] ERROR: Could not open CurrentSet meta temp file");
        return false;
    }
    deferredSaveFile.seek(0);

    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(deferredSaveFile,
                                                              deferredSaveWorkspaceEpoch)) {
        Serial.println("[StorageManager] ERROR: Deferred save failed writing epoch header");
        deferredSaveFile.close();
        return false;
    }

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
                               CapturePassSlotFileHeader& passHeader,
                               const ChunkIdList*& chunkRefs) {
    uint16_t overdubIndex = cursor;
    if (passes.hasRecordPass()) {
        if (cursor == 0) {
            passHeader.id = passes.recordPass.id;
            passHeader.mergeSequence = 0;
            passHeader.stateRaw = static_cast<uint8_t>(passes.recordPass.state);
            passHeader.typeRaw = 0;
            passHeader.sealedAtTick = passes.recordPass.sealedAtTick;
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
    passHeader.id = pass.id;
    passHeader.mergeSequence = pass.mergeSequence;
    passHeader.stateRaw = static_cast<uint8_t>(pass.state);
    passHeader.typeRaw = 1;
    passHeader.sealedAtTick = pass.sealedAtTick;
    chunkRefs = &pass.chunkRefs;
    return true;
}

bool writeDeferredLoopHeader(File& file, LoopId loopId, uint32_t startLoopTick,
                             uint32_t loopLengthTicks, uint32_t loopStartTick,
                             PassId nextPassId, uint32_t nextMergeSequence,
                             PassId lastPublishedPassId, const LoopPasses& passes,
                             LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    if (!persistenceWriteRaw(file, &loopId, sizeof(loopId), crcMode)) return false;
    if (!persistenceWriteRaw(file, &startLoopTick, sizeof(startLoopTick), crcMode)) return false;
    if (!persistenceWriteRaw(file, &loopLengthTicks, sizeof(loopLengthTicks), crcMode)) {
        return false;
    }
    if (!persistenceWriteRaw(file, &loopStartTick, sizeof(loopStartTick), crcMode)) return false;
    if (!persistenceWriteRaw(file, &nextPassId, sizeof(nextPassId), crcMode)) return false;
    if (!persistenceWriteRaw(file, &nextMergeSequence, sizeof(nextMergeSequence), crcMode)) {
        return false;
    }
    if (!persistenceWriteRaw(file, &lastPublishedPassId, sizeof(lastPublishedPassId), crcMode)) {
        return false;
    }

    const uint32_t persistedCount = static_cast<uint32_t>(passes.capturePassCount());
    return persistenceWriteRaw(file, &persistedCount, sizeof(persistedCount), crcMode);
}

bool writeDeferredCapturePassHeader(File& file, const CapturePassSlotFileHeader& passHeader,
                                    const ChunkIdList& chunkRefs,
                                    LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    if (!persistenceWriteRaw(file, &passHeader.id, sizeof(passHeader.id), crcMode)) return false;
    if (!persistenceWriteRaw(file, &passHeader.mergeSequence, sizeof(passHeader.mergeSequence), crcMode)) {
        return false;
    }
    if (!persistenceWriteRaw(file, &passHeader.stateRaw, sizeof(passHeader.stateRaw), crcMode)) return false;
    if (!persistenceWriteRaw(file, &passHeader.typeRaw, sizeof(passHeader.typeRaw), crcMode)) return false;
    if (!persistenceWriteRaw(file, &passHeader.sealedAtTick, sizeof(passHeader.sealedAtTick), crcMode)) {
        return false;
    }

    const uint32_t midiCount =
        static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(chunkRefs));
    return persistenceWriteRaw(file, &midiCount, sizeof(midiCount), crcMode);
}

bool writeDeferredCapturePassChunk(File& file, uint16_t chunkId,
                                   LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    deferredSaveMidiBatch.clear();
    LoopEventStore::appendChunkRefEvent(chunkId, deferredSaveMidiBatch);
    if (deferredSaveMidiBatch.empty()) {
        return true;
    }
    return persistenceWriteRaw(file, deferredSaveMidiBatch.data(),
                              deferredSaveMidiBatch.size() * sizeof(MidiEvent), crcMode);
}

bool stepDeferredLoopPersist(File& file, const Loop& loop, bool& loopDone,
                              LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    loopDone = false;

    switch (deferredLoopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loop.loopId, loop.startLoopTick, loop.loopLengthTicks,
                                         loop.loopStartTick, loop.nextPassId_,
                                         loop.nextMergeSequence_, loop.lastPublishedPassId_,
                                         loop.passes, crcMode)) {
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

            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, deferredSaveCapturePassCursor, passHeader, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, passHeader, *chunkRefs, crcMode)) {
                return false;
            }
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, deferredSaveCapturePassCursor, passHeader, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)passHeader;

            if (deferredSaveChunkCursor < chunkRefs->size()) {
                const uint16_t chunkId = (*chunkRefs)[deferredSaveChunkCursor++];
                return writeDeferredCapturePassChunk(file, chunkId, crcMode);
            }

            ++deferredSaveCapturePassCursor;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;
        }

        case DeferredLoopWriteStage::EditTail: {
            const StorageIo io = crcMode == LoopPersistPayloadCrc::RevisionCommit
                                     ? storageIoFromFileWriteWithRevisionPayloadCrc(file)
                                     : storageIoFromFileWrite(file);
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

            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, deferredSaveCapturePassCursor, passHeader,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, passHeader, *chunkRefs)) {
                return false;
            }
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, deferredSaveCapturePassCursor, passHeader,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)passHeader;

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
                deferredSaveLoopSlotsSkipped += Config::MAX_LOOPS_PER_TRACK;
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
            currentSetLastActiveUnix = lastActiveUnix;
            if (!writeWorkspaceMetaAfterDeferredSave()) {
                Serial.println("[StorageManager] ERROR: Deferred save failed writing workspace.bin");
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

    char autoSavedFolderName[16] = {};
    const bool shouldAutoSave =
        CurrentSetStorage::shouldAutoSaveBeforeLoadIntoCurrent(currentSetAnchorFields);
    if (shouldAutoSave &&
        !saveNewSetInternal(looperState.getLooperState(), autoSavedFolderName,
                            sizeof(autoSavedFolderName))) {
        return false;
    }

    if (!copySavedSetIntoCurrent(sourceSetDir) ||
        !loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir,
                                     looperState.getLooperState())) {
        return false;
    }

    forceCurrentSetFullLoopWrite = false;
    syncCurrentSetDirtyTrackingFromLoadedState();
    setCurrentSetLoadedFromFolder(savedSetFolderName);
    CurrentSetStorage::applyLoadedSetAnchorFields(sourceSequence, currentSetAnchorFields);
    if (!patchCurrentSetAnchor()) {
        return false;
    }
    if (shouldAutoSave && autoSavedFolderName[0] != '\0') {
        const int written = std::snprintf(autoSaveBeforeLoadFolderPending,
                                          sizeof(autoSaveBeforeLoadFolderPending), "%s",
                                          autoSavedFolderName);
        autoSaveBeforeLoadFolderPendingValid =
            written > 0 &&
            static_cast<size_t>(written) < sizeof(autoSaveBeforeLoadFolderPending);
    } else {
        clearAutoSaveBeforeLoadFolderPending();
    }
    return true;
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

uint32_t StorageManager::getCurrentSetLastActiveUnix() {
    return currentSetLastActiveUnix;
}

bool StorageManager::isCurrentWorkspaceDirty() {
    return CurrentWorkspaceStorage::isWorkspaceDirty(currentWorkspaceEpoch,
                                                     lastCommittedWorkspaceEpoch);
}

uint32_t StorageManager::getCurrentWorkspaceEpoch() {
    return currentWorkspaceEpoch;
}

uint32_t StorageManager::getLastCommittedWorkspaceEpoch() {
    return lastCommittedWorkspaceEpoch;
}

StorageManager::SetBrowserOverlayMode StorageManager::getSetBrowserOverlayMode() {
    const bool minimalLoading = RevisionLoadPolicy::isMinimalLoadingOverlayActive(
        revisionLoadPipelineActive, revisionCommitInProgress, revisionLoadPending,
        revisionLoadInProgress);
    return SetBrowserOverlayPolicy::resolveActiveMode(
        setBrowserOverlayNavigation, revisionLoadDirtyPromptActive, minimalLoading);
}

StorageManager::SetBrowserOverlayEntryKind StorageManager::getSetBrowserOverlayEntryKind() {
    return setBrowserOverlayNavigation.entryKind;
}

void StorageManager::resetSetBrowserOverlayNavigation() {
    SetBrowserOverlayPolicy::resetNavigation(setBrowserOverlayNavigation);
}

void StorageManager::setSetBrowserOverlayEntryKind(SetBrowserOverlayEntryKind kind) {
    SetBrowserOverlayPolicy::setEntryKind(setBrowserOverlayNavigation, kind);
}

bool StorageManager::openSetBrowserRevisionHistory(uint16_t setId, uint8_t listSelection,
                                                   uint8_t listScrollOffset) {
    if (setId == 0) {
        return false;
    }
    SetBrowserOverlayPolicy::openRevisionHistory(setBrowserOverlayNavigation, setId, listSelection,
                                                 listScrollOffset);
    return true;
}

bool StorageManager::openSetBrowserLoopPick(uint16_t setId, uint8_t listSelection,
                                            uint8_t listScrollOffset) {
    if (setId == 0) {
        return false;
    }
    SetBrowserOverlayPolicy::openLoopPick(setBrowserOverlayNavigation, setId, listSelection,
                                          listScrollOffset);
    return true;
}

bool StorageManager::navigateSetBrowserOverlayBack(uint8_t& outListSelection,
                                                   uint8_t& outListScrollOffset) {
    return SetBrowserOverlayPolicy::navigateBack(setBrowserOverlayNavigation, outListSelection,
                                                 outListScrollOffset);
}

uint16_t StorageManager::getSetBrowserOverlayDrilledSetId() {
    return setBrowserOverlayNavigation.drilledSetId;
}

bool StorageManager::isRevisionLoadDirtyPromptActive() {
    return revisionLoadDirtyPromptActive;
}

uint8_t StorageManager::getRevisionLoadDirtyPromptSelection() {
    return revisionLoadDirtyPromptSelection;
}

void StorageManager::adjustRevisionLoadDirtyPromptSelection(int delta) {
    if (!revisionLoadDirtyPromptActive || delta == 0) {
        return;
    }
    int next = static_cast<int>(revisionLoadDirtyPromptSelection) + delta;
    if (next < 0) {
        next = 0;
    } else if (next >= static_cast<int>(RevisionLoadPolicy::kDirtyPromptRowCount)) {
        next = static_cast<int>(RevisionLoadPolicy::kDirtyPromptRowCount) - 1;
    }
    revisionLoadDirtyPromptSelection = static_cast<uint8_t>(next);
}

void StorageManager::confirmRevisionLoadDirtyPromptSaveThenLoad() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    if (!revisionLoadDirtyPromptActive || !revisionLoadRequestStaged) {
        return;
    }
    revisionLoadDirtyPromptActive = false;
    revisionLoadPipelineActive = true;
    revisionLoadSaveThenLoadPipeline = true;
    SC_PERSIST("rev_load_dirty_yes", 0, revisionLoadStagedSetId, revisionLoadStagedRevisionId,
               "save_then_load");
    requestCommitRevision();
#endif
}

void StorageManager::confirmRevisionLoadDirtyPromptDiscard() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    if (!revisionLoadDirtyPromptActive || !revisionLoadRequestStaged) {
        return;
    }
    revisionLoadDirtyPromptActive = false;
    revisionLoadSaveThenLoadPipeline = false;
    SC_PERSIST("rev_load_dirty_no", 0, revisionLoadStagedSetId, revisionLoadStagedRevisionId,
               "discard_load");
    dispatchStagedRevisionLoad();
#endif
}

void StorageManager::cancelRevisionLoadDirtyPrompt() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    if (!revisionLoadDirtyPromptActive) {
        return;
    }
    SC_PERSIST("rev_load_dirty_cancel", 0, revisionLoadStagedSetId, revisionLoadStagedRevisionId,
               "cancel");
    clearRevisionLoadPromptAndPipelineState();
#endif
}

bool StorageManager::copyCurrentSetLoadedFromFolder(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || currentSetLoadedFromFolder[0] == '\0') {
        return false;
    }
    const int written = std::snprintf(out, outSize, "%s", currentSetLoadedFromFolder);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

bool StorageManager::consumeAutoSaveBeforeLoadFolder(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || !autoSaveBeforeLoadFolderPendingValid) {
        return false;
    }
    const int written =
        std::snprintf(out, outSize, "%s", autoSaveBeforeLoadFolderPending);
    const bool copied =
        written > 0 && static_cast<size_t>(written) < outSize;
    clearAutoSaveBeforeLoadFolderPending();
    return copied;
}

bool readSavedSetMetadataFromMetaPath(const char* metaPath,
                                      SavedSetCatalog::SavedSetMetadata& metadata) {
    File file = SD.open(metaPath, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    const size_t trailerTailSize =
        SavedSetCatalog::kSavedSetMetadataTrailerByteSize + sizeof(CurrentSetStorage::kSaveFileToken);
    if (fileSize < trailerTailSize) {
        file.close();
        return false;
    }
    if (!file.seek(fileSize - trailerTailSize)) {
        file.close();
        return false;
    }
    const StorageIo trailerIo = storageIoFromFileRead(file);
    const bool ok = SavedSetCatalog::readSavedSetMetadataTrailer(trailerIo, metadata);
    file.close();
    return ok;
}

size_t StorageManager::listSavedSetFolderEntries(SavedSetCatalog::SavedSetFolderListEntry* entries,
                                                 size_t maxEntries) {
    if (entries == nullptr || maxEntries == 0 ||
        !SD.exists(CurrentSetStorage::kSetsArchiveDir)) {
        return 0;
    }

    size_t count = 0;
    File dir = SD.open(CurrentSetStorage::kSetsArchiveDir);
    if (!dir) {
        return 0;
    }
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        entry.close();
        uint32_t sequence = 0;
        if (!isDirectory || !parseSavedSetSequence(name, sequence) || sequence == 0) {
            continue;
        }
        const char* baseName = std::strrchr(name, '/');
        if (baseName != nullptr) {
            baseName += 1;
        } else {
            baseName = name;
        }
        if (count < maxEntries) {
            const int written = std::snprintf(entries[count].folderName,
                                              sizeof(entries[count].folderName), "%s", baseName);
            if (written <= 0 ||
                static_cast<size_t>(written) >= sizeof(entries[count].folderName)) {
                continue;
            }
            entries[count].sequence = sequence;
        }
        ++count;
    }
    dir.close();

    const size_t sortCount = count < maxEntries ? count : maxEntries;
    for (size_t i = 0; i + 1 < sortCount; ++i) {
        for (size_t j = i + 1; j < sortCount; ++j) {
            if (entries[j].sequence > entries[i].sequence) {
                const SavedSetCatalog::SavedSetFolderListEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
    return count < maxEntries ? count : maxEntries;
}

bool StorageManager::readSavedSetMetadataForFolder(const char* folderName,
                                                   SavedSetCatalog::SavedSetMetadata& metadata) {
    if (folderName == nullptr || folderName[0] == '\0') {
        return false;
    }
    char metaPath[kSavedSetPathCapacity];
    const int written =
        std::snprintf(metaPath, sizeof(metaPath), "%s/%s/%s", CurrentSetStorage::kSetsRoot,
                      folderName, CurrentSetStorage::kSetBinFileName);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(metaPath)) {
        return false;
    }
    return readSavedSetMetadataFromMetaPath(metaPath, metadata);
}

bool StorageManager::readCurrentSetBrowserMetadata(SavedSetCatalog::SavedSetMetadata& metadata) {
    const uint32_t sequence = currentSetAnchorFields.loadedFromSequence;
    if (!buildSavedSetMetadata(sequence != 0 ? sequence : 1,
                               SavedSetCatalog::FolderNamingMode::Unknown,
                               currentSetLastActiveUnix, metadata)) {
        return false;
    }
    metadata.sequence = sequence;
    metadata.createdAtUnix = currentSetLastActiveUnix;
    metadata.userLabel[0] = '\0';
    return true;
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
    resetRevisionCommitJobState();
    resetRevisionLoadJobState();
    return;
#endif
    deferredSaveSdIoActive = false;
    revisionCommitSdIoActive = false;
    revisionLoadSdIoActive = false;

    const uint32_t sliceBudgetUs = resolvePersistenceSliceBudgetUs(state);
    const uint32_t sliceStartUs = micros();

    auto sliceBudgetExhausted = [&]() {
        return PersistenceBudget::persistenceSliceBudgetExhausted(sliceBudgetUs,
                                                                  micros() - sliceStartUs);
    };

    if (bootRevisionRecoveryPending && !revisionLoadPending && !revisionLoadInProgress &&
        !revisionCommitPending && !revisionCommitInProgress && !deferredSavePending &&
        !deferredSaveInProgress) {
        bootRevisionRecoveryPending = false;
        revisionLoadSetId = bootRevisionRecoverySetId;
        revisionLoadRevisionId = bootRevisionRecoveryRevisionId;
        revisionLoadPending = true;
        revisionLoadPipelineActive = true;
        Serial.print("[StorageManager] Boot recovery: queued revision load S");
        Serial.print(revisionLoadSetId);
        Serial.print(" v");
        Serial.println(revisionLoadRevisionId);
    }

    while (!sliceBudgetExhausted()) {
        const bool revisionBlockedByDeferredSave =
            revisionCommitPending && (deferredSavePending || deferredSaveInProgress);
        const bool revisionBlockedByLoad =
            revisionCommitPending && (revisionLoadPending || revisionLoadInProgress);
        if (revisionBlockedByDeferredSave || revisionBlockedByLoad) {
            const uint32_t nowMs = millis();
            if (nowMs - lastRevisionCommitBlockedLogAtMs >= 5000U) {
                lastRevisionCommitBlockedLogAtMs = nowMs;
                SC_PERSIST("rev_blocked", 0, deferredSavePending ? 1U : 0U,
                           deferredSaveInProgress ? 1U : 0U,
                           revisionBlockedByLoad ? "load_active" : "deferred_save_active");
            }
        } else if (revisionCommitInProgress || revisionCommitPending) {
            if (!revisionCommitInProgress && revisionCommitPending) {
                revisionCommitPending = false;
                revisionCommitInProgress = true;
                revisionCommitStage = RevisionCommitStage::Snapshot;
                SC_PERSIST("rev_dispatch", 0, 0, 0, "run");
            }

            const uint32_t ioStartUs = micros();
            revisionCommitSdIoActive = true;
            const bool revStepOk = stepRevisionCommitJob();
            revisionCommitSdIoActive = false;
            deferredSaveDisplayBlockUs += micros() - ioStartUs;

            if (!revStepOk) {
                Serial.print("[StorageManager] ERROR: Revision commit failed at stage ");
                Serial.println(static_cast<uint8_t>(revisionCommitStage));
                resetRevisionCommitJobState();
                break;
            }
            if (revisionCommitInProgress) {
                break;
            }
            if (RevisionLoadPolicy::shouldDispatchStagedLoadAfterCommitComplete(
                    revisionLoadSaveThenLoadPipeline, true)) {
                revisionLoadSaveThenLoadPipeline = false;
                dispatchStagedRevisionLoad();
            }
            continue;
        }

        const bool revisionLoadBlockedByDeferredSave =
            revisionLoadPending && (deferredSavePending || deferredSaveInProgress);
        const bool revisionLoadBlockedByCommit =
            revisionLoadPending && (revisionCommitPending || revisionCommitInProgress);
        if (revisionLoadBlockedByDeferredSave || revisionLoadBlockedByCommit) {
            const uint32_t nowMs = millis();
            if (nowMs - lastRevisionLoadBlockedLogAtMs >= 5000U) {
                lastRevisionLoadBlockedLogAtMs = nowMs;
                SC_PERSIST("rev_load_blocked", 0,
                           revisionLoadBlockedByDeferredSave ? 1U : 0U,
                           revisionLoadBlockedByCommit ? 1U : 0U,
                           revisionLoadBlockedByDeferredSave ? "deferred_save_active"
                                                               : "commit_active");
            }
        } else if (revisionLoadInProgress || revisionLoadPending) {
            if (!revisionLoadInProgress && revisionLoadPending) {
                revisionLoadPending = false;
                revisionLoadInProgress = true;
                revisionLoadStage = RevisionLoadStage::Validate;
                SC_PERSIST("rev_load_dispatch", 0, revisionLoadSetId, revisionLoadRevisionId,
                           "run");
            }

            const uint32_t ioStartUs = micros();
            revisionLoadSdIoActive = true;
            const bool revLoadStepOk =
                stepRevisionLoadJob(const_cast<LooperState&>(state));
            revisionLoadSdIoActive = false;
            deferredSaveDisplayBlockUs += micros() - ioStartUs;

            if (!revLoadStepOk) {
                Serial.print("[StorageManager] ERROR: Revision load failed at stage ");
                Serial.println(static_cast<uint8_t>(revisionLoadStage));
                resetRevisionLoadJobState();
                break;
            }
            if (revisionLoadInProgress) {
                break;
            }
            continue;
        }

        if (!deferredSavePending && !deferredSaveInProgress) {
            break;
        }

        if (isCaptureActiveForPersistence()) {
            break;
        }

        if (!deferredSaveInProgress) {
            if (deferredSaveAdmissionHeap != UINT32_MAX &&
                !LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(
                    deferredSaveAdmissionHeap)) {
                if (!deferredSaveHeapFloorDeferred) {
                    SC_PERSIST("defer", 0, deferredSaveAdmissionHeap, deferredSaveAdmissionHeap,
                               "heap_floor");
                    deferredSaveHeapFloorDeferred = true;
                }
                break;
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
                break;
            }
            deferredSaveUrgentRequested = false;
            break;
        }

        if (!deferredSaveInProgress) {
            break;
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
            break;
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
        break;
    }
}

void StorageManager::requestCommitRevision() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    revisionCommitPending = true;
    SC_PERSIST("rev_request", 0, 0, 0, "queued");
}

bool StorageManager::hasRevisionCommitWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return revisionCommitPending || revisionCommitInProgress;
#endif
}

bool StorageManager::isRevisionCommitActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return revisionCommitSdIoActive;
#endif
}

void StorageManager::requestLoadRevision(uint16_t setId, uint16_t revisionId) {
#if BYPASS_STOP_UNDO_SAVE
    (void)setId;
    (void)revisionId;
    return;
#endif
    if (setId == 0 || revisionId == 0) {
        return;
    }
    if (revisionLoadDirtyPromptActive || revisionLoadPipelineActive || revisionLoadPending ||
        revisionLoadInProgress) {
        return;
    }
    revisionLoadStagedSetId = setId;
    revisionLoadStagedRevisionId = revisionId;
    revisionLoadRequestStaged = true;
    revisionLoadDirtyPromptSelection = 0;

    if (RevisionLoadPolicy::resolveLoadRequestGate(isCurrentWorkspaceDirty()) ==
        RevisionLoadPolicy::LoadRequestGate::ShowDirtyPrompt) {
        revisionLoadDirtyPromptActive = true;
        SC_PERSIST("rev_load_dirty_prompt", 0, setId, revisionId, "shown");
        return;
    }

    dispatchStagedRevisionLoad();
}

bool StorageManager::hasRevisionLoadWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return revisionLoadPending || revisionLoadInProgress;
#endif
}

bool StorageManager::isRevisionLoadActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return revisionLoadSdIoActive;
#endif
}

#if defined(SESSION_CAPTURE)
void StorageManager::requestLoadRevisionForHitl(uint16_t setId, uint16_t revisionId) {
    requestLoadRevision(setId, revisionId);
    SC_PERSIST("rev_load_hitl_arm", 0, setId, revisionId, "armed");
}

void StorageManager::confirmRevisionLoadDirtyPromptSaveThenLoadForHitl() {
    confirmRevisionLoadDirtyPromptSaveThenLoad();
}

void StorageManager::confirmRevisionLoadDirtyPromptDiscardForHitl() {
    confirmRevisionLoadDirtyPromptDiscard();
}

void StorageManager::cancelRevisionLoadDirtyPromptForHitl() {
    cancelRevisionLoadDirtyPrompt();
}
#endif

#if defined(SESSION_CAPTURE)
bool removeEmptyDirectoryIfPresent(const char* path) {
    if (path == nullptr || path[0] == '\0' || !SD.exists(path)) {
        return true;
    }
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        return false;
    }
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        entry.close();
        return false;
    }
    dir.close();
    return SD.rmdir(path);
}

bool parseRevisionSetFolderEntryName(const char* name, uint16_t& setIdOut) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    const char* base = name;
    if (const char* slash = std::strrchr(name, '/')) {
        base = slash + 1;
    }
    if (base[0] != 'S') {
        return false;
    }
    unsigned setId = 0;
    for (const char* cursor = base + 1; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        setId = setId * 10U + static_cast<unsigned>(*cursor - '0');
    }
    if (setId == 0U || setId > 0xFFFFU) {
        return false;
    }
    setIdOut = static_cast<uint16_t>(setId);
    return true;
}

bool removeHitlSetFolderTree(const char* setFolderPath) {
    if (setFolderPath == nullptr || setFolderPath[0] == '\0') {
        return false;
    }
    char revisionsDir[56];
    const int revisionsWritten =
        std::snprintf(revisionsDir, sizeof(revisionsDir), "%s/revisions", setFolderPath);
    if (revisionsWritten <= 0 ||
        static_cast<size_t>(revisionsWritten) >= sizeof(revisionsDir)) {
        return false;
    }
    if (SD.exists(revisionsDir)) {
        File revisions = SD.open(revisionsDir);
        if (revisions && revisions.isDirectory()) {
            while (true) {
                File entry = revisions.openNextFile();
                if (!entry) {
                    break;
                }
                char entryPath[96];
                const char* name = entry.name();
                entry.close();
                const int entryWritten =
                    std::snprintf(entryPath, sizeof(entryPath), "%s/%s", revisionsDir, name);
                if (entryWritten <= 0 ||
                    static_cast<size_t>(entryWritten) >= sizeof(entryPath)) {
                    return false;
                }
                if (!SD.remove(entryPath)) {
                    return false;
                }
            }
            revisions.close();
        } else if (revisions) {
            revisions.close();
        }
        if (!removeEmptyDirectoryIfPresent(revisionsDir)) {
            return false;
        }
    }

    char setMetaPath[64];
    const int metaWritten =
        std::snprintf(setMetaPath, sizeof(setMetaPath), "%s/set.bin", setFolderPath);
    if (metaWritten > 0 && static_cast<size_t>(metaWritten) < sizeof(setMetaPath) &&
        SD.exists(setMetaPath)) {
        if (!SD.remove(setMetaPath)) {
            return false;
        }
    }
    char setMetaTempPath[68];
    const int metaTempWritten =
        std::snprintf(setMetaTempPath, sizeof(setMetaTempPath), "%s/set.bin.tmp", setFolderPath);
    if (metaTempWritten > 0 && static_cast<size_t>(metaTempWritten) < sizeof(setMetaTempPath) &&
        SD.exists(setMetaTempPath)) {
        (void)SD.remove(setMetaTempPath);
    }
    return removeEmptyDirectoryIfPresent(setFolderPath);
}

bool StorageManager::nukeHitlSetsCatalog() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    if (revisionCommitPending || revisionCommitInProgress || revisionLoadPending ||
        revisionLoadInProgress) {
        SC_PERSIST("rev_nuke_sets", 0, 0, 0, "active_job");
        return false;
    }

    SC_PERSIST("rev_nuke_sets", 0, 0, 0, "started");
    hitlRevisionCommitBackup = HitlRevisionCommitBackup{};

    constexpr uint8_t kMaxSetFolders = 32;
    char setFolderPaths[kMaxSetFolders][52];
    uint8_t setFolderCount = 0;

    if (SD.exists(SetRevisionCatalog::kSetsRoot)) {
        File setsDir = SD.open(SetRevisionCatalog::kSetsRoot);
        if (setsDir) {
            while (true) {
                File entry = setsDir.openNextFile();
                if (!entry) {
                    break;
                }
                const bool isDirectory = entry.isDirectory();
                const char* name = entry.name();
                entry.close();
                if (!isDirectory || setFolderCount >= kMaxSetFolders) {
                    continue;
                }
                uint16_t setId = 0;
                if (!parseRevisionSetFolderEntryName(name, setId)) {
                    continue;
                }
                if (!SetRevisionCatalog::formatSetFolderPath(setFolderPaths[setFolderCount],
                                                              sizeof(setFolderPaths[0]), setId)) {
                    continue;
                }
                ++setFolderCount;
            }
            setsDir.close();
        }
    }

    bool ok = true;
    for (uint8_t i = 0; i < setFolderCount; ++i) {
        if (!removeHitlSetFolderTree(setFolderPaths[i])) {
            ok = false;
        }
    }

    if (SD.exists(SetRevisionCatalog::kSetIndexPath)) {
        ok = SD.remove(SetRevisionCatalog::kSetIndexPath) && ok;
    }
    if (SD.exists(SetRevisionCatalog::kSetIndexTempPath)) {
        (void)SD.remove(SetRevisionCatalog::kSetIndexTempPath);
    }

    workspaceDerivedFromSetId = 0;
    workspaceDerivedFromRevisionId = 0;
    workspaceLastCommittedRevisionId = 0;
    if (!writeWorkspaceMetaAfterDeferredSave()) {
        ok = false;
    }

    SC_PERSIST("rev_nuke_sets", 0, setFolderCount, 0, ok ? "ok" : "failed");
    Serial.print("[StorageManager] HITL sets catalog nuke removed ");
    Serial.print(setFolderCount);
    Serial.println(ok ? " folders" : " folders (partial failure)");
    return ok;
#endif
}

void StorageManager::requestCommitRevisionForHitl() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    hitlRevisionCommitBackup = HitlRevisionCommitBackup{};
    hitlRevisionCommitBackup.armed = true;
    hitlRevisionCommitBackup.workspaceCurrentEpoch = currentWorkspaceEpoch;
    hitlRevisionCommitBackup.workspaceLastCommittedEpoch = lastCommittedWorkspaceEpoch;
    hitlRevisionCommitBackup.workspaceDerivedFromSetId = workspaceDerivedFromSetId;
    hitlRevisionCommitBackup.workspaceDerivedFromRevisionId = workspaceDerivedFromRevisionId;
    hitlRevisionCommitBackup.workspaceLastCommittedRevisionId = workspaceLastCommittedRevisionId;

    if (SD.exists(SetRevisionCatalog::kSetIndexPath)) {
        File indexFile = SD.open(SetRevisionCatalog::kSetIndexPath, FILE_READ);
        if (indexFile) {
            const StorageIo indexIo = storageIoFromFileRead(indexFile);
            hitlRevisionCommitBackup.hadIndexOnSd =
                SetRevisionCatalog::readSetCatalogIndex(indexIo,
                                                        hitlRevisionCommitBackup.catalogIndex);
            indexFile.close();
        }
    }

    if (workspaceDerivedFromSetId != 0) {
        char setMetaPath[64];
        if (SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                                  workspaceDerivedFromSetId)) {
            File setMetaFile = SD.open(setMetaPath, FILE_READ);
            if (setMetaFile) {
                const StorageIo setMetaIo = storageIoFromFileRead(setMetaFile);
                hitlRevisionCommitBackup.hadSetMetaOnSd = SetRevisionCatalog::readSetMetaRecord(
                    setMetaIo, hitlRevisionCommitBackup.setMeta);
                setMetaFile.close();
            }
        }
    }

    requestCommitRevision();
    SC_PERSIST("rev_hitl_arm", 0, 0, 0, "armed");
#endif
}

bool StorageManager::cleanupHitlRevisionCommit() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    if (!hitlRevisionCommitBackup.armed) {
        SC_PERSIST("rev_cleanup", 0, 0, 0, "not_armed");
        return false;
    }

    bool ok = true;
    if (hitlRevisionCommitBackup.revisionFinalPath[0] != '\0' &&
        SD.exists(hitlRevisionCommitBackup.revisionFinalPath)) {
        ok = SD.remove(hitlRevisionCommitBackup.revisionFinalPath) && ok;
    }
    char revisionTempPath[84];
    const int tempWritten = std::snprintf(revisionTempPath, sizeof(revisionTempPath), "%s.tmp",
                                          hitlRevisionCommitBackup.revisionFinalPath);
    if (tempWritten > 0 && static_cast<size_t>(tempWritten) < sizeof(revisionTempPath) &&
        SD.exists(revisionTempPath)) {
        (void)SD.remove(revisionTempPath);
    }

    if (hitlRevisionCommitBackup.createdNewSetFolder) {
        ok = removeHitlSetFolderTree(hitlRevisionCommitBackup.setFolderPath) && ok;
        if (hitlRevisionCommitBackup.hadIndexOnSd) {
            ok = writeSetCatalogIndexFile(hitlRevisionCommitBackup.catalogIndex) && ok;
        } else if (SD.exists(SetRevisionCatalog::kSetIndexPath)) {
            ok = SD.remove(SetRevisionCatalog::kSetIndexPath) && ok;
        }
    } else if (hitlRevisionCommitBackup.hadSetMetaOnSd) {
        ok = writeSetMetaRecordFile(hitlRevisionCommitBackup.committedSetId,
                                    hitlRevisionCommitBackup.setMeta) &&
             ok;
    }

    currentWorkspaceEpoch = hitlRevisionCommitBackup.workspaceCurrentEpoch;
    lastCommittedWorkspaceEpoch = hitlRevisionCommitBackup.workspaceLastCommittedEpoch;
    workspaceDerivedFromSetId = hitlRevisionCommitBackup.workspaceDerivedFromSetId;
    workspaceDerivedFromRevisionId = hitlRevisionCommitBackup.workspaceDerivedFromRevisionId;
    workspaceLastCommittedRevisionId = hitlRevisionCommitBackup.workspaceLastCommittedRevisionId;
    if (!writeWorkspaceMetaAfterDeferredSave()) {
        ok = false;
    }

    hitlRevisionCommitBackup = HitlRevisionCommitBackup{};
    SC_PERSIST("rev_cleanup", 0, 0, 0, ok ? "ok" : "failed");
    return ok;
#endif
}

void StorageManager::processHitlSerialCommands() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    static char lineBuffer[48];
    static size_t lineLength = 0;
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            lineBuffer[lineLength] = '\0';
            if (std::strcmp(lineBuffer, "!REV_COMMIT") == 0) {
                requestCommitRevisionForHitl();
            } else if (std::strcmp(lineBuffer, "!REV_CLEANUP") == 0) {
                cleanupHitlRevisionCommit();
            } else if (std::strcmp(lineBuffer, "!REV_NUKE_SETS") == 0) {
                nukeHitlSetsCatalog();
            } else if (std::strcmp(lineBuffer, "!REV_LOAD_DIRTY_YES") == 0) {
                confirmRevisionLoadDirtyPromptSaveThenLoadForHitl();
            } else if (std::strcmp(lineBuffer, "!REV_LOAD_DIRTY_NO") == 0) {
                confirmRevisionLoadDirtyPromptDiscardForHitl();
            } else if (std::strcmp(lineBuffer, "!REV_LOAD_DIRTY_CANCEL") == 0) {
                cancelRevisionLoadDirtyPromptForHitl();
            } else if (std::strncmp(lineBuffer, "!REV_LOAD ", 10) == 0) {
                const char* cursor = lineBuffer + 10;
                unsigned setId = 0;
                unsigned revisionId = 0;
                while (*cursor == ' ') {
                    ++cursor;
                }
                while (*cursor >= '0' && *cursor <= '9') {
                    setId = setId * 10U + static_cast<unsigned>(*cursor - '0');
                    ++cursor;
                }
                while (*cursor == ' ') {
                    ++cursor;
                }
                while (*cursor >= '0' && *cursor <= '9') {
                    revisionId = revisionId * 10U + static_cast<unsigned>(*cursor - '0');
                    ++cursor;
                }
                if (setId > 0U && setId <= 0xFFFFU && revisionId > 0U && revisionId <= 0xFFFFU) {
                    requestLoadRevisionForHitl(static_cast<uint16_t>(setId),
                                               static_cast<uint16_t>(revisionId));
                }
            }
            lineLength = 0;
            continue;
        }
        if (lineLength + 1 < sizeof(lineBuffer)) {
            lineBuffer[lineLength++] = ch;
        }
    }
#endif
}
#endif

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

bool StorageManager::consumeRevisionLoadDisplayRefreshPending() {
    if (!revisionLoadDisplayRefreshPending) {
        return false;
    }
    revisionLoadDisplayRefreshPending = false;
    return true;
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
    if (fileSize < sizeof(CurrentSetStorage::kSaveFileToken)) {
        return false;
    }
    size_t payloadOffset = 0;
    if (CurrentWorkspaceStorage::fileStartsWithEpochHeader(file)) {
        CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
        const StorageIo epochIo = storageIoFromFileRead(file);
        if (!CurrentWorkspaceStorage::readEpochFileHeader(epochIo, epochHeader)) {
            return false;
        }
        payloadOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
    }
    if (fileSize < payloadOffset + sizeof(CurrentSetStorage::kSaveFileToken)) {
        return false;
    }
    const size_t payloadSize = fileSize - payloadOffset - sizeof(CurrentSetStorage::kSaveFileToken);
    if (!file.seek(payloadOffset)) {
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
        Serial.println("[StorageManager] ERROR: readLoopPersisted failed for current loop file");
        return false;
    }
    uint32_t magic = 0;
    return readRaw(file, &magic, sizeof(magic)) && magic == CurrentSetStorage::kSaveFileToken;
}

static void applyLoadedTrackStateAfterLoopSlots(Track& track, TrackState loadedTrackState,
                                                bool anySlotHasEvents, bool muted) {
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

static bool loadLoopSlotFromCurrentSetSd(uint8_t trackIndex, uint8_t slotIndex, Track& track,
                                         bool& anySlotHasEventsOut) {
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        return false;
    }
    Loop& loop = track.getLoop(slotIndex);
    if (!SD.exists(loopPath)) {
        resetLoopSlotToEmpty(loop, slotIndex);
        return true;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(loopPath)) {
        Serial.print("[StorageManager] WARN: loop file incomplete, treating slot as empty ");
        Serial.println(loopPath);
        resetLoopSlotToEmpty(loop, slotIndex);
        return true;
    }
    File loopFile = SD.open(loopPath, FILE_READ);
    if (!loopFile) {
        Serial.print("[StorageManager] WARN: could not open loop file, treating slot as empty ");
        Serial.println(loopPath);
        resetLoopSlotToEmpty(loop, slotIndex);
        return true;
    }
    const bool readOk = readLoopFromCurrentSetFile(loopFile, loop);
    loopFile.close();
    if (!readOk) {
        Serial.print("[StorageManager] WARN: loop read failed, treating slot as empty ");
        Serial.println(loopPath);
        resetLoopSlotToEmpty(loop, slotIndex);
        return true;
    }
    if (loop.hasPublishedEvents()) {
        anySlotHasEventsOut = true;
    }
    return true;
}

static bool readCurrentSetTrackSlotMetadata(File& file, uint8_t trackIndex, Track& track,
                                            TrackState& loadedTrackStateOut, bool& mutedOut) {
    track.ensureLoopsAllocated();

    uint32_t trackStateRaw = 0;
    if (!readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
        return false;
    }
    loadedTrackStateOut = static_cast<TrackState>(trackStateRaw);

    bool muted = false;
    if (!readRaw(file, &muted, sizeof(muted))) {
        return false;
    }
    mutedOut = muted;

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
        trackManager.setSlotEnabled(trackIndex, s, slotEnabled);
        trackManager.setSlotMuted(trackIndex, s, slotMuted);
        track.getLoop(s).loopId = slotLoopId;
    }
    return true;
}

static bool readCurrentSetFilePreamble(File& file, LooperState& loadedLooperStateOut,
                                       uint32_t& masterLoopLengthOut, uint8_t& numTracksOut) {
    if (!CurrentWorkspaceStorage::fileStartsWithEpochHeader(file)) {
        if (!file.seek(0)) {
            return false;
        }
    } else {
        CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
        const StorageIo epochIo = storageIoFromFileRead(file);
        if (!CurrentWorkspaceStorage::readEpochFileHeader(epochIo, epochHeader)) {
            Serial.println("[StorageManager] ERROR: Failed to read Current epoch header");
            return false;
        }
        currentWorkspaceEpoch = epochHeader.epoch;
        deferredSaveWorkspaceEpoch = epochHeader.epoch;
    }

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
    currentSetLastActiveUnix = metaHeader.lastActiveUnix;
    currentSetAnchorFields = metaHeader.anchor;
    if (currentSetAnchorFields.loadedFromSequence != 0) {
        if (!resolveSavedSetFolderNameBySequence(currentSetAnchorFields.loadedFromSequence,
                                                 currentSetLoadedFromFolder,
                                                 sizeof(currentSetLoadedFromFolder))) {
            std::snprintf(currentSetLoadedFromFolder, sizeof(currentSetLoadedFromFolder),
                          "%05lu",
                          static_cast<unsigned long>(currentSetAnchorFields.loadedFromSequence));
        }
    } else {
        clearCurrentSetLoadedFromFolder();
    }

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
    loadedLooperStateOut = sanitizeLoadedLooperState(static_cast<LooperState>(looperStateVal));

    uint32_t masterLoopLength = 0;
    if (!readRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        return false;
    }
    masterLoopLengthOut = masterLoopLength;

    uint8_t numTracks = 0;
    if (!readRaw(file, &numTracks, sizeof(numTracks)) || numTracks != Config::NUM_TRACKS) {
        return false;
    }
    numTracksOut = numTracks;
    return true;
}

static bool readCurrentSetFileEpilogue(File& file, uint8_t numTracks,
                                       std::vector<uint8_t>& activeLoopIndex,
                                       uint8_t& selectedTrackIdxOut) {
    if (!readRaw(file, &selectedTrackIdxOut, sizeof(selectedTrackIdxOut))) {
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
    if (tailMarker != CurrentSetStorage::kSaveFileToken) {
        Serial.println("[StorageManager] ERROR: CurrentSet meta completion marker mismatch");
        return false;
    }
    return true;
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
    (void)setDir;
    LooperState loadedLooperState = LOOPER_IDLE;
    uint32_t masterLoopLength = 0;
    uint8_t numTracks = 0;
    if (!readCurrentSetFilePreamble(file, loadedLooperState, masterLoopLength, numTracks)) {
        return false;
    }

    activeLoopIndex.assign(numTracks, 0);
    for (uint8_t t = 0; t < numTracks; ++t) {
        Track& track = trackManager.getTrack(t);
        TrackState loadedTrackState = TRACK_EMPTY;
        bool muted = false;
        if (!readCurrentSetTrackSlotMetadata(file, t, track, loadedTrackState, muted)) {
            return false;
        }

        bool anySlotHasEvents = false;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (!loadLoopSlotFromCurrentSetSd(t, s, track, anySlotHasEvents)) {
                return false;
            }
        }
        applyLoadedTrackStateAfterLoopSlots(track, loadedTrackState, anySlotHasEvents, muted);
    }

    if (!readCurrentSetFileEpilogue(file, numTracks, activeLoopIndex, selectedTrackIdx)) {
        return false;
    }

    return applyLoadedTransportFooter(numTracks, activeLoopIndex, selectedTrackIdx, state,
                                      loadedLooperState, masterLoopLength);
}

bool StorageManager::loadCurrentSetFromDirectory(const char* setDir, LooperState& state) {
    char metaPath[80];
    if (setDir != nullptr && std::strcmp(setDir, CurrentSetStorage::kCurrentSetDir) == 0) {
        const int written = std::snprintf(metaPath, sizeof(metaPath), "%s",
                                        CurrentSetStorage::kCurrentRuntimeBundlePath);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(metaPath)) {
            return false;
        }
    } else {
        const int written = std::snprintf(metaPath, sizeof(metaPath), "%s/%s", setDir,
                                          CurrentSetStorage::kSetBinFileName);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(metaPath)) {
            return false;
        }
    }
    if (!SD.exists(metaPath)) {
        Serial.print("[StorageManager] ERROR: CurrentSet meta missing ");
        Serial.println(metaPath);
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(metaPath)) {
        Serial.print("[StorageManager] ERROR: CurrentSet meta incomplete ");
        Serial.println(metaPath);
        return false;
    }
    File file = SD.open(metaPath, FILE_READ);
    if (!file) {
        Serial.print("[StorageManager] ERROR: Could not open CurrentSet meta ");
        Serial.println(metaPath);
        return false;
    }
    std::vector<uint8_t> activeLoopIndex;
    uint8_t selectedTrackIdx = 0;
    const bool ok = loadCurrentSetMetaAndTracks(file, setDir, state, activeLoopIndex, selectedTrackIdx);
    file.close();
    return ok;
}

bool StorageManager::loadCurrentWorkspaceFromSd(LooperState& state) {
    return loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir, state);
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

        uint32_t svokToken = 0;
        if (!readRaw(file, &svokToken, sizeof(svokToken))) {
            Serial.println("[StorageManager] ERROR: Failed to read storage completion marker for v4");
            return failAfterPartialLoad();
        }
        if (svokToken != CurrentSetStorage::kSaveFileToken) {
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

bool readSetLatestRevisionIdFromSd(uint16_t setId, uint16_t& latestRevisionIdOut) {
    latestRevisionIdOut = 0;
    char setMetaPath[64];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId)) {
        return false;
    }
    if (!SD.exists(setMetaPath)) {
        return false;
    }
    File file = SD.open(setMetaPath, FILE_READ);
    if (!file) {
        return false;
    }
    SetRevisionCatalog::SetMetaRecord meta{};
    const StorageIo io = storageIoFromFileRead(file);
    const bool ok = SetRevisionCatalog::readSetMetaRecord(io, meta);
    file.close();
    if (!ok || meta.latestRevisionId == 0) {
        return false;
    }
    latestRevisionIdOut = meta.latestRevisionId;
    return true;
}

void queueBootRevisionRecovery(uint16_t setId, uint16_t revisionId) {
    if (setId == 0 || revisionId == 0) {
        return;
    }
    bootRevisionRecoverySetId = setId;
    bootRevisionRecoveryRevisionId = revisionId;
    bootRevisionRecoveryPending = true;
}

void discardIncompleteCurrentWorkspaceTempFilesOnSd() {
    uint16_t removedCount = 0;
    if (SD.exists(CurrentSetStorage::kCurrentRuntimeBundleTempPath)) {
        if (SD.remove(CurrentSetStorage::kCurrentRuntimeBundleTempPath)) {
            ++removedCount;
            Serial.println("[StorageManager] Boot hygiene: removed incomplete runtime bundle temp");
        }
    }
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            char tempPath[48];
            if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                           slotIndex)) {
                continue;
            }
            if (!SD.exists(tempPath)) {
                continue;
            }
            if (SD.remove(tempPath)) {
                ++removedCount;
            }
        }
    }
    if (removedCount > 0) {
        Serial.print("[StorageManager] Boot hygiene: discarded ");
        Serial.print(removedCount);
        Serial.println(" incomplete current-workspace temp file(s)");
    }
}

void discardIncompleteRevisionTempFilesOnSd() {
    if (!SD.exists(SetRevisionCatalog::kSetsRoot)) {
        return;
    }
    File setsDir = SD.open(SetRevisionCatalog::kSetsRoot);
    if (!setsDir) {
        return;
    }
    uint16_t removedCount = 0;
    while (true) {
        File setEntry = setsDir.openNextFile();
        if (!setEntry) {
            break;
        }
        const bool isSetDirectory = setEntry.isDirectory();
        char setFolderName[16] = {};
        const char* setName = setEntry.name();
        if (setName != nullptr) {
            std::snprintf(setFolderName, sizeof(setFolderName), "%s", setName);
        }
        setEntry.close();
        if (!isSetDirectory || setFolderName[0] != 'S') {
            continue;
        }

        char revisionsDir[48];
        if (std::snprintf(revisionsDir, sizeof(revisionsDir), "%s/%s/revisions",
                          SetRevisionCatalog::kSetsRoot, setFolderName) <= 0) {
            continue;
        }
        if (!SD.exists(revisionsDir)) {
            continue;
        }
        File revisions = SD.open(revisionsDir);
        if (!revisions) {
            continue;
        }
        while (true) {
            File revisionEntry = revisions.openNextFile();
            if (!revisionEntry) {
                break;
            }
            char revisionName[24] = {};
            const char* revisionFileName = revisionEntry.name();
            revisionEntry.close();
            if (revisionFileName == nullptr) {
                continue;
            }
            std::snprintf(revisionName, sizeof(revisionName), "%s", revisionFileName);
            const size_t nameLen = std::strlen(revisionName);
            const size_t tempSuffixLen = std::strlen(SetRevisionCatalog::kRevisionTempSuffix);
            if (nameLen <= tempSuffixLen ||
                std::strcmp(revisionName + nameLen - tempSuffixLen,
                            SetRevisionCatalog::kRevisionTempSuffix) != 0) {
                continue;
            }
            char tempPath[72];
            if (std::snprintf(tempPath, sizeof(tempPath), "%s/%s", revisionsDir, revisionName) <=
                0) {
                continue;
            }
            if (SD.remove(tempPath)) {
                ++removedCount;
                Serial.print("[StorageManager] Boot hygiene: removed incomplete revision ");
                Serial.println(tempPath);
            }
        }
        revisions.close();
    }
    setsDir.close();
    if (removedCount > 0) {
        Serial.print("[StorageManager] Boot hygiene: discarded ");
        Serial.print(removedCount);
        Serial.println(" incomplete revision temp file(s)");
    }
}

bool StorageManager::loadCurrentWorkspaceAtBoot(LooperState& state) {
    Serial.println("[StorageManager] Loading Current workspace from SD...");
    loadWorkspaceMetaCountersFromSd();
    const bool ok = loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir, state);
    if (ok) {
        Serial.println("[StorageManager] Current workspace loaded successfully.");
    }
    return ok;
}

bool StorageManager::loadCurrentSetFromSd(LooperState& state) {
    return loadCurrentWorkspaceAtBoot(state);
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
    CurrentWorkspaceStorage::WorkspaceMetaRecord workspaceMeta{};
    if (!CurrentWorkspaceStorage::readWorkspaceMetaFile(workspaceMeta)) {
        workspaceMeta.derivedFromSetId = workspaceDerivedFromSetId;
        workspaceMeta.derivedFromRevisionId = workspaceDerivedFromRevisionId;
    }

    const BootRecoveryPolicy::RevisionRecoveryPlan plan =
        BootRecoveryPolicy::buildRevisionRecoveryPlan(
            workspaceMeta.derivedFromSetId, workspaceMeta.derivedFromRevisionId, 0);

    if (plan.setId != 0) {
        uint16_t catalogLatestRevisionId = 0;
        readSetLatestRevisionIdFromSd(plan.setId, catalogLatestRevisionId);
        const BootRecoveryPolicy::RevisionRecoveryPlan resolvedPlan =
            BootRecoveryPolicy::buildRevisionRecoveryPlan(
                plan.setId, plan.derivedRevisionId, catalogLatestRevisionId);

        if (resolvedPlan.derivedRevisionId != 0) {
            Serial.print("[StorageManager] Boot recovery: queue derived revision S");
            Serial.print(resolvedPlan.setId);
            Serial.print(" v");
            Serial.println(resolvedPlan.derivedRevisionId);
            queueBootRevisionRecovery(resolvedPlan.setId, resolvedPlan.derivedRevisionId);
            return true;
        }

        const uint16_t latestFallback = BootRecoveryPolicy::resolveLatestRevisionFallback(
            resolvedPlan.derivedRevisionId, resolvedPlan.latestRevisionId);
        if (latestFallback != 0) {
            Serial.print("[StorageManager] Boot recovery: queue latest revision S");
            Serial.print(resolvedPlan.setId);
            Serial.print(" v");
            Serial.println(latestFallback);
            queueBootRevisionRecovery(resolvedPlan.setId, latestFallback);
            return true;
        }
    }

    if (tryLoadLatestRecoveryPoint(state)) {
        Serial.println("[StorageManager] Boot recovered from recovery checkpoint.");
        return true;
    }
    Serial.println("[StorageManager] Boot recovery chain exhausted; starting empty.");
    return false;
}

bool StorageManager::loadState(LooperState& state) {
    RtcTime::init();
    discardIncompleteRevisionTempFilesOnSd();
    discardIncompleteCurrentWorkspaceTempFilesOnSd();

    const bool hasCurrentWorkspace =
        SD.exists(CurrentSetStorage::kCurrentMetaPath) ||
        SD.exists(CurrentWorkspaceStorage::kWorkspaceMetaPath);
    if (hasCurrentWorkspace) {
        if (loadCurrentWorkspaceAtBoot(state)) {
            SavedSetCatalog::SetIndex index{};
            if (!reconcileSetIndexOnSd(index)) {
                Serial.println("[StorageManager] WARN: could not reconcile MidiLooper/sets/index.bin");
            }
            forceCurrentSetFullLoopWrite = false;
            syncCurrentSetDirtyTrackingFromLoadedState();
            return true;
        }
        Serial.println("[StorageManager] Current workspace load failed; attempting boot recovery chain.");
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
