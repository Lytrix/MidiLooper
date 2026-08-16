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
#include "SavedSetCatalog.h"
#include "StorageActivitySnapshot.h"
#include "StorageLoopIo.h"
#include "PersistenceSyncDrainBudget.h"
#include "StorageManagerInternal/PersistenceWorkQueue.h"
#include "SlotLoadSession.h"
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

static_assert(!RevisionCommitPolicy::kWritePathUsesLoopPassesMaterialize,
              "revision commit WRITE must not call LoopPasses::materialize");
static_assert(RevisionCommitPolicy::kLoopSlotBodyUsesStorageLoopIoStream,
              "revision commit LoopSlot bodies must stream via StorageLoopIo");

extern bool urgentEditSavePending;
extern uint32_t lastEditAutosaveMs;
extern bool clearEditDirtyAfterDeferredSave;
extern bool quarantineLegacyMonolithAfterSave;
extern CurrentSetStorage::AnchorFields currentSetAnchorFields;
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
extern char autoSaveBeforeLoadFolderPending[16];
extern bool autoSaveBeforeLoadFolderPendingValid;
extern StorageSession storageSession;

extern bool workspaceFooterPersistDeferred;

bool anyAllocatedLoopEditStateDirty();
void markCurrentSetLoopSlotDirtyInternal(uint8_t trackIndex, uint8_t slotIndex,
                                         bool markMaterialChange = true);
void markCurrentSetTrackDirtyInternal(uint8_t trackIndex, bool markMaterialChange = true);
void markAllCurrentSetLoopSlotsDirtyInternal(bool markMaterialChange = true);

constexpr size_t kSavedSetPathCapacity = 64;

bool parseSavedSetSequence(const char* folderName, uint32_t& sequence);
bool formatSavedSetDirectoryPath(const char* folderName, char* out, size_t outSize);
bool resolveSavedSetFolderNameBySequence(uint32_t sequence, char* out, size_t outSize);
bool reconcileSetIndexOnSd(SavedSetCatalog::SetIndex& index);
bool buildSavedSetMetadata(uint32_t sequence, SavedSetCatalog::FolderNamingMode namingMode,
                           uint32_t createdAtUnix, SavedSetCatalog::SavedSetMetadata& metadata);
bool patchCurrentSetAnchor();
bool saveNewSetInternal(const LooperState& state, char* savedSetFolderOut, size_t outSize);
bool copySavedSetIntoCurrent(const char* sourceSetDir);

void loadWorkspaceMetaCountersFromSd();
void queueBootRevisionRecovery(uint16_t setId, uint16_t revisionId);
void discardIncompleteRevisionTempFilesOnSd();
void discardIncompleteCurrentWorkspaceTempFilesOnSd();
void syncWallClockFromSdTimestampsQuickForBootLoad();
void markAllCurrentSetLoopSlotsDirtyForBootRecovery();

void resetStorageSessionJobs();

StorageActivitySnapshot buildStorageActivitySnapshot();

bool writeRaw(File& file, const void* data, size_t size);
bool readRaw(File& file, void* data, size_t size);

LooperState sanitizeLooperStateForPersistence(LooperState state);
uint32_t persistedLooperStateRaw(LooperState state);
bool writeUndoLoopSnapshot(File& file, const LoopSnapshotRef& snapshot);
bool readUndoLoopSnapshot(File& file, LoopSnapshotRef& snapshot);
bool writeGlobalUndoStackToFile(File& file, const GlobalUndoStack& stack);
bool readGlobalUndoStackFromFile(File& file, GlobalUndoStack& stack);
bool readGlobalUndoStackMetadataFromFile(File& file, GlobalUndoStack& stack);
bool skipUndoLoopSnapshot(File& file);
void logBootLoadStageFailure(const char* stage, uint8_t track = 0xFF, uint32_t detail = 0);
StorageIo storageIoFromFileWrite(File& file);
StorageIo storageIoFromFileRead(File& file);

