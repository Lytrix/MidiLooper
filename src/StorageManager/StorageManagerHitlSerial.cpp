//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#if defined(SESSION_CAPTURE)

#include "StorageManager.h"

#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

#include "CurrentSetStorage.h"
#include "DisplayManager.h"
#include "Globals.h"
#include "HitlDisplayBridge.h"
#include "PersistenceLayout.h"
#include "SetRevisionCatalog.h"
#include "StorageManagerInternal.h"
#include "Utils/DebugSessionCapture.h"

#if defined(__IMXRT1062__)
#define CAPTURE_HITL_MEM FLASHMEM
#define CAPTURE_HITL_DATA DMAMEM
#else
#define CAPTURE_HITL_MEM
#define CAPTURE_HITL_DATA
#endif

using namespace StorageManagerInternal;

namespace StorageManagerInternal {

CAPTURE_HITL_DATA HitlRevisionCommitBackup hitlRevisionCommitBackup{};

}  // namespace StorageManagerInternal

namespace {

// Serial line accumulator must live in RAM: processHitlSerialCommands is FLASHMEM and cannot
// access DMAMEM on IMXRT1062.
char sHitlSerialLineBuffer[48];
size_t sHitlSerialLineLength = 0;

CAPTURE_HITL_MEM bool removeEmptyDirectoryIfPresent(const char* path) {
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

CAPTURE_HITL_MEM bool parseRevisionSetFolderEntryName(const char* name, uint16_t& setIdOut) {
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

CAPTURE_HITL_MEM bool removeHitlSetFolderTree(const char* setFolderPath) {
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

CAPTURE_HITL_MEM bool renamePathOnSdIfPresent(const char* src, const char* dest) {
    if (src == nullptr || dest == nullptr || src[0] == '\0' || dest[0] == '\0') {
        return false;
    }
    if (!SD.exists(src)) {
        return true;
    }
    if (SD.rename(src, dest)) {
        Serial.print("[StorageManager] Quarantined: ");
        Serial.print(src);
        Serial.print(" -> ");
        Serial.println(dest);
        return true;
    }
    Serial.print("[StorageManager] ERROR: quarantine rename failed: ");
    Serial.println(src);
    return false;
}

CAPTURE_HITL_MEM bool quarantineCurrentWorkspaceOnSdImpl() {
    const unsigned long stamp = static_cast<unsigned long>(millis());
    char dest[72];
    bool ok = true;

    int written = std::snprintf(dest, sizeof(dest), "%s.bad.%lu", PersistenceLayout::kCurrentRoot,
                                stamp);
    if (written > 0 && static_cast<size_t>(written) < sizeof(dest)) {
        ok = renamePathOnSdIfPresent(PersistenceLayout::kCurrentRoot, dest) && ok;
    } else {
        ok = false;
    }

    written = std::snprintf(dest, sizeof(dest), "%s/checkpoints.bad.%lu",
                            PersistenceLayout::kRecoveryRoot, stamp);
    if (written > 0 && static_cast<size_t>(written) < sizeof(dest)) {
        ok = renamePathOnSdIfPresent(CurrentSetStorage::kCheckpointsDir, dest) && ok;
    } else {
        ok = false;
    }

    if (SD.exists(CurrentSetStorage::kLegacyMonolithPath)) {
        written = std::snprintf(dest, sizeof(dest), "/state.bad.%lu", stamp);
        if (written > 0 && static_cast<size_t>(written) < sizeof(dest)) {
            ok = renamePathOnSdIfPresent(CurrentSetStorage::kLegacyMonolithPath, dest) && ok;
        } else {
            ok = false;
        }
    }

    Serial.println(ok ? "[StorageManager] Workspace quarantine complete."
                      : "[StorageManager] Workspace quarantine incomplete.");
    return ok;
}

CAPTURE_HITL_MEM void pollBootQuarantineLineFromSerial(uint32_t listenMs) {
    Serial.println(
        "[StorageManager] Boot: send !QUARANTINE_WORKSPACE now to quarantine SD workspace "
        "(current + recovery checkpoints)");
    char line[48];
    size_t len = 0;
    bool sawByte = false;
    const uint32_t startMs = millis();
    const uint32_t deadlineMs = startMs + listenMs;
    while (millis() < deadlineMs) {
        if (Serial.available() == 0) {
            if (!sawByte && millis() - startMs >= 250) {
                return;
            }
            delay(1);
            continue;
        }
        sawByte = true;
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[len] = '\0';
            if (StorageManagerInternal::handleHitlQuarantineCommandLine(line)) {
                return;
            }
            len = 0;
            continue;
        }
        if (len + 1 < sizeof(line)) {
            line[len++] = ch;
        }
    }
}

}  // namespace

CAPTURE_HITL_MEM bool StorageManager::nukeHitlSetsCatalog() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    if (storageSession.revisionCommit.pending || storageSession.revisionCommit.inProgress ||
        storageSession.revisionLoad.pending || storageSession.revisionLoad.inProgress) {
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

CAPTURE_HITL_MEM void StorageManager::requestCommitRevisionForHitl() {
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

CAPTURE_HITL_MEM bool StorageManager::cleanupHitlRevisionCommit() {
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

CAPTURE_HITL_MEM bool StorageManagerInternal::handleHitlQuarantineCommandLine(const char* line) {
    if (line == nullptr || std::strcmp(line, "!QUARANTINE_WORKSPACE") != 0) {
        return false;
    }
    (void)StorageManager::quarantineCurrentWorkspaceOnSd();
    return true;
}

CAPTURE_HITL_MEM bool StorageManager::quarantineCurrentWorkspaceOnSd() {
    return quarantineCurrentWorkspaceOnSdImpl();
}

CAPTURE_HITL_MEM void StorageManager::pollBootQuarantineWorkspaceBeforeLoad(uint32_t listenMs) {
    pollBootQuarantineLineFromSerial(listenMs);
}

CAPTURE_HITL_MEM void StorageManager::processHitlSerialCommands() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            sHitlSerialLineBuffer[sHitlSerialLineLength] = '\0';
            if (std::strcmp(sHitlSerialLineBuffer, "!REV_COMMIT") == 0) {
                requestCommitRevisionForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_CLEANUP") == 0) {
                cleanupHitlRevisionCommit();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_NUKE_SETS") == 0) {
                nukeHitlSetsCatalog();
            } else if (StorageManagerInternal::handleHitlQuarantineCommandLine(sHitlSerialLineBuffer)) {
                Serial.println("[StorageManager] Reboot Teensy to load empty workspace.");
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_YES") == 0) {
                confirmRevisionLoadAfterCommitForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_NO") == 0) {
                confirmRevisionLoadDiscardWorkspaceForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_CANCEL") == 0) {
                cancelRevisionLoadRequestForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_SAVE") == 0) {
                HitlDisplayBridge::confirmLoadSaveFocusedRow();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_CONFIRM") == 0) {
                HitlDisplayBridge::confirmLoadSaveFocusedRow();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_ENTER") == 0) {
                looperState.enterLoadSaveMode();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_EXIT") == 0) {
                looperState.exitLoadSaveMode();
            } else if (std::strncmp(sHitlSerialLineBuffer, "!OVERLAY_SCROLL ", 16) == 0) {
                const char* cursor = sHitlSerialLineBuffer + 16;
                while (*cursor == ' ') {
                    ++cursor;
                }
                int delta = 0;
                bool negative = false;
                if (*cursor == '-') {
                    negative = true;
                    ++cursor;
                } else if (*cursor == '+') {
                    ++cursor;
                }
                while (*cursor >= '0' && *cursor <= '9') {
                    delta = delta * 10 + (*cursor - '0');
                    ++cursor;
                }
                if (negative) {
                    delta = -delta;
                }
                if (delta != 0) {
                    HitlDisplayBridge::adjustLoadSaveListSelection(delta);
                }
            } else if (std::strncmp(sHitlSerialLineBuffer, "!OVERLAY_REV_HISTORY ", 21) == 0) {
                const char* cursor = sHitlSerialLineBuffer + 21;
                while (*cursor == ' ') {
                    ++cursor;
                }
                unsigned setId = 0;
                while (*cursor >= '0' && *cursor <= '9') {
                    setId = setId * 10U + static_cast<unsigned>(*cursor - '0');
                    ++cursor;
                }
                if (setId > 0U && setId <= 0xFFFFU) {
                    HitlDisplayBridge::openRevisionHistoryFromHitl(static_cast<uint16_t>(setId));
                }
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_BACK") == 0) {
                HitlDisplayBridge::navigateLoadSaveOverlayBackFromHitl();
            } else if (std::strncmp(sHitlSerialLineBuffer, "!REV_LOAD ", 10) == 0) {
                const char* cursor = sHitlSerialLineBuffer + 10;
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
            sHitlSerialLineLength = 0;
            continue;
        }
        if (sHitlSerialLineLength + 1 < sizeof(sHitlSerialLineBuffer)) {
            sHitlSerialLineBuffer[sHitlSerialLineLength++] = ch;
        }
    }
#endif
}

#endif  // SESSION_CAPTURE
