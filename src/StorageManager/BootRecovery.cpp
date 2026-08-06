//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Boot load entry, workspace load, and recovery chain (revision queue, checkpoints).

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "BootRecoveryPolicy.h"
#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "Globals.h"
#include "RtcTime.h"
#include "SavedSetCatalog.h"
#include "SetRevisionCatalog.h"
#include "TrackManager.h"
#include "Utils/BootTelemetry.h"
#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

namespace StorageManagerInternal {

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

void queueBootRevisionRecovery(uint16_t setId, uint16_t revisionId) {
    if (setId == 0 || revisionId == 0) {
        return;
    }
    storageSession.bootRecovery.setId = setId;
    storageSession.bootRecovery.revisionId = revisionId;
    storageSession.bootRecovery.pending = true;
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

}  // namespace StorageManagerInternal

bool StorageManager::loadCurrentWorkspaceAtBoot(LooperState& state) {
    Serial.println("[StorageManager] Loading Current workspace from SD...");
    StorageManagerInternal::loadWorkspaceMetaCountersFromSd();
    const bool ok = loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir, state);
    if (ok) {
        Serial.println("[StorageManager] Current workspace loaded successfully.");
    } else {
        Serial.println("[StorageManager] Current workspace load failed.");
    }
    return ok;
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
    std::snprintf(recoveryDir, sizeof(recoveryDir), "%s/%s", CurrentSetStorage::kCheckpointsDir,
                  latestName);
    Serial.print("[StorageManager] Boot recovery: trying RecoveryPoint ");
    Serial.println(recoveryDir);
    if (!loadCurrentSetFromDirectory(recoveryDir, state)) {
        return false;
    }
    StorageManagerInternal::forceCurrentSetFullLoopWrite = true;
    StorageManagerInternal::markAllCurrentSetLoopSlotsDirtyForBootRecovery();
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
    StorageManagerInternal::forceCurrentSetFullLoopWrite = true;
    StorageManagerInternal::markAllCurrentSetLoopSlotsDirtyForBootRecovery();
    requestDeferredSaveState(state);
    return true;
}

bool StorageManager::attemptBootRecoveryChain(LooperState& state) {
    CurrentWorkspaceStorage::WorkspaceMetaRecord workspaceMeta{};
    if (!CurrentWorkspaceStorage::readWorkspaceMetaFile(workspaceMeta)) {
        workspaceMeta.derivedFromSetId = StorageManagerInternal::workspaceDerivedFromSetId;
        workspaceMeta.derivedFromRevisionId = StorageManagerInternal::workspaceDerivedFromRevisionId;
    }

    const BootRecoveryPolicy::RevisionRecoveryPlan plan =
        BootRecoveryPolicy::buildRevisionRecoveryPlan(
            workspaceMeta.derivedFromSetId, workspaceMeta.derivedFromRevisionId, 0);

    if (plan.setId != 0) {
        uint16_t catalogLatestRevisionId = 0;
        StorageManagerInternal::readSetLatestRevisionIdFromSd(plan.setId, catalogLatestRevisionId);
        const BootRecoveryPolicy::RevisionRecoveryPlan resolvedPlan =
            BootRecoveryPolicy::buildRevisionRecoveryPlan(
                plan.setId, plan.derivedRevisionId, catalogLatestRevisionId);

        if (resolvedPlan.derivedRevisionId != 0) {
            Serial.print("[StorageManager] Boot recovery: queue derived revision S");
            Serial.print(resolvedPlan.setId);
            Serial.print(" v");
            Serial.println(resolvedPlan.derivedRevisionId);
            StorageManagerInternal::queueBootRevisionRecovery(resolvedPlan.setId,
                                                              resolvedPlan.derivedRevisionId);
            return true;
        }

        const uint16_t latestFallback = BootRecoveryPolicy::resolveLatestRevisionFallback(
            resolvedPlan.derivedRevisionId, resolvedPlan.latestRevisionId);
        if (latestFallback != 0) {
            Serial.print("[StorageManager] Boot recovery: queue latest revision S");
            Serial.print(resolvedPlan.setId);
            Serial.print(" v");
            Serial.println(latestFallback);
            StorageManagerInternal::queueBootRevisionRecovery(resolvedPlan.setId, latestFallback);
            return true;
        }
    }

    if (tryLoadLatestRecoveryPoint(state)) {
        Serial.println("[StorageManager] Boot recovered from recovery checkpoint.");
        return true;
    }
    Serial.println("[StorageManager] Boot recovery chain exhausted; starting empty.");
    StorageManagerInternal::forceCurrentSetFullLoopWrite = false;
    StorageManagerInternal::syncCurrentSetDirtyTrackingFromLoadedState();
    return false;
}

bool StorageManager::loadState(LooperState& state) {
    trackManager.beginBootLoad();
    emitBootMilestone("load", "start");
    struct BootLoadScope {
        ~BootLoadScope() { trackManager.endBootLoad(); }
    } bootLoadScope;

    StorageManagerInternal::resetStorageSessionJobs();
    RtcTime::init();
    StorageManagerInternal::syncWallClockFromSdTimestampsQuickForBootLoad();
    StorageManagerInternal::discardIncompleteRevisionTempFilesOnSd();
    StorageManagerInternal::discardIncompleteCurrentWorkspaceTempFilesOnSd();

    const bool hasCurrentWorkspace =
        SD.exists(CurrentSetStorage::kCurrentMetaPath) ||
        SD.exists(CurrentWorkspaceStorage::kWorkspaceMetaPath);
    if (hasCurrentWorkspace) {
        if (loadCurrentWorkspaceAtBoot(state)) {
            SavedSetCatalog::SetIndex index{};
            if (!StorageManagerInternal::reconcileSetIndexOnSd(index)) {
                Serial.println("[StorageManager] WARN: could not reconcile MidiLooper/sets/index.bin");
            }
            StorageManagerInternal::forceCurrentSetFullLoopWrite = false;
            StorageManagerInternal::syncCurrentSetDirtyTrackingFromLoadedState();
            emitBootMilestone("load", "ok");
            return true;
        }
        Serial.println("[StorageManager] Current workspace load failed; attempting boot recovery chain.");
        StorageManagerInternal::resetTracksAfterFailedLoad();
        if (attemptBootRecoveryChain(state)) {
            emitBootMilestone("load", "ok");
            return true;
        }
        StorageManagerInternal::forceCurrentSetFullLoopWrite = false;
        StorageManagerInternal::syncCurrentSetDirtyTrackingFromLoadedState();
        emitBootMilestone("load", "fail");
        return false;
    }
    if (SD.exists(CurrentSetStorage::kLegacyMonolithPath)) {
        const bool migrated = migrateV5MonolithToCurrentSet(state);
        emitBootMilestone("load", migrated ? "ok" : "fail");
        return migrated;
    }
    const bool recovered = attemptBootRecoveryChain(state);
    emitBootMilestone("load", recovered ? "ok" : "fail");
    return recovered;
}
