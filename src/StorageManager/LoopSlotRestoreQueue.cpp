//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Deferred loop-slot restore queue, SD payload RAM cache, and scheduler hook.

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "DeferredJobScheduler.h"
#include "Globals.h"
#include "LoadLoopBudget.h"
#include "SlotLoadSession.h"
#include "TrackManager.h"
#include "Utils/BootLoopSlotRestore.h"
#include <Arduino.h>
#include <SD.h>
#include <array>
#include <cstdint>

namespace StorageManagerInternal {

struct PendingLoopSlotRestoreQueue {
    static constexpr uint16_t kCapacity =
        static_cast<uint16_t>(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK);
    DeferredLoopSlotRestore entries[kCapacity];
    uint16_t count = 0;
};

PendingLoopSlotRestoreQueue pendingLoopSlotRestores_{};

/// After a completed restore attempt (success or empty/fail), do not re-enqueue.
bool loopSlotRestoreAttempted_[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK]{};

std::array<std::array<bool, Config::MAX_LOOPS_PER_TRACK>, Config::NUM_TRACKS>
    loopSlotPayloadOnSdInRam_{};

namespace {

bool probeLoopSlotPayloadOnSdFromSd(uint8_t trackIndex, uint8_t slotIndex) {
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        return false;
    }
    return SD.exists(loopPath) && CurrentSetStorage::verifySaveFileTokenAtPath(loopPath);
}

void sortPendingLoopSlotRestoresByPriority() {
    for (uint16_t i = 1; i < pendingLoopSlotRestores_.count; ++i) {
        const DeferredLoopSlotRestore item = pendingLoopSlotRestores_.entries[i];
        uint16_t j = i;
        while (j > 0 &&
               pendingLoopSlotRestores_.entries[j - 1].restorePriority > item.restorePriority) {
            pendingLoopSlotRestores_.entries[j] = pendingLoopSlotRestores_.entries[j - 1];
            --j;
        }
        pendingLoopSlotRestores_.entries[j] = item;
    }
}

uint16_t currentDeferredRestorePriority(uint8_t trackIndex, uint8_t slotIndex) {
    const uint8_t selectedTrackIdx = trackManager.getSelectedTrackIndex();
    uint8_t selectedSlotIndex[Config::NUM_TRACKS]{};
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
        selectedSlotIndex[t] = trackManager.getSelectedSlotIndex(t);
    }
    return computeDeferredRestorePriority(trackIndex, slotIndex, selectedTrackIdx, selectedSlotIndex,
                                          Config::NUM_TRACKS, Config::NUM_TRACKS,
                                          Config::MAX_LOOPS_PER_TRACK);
}

}  // namespace

STORAGE_PERSIST_MEM void queueDeferredLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex) {
    StorageManager::refreshLoopSlotPayloadOnSdInRam(trackIndex, slotIndex);
    if (!StorageManager::hasLoopSlotPayloadOnSdInRam(trackIndex, slotIndex)) {
        return;
    }
    const uint16_t restorePriority = currentDeferredRestorePriority(trackIndex, slotIndex);
    for (uint16_t i = 0; i < pendingLoopSlotRestores_.count; ++i) {
        DeferredLoopSlotRestore& pending = pendingLoopSlotRestores_.entries[i];
        if (pending.track == trackIndex && pending.slot == slotIndex) {
            pending.restorePriority = restorePriority;
            sortPendingLoopSlotRestoresByPriority();
            return;
        }
    }
    if (!StorageManager::needsSlotLoad(trackIndex, slotIndex)) {
        return;
    }
    if (pendingLoopSlotRestores_.count >= PendingLoopSlotRestoreQueue::kCapacity) {
        return;
    }
    pendingLoopSlotRestores_.entries[pendingLoopSlotRestores_.count++] = {trackIndex, slotIndex,
                                                                          restorePriority};
    sortPendingLoopSlotRestoresByPriority();
}