const char* deferredSaveStageName(DeferredSaveStage stage);
const char* deferredGlobalHeaderStageName(DeferredGlobalHeaderStage stage);
const char* deferredTrackWriteStageName(DeferredTrackWriteStage stage);
const char* deferredSlotWriteStageName(DeferredSlotWriteStage stage);
const char* deferredFooterWriteStageName(DeferredFooterWriteStage stage);
const char* deferredCompletionWriteStageName(DeferredCompletionWriteStage stage);
const char* deferredLoopWriteStageName(DeferredLoopWriteStage stage);
const char* deferredLoopFinalizeStageName(DeferredLoopFinalizeStage stage);
const char* deferredUndoWriteStageName(DeferredUndoWriteStage stage);
void emitDeferredSaveSliceTelemetry(const char* phase);

void fillSlotSummariesForTrack(uint8_t trackIndex, const Track& track,
                               CurrentWorkspaceStorage::SlotSummary* summaries,
                               size_t summaryCount);
bool writeWorkspaceMetaAfterDeferredSave();

bool isCaptureActiveForPersistence();
bool isTransportActiveForPersistence();
bool anyTrackArmedOrPendingRecordForPersistence();
void maybeAdmitDeferredWorkspaceFooter();
uint32_t resolvePersistenceSliceBudgetUs(const LooperState& state);

void resetDeferredLoopWriteState();
void resetDeferredLoopFinalizeState();
void beginDeferredLoopSlotFinalize();
bool deferredLoopSlotFinalizeInProgress();
void resetDeferredUndoWriteState();
bool writeCurrentSetMetaHeaderToOpenFile(File& file);
bool finalizeDeferredMetaTempFile();
bool openDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex);
bool stepFinalizeDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex, bool& finalizeDoneOut);
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
void resetMidPassChunkPersistState();
void resetPersistenceWorkItemJobState();
bool stepPersistenceWorkItem(const LooperState& state);
void maybeAdmitFinalizeWorkspaceAfterDrain();
bool resolvePersistKeyToTrackSlot(const PersistKey& key, uint8_t& trackIndexOut,
                                  uint8_t& slotIndexOut);
SyncDrainProgressSnapshot captureSyncDrainProgressSnapshot();
SyncDrainBudget buildSyncDrainBudgetForSession();
bool drainPersistenceWorkBlocking(const LooperState& state);
bool beginDeferredRuntimeBundleWrite(const LooperState& state);
bool completeScopedRuntimeBundleWorkItemIfDone();
bool stepDeferredRuntimeBundleSlice(bool& bundleDoneOut);
bool stepDeferredWorkspaceFinalizeSlice(bool& finalizeDoneOut);
void resetDeferredCompletionWriteState();
void beginCurrentSetCompletion();
bool stepMidPassChunkPersist();
bool beginDeferredSaveJob(const LooperState& state);
bool stepDeferredSaveJob();
bool stepDeferredSaveJobCurrentSetMeta();
bool stepDeferredSaveJobTrackHeaderAndSlots();
bool stepDeferredSaveJobCurrentSetLoopSlot();
bool stepDeferredSaveJobFooter();
bool stepDeferredSaveJobUndoStacks();
bool stepDeferredSaveJobCurrentSetCompletion();

void clearCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex);
void quarantineLegacyMonolithStorageFile();
bool closeDeferredMetaTempForLoopWrites();
bool reopenDeferredMetaTempForAppend();
bool shouldWriteCurrentSetLoopSlot(uint8_t trackIndex, uint8_t slotIndex);
bool trackHasCurrentSetDirtyLoopSlot(uint8_t trackIndex);

bool deferredSaveBlockedByActiveSlotLoadSd();
bool deferredSaveBlockedByPostLoadCommitHoldoff();
void stepWallClockFromSdCatalogSync(uint8_t maxSetsPerSlice);

struct DeferredLoopSlotRestore {
    uint8_t track = 0;
    uint8_t slot = 0;
    uint16_t restorePriority = 3;
};

void clearLoadLoopJob();
void demoteActiveLoadLoopJobForFocus(uint8_t focusTrack, uint8_t focusSlot);
void resumeParkedLoadLoopJobIfFocus(uint8_t focusTrack, uint8_t focusSlot);
void ensureActiveLoadLoopJobSelected(uint8_t focusTrack, uint8_t focusSlot);
SlotLoadAdvanceResult stepLoadLoopJob(uint32_t deadlineUs);

