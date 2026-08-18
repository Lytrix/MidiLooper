//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "Globals.h"
#include "LoadLoopBudget.h"
#include "LoadLoopSelectionPolicy.h"
#include "Loop.h"
#include "SlotLoadSession.h"
#include "StorageLoopIo.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include <Arduino.h>
#include <SD.h>

namespace StorageManagerInternal {

/// LoadLoopJob: read SD payload in timed slices, parse under budget into a staging
/// snapshot (Phase A.6), then one-shot atomic Commit. At most one active + one parked.
enum class LoadLoopJobPhase : uint8_t {
    Reading = 0,
    Parsing = 1,
    Committing = 2,
};
struct LoadLoopJob {
    bool active = false;
    LoadLoopJobPhase phase = LoadLoopJobPhase::Reading;
    uint8_t track = 0;
    uint8_t slot = 0;
    File file;
    size_t payloadOffset = 0;
    size_t payloadSize = 0;
    size_t bytesRead = 0;
    bool saveTokenVerified = false;
    std::vector<uint8_t, ExternalMemoryFirstAllocator<uint8_t>> buffer;
    PersistedLoopSnapshot snapshot{};
    PersistedLoopParseState parseState{};
    SlotLoadSession* session = nullptr;
};
#if defined(__IMXRT1062__)
DMAMEM LoadLoopJob loadLoopJob_{};
DMAMEM LoadLoopJob parkedLoadLoopJob_{};
#else
LoadLoopJob loadLoopJob_{};
LoadLoopJob parkedLoadLoopJob_{};
#endif
/// After boot playback ready, delay background fill so first transport press is not starved.
uint32_t backgroundRestoreHoldoffUntilMs_ = 0;
/// After LoadLoopJob Commit, block deferred save briefly so SD work cannot race the
/// post-commit prewarm frame (session_20260719_000659: silence after commit_prewarm_q).
uint32_t postLoadLoopCommitSaveHoldoffUntilMs_ = 0;
/// While true (boot title playback drain), do not park/demote — parked jobs block bootInteractiveReady.
bool bootTitleLoadDrain_ = false;

STORAGE_PERSIST_MEM void clearLoadLoopJobInstance(LoadLoopJob& job) {
    if (job.session != nullptr) {
        job.session->fail();
        delete job.session;
        job.session = nullptr;
    }
    if (job.file) {
        job.file.close();
    }
    releasePersistedLoopSnapshotChunks(job.snapshot);
    job.parseState = PersistedLoopParseState{};
    job.phase = LoadLoopJobPhase::Reading;
    job.buffer.clear();
    job.buffer.shrink_to_fit();
    job.active = false;
    job.bytesRead = 0;
    job.payloadSize = 0;
    job.payloadOffset = 0;
    job.saveTokenVerified = false;
}

STORAGE_PERSIST_MEM void clearLoadLoopJob() {
    clearLoadLoopJobInstance(loadLoopJob_);
    clearLoadLoopJobInstance(parkedLoadLoopJob_);
}

STORAGE_PERSIST_MEM void swapLoadLoopJobs(LoadLoopJob& a, LoadLoopJob& b) {
    using std::swap;
    swap(a.active, b.active);
    swap(a.phase, b.phase);
    swap(a.track, b.track);
    swap(a.slot, b.slot);
    swap(a.file, b.file);
    swap(a.payloadOffset, b.payloadOffset);
    swap(a.payloadSize, b.payloadSize);
    swap(a.bytesRead, b.bytesRead);
    swap(a.saveTokenVerified, b.saveTokenVerified);
    swap(a.buffer, b.buffer);
    swap(a.snapshot, b.snapshot);
    swap(a.parseState, b.parseState);
    swap(a.session, b.session);
}

STORAGE_PERSIST_MEM bool shouldFinishLoadLoopJobBeforePreempt() {
    if (!loadLoopJob_.active) {
        return false;
    }
    // Never abandon mid-publish.
    if (loadLoopJob_.phase == LoadLoopJobPhase::Committing) {
        return true;
    }
    // Mid-parse may demote/park — snapshot + cursors travel with the job.
    if (loadLoopJob_.phase == LoadLoopJobPhase::Parsing) {
        return false;
    }
    return LoadLoopBudget::shouldFinishActiveBeforePreempt(loadLoopJob_.bytesRead,
                                                          loadLoopJob_.payloadSize);
}

STORAGE_PERSIST_MEM void parkActiveLoadLoopJob() {
    if (!loadLoopJob_.active) {
        return;
    }
    if (parkedLoadLoopJob_.active) {
        return;
    }
    swapLoadLoopJobs(loadLoopJob_, parkedLoadLoopJob_);
}

STORAGE_PERSIST_MEM void resumeParkedLoadLoopJobIfFocus(uint8_t focusTrack, uint8_t focusSlot) {
    if (!parkedLoadLoopJob_.active) {
        return;
    }
    if (parkedLoadLoopJob_.track != focusTrack || parkedLoadLoopJob_.slot != focusSlot) {
        return;
    }
    if (loadLoopJob_.active) {
        if (shouldFinishLoadLoopJobBeforePreempt()) {
            return;
        }
        if (parkedLoadLoopJob_.active) {
            // Active is not focus; park occupied by focus — swap so focus becomes active.
            swapLoadLoopJobs(loadLoopJob_, parkedLoadLoopJob_);
            return;
        }
    }
    swapLoadLoopJobs(loadLoopJob_, parkedLoadLoopJob_);
}

STORAGE_PERSIST_MEM void demoteActiveLoadLoopJobForFocus(uint8_t focusTrack, uint8_t focusSlot) {
    if (bootTitleLoadDrain_) {
        // Boot playback drain must empty the queue without parking; parked jobs block title clear.
        return;
    }
    if (!loadLoopJob_.active) {
        return;
    }
    if (loadLoopJob_.track == focusTrack && loadLoopJob_.slot == focusSlot) {
        return;
    }
    // Only demote when focus actually needs hydration. If focus is already COMMITTED,
    // background fill must not park every non-focus job (starves UI / play button).
    if (!StorageManager::needsSlotLoad(focusTrack, focusSlot)) {
        bool focusQueued = false;
        focusQueued = isDeferredLoopSlotRestoreQueued(focusTrack, focusSlot);
        if (!focusQueued) {
            return;
        }
    }
    if (shouldFinishLoadLoopJobBeforePreempt()) {
        return;
    }
    if (parkedLoadLoopJob_.active) {
        if (parkedLoadLoopJob_.track == focusTrack &&
            parkedLoadLoopJob_.slot == focusSlot) {
            swapLoadLoopJobs(loadLoopJob_, parkedLoadLoopJob_);
#if defined(SESSION_CAPTURE)
            Serial.print("[StorageManager] LoadLoopJob resume ");
            Serial.print(loadLoopJob_.track);
            Serial.print('/');
            Serial.println(loadLoopJob_.slot);
#endif
        }
        // Park occupied by another slot: finish active under budget before starting focus.
        return;
    }
    parkActiveLoadLoopJob();
}

STORAGE_PERSIST_MEM bool loadLoopJobIsFocusSlot() {
    if (!loadLoopJob_.active || trackManager.getTrackCount() == 0) {
        return false;
    }
    return loadLoopJob_.track == trackManager.getSelectedTrackIndex() &&
           loadLoopJob_.slot == trackManager.getSelectedSlotIndex(loadLoopJob_.track);
}

/// Frame admission for Low (non-focus) LoadLoopJob work (not a job lifecycle suspend).
/// Pending-tap / OLED ordering are enforced in main before DeferredJobScheduler::runFrame.
/// While PLAYING/REC/OD, skip Low work — do not treat "focus still queued" as a license
/// to begin or step a neighbor slot (session_20260718_233730).
/// False ⇒ SKIP_THIS_FRAME: leave active/parked/queue progress; do not park.
STORAGE_PERSIST_MEM bool canRunBackgroundLoadLoopNow() {
    if (bootTitleLoadDrain_) {
        return true;
    }
    for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
        const Track& t = trackManager.getTrack(i);
        if (t.isRecording() || t.isOverdubbing() || t.isPlaying()) {
            return false;
        }
    }
    return true;
}

