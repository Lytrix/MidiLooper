//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "GlobalUndoStack.h"
#include "LooperState.h"
#include "StorageLoopIo.h"
#include "TrackUndo.h"

namespace StorageManagerInternal {

static bool writeOverdubPassIdsExtension(File& file, const UndoEntry& entry,
                                         bool overdubPassIdsExtension) {
    if (!overdubPassIdsExtension || entry.kind != UndoEntryKind::OverdubPassAdded) {
        return true;
    }
    const uint16_t count = static_cast<uint16_t>(entry.passIds.size());
    if (!writeRaw(file, &count, sizeof(count))) {
        return false;
    }
    for (const PassId id : entry.passIds) {
        if (!writeRaw(file, &id, sizeof(id))) {
            return false;
        }
    }
    return true;
}

static bool writeScopedEditUndoExtension(File& file, const UndoEntry& entry,
                                         bool overdubCompanionExtension,
                                         bool overdubPassIdsExtension) {
    switch (entry.kind) {
        case UndoEntryKind::NoteEditPassClosed:
        case UndoEntryKind::ControlChangeEditPassClosed:
            break;
        case UndoEntryKind::OverdubPassAdded:
            if (!overdubCompanionExtension) {
                return writeOverdubPassIdsExtension(file, entry, overdubPassIdsExtension);
            }
            break;
        default:
            return true;
    }
    if (!writeRaw(file, &entry.editPassIndex, sizeof(entry.editPassIndex))) {
        return false;
    }
    const uint8_t editPassTypeRaw = static_cast<uint8_t>(entry.editPassType);
    if (!writeRaw(file, &editPassTypeRaw, sizeof(editPassTypeRaw))) {
        return false;
    }
    const uint16_t count = static_cast<uint16_t>(entry.editPassIds.size());
    if (!writeRaw(file, &count, sizeof(count))) {
        return false;
    }
    for (const EditPassId id : entry.editPassIds) {
        if (!writeRaw(file, &id, sizeof(id))) {
            return false;
        }
    }
    return writeOverdubPassIdsExtension(file, entry, overdubPassIdsExtension);
}

static bool readOverdubPassIdsExtension(File& file, UndoEntry& entry, bool overdubPassIdsExtension) {
    entry.passIds.clear();
    if (!overdubPassIdsExtension || entry.kind != UndoEntryKind::OverdubPassAdded) {
        return true;
    }
    uint16_t count = 0;
    if (!readRaw(file, &count, sizeof(count))) {
        return false;
    }
    entry.passIds.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        PassId id = kInvalidPassId;
        if (!readRaw(file, &id, sizeof(id))) {
            return false;
        }
        entry.passIds.push_back(id);
    }
    return true;
}

static bool readScopedEditUndoExtension(File& file, UndoEntry& entry, bool scopedEditExtension,
                                        bool overdubCompanionExtension,
                                        bool overdubPassIdsExtension) {
    if (!scopedEditExtension) {
        return readOverdubPassIdsExtension(file, entry, overdubPassIdsExtension);
    }
    switch (entry.kind) {
        case UndoEntryKind::NoteEditPassClosed:
        case UndoEntryKind::ControlChangeEditPassClosed:
            break;
        case UndoEntryKind::OverdubPassAdded:
            if (!overdubCompanionExtension) {
                return readOverdubPassIdsExtension(file, entry, overdubPassIdsExtension);
            }
            break;
        default:
            return true;
    }
    uint8_t editPassTypeRaw = 0;
    uint16_t count = 0;
    if (!readRaw(file, &entry.editPassIndex, sizeof(entry.editPassIndex))) {
        return false;
    }
    if (!readRaw(file, &editPassTypeRaw, sizeof(editPassTypeRaw))) {
        return false;
    }
    entry.editPassType = static_cast<EditPassType>(editPassTypeRaw);
    if (!readRaw(file, &count, sizeof(count))) {
        return false;
    }
    entry.editPassIds.clear();
    entry.editPassIds.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        EditPassId id = kInvalidEditPassId;
        if (!readRaw(file, &id, sizeof(id))) {
            return false;
        }
        entry.editPassIds.push_back(id);
    }
    return readOverdubPassIdsExtension(file, entry, overdubPassIdsExtension);
}

static bool stackNeedsOverdubCompanionExtension(const GlobalUndoStack& stack) {
    for (const UndoEntry& entry : stack.entries) {
        if (entry.kind == UndoEntryKind::OverdubPassAdded && !entry.editPassIds.empty()) {
            return true;
        }
    }
    return false;
}