/// Enqueue every HEADER_READY SD payload for background fill (adjacent/track/forward priority).
STORAGE_PERSIST_MEM void enqueueRemainingLoopSlotRestores() {
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            queueDeferredLoopSlotRestore(t, s);
        }
    }
    reprioritizeDeferredLoopSlotRestoreEntries();
    // Give transport/UI a quiet window after title clears before SD fill resumes.
    armBackgroundRestoreHoldoff(2000);
}

bool readLoopSlotPayloadOnSdInRamEntry(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return false;
    }
    return loopSlotPayloadOnSdInRam_[trackIndex][slotIndex];
}

void writeLoopSlotPayloadOnSdInRamEntry(uint8_t trackIndex, uint8_t slotIndex, bool hasPayload) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    loopSlotPayloadOnSdInRam_[trackIndex][slotIndex] = hasPayload;
}

void refreshLoopSlotPayloadOnSdInRamEntry(uint8_t trackIndex, uint8_t slotIndex) {
    writeLoopSlotPayloadOnSdInRamEntry(trackIndex, slotIndex,
                                       probeLoopSlotPayloadOnSdFromSd(trackIndex, slotIndex));
}

STORAGE_PERSIST_MEM void markLoopSlotRestoreAttempted(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex < Config::NUM_TRACKS && slotIndex < Config::MAX_LOOPS_PER_TRACK) {
        loopSlotRestoreAttempted_[trackIndex][slotIndex] = true;
    }
}

bool isLoopSlotRestoreAttempted(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return false;
    }
    return loopSlotRestoreAttempted_[trackIndex][slotIndex];
}

uint16_t pendingLoopSlotRestoreCount() {
    return pendingLoopSlotRestores_.count;
}

bool isDeferredLoopSlotRestoreQueued(uint8_t trackIndex, uint8_t slotIndex) {
    for (uint16_t i = 0; i < pendingLoopSlotRestores_.count; ++i) {
        const DeferredLoopSlotRestore& entry = pendingLoopSlotRestores_.entries[i];
        if (entry.track == trackIndex && entry.slot == slotIndex) {
            return true;
        }
    }
    return false;
}

bool isFocusDeferredLoopSlotRestorePending(uint8_t focusTrack, uint8_t focusSlot) {
    for (uint16_t i = 0; i < pendingLoopSlotRestores_.count; ++i) {
        const DeferredLoopSlotRestore& entry = pendingLoopSlotRestores_.entries[i];
        if (entry.track != focusTrack || entry.slot != focusSlot) {
            continue;
        }
        if (loopSlotRestoreAttempted_[entry.track][entry.slot]) {
            return false;
        }
        if (entry.track < trackManager.getTrackCount() &&
            trackManager.getTrack(entry.track).getLoop(entry.slot).hasCommittedPasses()) {
            return false;
        }
        return true;
    }
    return false;
}

STORAGE_PERSIST_MEM bool popNextDeferredLoopSlotRestore(DeferredLoopSlotRestore& out) {
    while (pendingLoopSlotRestores_.count > 0) {
        const DeferredLoopSlotRestore& head = pendingLoopSlotRestores_.entries[0];
        const bool done =
            loopSlotRestoreAttempted_[head.track][head.slot] ||
            trackManager.getTrack(head.track).getLoop(head.slot).hasCommittedPasses();
        if (!done) {
            out = head;
            for (uint16_t i = 1; i < pendingLoopSlotRestores_.count; ++i) {
                pendingLoopSlotRestores_.entries[i - 1] = pendingLoopSlotRestores_.entries[i];
            }
            --pendingLoopSlotRestores_.count;
            return true;
        }
        for (uint16_t i = 1; i < pendingLoopSlotRestores_.count; ++i) {
            pendingLoopSlotRestores_.entries[i - 1] = pendingLoopSlotRestores_.entries[i];
        }
        --pendingLoopSlotRestores_.count;
    }
    return false;
}