STORAGE_PERSIST_MEM bool beginLoadLoopJob(uint8_t trackIndex, uint8_t slotIndex) {
    clearLoadLoopJobInstance(loadLoopJob_);
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return false;
    }
    Loop& loop = trackManager.getTrack(trackIndex).getLoop(slotIndex);
    if (!SD.exists(loopPath)) {
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(loopPath)) {
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return false;
    }
    File loopFile = SD.open(loopPath, FILE_READ);
    if (!loopFile) {
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return false;
    }

    size_t payloadOffset = 0;
    if (CurrentWorkspaceStorage::fileStartsWithEpochHeader(loopFile)) {
        CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
        const StorageIo epochIo = storageIoFromFileRead(loopFile);
        if (!CurrentWorkspaceStorage::readEpochFileHeader(epochIo, epochHeader)) {
            loopFile.close();
            resetLoopSlotToEmpty(loop, slotIndex);
            markLoopSlotRestoreAttempted(trackIndex, slotIndex);
            return false;
        }
        payloadOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
    }
    const size_t fileSize = loopFile.size();
    if (fileSize < payloadOffset + sizeof(CurrentSetStorage::kSaveFileToken)) {
        loopFile.close();
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return false;
    }
    const size_t payloadSize = fileSize - payloadOffset - sizeof(CurrentSetStorage::kSaveFileToken);
    if (!loopFile.seek(payloadOffset)) {
        loopFile.close();
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return false;
    }

    loadLoopJob_.active = true;
    loadLoopJob_.phase = LoadLoopJobPhase::Reading;
    loadLoopJob_.track = trackIndex;
    loadLoopJob_.slot = slotIndex;
    loadLoopJob_.payloadOffset = payloadOffset;
    loadLoopJob_.payloadSize = payloadSize;
    loadLoopJob_.bytesRead = 0;
    loadLoopJob_.saveTokenVerified = false;
    loadLoopJob_.buffer.clear();
    loadLoopJob_.buffer.reserve(payloadSize);
    releasePersistedLoopSnapshotChunks(loadLoopJob_.snapshot);
    loadLoopJob_.parseState = PersistedLoopParseState{};
    loadLoopJob_.session = new SlotLoadSession(trackIndex, slotIndex);
    (void)loadLoopJob_.session->advanceAfterPhaseWork();
    // Do not prewarm LoopPlaybackRuntime here while the 57KB SD buffer is reserved — that
    // pair plus parse staging exhausts EXTMEM under PLAYING (000205). Post-Commit prewarm
    // in main owns runtime alloc after buffer is freed.
    // Do not keep File open across frames — reopen in stepLoadLoopJob Reading.
    loopFile.close();
    loadLoopJob_.file = File();

    return true;
}

