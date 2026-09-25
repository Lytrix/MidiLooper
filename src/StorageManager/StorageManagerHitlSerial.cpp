//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#if defined(SESSION_CAPTURE)

#include "StorageManager.h"

#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

#include "CurrentSetStorage.h"
#include "ClockManager.h"
#include "DisplayManager.h"
#include "Globals.h"
#include "TrackManager.h"
#include "HitlDisplayBridge.h"
#include "PersistenceLayout.h"
#include "SetRevisionCatalog.h"
#include "StorageManagerInternal.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/StoppedHeapDiagnostics.h"
#include "Utils/InvestigationCaptureLog.h"

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

// Explicit stopped-only diagnostic retrieval; never modify the retained log.
CAPTURE_HITL_MEM void printCrashLogTailForHitl() {
    File log = SD.open("crashlog.txt", FILE_READ);
    if (!log) {
        Serial.println("CRASH_LOG,missing");
        return;
    }
    constexpr uint32_t kMaxBytes = 4096;
    const uint32_t size = log.size();
    const uint32_t start = size > kMaxBytes ? size - kMaxBytes : 0;
    if (!log.seek(start)) {
        Serial.println("CRASH_LOG,seek_failed");
        log.close();
        return;
    }
    Serial.printf("CRASH_LOG,begin,size=%lu,offset=%lu\r\n",
                  static_cast<unsigned long>(size), static_cast<unsigned long>(start));
    uint8_t buffer[64];
    uint32_t remaining = size - start;
    while (remaining != 0) {
        const int count = log.read(buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (count <= 0) break;
        Serial.write(buffer, static_cast<size_t>(count));
        remaining -= static_cast<uint32_t>(count);
    }
    log.close();
    Serial.println("\r\nCRASH_LOG,end");
}

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

/// Newest `<parentDir>/<baseName>.bad.<stamp>` directory, or false when none
/// exists. `quarantineCurrentWorkspaceOnSdImpl` names them that way.
CAPTURE_HITL_MEM bool findNewestQuarantinedSibling(const char* parentDir, const char* baseName,
                                                   unsigned long& stampOut, char* nameOut,
                                                   size_t nameOutLen) {
    stampOut = 0;
    if (nameOut != nullptr && nameOutLen > 0) {
        nameOut[0] = '\0';
    }
    if (parentDir == nullptr || baseName == nullptr || nameOut == nullptr || nameOutLen == 0) {
        return false;
    }
    File dir = SD.open(parentDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        return false;
    }
    const size_t baseLen = std::strlen(baseName);
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        char name[72];
        std::snprintf(name, sizeof(name), "%s", entry.name());
        const bool isDirectory = entry.isDirectory();
        entry.close();
        if (!isDirectory || std::strncmp(name, baseName, baseLen) != 0 || name[baseLen] != '.' ||
            std::strncmp(name + baseLen + 1, "bad.", 4) != 0) {
            continue;
        }
        unsigned long stamp = 0;
        for (const char* cursor = name + baseLen + 5; *cursor >= '0' && *cursor <= '9'; ++cursor) {
            stamp = stamp * 10UL + static_cast<unsigned long>(*cursor - '0');
        }
        if (stamp >= stampOut) {
            stampOut = stamp;
            std::snprintf(nameOut, nameOutLen, "%s", name);
        }
    }
    dir.close();
    return nameOut[0] != '\0';
}

/// Rename `parentDir/baseName` aside to `parentDir/baseName.bad.<stamp>`.
/// Rename only — this never deletes, so the live tree stays recoverable.
CAPTURE_HITL_MEM bool parkLiveTreeAsQuarantine(const char* parentDir, const char* baseName,
                                               unsigned long stamp) {
    char live[96];
    char parked[96];
    const int liveWritten = std::snprintf(live, sizeof(live), "%s/%s", parentDir, baseName);
    const int parkedWritten =
        std::snprintf(parked, sizeof(parked), "%s/%s.bad.%lu", parentDir, baseName, stamp);
    if (liveWritten <= 0 || static_cast<size_t>(liveWritten) >= sizeof(live) ||
        parkedWritten <= 0 || static_cast<size_t>(parkedWritten) >= sizeof(parked)) {
        return false;
    }
    if (!SD.exists(live)) {
        return true;
    }
    if (SD.rename(live, parked)) {
        Serial.printf_P(PSTR("[StorageManager] Parked: %s -> %s\n"), live, parked);
        return true;
    }
    Serial.printf_P(PSTR("[StorageManager] ERROR: park rename failed: %s\n"), live);
    return false;
}

