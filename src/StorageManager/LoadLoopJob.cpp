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
#include <cstring>

extern bool sequencerRunning;

namespace StorageManagerInternal {

/// LoadLoopJob: read SD payload in timed slices, parse under budget into a staging
/// snapshot (Phase A.6), then one-shot atomic Commit. At most one active + one parked.
enum class LoadLoopJobPhase : uint8_t {
    Reading = 0,
    Parsing = 1,
    Committing = 2,
    /// Commit has made the loaded loop visible. Releasing parser/session
    /// ownership and admitting post-load work is deliberately a later owner
    /// turn, never a hidden tail of the atomic publication.
    Cleanup = 3,
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
    bool usesLoopSnapshotBodyFormatV1 = false;
    LoopSnapshotBodyFormatV1LoadParseState lsv1ParseState{};
    ImmutableLoopSnapshotRef loadedLoopSnapshot;
    SlotLoadSession* session = nullptr;
};
#if defined(__IMXRT1062__)
DMAMEM LoadLoopJob loadLoopJob_{};
DMAMEM LoadLoopJob parkedLoadLoopJob_{};
#else
LoadLoopJob loadLoopJob_{};
LoadLoopJob parkedLoadLoopJob_{};
#endif
/// A2.4 (D5): bounded restore attempts per `(track, slot)` per power cycle. A **transient** SD failure
/// must be retryable within the session; `loopSlotRestoreAttempted_` is a hard latch, so a single
/// transient read failure used to make that slot `EMPTY` for the rest of the power cycle and made boots
/// disagree. A **persistent** failure still ends in a named refusal, after this bound.
constexpr uint8_t kMaxLoopSlotRestoreAttempts = 3;
uint8_t loopSlotRestoreAttempts_[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK]{};
volatile uint32_t g_loopSlotRestoreRefusalCount = 0;

/// Handle a failure that is **retryable**: reset the slot as the restore path's own cleanup (which must
/// keep the request identity, per A2.1), then either re-queue the request for another attempt or record
/// the `refused` terminal once the bound is exhausted. Returns true when the request was re-queued.
STORAGE_PERSIST_MEM bool scheduleLoopSlotRestoreRetry(uint8_t trackIndex, uint8_t slotIndex,
                                                      RestoreRequestTerminal exhaustedTerminal) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return false;
    }
    if (loopSlotRestoreAttempts_[trackIndex][slotIndex] >= kMaxLoopSlotRestoreAttempts) {
        // Bounded: the request reaches a named, non-retryable outcome instead of retrying forever.
        (void)markRestoreRequestTerminal(trackIndex, slotIndex, exhaustedTerminal);
        ++g_loopSlotRestoreRefusalCount;
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return false;
    }
    // The restore path's **own** cleanup keeps the identity and names a retry wait (A2.1/G2).
    markRestoreRequestWaiting(trackIndex, slotIndex, RestoreRequestWait::Retry);
    return appendBootLoopSlotRestore(trackIndex, slotIndex, 0);
}

/// After boot playback ready, delay background fill so first transport press is not starved.
uint32_t backgroundRestoreHoldoffUntilMs_ = 0;
/// After LoadLoopJob Commit, block deferred save briefly so SD work cannot race the
/// post-commit prewarm frame (session_20260719_000659: silence after commit_prewarm_q).
uint32_t postLoadLoopCommitSaveHoldoffUntilMs_ = 0;
/// While true (boot title playback drain), do not park/demote — parked jobs block bootInteractiveReady.
bool bootTitleLoadDrain_ = false;

/// Definition lives in `LoopSlotRestoreQueue.cpp`; the retry path re-queues through it.
bool appendBootLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex, uint16_t restorePriority);

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
    job.usesLoopSnapshotBodyFormatV1 = false;
    job.lsv1ParseState = LoopSnapshotBodyFormatV1LoadParseState{};
    job.loadedLoopSnapshot = nullptr;
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
    swap(a.usesLoopSnapshotBodyFormatV1, b.usesLoopSnapshotBodyFormatV1);
    swap(a.lsv1ParseState, b.lsv1ParseState);
    swap(a.loadedLoopSnapshot, b.loadedLoopSnapshot);
    swap(a.session, b.session);
}

