//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet B / A2.1 of `deferred-slot-restore-liveness`: the restore **request** record.
//
// `design.md` D1 makes the request the unit of correctness. A request is identified by
// `(track, slot, requestGeneration, setEpoch)` so that a later session reusing the same coordinates is a
// **different** request, and it is in exactly one lifecycle state at a time: queued, running, waiting, or
// terminal. Waits carry a named reason and never become terminals from elapsed turns; terminals are
// recorded once per identity.
//
// Placement: `STORAGE_PERSIST_DATA` for the records (external allocation, beside the existing restore
// arrays) and `STORAGE_PERSIST_MEM`/`FLASHMEM` for every function, because this tree sits on the RAM1
// FlexRAM bank edge. The post-link budget check caught the first version costing 64 B of ITCM
// (`padding 888 -> 824`, i.e. **below** the approved floor of 840) until the helpers were made
// FLASH-resident; the placement rule in `tasks.md` is enforced, not advisory.

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "Globals.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>

namespace StorageManagerInternal {

namespace {

struct RestoreRequestRecord {
    RestoreRequestState state = RestoreRequestState::Queued;
    RestoreRequestTerminal terminal = RestoreRequestTerminal::None;
    RestoreRequestWait wait = RestoreRequestWait::None;
    uint32_t requestGeneration = 0;
    uint32_t setEpoch = 0;
    /// Set once a terminal has been recorded for this identity, so a second terminal for the same
    /// identity is refused rather than overwriting the first.
    bool terminalRecorded = false;
    /// True only while this record describes a request that was actually begun at this generation. An
    /// external generation advance leaves the record **describing no request**, which is what makes
    /// "the earlier terminal does not describe the new request" checkable: comparing the stored
    /// generation against the counter would be self-satisfying, because the advance writes the new
    /// counter into the record.
    bool identityValid = false;
};

/// One record per `(track, slot)`. The identity inside it decides whether the record describes the
/// *current* request or an older one.
STORAGE_PERSIST_DATA RestoreRequestRecord restoreRequestRecords_[Config::NUM_TRACKS]
                                                            [Config::MAX_LOOPS_PER_TRACK]{};

/// The requestGeneration counter per `(track, slot)`. It advances **only** on an external change of the
/// coordinates (user clear, set change, a different owner committing that slot); the restore path's own
/// failure cleanup keeps the identity.
STORAGE_PERSIST_DATA uint32_t restoreRequestGeneration_[Config::NUM_TRACKS]
                                                     [Config::MAX_LOOPS_PER_TRACK]{};

/// The loaded current set's epoch. A set change invalidates every outstanding request as `superseded`.
STORAGE_PERSIST_DATA uint32_t restoreSetEpoch_ = 0;

bool coordinatesValid(uint8_t trackIndex, uint8_t slotIndex) {
    return trackIndex < Config::NUM_TRACKS && slotIndex < Config::MAX_LOOPS_PER_TRACK;
}

/// Leave the record describing **no** request. It keeps the current generation so a later
/// `beginRestoreRequest` reads the right counter, but `identityValid` stays false until a request is
/// actually begun.
void clearRecord(RestoreRequestRecord& record, uint32_t generation) {
    record = RestoreRequestRecord{};
    record.state = RestoreRequestState::Queued;
    record.requestGeneration = generation;
    record.setEpoch = restoreSetEpoch_;
    record.identityValid = false;
}

/// A1.8 (design D6): one event-shaped capture line per request transition. Reads the record so the line
/// always agrees with the state a probe would report.
STORAGE_PERSIST_MEM void emitRestoreRequestTransition(uint8_t trackIndex, uint8_t slotIndex,
                                                       const RestoreRequestRecord& record) {
    SC_RESTORE_REQUEST(trackIndex, slotIndex, record.requestGeneration, record.setEpoch,
                       restoreRequestStateName(record.state), restoreRequestWaitName(record.wait),
                       restoreRequestTerminalName(record.terminal));
}

}  // namespace

STORAGE_PERSIST_MEM void beginRestoreRequest(uint8_t trackIndex, uint8_t slotIndex) {
    if (!coordinatesValid(trackIndex, slotIndex)) {
        return;
    }
    // Idempotent for coordinates that already describe a live request: the queue's boot admission and
    // its explicit-request path can both reach the same slot, and re-beginning would erase progress the
    // live request has already made.
    if (restoreRequestRecordIsCurrent(trackIndex, slotIndex)) {
        return;
    }
    clearRecord(restoreRequestRecords_[trackIndex][slotIndex],
                restoreRequestGeneration_[trackIndex][slotIndex]);
    restoreRequestRecords_[trackIndex][slotIndex].identityValid = true;
    emitRestoreRequestTransition(trackIndex, slotIndex, restoreRequestRecords_[trackIndex][slotIndex]);
}

STORAGE_PERSIST_MEM RestoreRequestState restoreRequestState(uint8_t trackIndex, uint8_t slotIndex) {
    if (!coordinatesValid(trackIndex, slotIndex)) {
        return RestoreRequestState::Queued;
    }
    return restoreRequestRecords_[trackIndex][slotIndex].state;
}

/// True when the record still describes the live request for these coordinates: it belongs to the
/// current generation and set epoch. A stale record must never be reported as the current request's
/// outcome.
STORAGE_PERSIST_MEM bool restoreRequestRecordIsCurrent(uint8_t trackIndex, uint8_t slotIndex) {
    if (!coordinatesValid(trackIndex, slotIndex)) {
        return false;
    }
    const RestoreRequestRecord& record = restoreRequestRecords_[trackIndex][slotIndex];
    return record.identityValid &&
           record.requestGeneration == restoreRequestGeneration_[trackIndex][slotIndex] &&
           record.setEpoch == restoreSetEpoch_;
}

STORAGE_PERSIST_MEM void markRestoreRequestRunning(uint8_t trackIndex, uint8_t slotIndex) {
    if (!coordinatesValid(trackIndex, slotIndex)) {
        return;
    }
    RestoreRequestRecord& record = restoreRequestRecords_[trackIndex][slotIndex];
    if (!restoreRequestRecordIsCurrent(trackIndex, slotIndex) || record.terminalRecorded) {
        return;
    }
    record.state = RestoreRequestState::Running;
    record.wait = RestoreRequestWait::None;
    emitRestoreRequestTransition(trackIndex, slotIndex, record);
}

STORAGE_PERSIST_MEM void markRestoreRequestWaiting(uint8_t trackIndex, uint8_t slotIndex,
                                                   RestoreRequestWait wait) {
    if (!coordinatesValid(trackIndex, slotIndex) || wait == RestoreRequestWait::None) {
        return;
    }
    RestoreRequestRecord& record = restoreRequestRecords_[trackIndex][slotIndex];
    if (!restoreRequestRecordIsCurrent(trackIndex, slotIndex) || record.terminalRecorded) {
        return;
    }
    record.state = RestoreRequestState::Waiting;
    record.wait = wait;
    emitRestoreRequestTransition(trackIndex, slotIndex, record);
}

STORAGE_PERSIST_MEM bool markRestoreRequestTerminal(uint8_t trackIndex, uint8_t slotIndex,
                                                    RestoreRequestTerminal terminal) {
    if (!coordinatesValid(trackIndex, slotIndex) || terminal == RestoreRequestTerminal::None) {
        return false;
    }
    RestoreRequestRecord& record = restoreRequestRecords_[trackIndex][slotIndex];
    if (!restoreRequestRecordIsCurrent(trackIndex, slotIndex)) {
        return false;
    }
    // Exactly one terminal per identity: a second attempt is refused rather than overwriting the first,
    // so "exactly one terminal per request" is provable rather than incidental.
    if (record.terminalRecorded) {
        return false;
    }
    record.state = RestoreRequestState::Terminal;
    record.terminal = terminal;
    record.wait = RestoreRequestWait::None;
    record.terminalRecorded = true;
    emitRestoreRequestTransition(trackIndex, slotIndex, record);
    return true;
}

STORAGE_PERSIST_MEM void advanceRestoreRequestGeneration(uint8_t trackIndex, uint8_t slotIndex) {
    if (!coordinatesValid(trackIndex, slotIndex)) {
        return;
    }
    ++restoreRequestGeneration_[trackIndex][slotIndex];
    clearRecord(restoreRequestRecords_[trackIndex][slotIndex],
                restoreRequestGeneration_[trackIndex][slotIndex]);
}

STORAGE_PERSIST_MEM void invalidateAllRestoreRequestsAsSuperseded() {
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            RestoreRequestRecord& record = restoreRequestRecords_[trackIndex][slotIndex];
            if (record.terminalRecorded || !restoreRequestRecordIsCurrent(trackIndex, slotIndex)) {
                continue;
            }
            // `superseded` is distinct from `refused`: a set change is not a failure.
            record.state = RestoreRequestState::Terminal;
            record.terminal = RestoreRequestTerminal::Superseded;
            record.wait = RestoreRequestWait::None;
            record.terminalRecorded = true;
        }
    }
}