STORAGE_PERSIST_MEM void ensureActiveLoadLoopJobSelected(uint8_t focusTrack, uint8_t focusSlot) {
    resumeParkedLoadLoopJobIfFocus(focusTrack, focusSlot);
    demoteActiveLoadLoopJobForFocus(focusTrack, focusSlot);
    resumeParkedLoadLoopJobIfFocus(focusTrack, focusSlot);

    if (loadLoopJob_.active) {
        return;
    }

    // Prefer finishing a parked job before opening another SD file (begin is expensive).
    if (parkedLoadLoopJob_.active) {
        const bool parkedIsFocus =
            parkedLoadLoopJob_.track == focusTrack && parkedLoadLoopJob_.slot == focusSlot;
        bool focusQueued = false;
        focusQueued = isDeferredLoopSlotRestoreQueued(focusTrack, focusSlot);
        const auto action = LoadLoopSelectionPolicy::resolveParkedIdleAction(
            parkedIsFocus, focusQueued, canRunBackgroundLoadLoopNow());
        switch (action) {
            case LoadLoopSelectionPolicy::ParkedIdleAction::ResumeParkedFocus:
                swapLoadLoopJobs(loadLoopJob_, parkedLoadLoopJob_);
                return;
            case LoadLoopSelectionPolicy::ParkedIdleAction::BeginFocusFromQueue: {
                // Focus High must still begin while a Low job is parked under PLAYING
                // (session_20260719_001237).
                DeferredLoopSlotRestore focusNext{};
                if (popFocusDeferredLoopSlotRestore(focusTrack, focusSlot, focusNext)) {
                    (void)beginLoadLoopJob(focusNext.track, focusNext.slot);
                    return;
                }
                // Queue drained between peek and pop — same fallthrough as pre-policy path.
                if (canRunBackgroundLoadLoopNow()) {
                    swapLoadLoopJobs(loadLoopJob_, parkedLoadLoopJob_);
                }
                return;
            }
            case LoadLoopSelectionPolicy::ParkedIdleAction::PromoteParkedLow:
                swapLoadLoopJobs(loadLoopJob_, parkedLoadLoopJob_);
                return;
            case LoadLoopSelectionPolicy::ParkedIdleAction::SkipKeepParked:
                return;
        }
        return;
    }

    reprioritizeDeferredLoopSlotRestoreEntries();

    const bool focusQueued = isDeferredLoopSlotRestoreQueued(focusTrack, focusSlot);
    const bool backgroundQueued = pendingLoopSlotRestoreCount() > 0;
    const auto action = LoadLoopSelectionPolicy::resolveEmptyIdleAction(
        focusQueued, backgroundQueued, canRunBackgroundLoadLoopNow());
    DeferredLoopSlotRestore next{};
    switch (action) {
        case LoadLoopSelectionPolicy::EmptyIdleAction::BeginFocusFromQueue:
            if (popFocusDeferredLoopSlotRestore(focusTrack, focusSlot, next)) {
                (void)beginLoadLoopJob(next.track, next.slot);
                return;
            }
            // Fall through to background only when policy still allows it.
            if (canRunBackgroundLoadLoopNow() && popNextDeferredLoopSlotRestore(next)) {
                (void)beginLoadLoopJob(next.track, next.slot);
            }
            return;
        case LoadLoopSelectionPolicy::EmptyIdleAction::BeginBackgroundFromQueue:
            if (popNextDeferredLoopSlotRestore(next)) {
                (void)beginLoadLoopJob(next.track, next.slot);
            }
            return;
        case LoadLoopSelectionPolicy::EmptyIdleAction::Skip:
            return;
    }
}