static bool stackNeedsOverdubPassIdsExtension(const GlobalUndoStack& stack) {
    for (const UndoEntry& entry : stack.entries) {
        if (entry.kind == UndoEntryKind::OverdubPassAdded && !entry.passIds.empty()) {
            return true;
        }
    }
    return false;
}

static bool readScopedEditExtensionHeader(File& file, bool& scopedEditExtension,
                                          bool& overdubCompanionExtension,
                                          bool& overdubPassIdsExtension) {
    scopedEditExtension = false;
    overdubCompanionExtension = false;
    overdubPassIdsExtension = false;
    const size_t pos = file.position();
    uint32_t maybeToken = 0;
    if (!readRaw(file, &maybeToken, sizeof(maybeToken))) {
        return false;
    }
    if (maybeToken == kGlobalUndoStackOverdubPassIdsExtensionToken) {
        scopedEditExtension = true;
        overdubCompanionExtension = true;
        overdubPassIdsExtension = true;
        return true;
    }
    if (maybeToken == kGlobalUndoStackOverdubCompanionExtensionToken) {
        scopedEditExtension = true;
        overdubCompanionExtension = true;
        return true;
    }
    if (maybeToken == kGlobalUndoStackScopedEditExtensionToken) {
        scopedEditExtension = true;
        return true;
    }
    if (!file.seek(pos)) {
        return false;
    }
    return true;
}

STORAGE_PERSIST_MEM bool undoStackHeaderLooksValid(uint32_t entryCount, uint32_t cursor) {
    if (entryCount > Config::ABSOLUTE_MAX_UNDO_ENTRIES) {
        return false;
    }
    if (cursor > entryCount) {
        return false;
    }
    return true;
}

STORAGE_PERSIST_MEM LooperState sanitizeLooperStateForPersistence(LooperState state) {
    switch (state) {
        case LOOPER_RECORDING:
        case LOOPER_PLAYING:
        case LOOPER_OVERDUBBING:
            return LOOPER_IDLE;
        default:
            return state;
    }
}

STORAGE_PERSIST_MEM uint32_t persistedLooperStateRaw(LooperState state) {
    return static_cast<uint32_t>(sanitizeLooperStateForPersistence(state));
}

STORAGE_PERSIST_MEM bool writeUndoLoopSnapshot(File& file, const LoopSnapshotRef& snapshot) {
    bool hasSnapshot = snapshot != nullptr;
    if (!writeRaw(file, &hasSnapshot, sizeof(hasSnapshot))) {
        return false;
    }
    if (!hasSnapshot) {
        return true;
    }
    const StorageIo io = storageIoFromFileWrite(file);
    return writePersistedLoopSnapshot(io, *snapshot);
}

STORAGE_PERSIST_MEM bool readUndoLoopSnapshot(File& file, LoopSnapshotRef& snapshot) {
    bool hasSnapshot = false;
    if (!readRaw(file, &hasSnapshot, sizeof(hasSnapshot))) {
        return false;
    }
    if (!hasSnapshot) {
        snapshot.reset();
        return true;
    }
    auto loaded = std::make_shared<PersistedLoopSnapshot>();
    const StorageIo io = storageIoFromFileRead(file);
    if (!readPersistedLoopSnapshot(io, *loaded)) {
        return false;
    }
    snapshot = std::move(loaded);
    return true;
}

