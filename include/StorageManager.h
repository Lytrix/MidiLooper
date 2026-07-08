//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "DeferredSaveDisplayStatus.h"
#include "LooperState.h"
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
 * or flash) and to reload it on startup. Runtime callers should request deferred saves via
 * requestDeferredSaveState(); saveState() drains the deferred writer synchronously (maintenance /
 * explicit flush only — not for hot paths).
 */
class StorageManager {
public:
    static bool saveState(const LooperState& state);
    static bool loadState(LooperState& state);
    static bool loadCurrentWorkspaceFromSd(LooperState& state);
    static void requestDeferredSaveState(const LooperState& state, uint32_t admissionHeap = UINT32_MAX,
                                         bool isUrgentRequest = false);
    static void processDeferredSaveState(const LooperState& state);
    static bool isDeferredSaveActive();
    static bool hasDeferredSaveWork();
    /// True while boot-time loop slot payloads are still queued for idle restore.
    static bool hasPendingLoopSlotRestore();
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
    static void markCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex);
    static void markCurrentSetTrackDirty(uint8_t trackIndex);
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
    /// Idle slice: restore one deferred loop slot payload from SD (M5 stack-safe restore).
    static void processDeferredLoopSlotRestore();
    /// Idle slice: hydrate one track undo stack snapshot body from the runtime bundle.
    static void processDeferredUndoSnapshots();
    /// On slot select: load loop slot payload immediately if still deferred.
    static void requestLoopSlotRestoreFromSd(uint8_t trackIndex, uint8_t slotIndex);
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