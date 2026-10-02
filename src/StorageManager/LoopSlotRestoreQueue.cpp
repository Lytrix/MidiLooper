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

STORAGE_PERSIST_DATA PendingLoopSlotRestoreQueue pendingLoopSlotRestores_{};

// A2.1 retirement: `loopSlotRestoreAttempted_` is **subsumed** by the restore request record. Its
// meaning was "this slot's restore was attempted and must not be re-enqueued" -- which is exactly "the
// request reached a terminal outcome". Backing the accessors with the record removes a second 64-byte
// array (this tree is on the RAM1 FlexRAM boundary: the A2.4 attempt counter pushed RAM1 variables
// 96,128 -> 96,192 and tripped the post-link budget) and leaves one source of truth for exactly-once.

STORAGE_PERSIST_DATA std::array<std::array<bool, Config::MAX_LOOPS_PER_TRACK>, Config::NUM_TRACKS>
    loopSlotPayloadOnSdInRam_{};

// Incremental boot background-fill admission state.
bool bootBackgroundFillAdmissionStarted_ = false;
bool bootBackgroundFillAdmissionComplete_ = false;
uint8_t bootBackgroundFillTrackCursor_ = 0;
uint8_t bootBackgroundFillSlotCursor_ = 0;

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

void queueDeferredLoopSlotRestoreWithPayloadSource(uint8_t trackIndex, uint8_t slotIndex,
                                                   bool refreshPayloadFromSd,
                                                   bool deferPriorityAndSort) {
    if (refreshPayloadFromSd) {
        StorageManager::refreshLoopSlotPayloadOnSdInRam(trackIndex, slotIndex);
    }
    if (!StorageManager::hasLoopSlotPayloadOnSdInRam(trackIndex, slotIndex)) {
        return;
    }
    const uint16_t restorePriority =
        deferPriorityAndSort ? 0 : currentDeferredRestorePriority(trackIndex, slotIndex);
    for (uint16_t i = 0; i < pendingLoopSlotRestores_.count; ++i) {
        DeferredLoopSlotRestore& pending = pendingLoopSlotRestores_.entries[i];
        if (pending.track == trackIndex && pending.slot == slotIndex) {
            if (!deferPriorityAndSort) {
                pending.restorePriority = restorePriority;
                sortPendingLoopSlotRestoresByPriority();
            }
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
    // A2.1: the request is born here, with its identity and its initial `queued` state. A later reader
    // can therefore tell "queued and not yet served" from "finished".
    beginRestoreRequest(trackIndex, slotIndex);
    if (!deferPriorityAndSort) {
        sortPendingLoopSlotRestoresByPriority();
    }
}

}  // namespace

STORAGE_PERSIST_MEM void queueDeferredLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex) {
    queueDeferredLoopSlotRestoreWithPayloadSource(trackIndex, slotIndex, true, false);
}

/// Incrementally enqueue HEADER_READY SD payloads for background fill.
/// Admission is sliced across loop turns to keep STOPPED boot windows bounded.
STORAGE_PERSIST_MEM void enqueueRemainingLoopSlotRestores() {
    if (bootBackgroundFillAdmissionComplete_) {
        return;
    }
    if (!bootBackgroundFillAdmissionStarted_) {
        bootBackgroundFillAdmissionStarted_ = true;
        bootBackgroundFillTrackCursor_ = 0;
        bootBackgroundFillSlotCursor_ = 0;
    }

    constexpr uint16_t kBootAdmissionEntriesPerTurn = 4;
    uint16_t admittedThisTurn = 0;
    while (bootBackgroundFillTrackCursor_ < Config::NUM_TRACKS &&
           admittedThisTurn < kBootAdmissionEntriesPerTurn) {
        // Boot scan already populated loopSlotPayloadOnSdInRam_ for every slot.
        // Avoid repeating SD.exists/token probes for all slots in one turn.
        // Bulk enqueue: defer per-entry priority/sort and do one reprioritize at completion.
        queueDeferredLoopSlotRestoreWithPayloadSource(
            bootBackgroundFillTrackCursor_, bootBackgroundFillSlotCursor_, false, true);
        ++admittedThisTurn;
        ++bootBackgroundFillSlotCursor_;
        if (bootBackgroundFillSlotCursor_ >= Config::MAX_LOOPS_PER_TRACK) {
            bootBackgroundFillSlotCursor_ = 0;
            ++bootBackgroundFillTrackCursor_;
        }
    }

    if (bootBackgroundFillTrackCursor_ < Config::NUM_TRACKS) {
        return;
    }

    reprioritizeDeferredLoopSlotRestoreEntries();
    // Give transport/UI a quiet window after title clears before SD fill resumes.
    armBackgroundRestoreHoldoff(2000);
    bootBackgroundFillAdmissionComplete_ = true;
    bootBackgroundFillAdmissionStarted_ = false;
}

