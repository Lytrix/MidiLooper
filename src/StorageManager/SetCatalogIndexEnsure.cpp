//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// I4 — Set browser index ownership: the catalog index is ensured once per boot, **before** the legacy
// SavedSet reconcile repairs its own, separate index.
//
// Why this exists (`SET-BROWSER-EMPTY-CATALOG-DIAGNOSIS.md`): both owners shared `sets/index.bin` and
// `sets/index.bin.tmp` in different layouts (16-byte catalog index vs 8-byte `nextSequence` + `"SAVE"`).
// The reconcile rewrites that file in the legacy layout whenever it cannot read it -- which is always
// true for the catalog's 16-byte form -- so the catalog index never survived a boot: the browser listed
// no Sets and a commit could allocate set id 1 over an existing Set folder.
//
// Recovery is deliberately **not** a re-read of the old path alone: a clobbered card has no 16-byte
// index left, so the index is rebuilt from the validated Set folders (bounded, meta only, no revision
// payload opened). An intact 16-byte index still sitting at the legacy path (the window between a
// commit and the next boot) is adopted first, before the reconcile can overwrite it.
#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "SetRevisionCatalog.h"
#include <Arduino.h>
#include <SD.h>
#include <cstdio>

namespace StorageManagerInternal {
namespace {

/// Bounds for the rebuild scan: one entry per Set folder, and at most this many validated rows. A
/// larger card keeps every folder id known (`nextSetId` still clears them); only the informational
/// `setCount` stops growing, which is what bounds the work.
constexpr uint16_t kMaxCatalogRebuildEntries = 64;
constexpr uint32_t kMaxCatalogRebuildRows = 64;

STORAGE_PERSIST_MEM bool readCatalogIndexAt(const char* path, SetRevisionCatalog::SetCatalogIndex& index) {
    if (path == nullptr || !SD.exists(path)) {
        return false;
    }
    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }
    const StorageIo io = storageIoFromFileRead(file);
    const bool ok = SetRevisionCatalog::readSetCatalogIndex(io, index);
    file.close();
    return ok;
}

/// Temp + rename, so a failing or interrupted write cannot damage the index already on the card.
STORAGE_PERSIST_MEM bool writeCatalogIndexOnSd(const SetRevisionCatalog::SetCatalogIndex& index) {
    File temp = CurrentSetStorage::openForRewrite(SetRevisionCatalog::kSetIndexTempPath);
    if (!temp) {
        return false;
    }
    const StorageIo io = storageIoFromFileWrite(temp);
    const bool written = SetRevisionCatalog::writeSetCatalogIndex(io, index);
    temp.close();
    if (!written) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(SetRevisionCatalog::kSetIndexTempPath,
                                                   SetRevisionCatalog::kSetIndexPath);
}

/// Derive the catalog from the Set folders: a row needs a strictly-parsed `S####` folder whose `set.bin`
/// validates (schema + CRC) and belongs to that id with a committed revision. `nextSetId` clears every
/// parsed folder id, accepted or not, so the next allocation can never land on an existing Set.
STORAGE_PERSIST_MEM bool rebuildCatalogIndexFromSetFolders(SetRevisionCatalog::SetCatalogIndex& indexOut) {
    uint16_t maxFolderId = 0;
    uint32_t acceptedRows = 0;

    if (SD.exists(SetRevisionCatalog::kSetsRoot)) {
        File setsDir = SD.open(SetRevisionCatalog::kSetsRoot);
        if (setsDir) {
            uint16_t examined = 0;
            while (examined < kMaxCatalogRebuildEntries) {
                File entry = setsDir.openNextFile();
                if (!entry) {
                    break;
                }
                const bool isDirectory = entry.isDirectory();
                const char* entryName = entry.name();
                char nameBuffer[24] = {};
                if (entryName != nullptr) {
                    // The device reports a bare entry name, the native card a full path; the strict
                    // parser drops any path prefix, and the buffer bounds both.
                    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", entryName);
                }
                entry.close();
                if (!isDirectory) {
                    continue;
                }
                ++examined;
                uint16_t setId = 0;
                if (!SetRevisionCatalog::parseSetFolderBasename(nameBuffer, setId)) {
                    continue;
                }
                if (setId > maxFolderId) {
                    maxFolderId = setId;
                }
                if (acceptedRows >= kMaxCatalogRebuildRows) {
                    continue;
                }
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
                const bool metaOk = SetRevisionCatalog::readSetMetaRecord(metaIo, meta);
                metaFile.close();
                if (!metaOk || meta.setId != setId || meta.latestRevisionId == 0) {
                    continue;
                }
                ++acceptedRows;
            }
            setsDir.close();
        }
    }

    indexOut = SetRevisionCatalog::SetCatalogIndex{};
    indexOut.nextSetId = static_cast<uint32_t>(maxFolderId) + 1U;
    indexOut.setCount = acceptedRows;
    return writeCatalogIndexOnSd(indexOut);
}

}  // namespace

STORAGE_PERSIST_MEM const char* setCatalogEnsureOutcomeName(SetCatalogEnsureOutcome outcome) {
    switch (outcome) {
        case SetCatalogEnsureOutcome::Ok:
            return "ok";
        case SetCatalogEnsureOutcome::Adopted:
            return "adopted";
        case SetCatalogEnsureOutcome::Rebuilt:
            return "rebuilt";
        case SetCatalogEnsureOutcome::Failed:
            return "failed";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM SetCatalogEnsureOutcome ensureSetCatalogIndexOnSd() {
    SetRevisionCatalog::SetCatalogIndex index{};
    if (readCatalogIndexAt(SetRevisionCatalog::kSetIndexPath, index)) {
        return SetCatalogEnsureOutcome::Ok;
    }
    if (readCatalogIndexAt(CurrentSetStorage::kSetIndexPath, index)) {
        if (!writeCatalogIndexOnSd(index)) {
            return SetCatalogEnsureOutcome::Failed;
        }
        return SetCatalogEnsureOutcome::Adopted;
    }
    if (!rebuildCatalogIndexFromSetFolders(index)) {
        return SetCatalogEnsureOutcome::Failed;
    }
    return SetCatalogEnsureOutcome::Rebuilt;
}

}  // namespace StorageManagerInternal