STORAGE_PERSIST_MEM SlotLoadAdvanceResult commitLoadLoopJobPublish() {
    if (!loadLoopJob_.active || loadLoopJob_.session == nullptr) {
        return SlotLoadAdvanceResult::Failed;
    }
    Loop& loop = trackManager.getTrack(loadLoopJob_.track).getLoop(loadLoopJob_.slot);

    if (!loadLoopJob_.saveTokenVerified) {
        Serial.println("[StorageManager] WARN: LoadLoopJob token mismatch");
        resetLoopSlotToEmpty(loop, loadLoopJob_.slot);
        markLoopSlotRestoreAttempted(loadLoopJob_.track, loadLoopJob_.slot);
        clearLoadLoopJobInstance(loadLoopJob_);
        return SlotLoadAdvanceResult::Failed;
    }

    (void)loadLoopJob_.session->advanceAfterPhaseWork();
#if defined(PERF_TELEMETRY)
    const uint32_t applyStartUs = micros();
#endif
    applySnapshotToLoop(loop, loadLoopJob_.snapshot);
    TrackUndo::rebuildSlotFromLoopContent(trackManager.getTrack(loadLoopJob_.track),
                                          loadLoopJob_.slot);
#if defined(PERF_TELEMETRY)
    const uint32_t applyUs = micros() - applyStartUs;
    if (applyUs > LoadLoopBudget::FocusRestoreUs && loadLoopJobIsFocusSlot()) {
        Serial.print("[StorageManager] LoadLoopJob apply overshoot us=");
        Serial.println(applyUs);
    }
#endif
    // Snapshot ownership moved onto Loop — drop empty shell without releaseChunkRefs.
    loadLoopJob_.snapshot = PersistedLoopSnapshot{};
    loadLoopJob_.parseState = PersistedLoopParseState{};

    markLoopCommittedChunksPersistedFromSdLoad(loop);
    (void)loadLoopJob_.session->advanceAfterPhaseWork();
    markLoopSlotRestoreAttempted(loadLoopJob_.track, loadLoopJob_.slot);

#if defined(PERF_TELEMETRY)
    if (loadLoopJobIsFocusSlot()) {
        Serial.print("[StorageManager] LoadLoopJob done ");
        Serial.print(loadLoopJob_.track);
        Serial.print('/');
        Serial.println(loadLoopJob_.slot);
    }
#endif

    delete loadLoopJob_.session;
    loadLoopJob_.session = nullptr;
    if (loadLoopJob_.file) {
        loadLoopJob_.file.close();
    }
    loadLoopJob_.buffer.clear();
    loadLoopJob_.buffer.shrink_to_fit();
    loadLoopJob_.active = false;
    loadLoopJob_.phase = LoadLoopJobPhase::Reading;
    // Hold deferred save across the post-commit prewarm frame (000659).
    postLoadLoopCommitSaveHoldoffUntilMs_ = millis() + 100u;
    return SlotLoadAdvanceResult::Completed;
}

