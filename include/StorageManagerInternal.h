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
#include "PersistenceDrainOutcome.h"
#include "PersistenceDrainGuard.h"
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
/// Packet D (A4.1) of `deferred-slot-restore-liveness`: the boot catalog reconcile's **named outcome**.
/// `reconcileSetIndexOnSd` is self-healing (an unreadable `index.bin` is recoverable metadata), but it
/// reported only success/failure, so the boot log could not distinguish "nothing to do" from "rebuilt
/// it" -- the ambiguity that made the device's `could not reconcile` state hard to read.
enum class SetIndexReconcileOutcome : uint8_t {
    /// The index read back and already matched the card; nothing was written.
    Ok = 0,
    /// The index was missing, unreadable, or stale, and was rebuilt/rewritten successfully.
    Repaired = 1,
    /// The repair was needed but could not be written.
    Failed = 2,
};

const char* setIndexReconcileOutcomeName(SetIndexReconcileOutcome outcome);
/// The named-outcome entry point. `reconcileSetIndexOnSd` remains the compatibility wrapper, returning
/// `Ok || Repaired`.
SetIndexReconcileOutcome reconcileSetIndexOnSdWithOutcome(SavedSetCatalog::SetIndex& index);
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
const char* deferredUndoWriteStageName(DeferredUndoWriteStage stage);
void emitDeferredSaveSliceTelemetry(const char* phase);

void fillSlotSummariesForTrack(uint8_t trackIndex, const Track& track,
                               CurrentWorkspaceStorage::SlotSummary* summaries,
                               size_t summaryCount);
bool writeWorkspaceMetaAfterDeferredSave();

bool isCaptureActiveForPersistence();
/// True while a track is actively recording. Audible playback is deliberately NOT a gate:
/// the music never stops (PROJECT_INTENT §1, DEC-081).
bool isCaptureRecordingForPersistence();
bool anyTrackArmedOrPendingRecordForPersistence();
void maybeAdmitDeferredWorkspaceFooter();
uint32_t resolvePersistenceSliceBudgetUs(const LooperState& state);

void resetDeferredLoopWriteState();
bool loopPersistTailWriteResumeActive(const PersistKey& key);
void armLoopPersistTailWriteResume(const PersistKey& key);
void disarmLoopPersistTailWriteResume();
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
/// Admit the workspace finalize when the queue has drained and a save is still owed. Returns true
/// when a work item is available to step (already queued/writing/active, or just admitted), false
/// when an owed save has no work item that can carry it -- which the blocking drain treats as
/// terminal instead of spinning its stuck budget on a state that cannot change.
bool maybeAdmitFinalizeWorkspaceAfterDrain();
bool resolvePersistKeyToTrackSlot(const PersistKey& key, uint8_t& trackIndexOut,
                                  uint8_t& slotIndexOut);
SyncDrainProgressSnapshot captureSyncDrainProgressSnapshot();
SyncDrainBudget buildSyncDrainBudgetForSession();
/// A3.4: the detailed drain entry point. `saveState` remains a `Completed`-only wrapper so no existing
/// caller silently gains new semantics, and both destructive callers use this one.
struct SyncDrainResult {
    SyncDrainOutcome outcome = SyncDrainOutcome::Failed;
    /// The named reason for a blocked or failed outcome; a `waiting_*` name for a restore block.
    const char* reason = "failed";
};
SyncDrainResult drainPersistenceWorkBlockingDetailed(const LooperState& state);
/// Compatibility entry point: true only for `Completed`.
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
/// A3.5: the finite post-load commit holdoff's remaining wait in ms, or 0 when none is pending. Read-only:
/// it never clears the holdoff, so the drain can compare the remaining interval against its wall-clock
/// budget before deciding whether to wait it out or report the named `post_load_holdoff` block.
uint32_t postLoadCommitSaveHoldoffRemainingMs();
/// A3.5: arm the post-load commit save holdoff directly (the production setter is
/// `cleanupLoadLoopJobPublish`, which a native fixture cannot reach without the full commit closure).
/// Documented controllable input, like `armBackgroundRestoreHoldoff`; it writes the same state.
void armPostLoadCommitSaveHoldoff(uint32_t delayMs);
void stepWallClockFromSdCatalogSync(uint8_t maxSetsPerSlice);

/// A2.1 (`design.md` D1): a restore request's lifecycle state. Queued work awaits selection; running
/// work is advancing an admitted job through read/parse/commit/cleanup; waiting work has a named
/// blocking reason; terminal work has exactly one recorded outcome. A service yield between runnable
/// slices is **running**, never a fictitious wait.
enum class RestoreRequestState : uint8_t {
    Queued = 0,
    Running = 1,
    Waiting = 2,
    Terminal = 3,
};

/// The named reasons a request may wait. A wait is never a terminal, and it never becomes one merely
/// because it persisted for a number of turns.
enum class RestoreRequestWait : uint8_t {
    None = 0,
    /// A live owner holds the slot.
    SlotOwned = 1,
    /// A track on that slot is recording (the SD-session exclusion).
    Capture = 2,
    /// The commit admission does not admit (no content-independent bound).
    CommitGate = 3,
    /// Another request holds the commit/cleanup phase, so the whole load child is denied this turn.
    JobCapacity = 4,
    /// A transient failure, within its bounded attempt count.
    Retry = 5,
};

