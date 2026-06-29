//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <SD.h>
#include <array>
#include <cstdint>
#include <vector>

#include "Globals.h"
#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "LooperState.h"
#include "RevisionCommitPolicy.h"
#include "RevisionPackedBlob.h"
#include "SetRevisionCatalog.h"
#include "StorageActivitySnapshot.h"
#include "StorageLoopIo.h"
#include "StorageSession.h"
#include "TrackState.h"
#include "MidiEvent.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include <Arduino.h>

#if defined(__IMXRT1062__)
#define STORAGE_PERSIST_MEM FLASHMEM
#define STORAGE_PERSIST_DATA DMAMEM
#else
#define STORAGE_PERSIST_MEM
#define STORAGE_PERSIST_DATA
#endif

class Track;
class Loop;
struct GlobalUndoStack;
struct UndoEntry;

namespace StorageManagerInternal {

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
    GlobalUndoStackToken,
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

enum class RevisionCommitStage : uint8_t {
    Idle = 0,
    Snapshot,
    Write,
    Validate,
    CatalogUpdate,
    Complete,
};

enum class RevisionWriteStage : uint8_t {
    PrepareLayout = 0,
    OpenTempFile,
    WriteHeader,
    WriteTransportChunk,
    WriteLoopSlotChunks,
    WriteSlotIndexChunk,
    WriteFooter,
};

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

static_assert(!RevisionCommitPolicy::kWritePathUsesLoopPassesMaterialize,
              "revision commit WRITE must not call LoopPasses::materialize");
static_assert(RevisionCommitPolicy::kLoopSlotBodyUsesStorageLoopIoStream,
              "revision commit LoopSlot bodies must stream via StorageLoopIo");

constexpr uint16_t kMaxRevisionLoopIndexEntries =
    static_cast<uint16_t>(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK);

extern bool deferredSavePending;
extern bool urgentEditSavePending;
extern uint32_t lastEditAutosaveMs;
extern bool clearEditDirtyAfterDeferredSave;
extern bool quarantineLegacyMonolithAfterSave;
extern bool deferredSaveInProgress;
extern bool deferredSaveSdIoActive;
extern bool deferredSaveUrgentRequested;
extern DeferredSaveStage deferredSaveStage;
extern DeferredGlobalHeaderStage deferredGlobalHeaderStage;
extern DeferredTrackWriteStage deferredTrackWriteStage;
extern DeferredSlotWriteStage deferredSlotWriteStage;
extern DeferredFooterWriteStage deferredFooterWriteStage;
extern DeferredLoopWriteStage deferredLoopWriteStage;
extern DeferredUndoWriteStage deferredUndoWriteStage;
extern LooperState deferredSaveStateSnapshot;
extern File deferredSaveFile;
extern File deferredSaveLoopFile;
extern bool deferredSaveLoopFileOpen;
extern CurrentSetStorage::AnchorFields currentSetAnchorFields;
extern uint8_t deferredSaveNumTracks;
extern uint8_t deferredSaveTrackCursor;
extern uint8_t deferredSaveSlotCursor;
extern uint8_t deferredSavePoolCursor;
extern uint8_t deferredSaveUndoTrackCursor;
extern uint16_t deferredSaveCapturePassCursor;
extern uint16_t deferredSaveChunkCursor;
extern uint32_t deferredSaveUndoEntryCursor;
extern bool deferredSaveTrackHeaderWritten;
extern uint8_t deferredSaveFooterTrackCursor;
extern uint32_t deferredSaveStartedAtUs;
extern uint32_t deferredSaveHeapBefore;
extern uint32_t deferredSaveAdmissionHeap;
extern bool deferredSaveHeapFloorDeferred;
extern bool deferredSaveLastCompletedOk;
extern uint32_t deferredSaveCompletedAtMs;
extern uint32_t deferredSaveFailedAtMs;
extern uint32_t revisionLoadCompletedAtMs;
extern uint32_t revisionLoadFailedAtMs;
extern uint16_t revisionLoadLastDisplaySetId;
extern uint16_t revisionLoadLastDisplayRevisionId;
extern std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> deferredSaveMidiBatch;
extern uint16_t deferredSaveLoopSlotsWritten;
extern uint16_t deferredSaveLoopSlotsSkipped;
extern uint32_t deferredSaveDisplayBlockUs;
extern bool forceCurrentSetFullLoopWrite;
extern std::array<std::array<bool, Config::MAX_LOOPS_PER_TRACK>, Config::NUM_TRACKS>
    currentSetLoopSlotDirty;
extern uint32_t currentSetLastActiveUnix;
extern char currentSetLoadedFromFolder[16];
extern uint32_t currentWorkspaceEpoch;
extern uint32_t lastCommittedWorkspaceEpoch;
extern uint16_t workspaceDerivedFromSetId;
extern uint16_t workspaceDerivedFromRevisionId;
extern uint16_t workspaceLastCommittedRevisionId;
extern uint32_t deferredSaveWorkspaceEpoch;
extern char autoSaveBeforeLoadFolderPending[16];
extern bool autoSaveBeforeLoadFolderPendingValid;
extern bool revisionCommitPending;
extern bool revisionCommitInProgress;
extern bool revisionCommitSdIoActive;
extern RevisionCommitStage revisionCommitStage;
extern RevisionWriteStage revisionWriteStage;
extern uint32_t revisionCommitSourceEpoch;
extern uint32_t revisionCommitWorkspaceEpochBeforeSnapshot;
extern uint16_t revisionCommitSetId;
extern uint16_t revisionCommitPendingRevisionId;
extern char revisionCommitTempPath[80];
extern char revisionCommitFinalPath[80];
extern File revisionCommitFile;
extern SetRevisionCatalog::SetCatalogIndex revisionCommitCatalogIndex;
extern SetRevisionCatalog::SetMetaRecord revisionCommitSetMeta;
extern RevisionPackedBlob::RevisionHeader revisionCommitHeader;
extern RevisionPackedBlob::RevisionLoopSlotDirectoryEntry revisionCommitSlotEntries[kMaxRevisionLoopIndexEntries];
extern RevisionPackedBlob::RevisionLoopSlotDirectoryEntry
    revisionLoadSlotDirectoryEntries[kMaxRevisionLoopIndexEntries];
extern uint16_t revisionCommitSlotIndexCount;
extern uint16_t revisionCommitSlotIndexWriteCursor;
extern uint32_t revisionCommitPayloadWriteOffset;
extern uint16_t revisionCommitChunkCount;
extern uint8_t revisionCommitCopyTrackCursor;
extern uint8_t revisionCommitCopySlotCursor;
extern uint32_t revisionCommitRuntimeBundleSize;
extern uint32_t revisionCommitRuntimeBundleReadPos;
extern uint32_t revisionCommitSlotReadPos;
extern uint32_t revisionCommitSlotBodyRemaining;
extern bool revisionCommitLoopSlotBodyActive;
extern File revisionCommitSourceFile;
extern bool revisionCommitSourceFileOpen;
extern std::array<uint8_t, 512> revisionCommitCopyBuffer;
extern uint32_t revisionCommitPayloadCrc;
extern bool revisionCommitPayloadCrcSeeded;
extern bool revisionCommitAllocatedNewSet;
extern bool revisionCommitSlotIndexChunkWritten;
extern uint32_t lastRevisionCommitBlockedLogAtMs;
extern RevisionLoadStage revisionLoadStage;
extern RevisionLoadWriteStage revisionLoadWriteStage;
extern uint16_t revisionLoadSetId;
extern uint16_t revisionLoadRevisionId;
extern char revisionLoadSourcePath[80];
extern RevisionPackedBlob::RevisionHeader revisionLoadHeader;
extern uint16_t revisionLoadSlotIndexCount;
extern uint32_t revisionLoadWorkspaceEpoch;
extern uint32_t revisionLoadTransportFileOffset;
extern uint32_t revisionLoadTransportBodySize;
extern uint32_t revisionLoadTransportReadPos;
extern uint8_t revisionLoadCopyTrackCursor;
extern uint8_t revisionLoadCopySlotCursor;
extern uint32_t revisionLoadSlotBodyRemaining;
extern uint32_t revisionLoadSlotReadPos;
extern File revisionLoadSourceFile;
extern bool revisionLoadSourceFileOpen;
extern File revisionLoadDestFile;
extern bool revisionLoadDestFileOpen;
extern bool revisionLoadWritingEmptySlot;
extern DeferredLoopWriteStage revisionLoadLoopWriteStage;
extern uint32_t lastRevisionLoadBlockedLogAtMs;
extern bool revisionLoadUsedDefaultTransport;
extern bool revisionLoadDisplayRefreshPending;
extern bool bootRevisionRecoveryPending;
extern uint16_t bootRevisionRecoverySetId;
extern uint16_t bootRevisionRecoveryRevisionId;
extern StorageSession storageSession;
extern RevisionLoadReloadRamStage revisionLoadReloadRamStage;
extern File revisionLoadReloadMetaFile;
extern bool revisionLoadReloadMetaFileOpen;
extern uint8_t revisionLoadReloadTrackCursor;
extern uint8_t revisionLoadReloadSlotCursor;
extern uint8_t revisionLoadReloadNumTracks;
extern bool revisionLoadReloadAnySlotHasEvents[Config::NUM_TRACKS];
extern TrackState revisionLoadReloadLoadedTrackState[Config::NUM_TRACKS];
extern bool revisionLoadReloadMuted[Config::NUM_TRACKS];
extern std::vector<uint8_t> revisionLoadReloadActiveLoopIndex;
extern uint8_t revisionLoadReloadSelectedTrackIdx;
extern LooperState revisionLoadReloadLooperState;
extern uint32_t revisionLoadReloadMasterLoopLength;

StorageActivitySnapshot buildStorageActivitySnapshot();

bool writeRaw(File& file, const void* data, size_t size);
bool readRaw(File& file, void* data, size_t size);

LooperState sanitizeLooperStateForPersistence(LooperState state);
uint32_t persistedLooperStateRaw(LooperState state);
bool writeUndoLoopSnapshot(File& file, const LoopSnapshotRef& snapshot);
bool readUndoLoopSnapshot(File& file, LoopSnapshotRef& snapshot);
bool writeGlobalUndoStackToFile(File& file, const GlobalUndoStack& stack);
bool readGlobalUndoStackFromFile(File& file, GlobalUndoStack& stack);
StorageIo storageIoFromFileWrite(File& file);
StorageIo storageIoFromFileRead(File& file);

const char* deferredSaveStageName(DeferredSaveStage stage);
const char* deferredGlobalHeaderStageName(DeferredGlobalHeaderStage stage);
const char* deferredTrackWriteStageName(DeferredTrackWriteStage stage);
const char* deferredSlotWriteStageName(DeferredSlotWriteStage stage);
const char* deferredFooterWriteStageName(DeferredFooterWriteStage stage);
const char* deferredLoopWriteStageName(DeferredLoopWriteStage stage);
const char* deferredUndoWriteStageName(DeferredUndoWriteStage stage);
void emitDeferredSaveSliceTelemetry(const char* phase);

void fillSlotSummariesForTrack(uint8_t trackIndex, const Track& track,
                               CurrentWorkspaceStorage::SlotSummary* summaries,
                               size_t summaryCount);
bool writeWorkspaceMetaAfterDeferredSave();

bool isCaptureActiveForPersistence();
uint32_t resolvePersistenceSliceBudgetUs(const LooperState& state);

void resetDeferredLoopWriteState();
void resetDeferredUndoWriteState();
bool writeCurrentSetMetaHeaderToOpenFile(File& file);
bool finalizeDeferredMetaTempFile();
bool finalizeDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex);
bool stepDeferredLoopPersist(File& file, const Loop& loop, bool& loopDone,
                             LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None);
