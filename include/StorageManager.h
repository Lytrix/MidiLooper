//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "DeferredSaveDisplayStatus.h"
#include "LooperState.h"
#include "LoopPasses.h"
#include "RevisionLoadPolicy.h"
#include "SavedSetCatalog.h"
#include "SetRevisionCatalog.h"
#include "SetBrowserOverlayPolicy.h"

#if defined(ARDUINO)
#include <SD.h>
#endif

/**
 * @class StorageManager
 * @brief Manages persistent saving and loading of the looper state to non-volatile storage.
 *
 * Provides static methods to serialize the current LooperState to external memory (e.g., SD card
 * or flash) and to reload it on startup. Domain code admits stale work via admit* (e.g.
 * admitLoopPersist, admitTrackMeta, admitLoopSlotPersist) and wakes the scheduler with
 * requestDeferredSaveState() at transition boundaries. PersistenceWorkQueue (internal) and
 * processDeferredSaveState() own scheduling; legacy monolithic save stages were retired (B4).
 */
class StorageManager {
public:
    /// Deprecated — loopId is per-track slot index, not globally unique. Use admitLoopSlotPersist.
    static void admitLoopPersist(LoopId loopId);
    /// Stage 3 no-op — Loop undo history is derived from content at load (DEC-035).
    static void admitLoopUndoHistory(uint8_t trackIndex, uint8_t slotIndex);
    /// Admit stale slot assignment / enable row (interim UI assignment).
    static void admitSlotMeta(uint8_t trackIndex, uint8_t slotIndex);
    /// Admit stale track header / mute state.
    static void admitTrackMeta(uint8_t trackIndex);
    /// Admit stale workspace footer (selection / active indices).
    static void admitWorkspaceFooter();
    /// Queue workspace footer persist; admitted when capture, transport, and ARMED are idle.
    static void requestWorkspaceFooterPersistWhenSafe();
    /// Admit stale global looper meta (transport, BPM, master length).
    static void admitGlobalMeta();

    /// Mark current-set slot for workspace body write (material-change anchor).
    static void markLoopSlotMaterialDirty(uint8_t trackIndex, uint8_t slotIndex);
    /// Mark all slots on a track dirty (does not admit persistence work).
    static void markTrackSlotsMaterialDirty(uint8_t trackIndex);
    /// Admit slot meta + slot-keyed loop payload persist for one slot (clear / full slot rewrite).
    static void admitLoopSlotPersist(uint8_t trackIndex, uint8_t slotIndex);
    /// Admit track meta + slot-keyed loop persist for every slot on a track.
    static void admitTrackSlotPersistence(uint8_t trackIndex);

