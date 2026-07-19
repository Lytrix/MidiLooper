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
 * or flash) and to reload it on startup. Domain code should admit stale persistence work via
 * admitLoopPersist() / admitTrackMeta() / … — not markCurrentSet*Dirty(). requestDeferredSaveState()
 * wakes the legacy deferred writer until the work-item scheduler retires the monolith (B4).
 */
class StorageManager {
public:
    /// Admit stale loop payload persistence (record, overdub, undo, edit, …).
    static void admitLoopPersist(LoopId loopId);
    /// Admit stale loop undo history for a track slot (DEC-024 interim wire).
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

    static bool saveState(const LooperState& state);
    static bool loadState(LooperState& state);
    static bool loadCurrentWorkspaceFromSd(LooperState& state);
    static void requestDeferredSaveState(const LooperState& state, uint32_t admissionHeap = UINT32_MAX,
                                         bool isUrgentRequest = false);
    /// @deprecated Domain code: use admit* + requestDeferredSaveState only at transition boundaries.
    /// Keep save queued but do not start full workspace dispatch until grace elapses (PLAYING path).
    static void deferWorkspaceSaveDispatchDuringPlayback(uint32_t graceMs);
    static void processDeferredSaveState(const LooperState& state);
    static bool isDeferredSaveActive();
    static bool hasDeferredSaveWork();
    /// True while boot-time loop slot payloads are still queued for idle restore.
    static bool hasPendingLoopSlotRestore();
    /// True when the in-flight, parked, or next queued restore targets the selected track/slot.
    static bool isFocusedLoopSlotRestoreWork();
    /// Boot title audible drain: disable demote/park so parked jobs cannot block title clear.
    static void setBootTitleLoadDrain(bool enabled);
    /// True when a slot payload is not committed, not queued, and not actively loading.
    static bool needsSlotLoad(uint8_t trackIndex, uint8_t slotIndex);
    /// True when boot restore queue is empty and no SlotLoadSession is active.
    static bool bootInteractiveReady();
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
    /// @deprecated Prefer admitLoopPersist() + admitSlotMeta().
    static void markCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex);
    /// @deprecated Prefer admitTrackMeta() + per-loop admitLoopPersist().
    static void markCurrentSetTrackDirty(uint8_t trackIndex);
    /// @deprecated Prefer targeted admit* at transition boundaries.
    static void markAllCurrentSetLoopSlotsDirty();
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
    /// Idle slice: restore deferred loop slot payload(s) via DeferredJobScheduler.
    static void processDeferredLoopSlotRestore();
    /// Idle slice: hydrate one track undo stack snapshot body from the runtime bundle.
    static void processDeferredUndoSnapshots();
    /// On slot select: queue loop slot payload for deferred restore (never sync-loads).
    static void requestLoopSlotRestoreFromSd(uint8_t trackIndex, uint8_t slotIndex);
    /// Enqueue all HEADER_READY SD payloads for background fill (neighbors → track → forward).
    static void enqueueRemainingLoopSlotRestoresFromSd();
    /// Re-sort deferred restore queue using current track/slot focus.
    static void reprioritizeDeferredLoopSlotRestore();
    /// Queue focus slot and remaining payloads for deferred restore (priority fill).
    static void prioritizeLoopSlotRestoreForFocus(uint8_t trackIndex, uint8_t slotIndex);
    /// True when a verified loop-slot payload exists on SD (not yet loaded into RAM).
    static bool loopSlotHasPayloadOnSd(uint8_t trackIndex, uint8_t slotIndex);
    /// Before undo: finish deferred undo snapshot hydration when still pending.
    static void restoreDeferredUndoSnapshotsBeforeUse();

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