/// The outcomes a request may reach, exactly one per identity.
enum class RestoreRequestTerminal : uint8_t {
    None = 0,
    Committed = 1,
    Unnecessary = 2,
    Refused = 3,
    Superseded = 4,
};

/// Identity, state and terminal accessors for the restore request record (A2.1). The record is keyed by
/// `(track, slot)` with the full identity stored beside the state, so a reused coordinate pair is a
/// different request and a stale record can never be reported as the current outcome.
void beginRestoreRequest(uint8_t trackIndex, uint8_t slotIndex);
RestoreRequestState restoreRequestState(uint8_t trackIndex, uint8_t slotIndex);
/// The request's named wait reason (A3.2), so the drain can report *why* a restore holds the save gate.
RestoreRequestWait restoreRequestWaitFor(uint8_t trackIndex, uint8_t slotIndex);
/// A3.2: the wait reason of the request currently holding the save gate, or `None`. Consults the live job
/// instances first (they hold the SD session), then any request in a named wait.
RestoreRequestWait restoreWaitReasonBlockingSave();
/// A3.2: the same answer as a `SyncDrainWait` code, so the pure drain verdict takes a small integer and
/// the drain TU needs no vocabulary of its own.
uint8_t restoreWaitCodeBlockingSave();
bool restoreRequestRecordIsCurrent(uint8_t trackIndex, uint8_t slotIndex);
/// A2.1: true once a terminal is recorded for the current record, so a publication can reject a request
/// that was decided while its job was in flight.
bool restoreRequestTerminalRecorded(uint8_t trackIndex, uint8_t slotIndex);
/// A2.1/A1.8: true when any record exists for these coordinates (a live request or a decided terminal).
bool restoreRequestRecordExists(uint8_t trackIndex, uint8_t slotIndex);
/// A2.1/A1.8: the terminal recorded for these coordinates (`None` when undecided). Readable after an
/// external generation advance, so a probe can report a superseded outcome.
RestoreRequestTerminal restoreRequestTerminalFor(uint8_t trackIndex, uint8_t slotIndex);
void markRestoreRequestRunning(uint8_t trackIndex, uint8_t slotIndex);
void markRestoreRequestWaiting(uint8_t trackIndex, uint8_t slotIndex, RestoreRequestWait wait);
/// Returns false when a terminal was already recorded for this identity (exactly-once).
bool markRestoreRequestTerminal(uint8_t trackIndex, uint8_t slotIndex,
                                RestoreRequestTerminal terminal);
/// External change of the coordinates: a user CLEAR, a set change, or a different owner committing that
/// slot. The restore path's **own** failure cleanup must NOT call this (see `waiting_retry`).
void advanceRestoreRequestGeneration(uint8_t trackIndex, uint8_t slotIndex);
/// S2b.8: the loaded current set's epoch moves here and every outstanding request is recorded
/// `superseded` **under the identity it was created with** (the epoch that is still current when this is
/// called). The supersession is emitted, so the reason is observable.
void noteRestoreSetEpoch(uint32_t setEpoch);
uint32_t restoreSetEpoch();
uint32_t restoreRequestGenerationFor(uint8_t trackIndex, uint8_t slotIndex);
/// S2b.8: the epoch the record itself was created under. Distinct from `restoreSetEpoch()` once an
/// external reset has advanced the live epoch, so "superseded under the original identity" is checkable
/// rather than assumed.
uint32_t restoreRequestEpochFor(uint8_t trackIndex, uint8_t slotIndex);
void resetAllRestoreRequestState();
/// A3.3: a monotonic counter bumped each time the restore path is serviced (a job grain or a
/// selection/admission turn). The blocking drain compares it to decide whether the restore it waits on
/// is making progress, so a drain advancing a large restore is not misreported as stuck.
void bumpRestoreProgressCounter();
uint32_t restoreProgressCounter();
/// A3.3: the restore work the drain may have to advance, derived from the live job instances, so the
/// drain's step budget covers it.
uint32_t restoreWorkSliceSteps();
uint32_t restoreWorkSdBytes();
/// A3.3: 255 when no job instance is servicing preparation, else the active instance's phase.
uint8_t restoreWorkPhase();
const char* restoreRequestStateName(RestoreRequestState state);
const char* restoreRequestWaitName(RestoreRequestWait wait);
const char* restoreRequestTerminalName(RestoreRequestTerminal terminal);

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
/// S2b.5 liveness predicate, shared by the selection path and the startability-aware pop.
bool queuedRestoreMayBegin(uint8_t trackIndex, uint8_t slotIndex);
/// A2.3 (D3): first startable request in priority order; blocked entries stay queued.
bool popNextStartableDeferredLoopSlotRestore(DeferredLoopSlotRestore& out);
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

/// S1.3: true once the remaining-library background admission scan has finished. The census checkpoint
/// needs this in addition to `hasPendingLoopSlotRestore()`, which cannot distinguish "scan complete" from
/// "scan momentarily between turns".
bool bootBackgroundFillAdmissionComplete();

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