STORAGE_PERSIST_MEM bool writeGlobalUndoStackToFile(File& file, const GlobalUndoStack& stack) {
    uint32_t entryCount = static_cast<uint32_t>(stack.entries.size());
    uint32_t cursor = static_cast<uint32_t>(stack.cursor);
    uint32_t nextEntryId = stack.nextEntryId;
    if (!writeRaw(file, &entryCount, sizeof(entryCount))) {
        return false;
    }
    if (!writeRaw(file, &cursor, sizeof(cursor))) {
        return false;
    }
    if (!writeRaw(file, &nextEntryId, sizeof(nextEntryId))) {
        return false;
    }
    const bool overdubPassIdsExtension = stackNeedsOverdubPassIdsExtension(stack);
    const bool overdubCompanionExtension =
        overdubPassIdsExtension || stackNeedsOverdubCompanionExtension(stack);
    const uint32_t stackExtensionToken = overdubPassIdsExtension
                                             ? kGlobalUndoStackOverdubPassIdsExtensionToken
                                             : (overdubCompanionExtension
                                                    ? kGlobalUndoStackOverdubCompanionExtensionToken
                                                    : kGlobalUndoStackScopedEditExtensionToken);
    if (!writeRaw(file, &stackExtensionToken, sizeof(stackExtensionToken))) {
        return false;
    }

    for (const UndoEntry& entry : stack.entries) {
        uint8_t kind = static_cast<uint8_t>(entry.kind);
        if (!writeRaw(file, &entry.id, sizeof(entry.id))) {
            return false;
        }
        if (!writeRaw(file, &kind, sizeof(kind))) {
            return false;
        }
        if (!writeRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) {
            return false;
        }
        if (!writeRaw(file, &entry.loopId, sizeof(entry.loopId))) {
            return false;
        }
        if (!writeRaw(file, &entry.passId, sizeof(entry.passId))) {
            return false;
        }

        if (!writeUndoLoopSnapshot(file, entry.beforeSnapshot)) {
            return false;
        }
        if (!writeUndoLoopSnapshot(file, entry.afterSnapshot)) {
            return false;
        }

        if (!writeRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) {
            return false;
        }
        if (!writeRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) {
            return false;
        }
        if (!writeRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) {
            return false;
        }
        if (!writeRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) {
            return false;
        }
        if (!writeRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) {
            return false;
        }
        if (!writeRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) {
            return false;
        }

        uint32_t beforeTrackState = static_cast<uint32_t>(entry.beforeTrackState);
        uint32_t afterTrackState = static_cast<uint32_t>(entry.afterTrackState);
        if (!writeRaw(file, &beforeTrackState, sizeof(beforeTrackState))) {
            return false;
        }
        if (!writeRaw(file, &afterTrackState, sizeof(afterTrackState))) {
            return false;
        }
        if (!writeRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) {
            return false;
        }
        if (!writeRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload))) {
            return false;
        }
        if (!writeScopedEditUndoExtension(file, entry, overdubCompanionExtension,
                                          overdubPassIdsExtension)) {
            return false;
        }
    }

    return true;
}

STORAGE_PERSIST_MEM bool skipUndoLoopSnapshot(File& file) {
    bool hasSnapshot = false;
    if (!readRaw(file, &hasSnapshot, sizeof(hasSnapshot))) {
        return false;
    }
    if (!hasSnapshot) {
        return true;
    }
    const StorageIo io = storageIoFromFileRead(file);
    return skipPersistedLoopSnapshotPayload(io, false);
}

STORAGE_PERSIST_MEM bool readGlobalUndoStackMetadataFromFile(File& file, GlobalUndoStack& stack) {
    uint32_t entryCount = 0;
    uint32_t cursor = 0;
    uint32_t nextEntryId = 1;
    if (!readRaw(file, &entryCount, sizeof(entryCount))) {
        return false;
    }
    if (!readRaw(file, &cursor, sizeof(cursor))) {
        return false;
    }
    if (!readRaw(file, &nextEntryId, sizeof(nextEntryId))) {
        return false;
    }
    if (!undoStackHeaderLooksValid(entryCount, cursor)) {
        Serial.print("[StorageManager] ERROR: undo stack header invalid (entries=");
        Serial.print(entryCount);
        Serial.print(" cursor=");
        Serial.println(cursor);
        return false;
    }

    bool scopedEditExtension = false;
    bool overdubCompanionExtension = false;
    bool overdubPassIdsExtension = false;
    if (!readScopedEditExtensionHeader(file, scopedEditExtension, overdubCompanionExtension,
                                       overdubPassIdsExtension)) {
        return false;
    }

    stack.clear();
    stack.nextEntryId = nextEntryId;
    stack.entries.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        UndoEntry entry;
        uint8_t kindRaw = 0;
        uint32_t beforeTrackStateRaw = 0;
        uint32_t afterTrackStateRaw = 0;
        if (!readRaw(file, &entry.id, sizeof(entry.id))) {
            return false;
        }
        if (!readRaw(file, &kindRaw, sizeof(kindRaw))) {
            return false;
        }
        entry.kind = static_cast<UndoEntryKind>(kindRaw);
        if (!readRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) {
            return false;
        }
        if (!readRaw(file, &entry.loopId, sizeof(entry.loopId))) {
            return false;
        }
        if (!readRaw(file, &entry.passId, sizeof(entry.passId))) {
            return false;
        }

        if (!skipUndoLoopSnapshot(file)) {
            return false;
        }
        if (!skipUndoLoopSnapshot(file)) {
            return false;
        }

        if (!readRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) {
            return false;
        }
        if (!readRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) {
            return false;
        }
        if (!readRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) {
            return false;
        }
        if (!readRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) {
            return false;
        }
        if (!readRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) {
            return false;
        }
        if (!readRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) {
            return false;
        }
        if (!readRaw(file, &beforeTrackStateRaw, sizeof(beforeTrackStateRaw))) {
            return false;
        }
        if (!readRaw(file, &afterTrackStateRaw, sizeof(afterTrackStateRaw))) {
            return false;
        }
        if (!readRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) {
            return false;
        }
        if (!readRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload))) {
            return false;
        }

        if (!readScopedEditUndoExtension(file, entry, scopedEditExtension,
                                         overdubCompanionExtension, overdubPassIdsExtension)) {
            return false;
        }

        entry.beforeTrackState = static_cast<TrackState>(beforeTrackStateRaw);
        entry.afterTrackState = static_cast<TrackState>(afterTrackStateRaw);
        stack.entries.push_back(std::move(entry));
    }

    stack.cursor = (cursor <= stack.entries.size()) ? cursor : stack.entries.size();
    if (stack.nextEntryId == 0) {
        stack.nextEntryId = 1;
    }
    return true;
}

