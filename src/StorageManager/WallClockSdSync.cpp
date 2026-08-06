//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// SD timestamp wall-clock floor sync (quick path + sliced catalog walk).

#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "RtcTime.h"
#include "SetRevisionCatalog.h"
#include <Arduino.h>
#include <SD.h>

namespace StorageManagerInternal {

namespace {

void considerSdWallClockFloor(uint32_t& maxFloor, uint64_t unixCandidate) {
    if (unixCandidate == 0) {
        return;
    }
    const uint32_t floor = unixCandidate > UINT32_MAX ? UINT32_MAX
                                                      : static_cast<uint32_t>(unixCandidate);
    if (floor > maxFloor) {
        maxFloor = floor;
    }
}

bool wallClockSdCatalogSyncPending = false;
uint32_t wallClockSdCatalogSyncNextSetId = 1;
uint32_t wallClockSdCatalogSyncEndSetId = 0;

void applySdWallClockFloor(uint32_t maxFloor) {
    if (maxFloor > 0) {
        RtcTime::raiseWallClockToAtLeast(maxFloor);
    }
}

void collectSdWallClockFloorFromWorkspaceAndCurrent(uint32_t& maxFloor) {
    CurrentWorkspaceStorage::WorkspaceMetaRecord workspace{};
    if (CurrentWorkspaceStorage::readWorkspaceMetaFile(workspace)) {
        considerSdWallClockFloor(maxFloor, workspace.updatedUnix);
    }

    if (SD.exists(CurrentSetStorage::kCurrentMetaPath)) {
        File file = SD.open(CurrentSetStorage::kCurrentMetaPath, FILE_READ);
        if (file) {
            const StorageIo io = storageIoFromFileRead(file);
            CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
            if (CurrentWorkspaceStorage::readEpochFileHeader(io, epochHeader)) {
                CurrentSetStorage::MetaHeader metaHeader{};
                if (CurrentSetStorage::readMetaHeader(io, metaHeader)) {
                    considerSdWallClockFloor(maxFloor, metaHeader.lastActiveUnix);
                    considerSdWallClockFloor(maxFloor, metaHeader.anchor.lastMaterialChangeUnix);
                }
            }
            file.close();
        }
    }
}

void queueWallClockSdCatalogSync() {
#if defined(ARDUINO)
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    wallClockSdCatalogSyncPending = false;
    wallClockSdCatalogSyncNextSetId = 1;
    wallClockSdCatalogSyncEndSetId = 0;
    if (!SD.exists(SetRevisionCatalog::kSetIndexPath)) {
        return;
    }
    File indexFile = SD.open(SetRevisionCatalog::kSetIndexPath, FILE_READ);
    if (!indexFile) {
        return;
    }
    SetRevisionCatalog::SetCatalogIndex catalogIndex{};
    const StorageIo indexIo = storageIoFromFileRead(indexFile);
    const bool indexOk = SetRevisionCatalog::readSetCatalogIndex(indexIo, catalogIndex);
    indexFile.close();
    if (!indexOk || catalogIndex.nextSetId <= 1U) {
        return;
    }
    wallClockSdCatalogSyncNextSetId = 1;
    wallClockSdCatalogSyncEndSetId = catalogIndex.nextSetId;
    wallClockSdCatalogSyncPending = true;
#endif
#endif
}

void syncWallClockFromSdTimestampsQuick() {
#if defined(ARDUINO)
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    uint32_t maxFloor = 0;
    collectSdWallClockFloorFromWorkspaceAndCurrent(maxFloor);
    applySdWallClockFloor(maxFloor);
    queueWallClockSdCatalogSync();
#endif
#endif
}

void stepWallClockFromSdCatalogSyncAnon(uint8_t maxSetsPerSlice) {
#if defined(ARDUINO)
#if BYPASS_STOP_UNDO_SAVE
    (void)maxSetsPerSlice;
    return;
#else
    if (!wallClockSdCatalogSyncPending || maxSetsPerSlice == 0) {
        return;
    }
    uint32_t maxFloor = 0;
    uint8_t scanned = 0;
    while (wallClockSdCatalogSyncNextSetId < wallClockSdCatalogSyncEndSetId &&
           scanned < maxSetsPerSlice) {
        const uint16_t setId = static_cast<uint16_t>(wallClockSdCatalogSyncNextSetId++);
        ++scanned;
        char setMetaPath[64];
        if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId) ||
            !SD.exists(setMetaPath)) {
            continue;
        }
        File metaFile = SD.open(setMetaPath, FILE_READ);
        if (!metaFile) {
            continue;
        }
        SetRevisionCatalog::SetMetaRecord meta{};
        const StorageIo metaIo = storageIoFromFileRead(metaFile);
        const bool readOk = SetRevisionCatalog::readSetMetaRecord(metaIo, meta);
        metaFile.close();
        if (!readOk || meta.setId != setId) {
            continue;
        }
        considerSdWallClockFloor(maxFloor, meta.updatedUnix);
        considerSdWallClockFloor(maxFloor, meta.createdUnix);
    }
    applySdWallClockFloor(maxFloor);
    if (wallClockSdCatalogSyncNextSetId >= wallClockSdCatalogSyncEndSetId) {
        wallClockSdCatalogSyncPending = false;
    }
#endif
#endif
}

}  // namespace

void stepWallClockFromSdCatalogSync(uint8_t maxSetsPerSlice) {
    stepWallClockFromSdCatalogSyncAnon(maxSetsPerSlice);
}

void syncWallClockFromSdTimestampsQuickForBootLoad() {
    syncWallClockFromSdTimestampsQuick();
}

}  // namespace StorageManagerInternal