bool stepDeferredEmptyLoopPersist(File& file, LoopId loopId, bool& loopDone);
bool stepDeferredLoopSnapshotPersist(File& file, const PersistedLoopSnapshot& snapshot,
                                     bool& loopDone);
bool stepDeferredUndoStackPersist(File& file, const GlobalUndoStack& stack, bool& stackDone);

bool appendRevisionCommitPayloadCrc(const uint8_t* data, size_t size);
bool persistenceWriteRaw(File& file, const void* data, size_t size,
                         LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None);
StorageIo storageIoFromFileWriteWithRevisionPayloadCrc(File& file);

void resetDeferredSaveJobState();
bool beginDeferredSaveJob(const LooperState& state);
bool stepDeferredSaveJob();

void resetRevisionCommitJobState();
uint32_t resolveMaxPersistenceMicros(const LooperState& state);
uint32_t resolveRevisionCommitSourceEpoch();
bool slotSourceFileReadableForRevisionCommit(const char* path, uint32_t maxEpoch,
                                             uint32_t& bodySizeOut);
bool prepareRevisionCommitLayout();
bool beginRevisionCommitSnapshot();
bool stepRevisionCommitWrite();
bool stepRevisionCommitValidate();
bool stepRevisionCommitCatalogUpdate();
bool stepRevisionCommitComplete();
bool stepRevisionCommitJob();