STORAGE_PERSIST_MEM bool popFocusDeferredLoopSlotRestore(uint8_t focusTrack, uint8_t focusSlot,
                                                         DeferredLoopSlotRestore& out) {
    for (uint16_t i = 0; i < pendingLoopSlotRestores_.count; ++i) {
        const DeferredLoopSlotRestore& entry = pendingLoopSlotRestores_.entries[i];
        if (entry.track != focusTrack || entry.slot != focusSlot) {
            continue;
        }
        const bool done =
            loopSlotRestoreAttempted_[entry.track][entry.slot] ||
            trackManager.getTrack(entry.track).getLoop(entry.slot).hasCommittedPasses();
        if (done) {
            for (uint16_t j = i + 1; j < pendingLoopSlotRestores_.count; ++j) {
                pendingLoopSlotRestores_.entries[j - 1] = pendingLoopSlotRestores_.entries[j];
            }
            --pendingLoopSlotRestores_.count;
            return false;
        }
        out = entry;
        for (uint16_t j = i + 1; j < pendingLoopSlotRestores_.count; ++j) {
            pendingLoopSlotRestores_.entries[j - 1] = pendingLoopSlotRestores_.entries[j];
        }
        --pendingLoopSlotRestores_.count;
        return true;
    }
    return false;
}

void reprioritizeDeferredLoopSlotRestoreEntries() {
    if (pendingLoopSlotRestores_.count == 0) {
        return;
    }
    for (uint16_t i = 0; i < pendingLoopSlotRestores_.count; ++i) {
        DeferredLoopSlotRestore& pending = pendingLoopSlotRestores_.entries[i];
        pending.restorePriority = currentDeferredRestorePriority(pending.track, pending.slot);
    }
    sortPendingLoopSlotRestoresByPriority();
}

void clearPendingLoopSlotRestoresAtBoot() {
    pendingLoopSlotRestores_.count = 0;
}

void resetAllLoopSlotRestoreAttempted() {
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            loopSlotRestoreAttempted_[t][s] = false;
        }
    }
}

bool appendBootLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex, uint16_t restorePriority) {
    if (pendingLoopSlotRestores_.count >= PendingLoopSlotRestoreQueue::kCapacity) {
        return false;
    }
    pendingLoopSlotRestores_.entries[pendingLoopSlotRestores_.count++] = {trackIndex, slotIndex,
                                                                          restorePriority};
    return true;
}

void sortPendingLoopSlotRestoreQueue() {
    sortPendingLoopSlotRestoresByPriority();
}

bool peekFirstPendingLoopSlotRestore(DeferredLoopSlotRestore& out) {
    if (pendingLoopSlotRestores_.count == 0) {
        return false;
    }
    out = pendingLoopSlotRestores_.entries[0];
    return true;
}

}  // namespace StorageManagerInternal

bool STORAGE_PERSIST_MEM StorageManager::loopSlotHasPayloadOnSd(uint8_t trackIndex,
                                                                 uint8_t slotIndex) {
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        return false;
    }
    return SD.exists(loopPath) && CurrentSetStorage::verifySaveFileTokenAtPath(loopPath);
}

bool STORAGE_PERSIST_MEM StorageManager::hasLoopSlotPayloadOnSdInRam(uint8_t trackIndex,
                                                                     uint8_t slotIndex) {
    return StorageManagerInternal::readLoopSlotPayloadOnSdInRamEntry(trackIndex, slotIndex);
}

void STORAGE_PERSIST_MEM StorageManager::setLoopSlotPayloadOnSdInRam(uint8_t trackIndex,
                                                                     uint8_t slotIndex,
                                                                     bool hasPayload) {
    StorageManagerInternal::writeLoopSlotPayloadOnSdInRamEntry(trackIndex, slotIndex, hasPayload);
}

void STORAGE_PERSIST_MEM StorageManager::refreshLoopSlotPayloadOnSdInRam(uint8_t trackIndex,
                                                                         uint8_t slotIndex) {
    StorageManagerInternal::refreshLoopSlotPayloadOnSdInRamEntry(trackIndex, slotIndex);
}

void STORAGE_PERSIST_MEM StorageManager::processDeferredLoopSlotRestore() {
    const uint32_t budgetUs = LoadLoopBudget::resolveLoadLoopSliceBudgetUs(
        false, StorageManager::isFocusedLoopSlotRestoreWork(), false);
    DeferredJobScheduler::runFrame(budgetUs);
}