STORAGE_PERSIST_MEM void noteRestoreSetEpoch(uint32_t setEpoch) {
    if (setEpoch == restoreSetEpoch_) {
        return;
    }
    // A set change invalidates every outstanding request, each reaching `superseded` under the identity
    // it was created with. The epoch moves first, then the records are rebuilt as superseded terminals:
    // clearing them afterwards (as an earlier version did) wiped the terminal it had just recorded.
    restoreSetEpoch_ = setEpoch;
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            RestoreRequestRecord& record = restoreRequestRecords_[trackIndex][slotIndex];
            const bool wasCurrent = record.identityValid && !record.terminalRecorded;
            record = RestoreRequestRecord{};
            record.requestGeneration = restoreRequestGeneration_[trackIndex][slotIndex];
            record.setEpoch = restoreSetEpoch_;
            if (!wasCurrent) {
                continue;
            }
            record.state = RestoreRequestState::Terminal;
            record.terminal = RestoreRequestTerminal::Superseded;
            record.terminalRecorded = true;
            record.identityValid = true;
        }
    }
}

STORAGE_PERSIST_MEM uint32_t restoreSetEpoch() { return restoreSetEpoch_; }

STORAGE_PERSIST_DATA uint32_t g_restoreProgressCounter = 0;

STORAGE_PERSIST_MEM void bumpRestoreProgressCounter() { ++g_restoreProgressCounter; }


