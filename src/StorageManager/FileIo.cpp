//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include <Arduino.h>

namespace StorageManagerInternal {

STORAGE_PERSIST_MEM bool writeRaw(File& file, const void* data, size_t size) {
    return file.write(static_cast<const uint8_t*>(data), size) == size;
}

STORAGE_PERSIST_MEM bool readRaw(File& file, void* data, size_t size) {
    if ((file.size() - file.position()) < static_cast<int>(size)) {
        Serial.print("[StorageManager] readRaw: Not enough bytes left in file. Needed: ");
        Serial.print(size);
        Serial.print(", available: ");
        Serial.println(file.size() - file.position());
        return false;
    }
    const int bytesRead = file.read(static_cast<uint8_t*>(data), size);
    if (bytesRead != static_cast<int>(size)) {
        Serial.print("[StorageManager] readRaw: expected ");
        Serial.print(size);
        Serial.print(" bytes, got ");
        Serial.println(bytesRead);
        return false;
    }
    return true;
}

STORAGE_PERSIST_MEM StorageIo storageIoFromFileWrite(File& file) {
    return StorageIo{
        [&file](const void* data, size_t size) { return writeRaw(file, data, size); },
        nullptr,
    };
}

STORAGE_PERSIST_MEM StorageIo storageIoFromFileRead(File& file) {
    return StorageIo{
        nullptr,
        [&file](void* data, size_t size) { return readRaw(file, data, size); },
        [&file](void* data, size_t size) {
            const uint32_t pos = file.position();
            if ((file.size() - pos) < static_cast<int>(size)) {
                return false;
            }
            const int bytesRead = file.read(static_cast<uint8_t*>(data), size);
            const bool ok = bytesRead == static_cast<int>(size);
            (void)file.seek(pos);
            return ok;
        },
    };
}

STORAGE_PERSIST_MEM const char* deferredSaveStageName(DeferredSaveStage stage) {
    switch (stage) {
        case DeferredSaveStage::Idle:
            return "idle";
        case DeferredSaveStage::CurrentSetMeta:
            return "current_set_meta";
        case DeferredSaveStage::TrackHeaderAndSlots:
            return "track_header_slots";
        case DeferredSaveStage::CurrentSetLoopSlot:
            return "current_set_loop_slot";
        case DeferredSaveStage::Footer:
            return "footer";
        case DeferredSaveStage::UndoStacks:
            return "undo_stacks";
        case DeferredSaveStage::CurrentSetCompletion:
            return "current_set_completion";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM const char* deferredGlobalHeaderStageName(DeferredGlobalHeaderStage stage) {
    switch (stage) {
        case DeferredGlobalHeaderStage::Version:
            return "version";
        case DeferredGlobalHeaderStage::Bpm:
            return "bpm";
        case DeferredGlobalHeaderStage::LooperState:
            return "looper_state";
        case DeferredGlobalHeaderStage::MasterLoopLength:
            return "master_loop_length";
        case DeferredGlobalHeaderStage::TrackCount:
            return "track_count";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM const char* deferredTrackWriteStageName(DeferredTrackWriteStage stage) {
    switch (stage) {
        case DeferredTrackWriteStage::TrackState:
            return "track_state";
        case DeferredTrackWriteStage::Muted:
            return "muted";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM const char* deferredSlotWriteStageName(DeferredSlotWriteStage stage) {
    switch (stage) {
        case DeferredSlotWriteStage::SlotEnabled:
            return "slot_enabled";
        case DeferredSlotWriteStage::SlotMuted:
            return "slot_muted";
        case DeferredSlotWriteStage::SlotLoopId:
            return "slot_loop_id";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM const char* deferredFooterWriteStageName(DeferredFooterWriteStage stage) {
    switch (stage) {
        case DeferredFooterWriteStage::SelectedTrack:
            return "selected_track";
        case DeferredFooterWriteStage::ActiveLoopIndex:
            return "active_loop_index";
        case DeferredFooterWriteStage::SelectedSlotExtensionToken:
            return "selected_slot_extension_token";
        case DeferredFooterWriteStage::SelectedSlotIndex:
            return "selected_slot_index";
        case DeferredFooterWriteStage::GlobalUndoStackToken:
            return "global_undo_stack_token";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM const char* deferredLoopWriteStageName(DeferredLoopWriteStage stage) {
    switch (stage) {
        case DeferredLoopWriteStage::Header:
            return "loop_header";
        case DeferredLoopWriteStage::CapturePassHeader:
            return "capture_pass_header";
        case DeferredLoopWriteStage::CapturePassChunk:
            return "capture_pass_chunk";
        case DeferredLoopWriteStage::EditTail:
            return "edit_tail";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM const char* deferredUndoWriteStageName(DeferredUndoWriteStage stage) {
    switch (stage) {
        case DeferredUndoWriteStage::Header:
            return "undo_header";
        case DeferredUndoWriteStage::EntryHeader:
            return "entry_header";
        case DeferredUndoWriteStage::BeforeSnapshotPresence:
            return "before_snapshot_presence";
        case DeferredUndoWriteStage::BeforeSnapshotLoop:
            return "before_snapshot_loop";
        case DeferredUndoWriteStage::AfterSnapshotPresence:
            return "after_snapshot_presence";
        case DeferredUndoWriteStage::AfterSnapshotLoop:
            return "after_snapshot_loop";
        case DeferredUndoWriteStage::EntryTail:
            return "entry_tail";
    }
    return "unknown";
}

}  // namespace StorageManagerInternal