    static bool saveState(const LooperState& state);
    static bool loadState(LooperState& state);
    static bool bootRecoveryBlocked();
    static bool loadCurrentWorkspaceFromSd(LooperState& state);
    static void requestDeferredSaveState(const LooperState& state, uint32_t admissionHeap = UINT32_MAX,
                                         bool isUrgentRequest = false);
    /// Hold work-item persist until `now + graceMs` while transport is active.
    /// Call after `requestDeferredSaveState` on PLAYING stop paths — urgent request clears this.
    static void deferWorkspaceSaveDispatchDuringPlayback(uint32_t graceMs);
    static void processDeferredSaveState(const LooperState& state);
    static bool isDeferredSaveActive();
    static bool hasDeferredSaveWork();
    /// True while sealed capture chunks are waiting for the incremental SD flush. Separate from
    /// `hasDeferredSaveWork()`: during a live take the workspace save is parked, so this is what lets the
    /// main loop invoke the scheduler to persist a take as it is recorded.
    static bool hasQueuedCaptureChunksToPersist();
    static bool hasActivePersistenceWorkItem();
    /// True while a pending workspace save still has queued or in-flight work items.
    static bool shouldPrioritizeWorkspaceSaveDrain();
    /// True while boot-time loop slot payloads are still queued for idle restore.
    static bool hasPendingLoopSlotRestore();
    /// True when the in-flight, parked, or next queued restore targets the selected track/slot.
    static bool isFocusedLoopSlotRestoreWork();
    /// Boot title playback drain: disable demote/park so parked jobs cannot block title clear.
    static void setBootTitleLoadDrain(bool enabled);
    /// True while OSTINATIX title drain is active (set in setup, cleared at boot_commit).
    static bool isBootTitleLoadDrain();
    /// True when a slot payload is not committed, not queued, and not actively loading.
    static bool needsSlotLoad(uint8_t trackIndex, uint8_t slotIndex);
    /// True when boot restore queue is empty and no SlotLoadSession is active.
    static bool bootInteractiveReady();
    /// S1.3/S2b.3: emit the one-shot `post_restore_census` checkpoint once every census condition holds
    /// (background admission complete, restores + both job instances idle, undo hydration done, transport
    /// stopped, no holdoff or in-flight persistence work). Session-capture only. See the definition for
    /// why "last restore terminal plus a delay" is not the oracle.
    static void emitPostRestoreCensusIfReady(const LooperState& state);
    /// True while undo snapshot bodies are still queued for idle hydrate from the runtime bundle.
    static bool hasPendingUndoSnapshotHydrate();
    static void requestUrgentEditSave();
    static void processEditAutosave(const LooperState& state);
    static bool saveNewSet(char* savedSetFolderOut = nullptr, size_t outSize = 0);
    static bool loadSetIntoCurrent(const char* savedSetFolderName);
    static uint32_t getCurrentSetLastActiveUnix();
    static bool copyCurrentSetLoadedFromFolder(char* out, size_t outSize);
    static bool consumeAutoSaveBeforeLoadFolder(char* out, size_t outSize);
    static size_t listSavedSetFolderEntries(SavedSetCatalog::SavedSetFolderListEntry* entries,
                                            size_t maxEntries);
    static size_t listSetRevisionBrowserEntries(SetRevisionCatalog::SetBrowserListEntry* entries,
                                                size_t maxEntries);
    static size_t listSetRevisionHistoryEntries(
        uint16_t setId, SetRevisionCatalog::RevisionBrowserListEntry* entries, size_t maxEntries);
    static bool readSetRevisionHistoryBrowserMetadata(
        uint16_t setId, uint16_t revisionId, SavedSetCatalog::SavedSetMetadata& metadata,
        uint32_t& createdUnixOut);
    static bool readSetRevisionCatalogMetaForFolder(const char* folderName,
                                                    SetRevisionCatalog::SetMetaRecord& meta);
    static bool readSetRevisionCatalogBrowserMetadata(const char* folderName,
                                                      SavedSetCatalog::SavedSetMetadata& metadata,
                                                      uint16_t& setIdOut, uint16_t& revisionIdOut,
                                                      uint32_t& updatedUnixOut);
    static bool readSavedSetMetadataForFolder(const char* folderName,
                                              SavedSetCatalog::SavedSetMetadata& metadata);
    static bool readCurrentSetBrowserMetadata(SavedSetCatalog::SavedSetMetadata& metadata);
    static DeferredSaveDisplayStatus getDeferredSaveDisplayStatus(uint32_t nowMs);
    static DeferredSaveDisplayStatus getDeferredLoadDisplayStatus(uint32_t nowMs);
    static uint16_t getRevisionLoadDisplayTargetSetId();
    static uint16_t getRevisionLoadDisplayTargetRevisionId();
    static bool isCurrentWorkspaceDirty();
    /// True when transport-stop or autosave should queue a CurrentSet workspace write.
    static bool shouldQueueCurrentWorkspaceSave();
    static uint32_t getCurrentWorkspaceEpoch();
    static uint32_t getLastCommittedWorkspaceEpoch();
    static uint16_t getCurrentWorkspaceDerivedSetId();
    static uint16_t getCurrentWorkspaceDerivedRevisionId();
    using SetBrowserOverlayMode = SetBrowserOverlayPolicy::Mode;
    using SetBrowserOverlayEntryKind = SetBrowserOverlayPolicy::EntryKind;
    static SetBrowserOverlayMode getSetBrowserOverlayMode();
    static SetBrowserOverlayEntryKind getSetBrowserOverlayEntryKind();
    static void resetSetBrowserOverlayNavigation();
    static void setSetBrowserOverlayEntryKind(SetBrowserOverlayEntryKind kind);
    static bool openSetBrowserRevisionHistory(uint16_t setId, uint8_t listSelection,
                                              uint8_t listScrollOffset);
    static bool openSetBrowserLoopPick(uint16_t setId, uint8_t listSelection,
                                       uint8_t listScrollOffset);
    static bool navigateSetBrowserOverlayBack(uint8_t& outListSelection,
                                              uint8_t& outListScrollOffset);
    static uint16_t getSetBrowserOverlayDrilledSetId();
    static bool isRevisionLoadHeldForWorkspaceDirty();
    static uint8_t getRevisionLoadDirtyPromptSelection();
    static void adjustRevisionLoadDirtyPromptSelection(int delta);
    static void confirmRevisionLoadAfterCommit();
    static void confirmRevisionLoadDiscardWorkspace();
    static void cancelRevisionLoadRequest();
    static void requestCommitRevision();
    static void beginOverlaySaveRowCommit();
    static bool hasRevisionCommitWork();
    static bool isRevisionCommitActive();
    static void requestLoadRevision(uint16_t setId, uint16_t revisionId);
    static void requestLoadLatestRevisionForSet(uint16_t setId);
    static bool toggleSetRevisionCatalogFavorite(uint16_t setId);
    static bool hasRevisionLoadWork();
    static bool isRevisionLoadActive();
    static bool isOverlayCatalogReadAllowed();
    static SetBrowserOverlayPolicy::PersistencePhase getSetBrowserOverlayPersistencePhase();
    static bool consumeRevisionLoadDisplayRefreshPending();
#if defined(SESSION_CAPTURE)
    static void requestCommitRevisionForHitl();
    static bool cleanupHitlRevisionCommit();
    static void requestLoadRevisionForHitl(uint16_t setId, uint16_t revisionId);
    static void confirmRevisionLoadAfterCommitForHitl();
    static void confirmRevisionLoadDiscardWorkspaceForHitl();
    static void cancelRevisionLoadRequestForHitl();
    static bool nukeHitlSetsCatalog();
    /// Rename MidiLooper/current + recovery/checkpoints on SD (HITL / dev recovery).
    static bool quarantineCurrentWorkspaceOnSd();
    /// Before loadState: listen briefly for !QUARANTINE_WORKSPACE (exits early if no serial bytes).
    static void pollBootQuarantineWorkspaceBeforeLoad(uint32_t listenMs = 3000);
    static void processHitlSerialCommands();
#endif
    /// Select active/parked LoadLoopJob for this frame (Phase B.3).
    /// Call only from DeferredJobScheduler::runFrame before stepSubmittedLoadJobs.
    /// @return true when active became true this call (begin or promote from parked).
    static bool selectSubmittedLoadJobs();
    /// Domain step for submitted LoadLoopJob work under budgetUs.
    /// Call only from DeferredJobScheduler::runFrame after selectSubmittedLoadJobs (Phase B.2/B.3).
    /// @param activatedThisFrame result of the preceding selectSubmittedLoadJobs call.
    static void stepSubmittedLoadJobs(uint32_t budgetUs, bool activatedThisFrame);
    /// True when the active job has reached its unbounded publish operation
    /// or its ownership-releasing cleanup continuation. Active transport
    /// leaves either phase pending until a STOPPED owner turn.
    static bool hasActiveLoadLoopJobCommitPending();
    /// True when either active or parked work has reached the unbounded
    /// publish operation or cleanup continuation. Active-transport admission
    /// must check this before selection, because focus promotion can make a
    /// parked job active.
    static bool hasLoadLoopJobCommitPending();
    /// Idle slice: restore deferred loop slot payload(s) via DeferredJobScheduler.
    static void processDeferredLoopSlotRestore();
    /// S2b.7 (D4): one bounded restore-service step for the blocking drain. Same selection/admission and
    /// phase gates as `processDeferredLoopSlotRestore` (active **and** parked instances), used because the
    /// main loop is inside the drain and cannot run its deferred-load frame. Commit/cleanup remain on the
    /// global idle gate; this never relaxes it.
    static void serviceDeferredRestoreForDrain();
    /// Idle slice: hydrate one track undo stack snapshot body from the runtime bundle.
    static void processDeferredUndoSnapshots();
    /// `restored-library-stability` 1.3: request the bounded on-demand library observation. Raised only
    /// from the session-capture HITL surface (`!LIB_OBSERVE`); without a request the observation never
    /// advances and emits nothing.
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
    struct SlotPayloadPage {
        uint32_t fileSize = 0, bodyOffset = 0, setEpoch = 0;
        uint32_t contentIdentity = 0, rootIdentity = 0;
        uint8_t count = 0;
        uint8_t bytes[64]{};
    };
    /// Stateless bounded SD read; never loads/publishes a Loop or mutates the card.
    static const char* readSlotPayloadPageForHitl(uint8_t track, uint8_t slot,
                                                uint32_t offset, SlotPayloadPage& page);
#endif
    static void requestLibraryObservation();
    /// `restored-library-stability` 1.3: service the on-demand observation at the **unconditional
    /// observation tail** in `main` — the idle-owner position that already calls
    /// `processHitlSerialCommands` / `emitPostRestoreCensusIfReady`, deliberately **not** the
    /// admission-gated deferred load/restore frame.
    static void serviceLibraryObservation();
    /// On slot select: queue loop slot payload for deferred restore (never sync-loads).
    static void requestLoopSlotRestoreFromSd(uint8_t trackIndex, uint8_t slotIndex);
    /// Enqueue all HEADER_READY SD payloads for background fill (neighbors → track → forward).
    static void enqueueRemainingLoopSlotRestoresFromSd();
    /// Re-sort deferred restore queue using current track/slot focus.
    static void reprioritizeDeferredLoopSlotRestore();
    /// Queue focus slot and remaining payloads for deferred restore (priority fill).
    static void prioritizeLoopSlotRestoreForFocus(uint8_t trackIndex, uint8_t slotIndex);
    /// True when a verified loop-slot payload exists on SD (not yet loaded into RAM).
    /// Probes SD — use only on cold paths (boot scan, UI select, deferred restore admit).
    static bool loopSlotHasPayloadOnSd(uint8_t trackIndex, uint8_t slotIndex);
    /// O(1) RAM mirror of loopSlotHasPayloadOnSd; hot paths (clock slot-switch) must use this.
    static bool hasLoopSlotPayloadOnSdInRam(uint8_t trackIndex, uint8_t slotIndex);
    /// Probe SD and update the RAM mirror (cold path only).
    static void refreshLoopSlotPayloadOnSdInRam(uint8_t trackIndex, uint8_t slotIndex);
    /// Set RAM mirror after persist finalize or load without probing SD.
    static void setLoopSlotPayloadOnSdInRam(uint8_t trackIndex, uint8_t slotIndex, bool hasPayload);
    /// Before undo: finish deferred undo snapshot hydration when still pending.
    static void restoreDeferredUndoSnapshotsBeforeUse();
    /// Observation-only remainder DIAG: active LoadLoopJob identity. 255 = none.
    static void probeActiveLoadLoopJob(bool& active, uint8_t& track, uint8_t& slot, uint8_t& phase,
                                       uint8_t& isFocus);
    /// Observation-only: read/parse progress of the jobs that hold a slot-load session, summed over
    /// the active and parked instances. Zero/zero means no admitted preparation has happened yet.
    static void probeLoadLoopJobProgress(size_t& bytesRead, size_t& payloadSize);
    /// Observation-only: the **parked** LoadLoopJob instance. It is invisible to
    /// `probeActiveLoadLoopJob`, yet it keeps its `SlotLoadSession` alive, which is what
    /// `deferredSaveBlockedByActiveSlotLoadSd()` reports. All outputs are 0/false when none is parked.
    static void probeParkedLoadLoopJob(bool& active, uint8_t& track, uint8_t& slot, uint8_t& phase);
    /// Observation-only request/queue state (A1.8, `design.md` D6). One call answers "what is the
    /// restore work doing right now" without a bisect: how many requests are queued, whether the
    /// priority head can begin (and why not when it cannot), which request each job instance owns and
    /// in which phase, and whether the deferred save is currently held back by the restore.
    /// This is the same request/queue state the runtime already tracks; it adds no per-turn cost when
    /// it is not called.
    struct RestoreStateProbe {
        uint16_t queuedCount = 0;
        bool queueHeadPresent = false;
        uint8_t queueHeadTrack = 255;
        uint8_t queueHeadSlot = 255;
        uint16_t queueHeadPriority = 0;
        /// True when no live owner holds the head's slot, i.e. the head may begin.
        bool queueHeadStartable = false;
        bool activeJob = false;
        uint8_t activeJobTrack = 255;
        uint8_t activeJobSlot = 255;
        uint8_t activeJobPhase = 255;
        bool activeJobIsFocus = false;
        bool parkedJob = false;
        uint8_t parkedJobTrack = 255;
        uint8_t parkedJobSlot = 255;
        uint8_t parkedJobPhase = 255;
        size_t activeBytesRead = 0;
        size_t activePayloadSize = 0;
        size_t parkedBytesRead = 0;
        size_t parkedPayloadSize = 0;
        /// The request the deferred save is currently waiting on, when it is waiting on one.
        bool saveBlockedBySlotLoad = false;
        bool saveBlockedByPostLoadHoldoff = false;
        /// A1.8: the request-record vocabulary for the request each block above reports, so an operator
        /// can tell a prepare-complete wait (`waiting_commit_gate`) and a decided terminal from ownership
        /// alone. Null when no record exists for those coordinates (a cleared or never-begun record).
        const char* queueHeadRequestState = nullptr;
        const char* queueHeadRequestWait = nullptr;
        const char* queueHeadRequestTerminal = nullptr;
        const char* activeJobRequestState = nullptr;
        const char* activeJobRequestWait = nullptr;
        const char* activeJobRequestTerminal = nullptr;
        const char* parkedJobRequestState = nullptr;
        const char* parkedJobRequestWait = nullptr;
        const char* parkedJobRequestTerminal = nullptr;
    };
    static void probeRestoreState(RestoreStateProbe& out);

private:
    static bool loadCurrentSetFromSd(LooperState& state);
    static bool loadCurrentSetFromDirectory(const char* setDir, LooperState& state);
    static bool loadV5MonolithIntoRam(LooperState& state);
    static bool migrateV5MonolithToCurrentSet(LooperState& state);
    static bool attemptBootRecoveryChain(LooperState& state);
    static bool loadCurrentWorkspaceAtBoot(LooperState& state);
    static bool tryLoadLatestRecoveryPoint(LooperState& state);
    static bool tryLoadNewestSavedSet(LooperState& state);
    static bool loadCurrentSetBundleAndActiveLoopSlots(File& file, const char* setDir, LooperState& state,
                                                       std::vector<uint8_t>& activeLoopIndex,
                                                       uint8_t& selectedTrackIdx);
};

#include "StorageManagerStatusQueries.inl"