STORAGE_PERSIST_MEM uint32_t restoreProgressCounter() { return g_restoreProgressCounter; }


STORAGE_PERSIST_MEM uint32_t restoreRequestGenerationFor(uint8_t trackIndex, uint8_t slotIndex) {
    if (!coordinatesValid(trackIndex, slotIndex)) {
        return 0;
    }
    return restoreRequestGeneration_[trackIndex][slotIndex];
}

STORAGE_PERSIST_MEM void resetAllRestoreRequestState() {
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            restoreRequestGeneration_[trackIndex][slotIndex] = 0;
            clearRecord(restoreRequestRecords_[trackIndex][slotIndex], 0);
        }
    }
    restoreSetEpoch_ = 0;
}

STORAGE_PERSIST_MEM const char* restoreRequestStateName(RestoreRequestState state) {
    switch (state) {
        case RestoreRequestState::Queued:
            return "queued";
        case RestoreRequestState::Running:
            return "running";
        case RestoreRequestState::Waiting:
            return "waiting";
        case RestoreRequestState::Terminal:
            return "terminal";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM const char* restoreRequestWaitName(RestoreRequestWait wait) {
    switch (wait) {
        case RestoreRequestWait::None:
            return "none";
        case RestoreRequestWait::SlotOwned:
            return "waiting_slot_owned";
        case RestoreRequestWait::Capture:
            return "waiting_capture";
        case RestoreRequestWait::CommitGate:
            return "waiting_commit_gate";
        case RestoreRequestWait::JobCapacity:
            return "waiting_job_capacity";
        case RestoreRequestWait::Retry:
            return "waiting_retry";
    }
    return "unknown";
}

STORAGE_PERSIST_MEM const char* restoreRequestTerminalName(RestoreRequestTerminal terminal) {
    switch (terminal) {
        case RestoreRequestTerminal::None:
            return "none";
        case RestoreRequestTerminal::Committed:
            return "committed";
        case RestoreRequestTerminal::Unnecessary:
            return "unnecessary";
        case RestoreRequestTerminal::Refused:
            return "refused";
        case RestoreRequestTerminal::Superseded:
            return "superseded";
    }
    return "unknown";
}

}  // namespace StorageManagerInternal