/// S1.3: the remaining-library background admission scan finished. `hasPendingLoopSlotRestore()` cannot
/// answer this — the scan is incremental, so the queue can be momentarily empty mid-scan and a census
/// taken then would under-count the intended slots.
bool bootBackgroundFillAdmissionComplete() {
    return bootBackgroundFillAdmissionComplete_;
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
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    // A caller marking "attempted" is recording that this restore is finished with. If the request
    // already reached a terminal, that outcome is kept (exactly-once); otherwise record `unnecessary`,
    // which is the terminal the absent/invalid-payload paths mean.
    (void)markRestoreRequestTerminal(trackIndex, slotIndex, RestoreRequestTerminal::Unnecessary);
}

STORAGE_PERSIST_MEM bool isLoopSlotRestoreAttempted(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return false;
    }
    // NOTE (A2.1): deliberately still `state == Terminal`, i.e. a stale-but-decided record counts as
    // "finished with". Reading a stale terminal as *not* attempted makes `needsSlotLoad` true and lets a
    // user CLEAR auto-reload the just-cleared slot; on device that reload reserves ~21.3 MB and trips
    // `#MEM_EXT_REFUSED` / `FATAL: both internal heap and external memory are exhausted!` (recorded in
    // REVIEW.md as a runtime-working-set-ownership defect, out of this change's scope). The generation
    // advance still invalidates the identity for publication validation and for a fresh explicit request.
    return restoreRequestState(trackIndex, slotIndex) == RestoreRequestState::Terminal;
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
        if (isLoopSlotRestoreAttempted(entry.track, entry.slot)) {
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

/// S2b.5 / A2.1: a queued restore may only begin when no live owner holds its slot. The pop is a
/// one-way door (`popFocusDeferredLoopSlotRestore` discards an entry whose slot it judges done, and
/// `queueDeferredLoopSlotRestore` refuses to take it back), so a caller must check liveness before
/// popping. Liveness is per slot: a finalizing predecessor on one slot must not block a load into
/// another.
STORAGE_PERSIST_MEM bool queuedRestoreMayBegin(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return false;
    }
    return trackManager.getTrack(trackIndex).noLiveOwnerForSlot(slotIndex);
}

/// A2.3 (D3): select the **first startable request in priority order**, leaving a blocked entry queued
/// with its reason intact.
///
/// `popNextDeferredLoopSlotRestore` reads only the priority head and has no track/slot filter, so a head
/// whose slot a live owner holds stalls every entry behind it (design C4). Queue order is a *selection*
/// order, not a gate on service: this walks the priority order and takes the first entry that may begin.
/// Blocked entries are left exactly where they are — the request record already names their wait.
STORAGE_PERSIST_MEM bool popNextStartableDeferredLoopSlotRestore(DeferredLoopSlotRestore& out) {
    uint16_t index = 0;
    while (index < pendingLoopSlotRestores_.count) {
        const DeferredLoopSlotRestore& entry = pendingLoopSlotRestores_.entries[index];
        const bool done =
            isLoopSlotRestoreAttempted(entry.track, entry.slot) ||
            trackManager.getTrack(entry.track).getLoop(entry.slot).hasCommittedPasses();
        if (done) {
            // Satisfied requests leave the queue, as before: this is queue hygiene, not service.
            for (uint16_t i = index + 1; i < pendingLoopSlotRestores_.count; ++i) {
                pendingLoopSlotRestores_.entries[i - 1] = pendingLoopSlotRestores_.entries[i];
            }
            --pendingLoopSlotRestores_.count;
            continue;
        }
        if (!queuedRestoreMayBegin(entry.track, entry.slot)) {
            // Blocked: keep it queued with its reason and look past it.
            ++index;
            continue;
        }
        out = entry;
        for (uint16_t i = index + 1; i < pendingLoopSlotRestores_.count; ++i) {
            pendingLoopSlotRestores_.entries[i - 1] = pendingLoopSlotRestores_.entries[i];
        }
        --pendingLoopSlotRestores_.count;
        return true;
    }
    return false;
}

