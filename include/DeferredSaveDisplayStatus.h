//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

enum class DeferredSaveDisplayPhase : uint8_t {
    Idle = 0,
    Pending,
    InProgress,
    Completed,
    Failed,
};

struct DeferredSaveDisplayStatus {
    DeferredSaveDisplayPhase phase = DeferredSaveDisplayPhase::Idle;
    uint8_t rotateStep = 0;
};

struct DeferredSaveDisplayInputs {
    bool savePending = false;
    bool saveInProgress = false;
    bool revisionCommitPending = false;
    bool revisionCommitInProgress = false;
    uint32_t completedAtMs = 0;
    uint32_t failedAtMs = 0;
};

constexpr uint32_t kDeferredSaveDisplayFlashMs = 800;
constexpr uint32_t kDeferredSaveDisplayRotateMs = 200;

inline const char* deferredSaveDisplayPhaseName(DeferredSaveDisplayPhase phase) {
    switch (phase) {
        case DeferredSaveDisplayPhase::Idle:
            return "idle";
        case DeferredSaveDisplayPhase::Pending:
            return "pending";
        case DeferredSaveDisplayPhase::InProgress:
            return "in_progress";
        case DeferredSaveDisplayPhase::Completed:
            return "completed";
        case DeferredSaveDisplayPhase::Failed:
            return "failed";
        default:
            return "unknown";
    }
}

inline DeferredSaveDisplayStatus resolveDeferredSaveDisplayStatus(uint32_t nowMs,
                                                                  const DeferredSaveDisplayInputs& inputs) {
    DeferredSaveDisplayStatus status{};
    const bool persistenceInProgress =
        inputs.saveInProgress || inputs.revisionCommitInProgress;
    const bool persistencePending =
        inputs.savePending || inputs.revisionCommitPending;
    if (persistenceInProgress) {
        status.phase = DeferredSaveDisplayPhase::InProgress;
        status.rotateStep = static_cast<uint8_t>((nowMs / kDeferredSaveDisplayRotateMs) % 4);
        return status;
    }
    if (persistencePending) {
        status.phase = DeferredSaveDisplayPhase::Pending;
        return status;
    }
    if (inputs.completedAtMs != 0 &&
        nowMs >= inputs.completedAtMs &&
        (nowMs - inputs.completedAtMs) < kDeferredSaveDisplayFlashMs) {
        status.phase = DeferredSaveDisplayPhase::Completed;
        return status;
    }
    if (inputs.failedAtMs != 0 && nowMs >= inputs.failedAtMs &&
        (nowMs - inputs.failedAtMs) < kDeferredSaveDisplayFlashMs) {
        status.phase = DeferredSaveDisplayPhase::Failed;
        return status;
    }
    return status;
}

struct DeferredLoadDisplayInputs {
    bool loadPending = false;
    bool loadInProgress = false;
    bool saveThenLoadCommitInProgress = false;
    uint32_t completedAtMs = 0;
    uint32_t failedAtMs = 0;
};

inline DeferredSaveDisplayStatus resolveDeferredLoadDisplayStatus(
    uint32_t nowMs, const DeferredLoadDisplayInputs& inputs) {
    DeferredSaveDisplayStatus status{};
    const bool loadInProgress = inputs.loadInProgress || inputs.saveThenLoadCommitInProgress;
    if (loadInProgress) {
        status.phase = DeferredSaveDisplayPhase::InProgress;
        status.rotateStep = static_cast<uint8_t>((nowMs / kDeferredSaveDisplayRotateMs) % 4);
        return status;
    }
    if (inputs.loadPending) {
        status.phase = DeferredSaveDisplayPhase::Pending;
        return status;
    }
    if (inputs.completedAtMs != 0 && nowMs >= inputs.completedAtMs &&
        (nowMs - inputs.completedAtMs) < kDeferredSaveDisplayFlashMs) {
        status.phase = DeferredSaveDisplayPhase::Completed;
        return status;
    }
    if (inputs.failedAtMs != 0 && nowMs >= inputs.failedAtMs &&
        (nowMs - inputs.failedAtMs) < kDeferredSaveDisplayFlashMs) {
        status.phase = DeferredSaveDisplayPhase::Failed;
        return status;
    }
    return status;
}