bool anyLoadLoopJobActive();
bool loadLoopJobHasOpenSdFile();
bool isActiveLoadLoopJobFor(uint8_t trackIndex, uint8_t slotIndex);
bool isParkedLoadLoopJobFor(uint8_t trackIndex, uint8_t slotIndex);
void setBootTitleLoadDrain(bool enabled);
bool getBootTitleLoadDrain();
void armBackgroundRestoreHoldoff(uint32_t delayMs);

void markLoopSlotRestoreAttempted(uint8_t trackIndex, uint8_t slotIndex);
bool isLoopSlotRestoreAttempted(uint8_t trackIndex, uint8_t slotIndex);
bool popNextDeferredLoopSlotRestore(DeferredLoopSlotRestore& out);
bool popFocusDeferredLoopSlotRestore(uint8_t focusTrack, uint8_t focusSlot,
                                     DeferredLoopSlotRestore& out);
void reprioritizeDeferredLoopSlotRestoreEntries();
void queueDeferredLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex);
void enqueueRemainingLoopSlotRestores();
bool isFocusDeferredLoopSlotRestorePending(uint8_t trackIndex, uint8_t slotIndex);
bool readLoopSlotPayloadOnSdInRamEntry(uint8_t trackIndex, uint8_t slotIndex);
void writeLoopSlotPayloadOnSdInRamEntry(uint8_t trackIndex, uint8_t slotIndex, bool hasPayload);
void refreshLoopSlotPayloadOnSdInRamEntry(uint8_t trackIndex, uint8_t slotIndex);
bool isDeferredLoopSlotRestoreQueued(uint8_t trackIndex, uint8_t slotIndex);
uint16_t pendingLoopSlotRestoreCount();

void clearPendingLoopSlotRestoresAtBoot();
void resetAllLoopSlotRestoreAttempted();
bool appendBootLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex, uint16_t restorePriority);
void sortPendingLoopSlotRestoreQueue();
bool peekFirstPendingLoopSlotRestore(DeferredLoopSlotRestore& out);
void setRestoredSetBundlePath(const char* path);

/// Boot / revision load: deferred undo-stack hydrate state (shared by epilogue + idle hydrate).
extern char restoredSetBundlePath_[80];
extern std::array<uint32_t, Config::NUM_TRACKS> undoStackFileOffsets_;
extern uint8_t undoHydrateTrackIndex_;
extern bool undoSnapshotsPending_;

void resetBootUndoHydrateState();

bool hydrateLoopSlotMetadataFromCurrentSetSd(uint8_t trackIndex, uint8_t slotIndex, Loop& loop);

void resetLoopSlotForBootManifest(Loop& loop, uint8_t slotIndex);
void resetLoopSlotToEmpty(Loop& loop, uint8_t slotIndex);
void markLoopCommittedChunksPersistedFromSdLoad(Loop& loop);

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

void clearRevisionLoadRequestState();
void dispatchRequestedRevisionLoad();
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
void clearAutoSaveBeforeLoadFolderPending();
void syncCurrentSetDirtyTrackingFromLoadedState();
bool anyCurrentSetLoopSlotDirty();
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
                                std::vector<uint8_t>& selectedSlotIndex,
                                uint8_t& selectedTrackIdxOut,
                                bool deferUndoSnapshotBodies = false);
bool applyLoadedTransportFooter(uint8_t numTracks, const std::vector<uint8_t>& activeLoopIndex,
                                const std::vector<uint8_t>& selectedSlotIndex,
                                uint8_t selectedTrackIdx, LooperState& state,
                                LooperState loadedLooperState, uint32_t masterLoopLength);

#if defined(SESSION_CAPTURE)
bool handleHitlQuarantineCommandLine(const char* line);
void quarantineCorruptRuntimeBundleOnSd();
#endif

inline bool hasPersistenceWorkPending() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return storageSession.currentWorkspaceSave.pending ||
           PersistenceWorkQueue::queueDepth() > 0 ||
           PersistenceWorkQueue::writingWorkItemCount() > 0 ||
           storageSession.persistenceWorkItem.itemActive;
#endif
}

}  // namespace StorageManagerInternal