bool computeRevisionCommitPayloadCrcFromFile(File& file, uint32_t payloadOffset,
                                             uint32_t payloadSize, uint32_t& crcOut);
bool copyRevisionCommitChunk(File& dest, File& src, uint32_t& readPos, uint32_t& bytesRemaining,
                             uint32_t chunkSize);
bool writeSetMetaRecordFile(uint16_t setId, const SetRevisionCatalog::SetMetaRecord& record);
bool writeSetCatalogIndexFile(const SetRevisionCatalog::SetCatalogIndex& index);

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

extern HitlRevisionCommitBackup hitlRevisionCommitBackup;
#endif

void clearRevisionLoadPromptAndPipelineState();
void dispatchStagedRevisionLoad();
void resetRevisionLoadJobState();
bool stepRevisionLoadJob(LooperState& state);

void resetRevisionLoadReloadRamState();
bool findRevisionLoadSlotEntry(uint8_t trackIndex, uint8_t slotIndex,
                               RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entryOut);
bool beginRevisionLoadValidate();
bool stepRevisionLoadWrite();
bool stepRevisionLoadReloadRam(LooperState& state);
bool stepRevisionLoadComplete();
bool readSetLatestRevisionIdFromSd(uint16_t setId, uint16_t& latestRevisionIdOut);