/// List every `<parentDir>/<baseName>.bad.<stamp>` tree. The quarantine stamp is
/// device `millis()` at the moment of rename, so it is not reproducible from the
/// host; the inventory is the only reliable way to name a restore target.
CAPTURE_HITL_MEM void printQuarantineInventoryForHitl(const char* parentDir, const char* baseName) {
    File dir = SD.open(parentDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        Serial.printf_P(PSTR("[StorageManager] Quarantine inventory %s: unavailable\n"), parentDir);
        return;
    }
    const size_t baseLen = std::strlen(baseName);
    unsigned count = 0;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        char name[72];
        std::snprintf(name, sizeof(name), "%s", entry.name());
        const bool isDirectory = entry.isDirectory();
        entry.close();
        if (!isDirectory || std::strncmp(name, baseName, baseLen) != 0 || name[baseLen] != '.' ||
            std::strncmp(name + baseLen + 1, "bad.", 4) != 0) {
            continue;
        }
        ++count;
        Serial.printf_P(PSTR("[StorageManager] Quarantine inventory %s/%s\n"), parentDir, name);
    }
    dir.close();
    Serial.printf_P(PSTR("[StorageManager] Quarantine inventory %s: %u tree(s)\n"), baseName,
                    count);
}

/// Rename `parentDir/baseName.bad.<stamp>` back to the live name.
CAPTURE_HITL_MEM bool restoreQuarantinedSibling(const char* parentDir, const char* baseName,
                                                unsigned long stamp) {
    if (stamp == 0) {
        return false;
    }
    char src[96];
    char dest[96];
    const int srcWritten =
        std::snprintf(src, sizeof(src), "%s/%s.bad.%lu", parentDir, baseName, stamp);
    const int destWritten = std::snprintf(dest, sizeof(dest), "%s/%s", parentDir, baseName);
    if (srcWritten <= 0 || static_cast<size_t>(srcWritten) >= sizeof(src) || destWritten <= 0 ||
        static_cast<size_t>(destWritten) >= sizeof(dest)) {
        return false;
    }
    if (!SD.exists(src)) {
        Serial.printf_P(PSTR("[StorageManager] ERROR: no such quarantined tree: %s\n"), src);
        return false;
    }
    if (SD.rename(src, dest)) {
        Serial.printf_P(PSTR("[StorageManager] Restored: %s -> %s\n"), src, dest);
        return true;
    }
    Serial.printf_P(PSTR("[StorageManager] ERROR: restore rename failed: %s\n"), src);
    return false;
}