STORAGE_PERSIST_MEM bool popNextDeferredLoopSlotRestore(DeferredLoopSlotRestore& out) {
    while (pendingLoopSlotRestores_.count > 0) {
        const DeferredLoopSlotRestore& head = pendingLoopSlotRestores_.entries[0];
        const bool done =
            isLoopSlotRestoreAttempted(head.track, head.slot) ||
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
            isLoopSlotRestoreAttempted(entry.track, entry.slot) ||
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
    bootBackgroundFillAdmissionStarted_ = false;
    bootBackgroundFillAdmissionComplete_ = false;
    bootBackgroundFillTrackCursor_ = 0;
    bootBackgroundFillSlotCursor_ = 0;
}

STORAGE_PERSIST_MEM void resetAllLoopSlotRestoreAttempted() { resetAllRestoreRequestState(); }

bool appendBootLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex, uint16_t restorePriority) {
    if (pendingLoopSlotRestores_.count >= PendingLoopSlotRestoreQueue::kCapacity) {
        return false;
    }
    pendingLoopSlotRestores_.entries[pendingLoopSlotRestores_.count++] = {trackIndex, slotIndex,
                                                                          restorePriority};
    beginRestoreRequest(trackIndex, slotIndex);
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

// A3.5: one slot's persisted payload is removed only by an explicit user CLEAR of that slot (DEC-110).
// The owner is the persistence layer because the card file is its representation: the gesture asks, it acts
// and mirrors the outcome, so the library observation reports the slot as genuinely empty afterwards.
bool STORAGE_PERSIST_MEM StorageManager::removeLoopSlotPayload(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return false;
    }
    char finalPath[64];
    char tempPath[64];
    const bool haveFinal = CurrentSetStorage::formatLoopSlotPath(finalPath, sizeof(finalPath),
                                                                trackIndex, slotIndex);
    const bool haveTemp = CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath),
                                                                   trackIndex, slotIndex);
    if (haveFinal) {
        (void)SD.remove(finalPath);
    }
    if (haveTemp) {
        (void)SD.remove(tempPath);
    }
    (void)CurrentSetStorage::removeLoopSlotSealJournal(trackIndex, slotIndex);
    const bool absent = !StorageManager::loopSlotHasPayloadOnSd(trackIndex, slotIndex);
    StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, !absent);
    return absent;
}

void STORAGE_PERSIST_MEM StorageManager::processDeferredLoopSlotRestore() {
    const uint32_t budgetUs = LoadLoopBudget::resolveLoadLoopSliceBudgetUs(
        false, StorageManager::isFocusedLoopSlotRestoreWork(), false);
    DeferredJobScheduler::runFrame(budgetUs);
}

void STORAGE_PERSIST_MEM StorageManager::serviceDeferredRestoreForDrain() {
    // S2b.7 (D4): while the drain blocks, the main loop cannot run its deferred-load frame, so a restore
    // holding the SD session would never release it and the drain could only report the wait. Service the
    // request the drain waits on with the **same** selection/admission + bounded step the frame uses, so
    // this reuses the existing state machine and phase gates and adds no second loader, queue or admission
    // policy. `stepSubmittedLoadJobs` still enforces the **global** idle gate for commit/cleanup and names
    // `waiting_commit_gate`, so this never admits an unbounded commit:
    // `resumable-midi-boundary-playback-maintenance` task 3.3 keeps that bound.
    // D4 point 1 (active **and** parked instances) needs no extra promotion here: the selection step
    // below already promotes a parked instance through
    // `ensureActiveLoadLoopJobSelected` -> `LoadLoopSelectionPolicy::PromoteParkedLow`, so reusing it is
    // what keeps the drain from growing a parallel admission policy. Verified by removing an explicit
    // promote: the parked-instance case stays green without it.
    const uint32_t budgetUs = LoadLoopBudget::resolveLoadLoopSliceBudgetUs(
        false, StorageManager::isFocusedLoopSlotRestoreWork(), false);
    DeferredJobScheduler::runFrame(budgetUs);
}