STORAGE_PERSIST_MEM SlotLoadAdvanceResult stepLoadLoopJobParse(uint32_t deadlineUs) {
    if (!loadLoopJob_.active || loadLoopJob_.session == nullptr) {
        return SlotLoadAdvanceResult::Failed;
    }
#if defined(PERF_TELEMETRY)
    const uint32_t parseStartUs = micros();
#endif
    const PersistedLoopParseStepResult result = stepPersistedLoopSnapshotParse(
        loadLoopJob_.buffer.data(), loadLoopJob_.buffer.size(), loadLoopJob_.snapshot,
        loadLoopJob_.parseState, deadlineUs);
#if defined(PERF_TELEMETRY)
    const uint32_t parseUs = micros() - parseStartUs;
    (void)parseUs;
#endif
    if (result == PersistedLoopParseStepResult::Failed) {
        Serial.println("[StorageManager] WARN: LoadLoopJob parse failed");
        Loop& loop = trackManager.getTrack(loadLoopJob_.track).getLoop(loadLoopJob_.slot);
        resetLoopSlotToEmpty(loop, loadLoopJob_.slot);
        markLoopSlotRestoreAttempted(loadLoopJob_.track, loadLoopJob_.slot);
        clearLoadLoopJobInstance(loadLoopJob_);
        return SlotLoadAdvanceResult::Failed;
    }
    if (result == PersistedLoopParseStepResult::MoreWork) {
        return SlotLoadAdvanceResult::MoreWork;
    }
    // Parse complete — drop SD payload bytes before Commit so EXTMEM can absorb adopt +
    // pressure reclaim on the next frame (000205: 57KB buffer still held into Committing).
    loadLoopJob_.buffer.clear();
    loadLoopJob_.buffer.shrink_to_fit();
    // Publish on a later frame (one expensive op per frame).
    loadLoopJob_.phase = LoadLoopJobPhase::Committing;
    (void)loadLoopJob_.session->advanceAfterPhaseWork();
    return SlotLoadAdvanceResult::MoreWork;
}

