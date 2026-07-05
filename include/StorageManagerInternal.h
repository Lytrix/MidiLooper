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
                                std::vector<uint8_t>& selectedSlotIndex,
                                uint8_t& selectedTrackIdxOut);
bool applyLoadedTransportFooter(uint8_t numTracks, const std::vector<uint8_t>& activeLoopIndex,
                                const std::vector<uint8_t>& selectedSlotIndex,
                                uint8_t selectedTrackIdx, LooperState& state,
                                LooperState loadedLooperState, uint32_t masterLoopLength);

}  // namespace StorageManagerInternal