bool readSlotIndexEntriesFromRevisionFile(
    File& file, size_t fileSize, const RevisionPackedBlob::RevisionHeader& header,
    RevisionPackedBlob::RevisionLoopSlotDirectoryEntry* entriesOut, uint16_t maxEntries,
    uint16_t& entryCountOut);

void resetTracksAfterFailedLoad();
void clearCurrentSetLoadedFromFolder();
void syncCurrentSetDirtyTrackingFromLoadedState();
bool readCurrentSetFilePreamble(File& file, LooperState& loadedLooperStateOut,
                                uint32_t& masterLoopLengthOut, uint8_t& numTracksOut);
bool readCurrentSetTrackSlotMetadata(File& file, uint8_t trackIndex, Track& track,
                                     TrackState& loadedTrackStateOut, bool& mutedOut);
bool loadLoopSlotFromCurrentSetSd(uint8_t trackIndex, uint8_t slotIndex, Track& track,
                                  bool& anySlotHasEventsOut);
void applyLoadedTrackStateAfterLoopSlots(Track& track, TrackState loadedTrackState,
                                         bool anySlotHasEvents, bool muted);
bool readCurrentSetFileEpilogue(File& file, uint8_t numTracks,
                                std::vector<uint8_t>& activeLoopIndex,
                                uint8_t& selectedTrackIdxOut);
bool applyLoadedTransportFooter(uint8_t numTracks, const std::vector<uint8_t>& activeLoopIndex,
                                uint8_t selectedTrackIdx, LooperState& state,
                                LooperState loadedLooperState, uint32_t masterLoopLength);

}  // namespace StorageManagerInternal