STORAGE_PERSIST_MEM SlotLoadAdvanceResult stepLoadLoopJob(uint32_t deadlineUs) {
    if (!loadLoopJob_.active || loadLoopJob_.session == nullptr) {
        return SlotLoadAdvanceResult::Failed;
    }
    if (loadLoopJob_.phase == LoadLoopJobPhase::Committing) {
        return commitLoadLoopJobPublish();
    }
    if (loadLoopJob_.phase == LoadLoopJobPhase::Parsing) {
        return stepLoadLoopJobParse(deadlineUs);
    }

    Loop& loop = trackManager.getTrack(loadLoopJob_.track).getLoop(loadLoopJob_.slot);
    uint8_t steps = 0;

    // Re-open per Reading turn so processDeferredSaveState / other SD users never share
    // a live File with LoadLoopJob across frames (233730 hang after begin).
    if (!loadLoopJob_.file) {
        char loopPath[64];
        if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), loadLoopJob_.track,
                                                   loadLoopJob_.slot)) {
            resetLoopSlotToEmpty(loop, loadLoopJob_.slot);
            markLoopSlotRestoreAttempted(loadLoopJob_.track, loadLoopJob_.slot);
            clearLoadLoopJobInstance(loadLoopJob_);
            return SlotLoadAdvanceResult::Failed;
        }
        File loopFile = SD.open(loopPath, FILE_READ);
        if (!loopFile ||
            !loopFile.seek(loadLoopJob_.payloadOffset + loadLoopJob_.bytesRead)) {
            if (loopFile) {
                loopFile.close();
            }
            Serial.println("[StorageManager] WARN: LoadLoopJob reopen/seek failed");
            resetLoopSlotToEmpty(loop, loadLoopJob_.slot);
            markLoopSlotRestoreAttempted(loadLoopJob_.track, loadLoopJob_.slot);
            clearLoadLoopJobInstance(loadLoopJob_);
            return SlotLoadAdvanceResult::Failed;
        }
        loadLoopJob_.file = loopFile;
    }

    while (loadLoopJob_.bytesRead < loadLoopJob_.payloadSize) {
        if (steps > 0 && micros() >= deadlineUs) {
            if (loadLoopJob_.file) {
                loadLoopJob_.file.close();
            }
            return SlotLoadAdvanceResult::MoreWork;
        }
        const size_t remaining = loadLoopJob_.payloadSize - loadLoopJob_.bytesRead;
        const size_t chunk =
            remaining > LoadLoopBudget::ReadChunkBytes ? LoadLoopBudget::ReadChunkBytes : remaining;
        const size_t oldSize = loadLoopJob_.buffer.size();
        loadLoopJob_.buffer.resize(oldSize + chunk);
        const int n = loadLoopJob_.file.read(loadLoopJob_.buffer.data() + oldSize, chunk);
        if (n != static_cast<int>(chunk)) {
            Serial.println("[StorageManager] WARN: LoadLoopJob read failed");
            resetLoopSlotToEmpty(loop, loadLoopJob_.slot);
            markLoopSlotRestoreAttempted(loadLoopJob_.track, loadLoopJob_.slot);
            clearLoadLoopJobInstance(loadLoopJob_);
            return SlotLoadAdvanceResult::Failed;
        }
        loadLoopJob_.bytesRead += chunk;
        ++steps;
        if (micros() >= deadlineUs) {
            if (loadLoopJob_.file) {
                loadLoopJob_.file.close();
            }
            return SlotLoadAdvanceResult::MoreWork;
        }
    }

    uint32_t magic = 0;
    if (!loadLoopJob_.file.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) ||
        magic != CurrentSetStorage::kSaveFileToken) {
        Serial.println("[StorageManager] WARN: LoadLoopJob token mismatch");
        resetLoopSlotToEmpty(loop, loadLoopJob_.slot);
        markLoopSlotRestoreAttempted(loadLoopJob_.track, loadLoopJob_.slot);
        clearLoadLoopJobInstance(loadLoopJob_);
        return SlotLoadAdvanceResult::Failed;
    }
    loadLoopJob_.saveTokenVerified = true;
    if (loadLoopJob_.file) {
        loadLoopJob_.file.close();
    }
    // SD fill done — enter Parsing next frame (do not parse on the same turn as last read).
    loadLoopJob_.phase = LoadLoopJobPhase::Parsing;
    loadLoopJob_.parseState = PersistedLoopParseState{};
    releasePersistedLoopSnapshotChunks(loadLoopJob_.snapshot);
    (void)loadLoopJob_.session->advanceAfterPhaseWork();
    return SlotLoadAdvanceResult::MoreWork;
}


bool deferredSaveBlockedByActiveSlotLoadSd() {
    return SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile();
}

bool deferredSaveBlockedByPostLoadCommitHoldoff() {
    if (postLoadLoopCommitSaveHoldoffUntilMs_ != 0 &&
        millis() < postLoadLoopCommitSaveHoldoffUntilMs_) {
        return true;
    }
    postLoadLoopCommitSaveHoldoffUntilMs_ = 0;
    return false;
}

bool anyLoadLoopJobActive() {
    return loadLoopJob_.active || parkedLoadLoopJob_.active;
}

bool loadLoopJobHasOpenSdFile() {
    return static_cast<bool>(loadLoopJob_.file) || static_cast<bool>(parkedLoadLoopJob_.file);
}

bool isActiveLoadLoopJobFor(uint8_t trackIndex, uint8_t slotIndex) {
    return loadLoopJob_.active && loadLoopJob_.track == trackIndex && loadLoopJob_.slot == slotIndex;
}

bool isParkedLoadLoopJobFor(uint8_t trackIndex, uint8_t slotIndex) {
    return parkedLoadLoopJob_.active && parkedLoadLoopJob_.track == trackIndex &&
           parkedLoadLoopJob_.slot == slotIndex;
}

void setBootTitleLoadDrain(bool enabled) {
    bootTitleLoadDrain_ = enabled;
}