STORAGE_PERSIST_MEM bool readGlobalUndoStackFromFile(File& file, GlobalUndoStack& stack) {
    uint32_t entryCount = 0;
    uint32_t cursor = 0;
    uint32_t nextEntryId = 1;
    if (!readRaw(file, &entryCount, sizeof(entryCount))) {
        return false;
    }
    if (!readRaw(file, &cursor, sizeof(cursor))) {
        return false;
    }
    if (!readRaw(file, &nextEntryId, sizeof(nextEntryId))) {
        return false;
    }
    if (!undoStackHeaderLooksValid(entryCount, cursor)) {
        Serial.print("[StorageManager] ERROR: undo stack header invalid (entries=");
        Serial.print(entryCount);
        Serial.print(" cursor=");
        Serial.println(cursor);
        return false;
    }

    bool scopedEditExtension = false;
    bool overdubCompanionExtension = false;
    bool overdubPassIdsExtension = false;
    if (!readScopedEditExtensionHeader(file, scopedEditExtension, overdubCompanionExtension,
                                       overdubPassIdsExtension)) {
        return false;
    }

    stack.clear();
    stack.nextEntryId = nextEntryId;
    stack.entries.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        UndoEntry entry;
        uint8_t kindRaw = 0;
        uint32_t beforeTrackStateRaw = 0;
        uint32_t afterTrackStateRaw = 0;
        if (!readRaw(file, &entry.id, sizeof(entry.id))) {
            return false;
        }
        if (!readRaw(file, &kindRaw, sizeof(kindRaw))) {
            return false;
        }
        entry.kind = static_cast<UndoEntryKind>(kindRaw);
        if (!readRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) {
            return false;
        }
        if (!readRaw(file, &entry.loopId, sizeof(entry.loopId))) {
            return false;
        }
        if (!readRaw(file, &entry.passId, sizeof(entry.passId))) {
            return false;
        }

        if (!readUndoLoopSnapshot(file, entry.beforeSnapshot)) {
            return false;
        }
        if (!readUndoLoopSnapshot(file, entry.afterSnapshot)) {
            return false;
        }

        if (!readRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) {
            return false;
        }
        if (!readRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) {
            return false;
        }
        if (!readRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) {
            return false;
        }
        if (!readRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) {
            return false;
        }
        if (!readRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) {
            return false;
        }
        if (!readRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) {
            return false;
        }
        if (!readRaw(file, &beforeTrackStateRaw, sizeof(beforeTrackStateRaw))) {
            return false;
        }
        if (!readRaw(file, &afterTrackStateRaw, sizeof(afterTrackStateRaw))) {
            return false;
        }
        if (!readRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) {
            return false;
        }
        if (!readRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload))) {
            return false;
        }

        if (!readScopedEditUndoExtension(file, entry, scopedEditExtension,
                                         overdubCompanionExtension, overdubPassIdsExtension)) {
            return false;
        }

        entry.beforeTrackState = static_cast<TrackState>(beforeTrackStateRaw);
        entry.afterTrackState = static_cast<TrackState>(afterTrackStateRaw);
        stack.entries.push_back(std::move(entry));
    }

    stack.cursor = (cursor <= stack.entries.size()) ? cursor : stack.entries.size();
    if (stack.nextEntryId == 0) {
        stack.nextEntryId = 1;
    }
    return true;
}

}  // namespace StorageManagerInternal