STORAGE_PERSIST_MEM bool shouldFinishLoadLoopJobBeforePreempt() {
    if (!loadLoopJob_.active) {
        return false;
    }
    // Never abandon mid-publish.
    if (loadLoopJob_.phase == LoadLoopJobPhase::Committing ||
        loadLoopJob_.phase == LoadLoopJobPhase::Cleanup) {
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
    // Background slot-load work must not compete with active transport clock service.
    // If transport clock is running, park/defer Low job work and retry later.
    if (sequencerRunning) {
        return false;
    }
    for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
        const Track& t = trackManager.getTrack(i);
        if (t.isRecording() || t.isAudiblePlaybackActive()) {
            return false;
        }
    }
    return true;
}

/// A2.2 (D2): background **preparation** admission, which is deliberately narrower than
/// `canRunBackgroundLoadLoopNow()`.
///
/// `design.md` D2 makes a bounded background-restore service slice admissible under active transport so
/// that a queued request's preparation (open, read, parse) advances while playback continues, while
/// **commit and cleanup remain gated** by the commit stop-gate (`admitLoadJobChild`) until
/// `resumable-midi-boundary-playback-maintenance` task 3.3 lands bounded Commit. This predicate decides
/// only whether a job may hold/advance a *preparation* phase; it never admits commit or cleanup, and it
/// never touches that gate.
///
/// Recording still defers the load (the SD-session exclusion): the existing `canRunBackgroundLoadLoopNow`
/// answer is kept for capture, so this returns false whenever a track is recording.
STORAGE_PERSIST_MEM bool canPrepareBackgroundLoadLoopNow() {
    if (canRunBackgroundLoadLoopNow()) {
        return true;  // nothing is sounding; the full path is available anyway
    }
    if (bootTitleLoadDrain_) {
        return true;
    }
    // The SD-session exclusion is unchanged: capture keeps its existing deferral (proposal TBD-2 / O1).
    for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
        if (trackManager.getTrack(i).isRecording()) {
            return false;
        }
    }
    // Preparation under transport is admissible only while no restore job is commit-pending. The gate
    // is **global**: `hasLoadLoopJobCommitPending()` is true when *either* instance is in
    // commit/cleanup, and `main.cpp` then denies the whole load child for the turn.
    return !StorageManager::hasLoadLoopJobCommitPending();
}

/// True when this job instance may be *stepped* for background work right now: a runnable preparation
/// phase under the A2.2 preparation admission, or the pre-existing full admission. Commit and cleanup
/// are never admitted by the preparation path.
STORAGE_PERSIST_MEM bool backgroundJobMayBeServicedNow() {
    if (loadLoopJob_.phase != LoadLoopJobPhase::Committing &&
        loadLoopJob_.phase != LoadLoopJobPhase::Cleanup) {
        return canPrepareBackgroundLoadLoopNow();
    }
    return canRunBackgroundLoadLoopNow();
}