bool getBootTitleLoadDrain() {
    return bootTitleLoadDrain_;
}

void armBackgroundRestoreHoldoff(uint32_t delayMs) {
    backgroundRestoreHoldoffUntilMs_ = millis() + delayMs;
}

bool isActiveLoadLoopJobRunning() {
    return loadLoopJob_.active;
}

LoadLoopJobPhase activeLoadLoopJobPhase() {
    return loadLoopJob_.phase;
}

bool isBackgroundRestoreHoldoffBlockingNonFocus() {
    return backgroundRestoreHoldoffUntilMs_ != 0 && millis() < backgroundRestoreHoldoffUntilMs_;
}

bool STORAGE_PERSIST_MEM selectSubmittedLoadJobsImpl() {
    if (trackManager.getTrackCount() == 0) {
        return false;
    }
    const uint8_t focusTrack = trackManager.getSelectedTrackIndex();
    const uint8_t focusSlot = trackManager.getSelectedSlotIndex(focusTrack);
    const bool hadActiveAtEntry = loadLoopJob_.active;
    ensureActiveLoadLoopJobSelected(focusTrack, focusSlot);
    return !hadActiveAtEntry && loadLoopJob_.active;
}

void STORAGE_PERSIST_MEM stepSubmittedLoadJobsImpl(uint32_t budgetUs, bool activatedThisFrame) {
    const uint32_t frameStartUs = micros();

    if (trackManager.getTrackCount() == 0) {
        return;
    }
    if (!loadLoopJob_.active) {
        return;
    }
    if (!LoadLoopSelectionPolicy::shouldStepLoadLoopJob(loadLoopJobIsFocusSlot(),
                                                        canRunBackgroundLoadLoopNow())) {
        return;
    }
    if (loadLoopJob_.phase == LoadLoopJobPhase::Committing) {
        (void)stepLoadLoopJob(frameStartUs + LoadLoopBudget::FocusRestoreUs);
        return;
    }
    if (isBackgroundRestoreHoldoffBlockingNonFocus()) {
        if (!loadLoopJobIsFocusSlot()) {
            return;
        }
    }
    if (budgetUs == 0) {
        return;
    }
    if (activatedThisFrame) {
        return;
    }

    const uint32_t deadlineUs = frameStartUs + budgetUs;
    while (micros() < deadlineUs) {
        if (!loadLoopJob_.active) {
            break;
        }
        const LoadLoopJobPhase phaseBefore = loadLoopJob_.phase;
        const SlotLoadAdvanceResult result = stepLoadLoopJob(deadlineUs);
        if (result != SlotLoadAdvanceResult::MoreWork) {
            break;
        }
        if (loadLoopJob_.active && loadLoopJob_.phase == LoadLoopJobPhase::Committing &&
            phaseBefore == LoadLoopJobPhase::Parsing) {
            (void)stepLoadLoopJob(deadlineUs);
            break;
        }
        if (loadLoopJob_.active && loadLoopJob_.phase != phaseBefore) {
            break;
        }
        if (result == SlotLoadAdvanceResult::MoreWork &&
            phaseBefore == LoadLoopJobPhase::Parsing) {
            break;
        }
    }
}

}  // namespace StorageManagerInternal

bool STORAGE_PERSIST_MEM StorageManager::selectSubmittedLoadJobs() {
    return StorageManagerInternal::selectSubmittedLoadJobsImpl();
}

void STORAGE_PERSIST_MEM StorageManager::stepSubmittedLoadJobs(uint32_t budgetUs,
                                                              bool activatedThisFrame) {
    StorageManagerInternal::stepSubmittedLoadJobsImpl(budgetUs, activatedThisFrame);
}

void STORAGE_PERSIST_MEM StorageManager::probeActiveLoadLoopJob(bool& active, uint8_t& track, uint8_t& slot,
                                            uint8_t& phase, uint8_t& isFocus) {
    using namespace StorageManagerInternal;
    active = loadLoopJob_.active;
    if (!active) {
        track = 255;
        slot = 255;
        phase = 255;
        isFocus = 0;
        return;
    }
    track = loadLoopJob_.track;
    slot = loadLoopJob_.slot;
    phase = static_cast<uint8_t>(loadLoopJob_.phase);
    isFocus = loadLoopJobIsFocusSlot() ? 1 : 0;
}