/// Inverse of `!QUARANTINE_WORKSPACE`. `requestedStamp == 0` selects the newest
/// quarantined tree of each kind. Renames only: the live trees are parked as
/// `<name>.bad.<now>` rather than deleted, so the operation stays reversible.
/// Stopped-transport only; `.cursor/rules/Destructive-SD-Operations.mdc`
/// requires explicit user permission before running it.
CAPTURE_HITL_MEM void restoreQuarantinedWorkspaceForHitl(unsigned long requestedStamp,
                                                        bool listOnly) {
    const unsigned long parkStamp = millis();

    printQuarantineInventoryForHitl("/MidiLooper", "current");
    printQuarantineInventoryForHitl("/MidiLooper/recovery", "checkpoints");
    if (listOnly) {
        Serial.printf_P(PSTR("[StorageManager] Quarantine inventory only; nothing renamed.\n"));
        return;
    }

    unsigned long currentStamp = requestedStamp;
    if (currentStamp == 0) {
        char newest[72];
        (void)findNewestQuarantinedSibling("/MidiLooper", "current", currentStamp, newest,
                                           sizeof(newest));
        Serial.printf_P(PSTR("[StorageManager] Restore target current: %s\n"),
                        newest[0] != '\0' ? newest : "(none)");
    }

    unsigned long checkpointsStamp = requestedStamp;
    if (checkpointsStamp == 0) {
        char newest[72];
        (void)findNewestQuarantinedSibling("/MidiLooper/recovery", "checkpoints", checkpointsStamp,
                                           newest, sizeof(newest));
        Serial.printf_P(PSTR("[StorageManager] Restore target checkpoints: %s\n"),
                        newest[0] != '\0' ? newest : "(none)");
    }

    const bool parkedCurrent = parkLiveTreeAsQuarantine("/MidiLooper", "current", parkStamp);
    const bool parkedCheckpoints =
        parkLiveTreeAsQuarantine("/MidiLooper/recovery", "checkpoints", parkStamp);
    const bool restoredCurrent =
        parkedCurrent && restoreQuarantinedSibling("/MidiLooper", "current", currentStamp);
    const bool restoredCheckpoints =
        parkedCheckpoints &&
        restoreQuarantinedSibling("/MidiLooper/recovery", "checkpoints", checkpointsStamp);
    Serial.printf_P(
        PSTR("[StorageManager] Workspace restore: parked current=%u checkpoints=%u, "
             "restored current=%u checkpoints=%u\n"),
        parkedCurrent ? 1U : 0U, parkedCheckpoints ? 1U : 0U, restoredCurrent ? 1U : 0U,
        restoredCheckpoints ? 1U : 0U);
    if (restoredCurrent) {
        Serial.printf_P(
            PSTR("[StorageManager] Workspace restore complete. Reboot Teensy to load it.\n"));
    } else {
        Serial.printf_P(PSTR(
            "[StorageManager] Workspace restore incomplete (no matching quarantined current).\n"));
    }
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
            if (std::strncmp(sHitlSerialLineBuffer, "!TRACE_FENCE ", 13) == 0 &&
                std::strlen(sHitlSerialLineBuffer + 13) == 32 &&
                !clockManager.isTransportRunning()) {
                // Explicit host request only; bypass the retained capture ring
                // so an old buffered run cannot impersonate this acknowledgement.
                Serial.printf("#CAP,%lu,TRACE_FENCE,%s\r\n",
                              static_cast<unsigned long>(micros()), sHitlSerialLineBuffer + 13);
            } else if (std::strcmp(sHitlSerialLineBuffer, "!CRASH_LOG") == 0 &&
                       !clockManager.isTransportRunning()) {
                printCrashLogTailForHitl();
            } else if (std::strncmp(sHitlSerialLineBuffer, "!SLOT_BYTES ", 12) == 0) {
                uint32_t values[3]{};
                const char* cursor = sHitlSerialLineBuffer + 12;
                bool valid = true;
                for (uint8_t field = 0; field < 3 && valid; ++field) {
                    while (*cursor == ' ') ++cursor;
                    if (*cursor < '0' || *cursor > '9') { valid = false; break; }
                    while (*cursor >= '0' && *cursor <= '9') {
                        const uint32_t digit = static_cast<uint32_t>(*cursor++ - '0');
                        if (values[field] > (UINT32_MAX - digit) / 10u) { valid = false; break; }
                        values[field] = values[field] * 10u + digit;
                    }
                    if (*cursor != ' ' && *cursor != '\0') valid = false;
                }
                while (*cursor == ' ') ++cursor;
                const unsigned track = values[0], slot = values[1];
                const unsigned long offset = values[2];
                if (!valid || *cursor != '\0' || track >= Config::NUM_TRACKS ||
                    slot >= Config::MAX_LOOPS_PER_TRACK) {
                    Serial.println(F("SLOT_BYTES,error,invalid_request"));
                } else if (Serial.availableForWrite() >= 256) {
                    SlotPayloadPage page{};
                    const char* status = readSlotPayloadPageForHitl(track, slot, offset, page);
                    // Maximum successful response <256 bytes. One host request at a time.
                    Serial.printf_P(PSTR("SLOT_BYTES,%u,%u,%lu,%s,%lu,%lu,%lu,%lu,%lu,%u,"),
                                  track, slot, offset, status,
                                  static_cast<unsigned long>(page.fileSize),
                                  static_cast<unsigned long>(page.bodyOffset),
                                  static_cast<unsigned long>(page.setEpoch),
                                  static_cast<unsigned long>(page.contentIdentity),
                                  static_cast<unsigned long>(page.rootIdentity), page.count);
                    for (uint8_t i = 0; i < page.count; ++i) Serial.printf_P(PSTR("%02x"), page.bytes[i]);
                    Serial.println();
                }
            } else if (std::strcmp(sHitlSerialLineBuffer, "!CAPTURE_CONTENT") == 0 &&
                       !clockManager.isTransportRunning()) {
                trackManager.getTrack(trackManager.getSelectedTrackIndex())
                    .printStoppedCaptureContentForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!STOP_STATUS") == 0 &&
                       !clockManager.isTransportRunning()) {
                trackManager.getTrack(trackManager.getSelectedTrackIndex())
                    .printStoppedServiceStatusForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!LIB_OBSERVE") == 0) {
                // `restored-library-stability` 1.3: request the bounded on-demand library observation.
                // Deliberately not transport-gated: the states it must explain include a restore waiting
                // on the commit gate while another track plays. The observation itself respects the
                // SD-session admission and never blocks on it.
                StorageManager::requestLibraryObservation();
                Serial.println("[StorageManager] library observation requested");
            } else if (std::strcmp(sHitlSerialLineBuffer, "!TRACE_DRAIN") == 0 &&
                       !clockManager.isTransportRunning()) {
                // Explicit stopped-only retrieval also works while a pending
                // stop keeps the automatic capture drain timing-protected.
                DebugSessionCapture::flushCaptureBuffer(64);
            } else if (std::strcmp(sHitlSerialLineBuffer, "!CAP_IDENTITY_STATS") == 0 &&
                       !clockManager.isTransportRunning()) {
                // S1.3 save/restore-survival: read-only read-back of the bounded identity-path
                // counters (emitter entry, enqueue, rejection, transmission, eviction, last identity).
                // Stopped-only and outside the record-stop path: it neither records nor writes.
                DebugSessionCapture::emitIdentityPathStats();
#if defined(__IMXRT1062__)
            } else if (std::strcmp(sHitlSerialLineBuffer, "!HEAP_STATS") == 0) {
                StoppedHeapDiagnostics::Gate gate;
                gate.transport = clockManager.isTransportRunning();
                gate.clock = clockManager.isExternalClockPresent() || StoppedHeapDiagnostics::recentClock(
                    micros(), MemoryMonitor::heapDiagnosticLastClockUs, MemoryMonitor::heapDiagnosticClockSeen);
                for (size_t i = 0; i < Config::NUM_TRACKS; ++i) {
                    const auto& track = trackManager.getTrack(i);
                    gate.tracks |= track.isPlaying() || track.hasLiveCaptureInterval() || track.hasStopInFlight();
                }
                gate.storage = StorageManager::isDeferredSaveActive() ||
                    StorageManager::hasPendingLoopSlotRestore() ||
                    StorageManager::hasPendingUndoSnapshotHydrate() ||
                    StorageManager::isRevisionCommitActive() || StorageManager::isRevisionLoadActive() ||
                    trackManager.hasPendingDeferredPlaybackPrewarm();
                MemoryMonitor::emitStoppedHeapStats(gate);
#endif
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_COMMIT") == 0) {
                requestCommitRevisionForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_CLEANUP") == 0) {
                cleanupHitlRevisionCommit();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_NUKE_SETS") == 0) {
                nukeHitlSetsCatalog();
            } else if (StorageManagerInternal::handleHitlQuarantineCommandLine(sHitlSerialLineBuffer)) {
                Serial.println("[StorageManager] Reboot Teensy to load empty workspace.");
            } else if (std::strncmp(sHitlSerialLineBuffer, "!RESTORE_WORKSPACE", 18) == 0 &&
                       !clockManager.isTransportRunning()) {
                // Inverse of quarantine. Stopped-only, and renames only: the live
                // trees are parked as `.bad.<now>` instead of being deleted. An
                // optional trailing stamp selects which quarantine to restore
                // (`0`/absent picks the newest).
                unsigned long requestedStamp = 0;
                bool listOnly = false;
                const char* cursor = sHitlSerialLineBuffer + 18;
                while (*cursor == ' ') {
                    ++cursor;
                }
                if (std::strncmp(cursor, "list", 4) == 0) {
                    listOnly = true;
                }
                for (; *cursor >= '0' && *cursor <= '9'; ++cursor) {
                    requestedStamp =
                        requestedStamp * 10UL + static_cast<unsigned long>(*cursor - '0');
                }
                restoreQuarantinedWorkspaceForHitl(requestedStamp, listOnly);
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_YES") == 0) {
                confirmRevisionLoadAfterCommitForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_NO") == 0) {
                confirmRevisionLoadDiscardWorkspaceForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_CANCEL") == 0) {
                cancelRevisionLoadRequestForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!SNAPSHOT_YIELD") == 0) {
#if SESSION_CAPTURE_SNAPSHOT_IDLE
                InvestigationCaptureLog::armSnapshotIdleForcedYield();
#endif
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