/// The head of the priority queue, without popping it -- the `popNext` sites have no track/slot filter, so
/// the liveness check has to be made against the entry the pop would take.
static bool backgroundHeadMayBegin() {
    DeferredLoopSlotRestore head{};
    if (!peekFirstPendingLoopSlotRestore(head)) return false;
    return queuedRestoreMayBegin(head.track, head.slot);
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
        // A2.4: a bad token can be a torn/partial write, so it is retryable up to the bound rather than
        // latching the slot for the power cycle.
        (void)scheduleLoopSlotRestoreRetry(trackIndex, slotIndex, RestoreRequestTerminal::Refused);
        return false;
    }
    File loopFile = SD.open(loopPath, FILE_READ);
    if (!loopFile) {
        resetLoopSlotToEmpty(loop, slotIndex);
        (void)scheduleLoopSlotRestoreRetry(trackIndex, slotIndex, RestoreRequestTerminal::Refused);
        return false;
    }

    size_t payloadOffset = 0;
    if (CurrentWorkspaceStorage::fileStartsWithEpochHeader(loopFile)) {
        CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
        const StorageIo epochIo = storageIoFromFileRead(loopFile);
        if (!CurrentWorkspaceStorage::readEpochFileHeader(epochIo, epochHeader)) {
            loopFile.close();
            resetLoopSlotToEmpty(loop, slotIndex);
            (void)scheduleLoopSlotRestoreRetry(trackIndex, slotIndex,
                                               RestoreRequestTerminal::Refused);
            return false;
        }
        payloadOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
    }
    const size_t fileSize = loopFile.size();
    if (fileSize < payloadOffset + sizeof(CurrentSetStorage::kSaveFileToken)) {
        loopFile.close();
        resetLoopSlotToEmpty(loop, slotIndex);
        (void)scheduleLoopSlotRestoreRetry(trackIndex, slotIndex, RestoreRequestTerminal::Refused);
        return false;
    }
    const size_t payloadSize = fileSize - payloadOffset - sizeof(CurrentSetStorage::kSaveFileToken);
    if (!loopFile.seek(payloadOffset)) {
        loopFile.close();
        resetLoopSlotToEmpty(loop, slotIndex);
        (void)scheduleLoopSlotRestoreRetry(trackIndex, slotIndex, RestoreRequestTerminal::Refused);
        return false;
    }

    loadLoopJob_.active = true;
    loadLoopJob_.phase = LoadLoopJobPhase::Reading;
    // A2.1: the request is now being advanced by an admitted job, which is `running` -- not a
    // fictitious wait and not a terminal.
    markRestoreRequestRunning(trackIndex, slotIndex);
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
            parkedIsFocus, focusQueued, canPrepareBackgroundLoadLoopNow());
        switch (action) {
            case LoadLoopSelectionPolicy::ParkedIdleAction::ResumeParkedFocus:
                swapLoadLoopJobs(loadLoopJob_, parkedLoadLoopJob_);
                return;
            case LoadLoopSelectionPolicy::ParkedIdleAction::BeginFocusFromQueue: {
                // Focus High must still begin while a Low job is parked under PLAYING
                // (session_20260719_001237).
                DeferredLoopSlotRestore focusNext{};
                // S2b.5: never pop a request for a slot a live owner still holds -- the pop would discard it.
                if (!queuedRestoreMayBegin(focusTrack, focusSlot)) {
                    // A2.1: the denial is a named wait, preserved rather than dropped.
                    markRestoreRequestWaiting(focusTrack, focusSlot, RestoreRequestWait::SlotOwned);
                    return;
                }
                if (popFocusDeferredLoopSlotRestore(focusTrack, focusSlot, focusNext)) {
                    (void)beginLoadLoopJob(focusNext.track, focusNext.slot);
                    return;
                }
                // Queue drained between peek and pop — same fallthrough as pre-policy path.
                if (canPrepareBackgroundLoadLoopNow()) {
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
        focusQueued, backgroundQueued, canPrepareBackgroundLoadLoopNow());
    // A2.1: name the wait on the priority head when the only thing stopping it is a live owner. Without
    // this the request stays "queued" forever with no reason, which is exactly the C4 stall the
    // observable state exists to make diagnosable.
    if (backgroundQueued) {
        DeferredLoopSlotRestore blockedHead{};
        if (peekFirstPendingLoopSlotRestore(blockedHead) &&
            !queuedRestoreMayBegin(blockedHead.track, blockedHead.slot)) {
            markRestoreRequestWaiting(blockedHead.track, blockedHead.slot,
                                      RestoreRequestWait::SlotOwned);
        }
    }
    DeferredLoopSlotRestore next{};
    switch (action) {
        case LoadLoopSelectionPolicy::EmptyIdleAction::BeginFocusFromQueue:
            // S2b.5: leave the request queued while a live owner holds the focus slot; the pop would
            // discard it (see `queuedRestoreMayBegin`).
            if (!queuedRestoreMayBegin(focusTrack, focusSlot)) {
                markRestoreRequestWaiting(focusTrack, focusSlot, RestoreRequestWait::SlotOwned);
                return;
            }
            if (popFocusDeferredLoopSlotRestore(focusTrack, focusSlot, next)) {
                (void)beginLoadLoopJob(next.track, next.slot);
                return;
            }
            // Fall through to background only when policy still allows it and the head is startable.
            // A2.3: take the first **startable** request, not merely the priority head, so a blocked
            // head cannot stall the queue behind it.
            if (canPrepareBackgroundLoadLoopNow() &&
                popNextStartableDeferredLoopSlotRestore(next)) {
                (void)beginLoadLoopJob(next.track, next.slot);
            }
            return;
        case LoadLoopSelectionPolicy::EmptyIdleAction::BeginBackgroundFromQueue:
            if (popNextStartableDeferredLoopSlotRestore(next)) {
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
        const uint8_t retryTrack = loadLoopJob_.track;
        const uint8_t retrySlot = loadLoopJob_.slot;
        clearLoadLoopJobInstance(loadLoopJob_);
        (void)scheduleLoopSlotRestoreRetry(retryTrack, retrySlot,
                                           RestoreRequestTerminal::Refused);
        return SlotLoadAdvanceResult::Failed;
    }

    (void)loadLoopJob_.session->advanceAfterPhaseWork();
#if defined(PERF_TELEMETRY)
    const uint32_t applyStartUs = micros();
#endif
    applyLoadedLoopSlotBodyCommit(loop, trackManager.getTrack(loadLoopJob_.track),
                                  loadLoopJob_.slot, loadLoopJob_.usesLoopSnapshotBodyFormatV1,
                                  loadLoopJob_.snapshot, std::move(loadLoopJob_.loadedLoopSnapshot));
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
    loadLoopJob_.usesLoopSnapshotBodyFormatV1 = false;
    loadLoopJob_.lsv1ParseState = LoopSnapshotBodyFormatV1LoadParseState{};
    loadLoopJob_.loadedLoopSnapshot = nullptr;

    markLoopCommittedChunksPersistedFromSdLoad(loop);
    // A2.1: the payload is published, so this request has reached its `committed` terminal -- recorded
    // once per identity (a second attempt is refused by the record itself).
    (void)markRestoreRequestTerminal(loadLoopJob_.track, loadLoopJob_.slot,
                                     RestoreRequestTerminal::Committed);
    (void)loadLoopJob_.session->advanceAfterPhaseWork();

#if defined(PERF_TELEMETRY)
    if (loadLoopJobIsFocusSlot()) {
        Serial.print("[StorageManager] LoadLoopJob done ");
        Serial.print(loadLoopJob_.track);
        Serial.print('/');
        Serial.println(loadLoopJob_.slot);
    }
#endif

    // The loop is now atomically visible. Do not let allocation release,
    // session destruction, restore-queue bookkeeping, or save admission hide
    // in this Commit grain. The scheduler will execute Cleanup only under the
    // same STOPPED-only admission as Commit.
    loadLoopJob_.phase = LoadLoopJobPhase::Cleanup;
    return SlotLoadAdvanceResult::MoreWork;
}

STORAGE_PERSIST_MEM SlotLoadAdvanceResult cleanupLoadLoopJobPublish() {
    if (!loadLoopJob_.active || loadLoopJob_.phase != LoadLoopJobPhase::Cleanup) {
        return SlotLoadAdvanceResult::Failed;
    }
    markLoopSlotRestoreAttempted(loadLoopJob_.track, loadLoopJob_.slot);
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
    if (loadLoopJob_.usesLoopSnapshotBodyFormatV1) {
        const LoopSnapshotBodyFormatV1LoadStepResult lsv1Result =
            stepLoopSnapshotBodyFormatV1LoadParse(loadLoopJob_.buffer.data(),
                                                  loadLoopJob_.buffer.size(),
                                                  loadLoopJob_.snapshot,
                                                  loadLoopJob_.lsv1ParseState, deadlineUs);
#if defined(PERF_TELEMETRY)
        const uint32_t parseUs = micros() - parseStartUs;
        (void)parseUs;
#endif
        if (lsv1Result == LoopSnapshotBodyFormatV1LoadStepResult::Failed) {
            Serial.println("[StorageManager] WARN: LoadLoopJob LSV1 parse failed");
            Loop& loop = trackManager.getTrack(loadLoopJob_.track).getLoop(loadLoopJob_.slot);
            resetLoopSlotToEmpty(loop, loadLoopJob_.slot);
            const uint8_t retryTrack = loadLoopJob_.track;
            const uint8_t retrySlot = loadLoopJob_.slot;
            clearLoadLoopJobInstance(loadLoopJob_);
            (void)scheduleLoopSlotRestoreRetry(retryTrack, retrySlot,
                                               RestoreRequestTerminal::Refused);
            return SlotLoadAdvanceResult::Failed;
        }
        if (lsv1Result == LoopSnapshotBodyFormatV1LoadStepResult::MoreWork) {
            return SlotLoadAdvanceResult::MoreWork;
        }
        loadLoopJob_.loadedLoopSnapshot = std::move(loadLoopJob_.lsv1ParseState.loopSnapshot);
        loadLoopJob_.lsv1ParseState.loopSnapshot = nullptr;
    } else {
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
            const uint8_t retryTrack = loadLoopJob_.track;
            const uint8_t retrySlot = loadLoopJob_.slot;
            clearLoadLoopJobInstance(loadLoopJob_);
            (void)scheduleLoopSlotRestoreRetry(retryTrack, retrySlot,
                                               RestoreRequestTerminal::Refused);
            return SlotLoadAdvanceResult::Failed;
        }
        if (result == PersistedLoopParseStepResult::MoreWork) {
            return SlotLoadAdvanceResult::MoreWork;
        }
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
    if (loadLoopJob_.phase == LoadLoopJobPhase::Cleanup) {
        return cleanupLoadLoopJobPublish();
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
            const uint8_t retryTrack = loadLoopJob_.track;
            const uint8_t retrySlot = loadLoopJob_.slot;
            clearLoadLoopJobInstance(loadLoopJob_);
            (void)scheduleLoopSlotRestoreRetry(retryTrack, retrySlot,
                                               RestoreRequestTerminal::Refused);
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
            const uint8_t retryTrack = loadLoopJob_.track;
            const uint8_t retrySlot = loadLoopJob_.slot;
            clearLoadLoopJobInstance(loadLoopJob_);
            (void)scheduleLoopSlotRestoreRetry(retryTrack, retrySlot,
                                               RestoreRequestTerminal::Refused);
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
        const uint8_t retryTrack = loadLoopJob_.track;
        const uint8_t retrySlot = loadLoopJob_.slot;
        clearLoadLoopJobInstance(loadLoopJob_);
        (void)scheduleLoopSlotRestoreRetry(retryTrack, retrySlot,
                                           RestoreRequestTerminal::Refused);
        return SlotLoadAdvanceResult::Failed;
    }
    loadLoopJob_.saveTokenVerified = true;
    if (loadLoopJob_.file) {
        loadLoopJob_.file.close();
    }
    // SD fill done — enter Parsing next frame (do not parse on the same turn as last read).
    const LoopSnapshotBodyFormatProbeResult bodyProbe = probeLoopSnapshotBodyFormat(
        loadLoopJob_.buffer.data(), loadLoopJob_.buffer.size());
    const bool looksLikeLsv1Prefix =
        loadLoopJob_.buffer.size() >= 4 &&
        std::memcmp(loadLoopJob_.buffer.data(), "LSV1", 4) == 0;
    if (bodyProbe == LoopSnapshotBodyFormatProbeResult::UnsupportedVersion ||
        (bodyProbe == LoopSnapshotBodyFormatProbeResult::Truncated && looksLikeLsv1Prefix)) {
        Serial.println("[StorageManager] WARN: LoadLoopJob LSV1 probe failed");
        resetLoopSlotToEmpty(loop, loadLoopJob_.slot);
        const uint8_t retryTrack = loadLoopJob_.track;
        const uint8_t retrySlot = loadLoopJob_.slot;
        clearLoadLoopJobInstance(loadLoopJob_);
        (void)scheduleLoopSlotRestoreRetry(retryTrack, retrySlot,
                                           RestoreRequestTerminal::Refused);
        return SlotLoadAdvanceResult::Failed;
    }
    loadLoopJob_.usesLoopSnapshotBodyFormatV1 =
        (bodyProbe == LoopSnapshotBodyFormatProbeResult::SupportedV1);
    loadLoopJob_.lsv1ParseState = LoopSnapshotBodyFormatV1LoadParseState{};
    loadLoopJob_.loadedLoopSnapshot = nullptr;
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
    const bool jobIsFocus = loadLoopJobIsFocusSlot();
    const bool backgroundAllowed = canRunBackgroundLoadLoopNow();
    // A2.2: preparation is admissible under transport, so a non-focus job is serviced rather than parked
    // while it has preparation work left. Commit/cleanup still require the full (idle) admission, so they
    // remain parked and reachable only through the commit gate.
    const bool preparationAllowed = backgroundJobMayBeServicedNow();
    // A2.2: a request whose preparation is complete and whose commit the gate denies is in a **named**
    // wait, not still queued. The gate is global (`hasLoadLoopJobCommitPending`), and bounding it is
    // runtime-maintenance task 3.3 -- this change names the wait rather than relaxing the gate.
    const bool commitPendingNow =
        loadLoopJob_.phase == LoadLoopJobPhase::Committing ||
        loadLoopJob_.phase == LoadLoopJobPhase::Cleanup;
    if (!jobIsFocus && !backgroundAllowed && commitPendingNow) {
        markRestoreRequestWaiting(loadLoopJob_.track, loadLoopJob_.slot,
                                  RestoreRequestWait::CommitGate);
    }
    if (!jobIsFocus && !preparationAllowed) {
        if (!bootTitleLoadDrain_) {
            parkActiveLoadLoopJob();
        }
        return;
    }
    if (!LoadLoopSelectionPolicy::shouldStepLoadLoopJob(jobIsFocus, preparationAllowed)) {
        return;
    }
    if (loadLoopJob_.phase == LoadLoopJobPhase::Committing ||
        loadLoopJob_.phase == LoadLoopJobPhase::Cleanup) {
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
        // A2.2: the moment preparation completes, a request whose commit the gate denies is in its
        // **named** commit-gate wait. Recording it here (not only at the next frame's entry check) means
        // the state is observable on the same turn the phase changes, which is what a fixture and an
        // operator both read.
        if (loadLoopJob_.active && !jobIsFocus && !backgroundAllowed &&
            (loadLoopJob_.phase == LoadLoopJobPhase::Committing ||
             loadLoopJob_.phase == LoadLoopJobPhase::Cleanup)) {
            markRestoreRequestWaiting(loadLoopJob_.track, loadLoopJob_.slot,
                                      RestoreRequestWait::CommitGate);
        }
        if (result != SlotLoadAdvanceResult::MoreWork) {
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

bool STORAGE_PERSIST_MEM StorageManager::hasActiveLoadLoopJobCommitPending() {
    using namespace StorageManagerInternal;
    return loadLoopJob_.active &&
           (loadLoopJob_.phase == LoadLoopJobPhase::Committing ||
            loadLoopJob_.phase == LoadLoopJobPhase::Cleanup);
}

bool STORAGE_PERSIST_MEM StorageManager::hasLoadLoopJobCommitPending() {
    using namespace StorageManagerInternal;
    return (loadLoopJob_.active &&
            (loadLoopJob_.phase == LoadLoopJobPhase::Committing ||
             loadLoopJob_.phase == LoadLoopJobPhase::Cleanup)) ||
           (parkedLoadLoopJob_.active &&
            (parkedLoadLoopJob_.phase == LoadLoopJobPhase::Committing ||
             parkedLoadLoopJob_.phase == LoadLoopJobPhase::Cleanup));
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

void STORAGE_PERSIST_MEM StorageManager::probeLoadLoopJobProgress(size_t& bytesRead,
                                                                 size_t& payloadSize) {
    using namespace StorageManagerInternal;
    // Both instances: `deferredSaveBlockedByActiveSlotLoadSd` counts either one, so a fixture asking
    // "did preparation advance?" must not miss the parked instance.
    bytesRead = loadLoopJob_.bytesRead + parkedLoadLoopJob_.bytesRead;
    payloadSize = loadLoopJob_.payloadSize + parkedLoadLoopJob_.payloadSize;
}

void STORAGE_PERSIST_MEM StorageManager::probeParkedLoadLoopJob(bool& active, uint8_t& track,
                                                               uint8_t& slot, uint8_t& phase) {
    using namespace StorageManagerInternal;
    active = parkedLoadLoopJob_.active;
    if (!active) {
        track = 255;
        slot = 255;
        phase = 255;
        return;
    }
    track = parkedLoadLoopJob_.track;
    slot = parkedLoadLoopJob_.slot;
    phase = static_cast<uint8_t>(parkedLoadLoopJob_.phase);
}

void STORAGE_PERSIST_MEM StorageManager::probeRestoreState(RestoreStateProbe& out) {
    using namespace StorageManagerInternal;
    out = RestoreStateProbe{};

    out.queuedCount = pendingLoopSlotRestoreCount();
    DeferredLoopSlotRestore head{};
    if (peekFirstPendingLoopSlotRestore(head)) {
        out.queueHeadPresent = true;
        out.queueHeadTrack = head.track;
        out.queueHeadSlot = head.slot;
        out.queueHeadPriority = head.restorePriority;
        // The head's own startability, which is the state an operator needs to tell "the head is
        // blocked on an owner" from "the queue is empty" without a bisect.
        out.queueHeadStartable = queuedRestoreMayBegin(head.track, head.slot);
    }

    if (loadLoopJob_.active) {
        out.activeJob = true;
        out.activeJobTrack = loadLoopJob_.track;
        out.activeJobSlot = loadLoopJob_.slot;
        out.activeJobPhase = static_cast<uint8_t>(loadLoopJob_.phase);
        out.activeJobIsFocus = loadLoopJobIsFocusSlot();
        out.activeBytesRead = loadLoopJob_.bytesRead;
        out.activePayloadSize = loadLoopJob_.payloadSize;
    }
    if (parkedLoadLoopJob_.active) {
        out.parkedJob = true;
        out.parkedJobTrack = parkedLoadLoopJob_.track;
        out.parkedJobSlot = parkedLoadLoopJob_.slot;
        out.parkedJobPhase = static_cast<uint8_t>(parkedLoadLoopJob_.phase);
        out.parkedBytesRead = parkedLoadLoopJob_.bytesRead;
        out.parkedPayloadSize = parkedLoadLoopJob_.payloadSize;
    }

    out.saveBlockedBySlotLoad = deferredSaveBlockedByActiveSlotLoadSd();
    out.saveBlockedByPostLoadHoldoff = deferredSaveBlockedByPostLoadCommitHoldoff();
}
