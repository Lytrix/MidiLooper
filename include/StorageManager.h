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
    static void requestUrgentEditSave();
    static void processEditAutosave(const LooperState& state);
    static bool saveNewSet(char* savedSetFolderOut = nullptr, size_t outSize = 0);
    static bool loadSetIntoCurrent(const char* savedSetFolderName);
    static void processSavedSetFailsafe(const LooperState& state);
    static uint32_t getCurrentSetLastActiveUnix();
    static bool copyCurrentSetLoadedFromFolder(char* out, size_t outSize);
    static bool consumeAutoSaveBeforeLoadFolder(char* out, size_t outSize);
    static size_t listSavedSetFolderEntries(SavedSetCatalog::SavedSetFolderListEntry* entries,
                                            size_t maxEntries);
    static bool readSavedSetMetadataForFolder(const char* folderName,
                                              SavedSetCatalog::SavedSetMetadata& metadata);
    static bool readCurrentSetBrowserMetadata(SavedSetCatalog::SavedSetMetadata& metadata);
    static void markCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex);
    static void markCurrentSetTrackDirty(uint8_t trackIndex);
    static void markAllCurrentSetLoopSlotsDirty();
    static DeferredSaveDisplayStatus getDeferredSaveDisplayStatus(uint32_t nowMs);
    static bool isCurrentWorkspaceDirty();
    static uint32_t getCurrentWorkspaceEpoch();
    static uint32_t getLastCommittedWorkspaceEpoch();
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
    static bool isRevisionLoadDirtyPromptActive();
    static uint8_t getRevisionLoadDirtyPromptSelection();
    static void adjustRevisionLoadDirtyPromptSelection(int delta);
    static void confirmRevisionLoadDirtyPromptSaveThenLoad();
    static void confirmRevisionLoadDirtyPromptDiscard();
    static void cancelRevisionLoadDirtyPrompt();
    static void requestCommitRevision();
    static bool hasRevisionCommitWork();
    static bool isRevisionCommitActive();
    static void requestLoadRevision(uint16_t setId, uint16_t revisionId);
    static void requestLoadLatestRevisionForSet(uint16_t setId);
    static bool toggleSetRevisionCatalogFavorite(uint16_t setId);
    static bool hasRevisionLoadWork();
    static bool isRevisionLoadActive();
    static bool consumeRevisionLoadDisplayRefreshPending();
#if defined(SESSION_CAPTURE)
    static void requestCommitRevisionForHitl();
    static bool cleanupHitlRevisionCommit();
    static void requestLoadRevisionForHitl(uint16_t setId, uint16_t revisionId);
    static void confirmRevisionLoadDirtyPromptSaveThenLoadForHitl();
    static void confirmRevisionLoadDirtyPromptDiscardForHitl();
    static void cancelRevisionLoadDirtyPromptForHitl();
    static bool nukeHitlSetsCatalog();
    static void processHitlSerialCommands();
#endif

private:
    static bool loadCurrentSetFromSd(LooperState& state);
    static bool loadCurrentSetFromDirectory(const char* setDir, LooperState& state);
    static bool loadV5MonolithIntoRam(LooperState& state);
    static bool migrateV5MonolithToCurrentSet(LooperState& state);
    static bool attemptBootRecoveryChain(LooperState& state);
    static bool loadCurrentWorkspaceAtBoot(LooperState& state);
    static bool tryLoadLatestRecoveryPoint(LooperState& state);
    static bool tryLoadNewestSavedSet(LooperState& state);
    static bool loadCurrentSetMetaAndTracks(File& file, const char* setDir, LooperState& state,
                                            std::vector<uint8_t>& activeLoopIndex,
                                            uint8_t& selectedTrackIdx);
}; 