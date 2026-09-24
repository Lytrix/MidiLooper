//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `restored-library-stability` task 1.3 — the bounded, on-demand library observation.
//
// Why it exists (`proposal.md`, `design.md` D2): `StorageManager::emitPostRestoreCensusIfReady` fires at
// most once per boot and only when the system is settled, including `bootInteractiveReady()`. A restore
// request stuck in a named wait therefore suppresses the very report meant to explain it. This
// observation is an **explicitly requested**, bounded diagnostic alongside — never a relaxation of — the
// settled census checkpoint.
//
// Contract, in one place (the spec delta and `ARCHITECTURE-REVIEW.md` § Packet for task 1.3 carry the
// normative text):
//   * **owner** — `StorageManager` (the existing persistence/restore owner; no new owner);
//   * **service point** — `serviceLibraryObservation()`, called from the unconditional observation tail in
//     `main` **independently of normal load admission**, never from the admission-gated deferred
//     load/restore frame;
//   * **incremental and bounded** — at most `kSlotsPerInvocation` slots observed (and at most that many
//     records emitted) and `kSdProbesPerInvocation` SD token probes per invocation;
//   * **SD-read admission** — respects the existing SD-session exclusion; never opens or holds SD
//     concurrently with capture, a load job, or a persistence write;
//   * **invalidation** — each record is scoped to `(requestGeneration, setEpoch)` **and a content
//     revision identity**, and is re-checked immediately before it is emitted; a set-epoch change restarts
//     the whole observation (bounded), so no record mixes fields from two epochs;
//   * **permitted degradation** — a field that could not be observed stays `unknown`, and the report is
//     still produced;
//   * **not the settled measurement** — a distinct `LIBOBS` record family, and the settled census is
//     untouched.
//
// The state machine is compiled in every build so it is natively testable; the request trigger lives on
// the session-capture HITL surface and the records are emitted through the session-capture emitters, so a
// non-capture build can neither request it nor observe it.

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "Globals.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "SlotLoadSession.h"
#include "StorageManager/LibraryObservation.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/BootLoopSlotRestore.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#include <SD.h>
#include <cstring>

namespace StorageManagerInternal {

namespace {

/// Declared per-invocation bounds. `kSlotsPerInvocation` also bounds the emitted records per invocation,
/// so no single turn ever writes the whole library.
constexpr uint16_t kSlotsPerInvocation = 2;
constexpr uint16_t kSdProbesPerInvocation = 2;
/// A set-epoch change restarts the observation at most this many times; after that the records are
/// invalidated and the report is emitted, so a churning epoch cannot spin the service point.
constexpr uint16_t kMaxRestarts = 2;
constexpr uint16_t kSlotCount = Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK;

enum class LibraryObservationPhase : uint8_t { Idle = 0, Collecting = 1, Emitting = 2, Done = 3 };

struct LibraryObservationState {
    bool requested = false;
    bool reportReady = false;
    LibraryObservationPhase phase = LibraryObservationPhase::Idle;
    uint8_t cursorTrack = 0;
    uint8_t cursorSlot = 0;
    uint16_t turns = 0;
    uint16_t restarts = 0;
    uint16_t collected = 0;
    uint16_t emitted = 0;
    uint32_t windowStartUs = 0;
    uint32_t windowEndUs = 0;
    uint32_t setEpochAtStart = 0;
    /// Slots whose request record already read terminal `committed` when the window opened, so
    /// `observedRestoreCompletions` counts only completions **within this window** (design D4).
    uint64_t committedAtWindowStart = 0;
    uint8_t lastInvocationSlots = 0;
    uint8_t lastInvocationSdProbes = 0;
    bool everyInitialPlaybackObserved = true;
    bool consistentView = true;
    LibraryObservation::SlotResult results[kSlotCount];
    uint32_t observedContentRevision[kSlotCount]{};
    uint32_t observedGeneration[kSlotCount]{};
};

STORAGE_PERSIST_DATA LibraryObservationState libraryObservationState_{};

STORAGE_PERSIST_MEM uint16_t flatSlotIndex(uint8_t trackIndex, uint8_t slot) {
    return static_cast<uint16_t>(trackIndex) * Config::MAX_LOOPS_PER_TRACK + slot;
}

/// The existing SD-session admission: the same exclusion capture, a load job and a persistence write
/// already obey. The observation never opens SD when any of them holds the session.
STORAGE_PERSIST_MEM bool sdAdmissionGranted() {
    if (SlotLoadSession::isActive()) {
        return false;
    }
    if (anyLoadLoopJobActive()) {
        return false;
    }
    if (StorageManager::hasLoadLoopJobCommitPending()) {
        return false;
    }
    if (StorageManager::hasActivePersistenceWorkItem()) {
        return false;
    }
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        if (trackManager.getTrack(trackIndex).isRecording()) {
            return false;
        }
    }
    return true;
}

STORAGE_PERSIST_MEM uint64_t committedMaskAtNow() {
    uint64_t mask = 0;
    const uint8_t tracks = trackManager.getTrackCount();
    for (uint8_t trackIndex = 0; trackIndex < tracks; ++trackIndex) {
        for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
            if (restoreRequestTerminalFor(trackIndex, slot) == RestoreRequestTerminal::Committed) {
                mask |= (static_cast<uint64_t>(1) << flatSlotIndex(trackIndex, slot));
            }
        }
    }
    return mask;
}

STORAGE_PERSIST_MEM uint32_t committedEventCountFor(const Loop& loop) {
    uint32_t events = 0;
    if (loop.passes.hasRecordPass()) {
        events += static_cast<uint32_t>(
            LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.committedChunkIds));
    }
    for (const OverdubPass& pass : loop.passes.overdubPasses) {
        events += static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(pass.committedChunkIds));
    }
    return events;
}

STORAGE_PERSIST_MEM uint32_t highestCommittedPassIdFor(const Loop& loop) {
    uint32_t highest = 0;
    if (loop.passes.hasRecordPass()) {
        highest = static_cast<uint32_t>(loop.passes.recordPass.id);
    }
    for (const OverdubPass& pass : loop.passes.overdubPasses) {
        const uint32_t id = static_cast<uint32_t>(pass.id);
        if (id > highest) {
            highest = id;
        }
    }
    return highest;
}

/// Design D2's content revision identity: re-read from existing content at each slice — committed pass
/// count, highest committed pass id, loop length and committed event count. `Loop::nextPassId_` alone is
/// rejected (it is reset by `SlotReset`, the boot load and the edit-pass restore), and a new per-slot
/// monotonic counter is rejected against the RAM1 floor.
STORAGE_PERSIST_MEM uint32_t contentRevisionIdentity(uint8_t trackIndex, uint8_t slot) {
    const Track& track = trackManager.getTrack(trackIndex);
    if (!track.loopsAllocated()) {
        return 0;
    }
    const Loop& loop = track.getLoop(slot);
    uint32_t hash = 2166136261u;  // FNV-1a
    const auto mix = [&hash](uint32_t value) {
        hash ^= value;
        hash *= 16777619u;
    };
    mix(static_cast<uint32_t>(loop.passes.capturePassCount()));
    mix(highestCommittedPassIdFor(loop));
    mix(loop.loopLengthTicks);
    mix(committedEventCountFor(loop));
    return hash;
}

STORAGE_PERSIST_MEM LibraryObservation::Terminal mapTerminal(RestoreRequestTerminal terminal) {
    switch (terminal) {
        case RestoreRequestTerminal::None:
            return LibraryObservation::Terminal::None;
        case RestoreRequestTerminal::Committed:
            return LibraryObservation::Terminal::Committed;
        case RestoreRequestTerminal::Unnecessary:
            return LibraryObservation::Terminal::Unnecessary;
        case RestoreRequestTerminal::Refused:
            return LibraryObservation::Terminal::Refused;
        case RestoreRequestTerminal::Superseded:
            return LibraryObservation::Terminal::Superseded;
    }
    return LibraryObservation::Terminal::None;
}

/// True when the slot is in the boot playback set (per-track union of the selected and active slot),
/// read from the production `isBootPlaybackSlot` predicate.
STORAGE_PERSIST_MEM bool requiredForInitialPlayback(uint8_t trackIndex, uint8_t slot) {
    uint8_t active[Config::NUM_TRACKS]{};
    uint8_t selected[Config::NUM_TRACKS]{};
    const uint8_t tracks = trackManager.getTrackCount() < Config::NUM_TRACKS
                               ? trackManager.getTrackCount()
                               : static_cast<uint8_t>(Config::NUM_TRACKS);
    for (uint8_t t = 0; t < tracks; ++t) {
        active[t] = trackManager.getActiveLoopIndex(t);
        selected[t] = trackManager.getSelectedSlotIndex(t);
    }
    return isBootPlaybackSlot(trackIndex, slot, trackManager.getSelectedTrackIndex(), active, tracks,
                              selected, tracks);
}

/// Observe one slot into `libraryObservationState_.results`. SD work is bounded by \p sdProbes.
STORAGE_PERSIST_MEM void observeSlot(uint8_t trackIndex, uint8_t slot, bool sdAdmitted, uint16_t& sdProbes) {
    LibraryObservation::SlotInput input;
    input.track = trackIndex;
    input.slot = slot;

    // Payload: presence comes from the RAM mirror (no SD read). Token verification needs SD, so it
    // degrades to `unknown` — never to a loss claim — when admission is denied.
    const bool mirrorPresent = readLoopSlotPayloadOnSdInRamEntry(trackIndex, slot);
    if (!mirrorPresent) {
        input.payload = LibraryObservation::Payload::Absent;
    } else if (!sdAdmitted || sdProbes >= kSdProbesPerInvocation) {
        input.payload = LibraryObservation::Payload::Unknown;
    } else {
        ++sdProbes;
        char path[64];
        if (!CurrentSetStorage::formatLoopSlotPath(path, sizeof(path), trackIndex, slot)) {
            input.payload = LibraryObservation::Payload::Unknown;
        } else if (!SD.exists(path)) {
            input.payload = LibraryObservation::Payload::Absent;
        } else {
            input.payload = CurrentSetStorage::verifySaveFileTokenAtPath(path)
                                ? LibraryObservation::Payload::PresentVerified
                                : LibraryObservation::Payload::InvalidToken;
        }
    }

    input.requestRecordExists = restoreRequestRecordExists(trackIndex, slot);
    input.offered = input.requestRecordExists ? LibraryObservation::Offered::Offered
                                              : LibraryObservation::Offered::NotOffered;
    input.requestGeneration = restoreRequestGenerationFor(trackIndex, slot);
    input.setEpoch = restoreRequestEpochFor(trackIndex, slot);
    if (!input.requestRecordExists) {
        input.request = LibraryObservation::Request::None;
    } else {
        switch (restoreRequestState(trackIndex, slot)) {
            case RestoreRequestState::Queued:
                input.request = LibraryObservation::Request::Queued;
                break;
            case RestoreRequestState::Running:
                input.request = LibraryObservation::Request::Running;
                break;
            case RestoreRequestState::Waiting:
                input.request = LibraryObservation::Request::Waiting;
                input.requestWait = static_cast<uint8_t>(restoreRequestWaitFor(trackIndex, slot));
                break;
            case RestoreRequestState::Terminal:
                input.request = LibraryObservation::Request::Terminal;
                input.terminal = mapTerminal(restoreRequestTerminalFor(trackIndex, slot));
                break;
        }
    }

    const uint16_t index = flatSlotIndex(trackIndex, slot);
    const uint64_t windowBit = static_cast<uint64_t>(1) << index;
    input.committedTerminalInWindow =
        input.terminal == LibraryObservation::Terminal::Committed &&
        (libraryObservationState_.committedAtWindowStart & windowBit) == 0;

    // Committed content lives in RAM, so it is observable even when SD admission is denied.
    input.contentObserved = true;
    const Track& track = trackManager.getTrack(trackIndex);
    if (track.loopsAllocated()) {
        const Loop& loop = track.getLoop(slot);
        input.hasCommittedContent = loop.passes.hasRecordPass() || !loop.passes.overdubPasses.empty();
        input.committedPassCount = static_cast<uint16_t>(loop.passes.capturePassCount());
        input.highestCommittedPassId = highestCommittedPassIdFor(loop);
        input.loopLengthTicks = loop.loopLengthTicks;
        input.committedEventCount = committedEventCountFor(loop);
    }

    input.selectionObserved = true;
    input.requiredForInitialPlayback = requiredForInitialPlayback(trackIndex, slot);

    libraryObservationState_.results[index] = LibraryObservation::classifySlot(input);
    libraryObservationState_.observedGeneration[index] = input.requestGeneration;
    libraryObservationState_.observedContentRevision[index] = contentRevisionIdentity(trackIndex, slot);
    ++libraryObservationState_.collected;
}

STORAGE_PERSIST_MEM void clearResults() {
    for (uint16_t i = 0; i < kSlotCount; ++i) {
        libraryObservationState_.results[i] = LibraryObservation::SlotResult{};
        libraryObservationState_.observedContentRevision[i] = 0;
        libraryObservationState_.observedGeneration[i] = 0;
    }
}

STORAGE_PERSIST_MEM void beginCollection(uint32_t nowUs) {
    LibraryObservationState& state = libraryObservationState_;
    state.phase = LibraryObservationPhase::Collecting;
    state.cursorTrack = 0;
    state.cursorSlot = 0;
    state.collected = 0;
    state.emitted = 0;
    state.everyInitialPlaybackObserved = true;
    state.consistentView = true;
    state.reportReady = false;
    state.windowStartUs = nowUs;
    state.windowEndUs = 0;
    state.setEpochAtStart = restoreSetEpoch();
    state.committedAtWindowStart = committedMaskAtNow();
    clearResults();
}

/// Re-check one record immediately before it is emitted. A request-generation advance, a set-epoch change
/// or a content-revision change (a restore publication under an unchanged request identity, or an ordinary
/// capture/overdub/edit/clear commit) invalidates it, so no emitted record mixes fields from two versions.
STORAGE_PERSIST_MEM bool recordStillCurrent(uint16_t index) {
    LibraryObservationState& state = libraryObservationState_;
    LibraryObservation::SlotResult& result = state.results[index];
    if (result.invalidated) {
        return false;
    }
    const uint8_t trackIndex = static_cast<uint8_t>(index / Config::MAX_LOOPS_PER_TRACK);
    const uint8_t slot = static_cast<uint8_t>(index % Config::MAX_LOOPS_PER_TRACK);
    const bool generationChanged =
        restoreRequestGenerationFor(trackIndex, slot) != state.observedGeneration[index];
    const bool contentChanged =
        contentRevisionIdentity(trackIndex, slot) != state.observedContentRevision[index];
    if (generationChanged || contentChanged) {
        LibraryObservation::invalidateSlotResult(result);
        return false;
    }
    return true;
}

STORAGE_PERSIST_MEM void beginEmission() {
    LibraryObservationState& state = libraryObservationState_;
    state.phase = LibraryObservationPhase::Emitting;
    state.emitted = 0;
    state.everyInitialPlaybackObserved = true;
    for (uint16_t i = 0; i < kSlotCount; ++i) {
        const LibraryObservation::SlotResult& result = state.results[i];
        if (result.selectionObserved && result.requiredForInitialPlayback && result.invalidated) {
            state.everyInitialPlaybackObserved = false;
        }
    }
}

STORAGE_PERSIST_MEM void emitSlotRecord(const LibraryObservation::SlotResult& result) {
    SC_LIBRARY_OBSERVATION_SLOT(
        result.track, result.slot, result.currentState, LibraryObservation::causeCode(result.cause),
        LibraryObservation::payloadCode(result.payload),
        LibraryObservation::offeredCode(result.offered),
        LibraryObservation::requestCode(result.request), result.requestWait,
        LibraryObservation::terminalCode(result.terminal), result.requestGeneration, result.setEpoch,
        result.contentObserved && result.hasCommittedContent ? 1 : 0, result.committedPassCount,
        result.highestCommittedPassId, result.loopLengthTicks, result.committedEventCount,
        result.requiredForInitialPlayback ? 1 : 0, result.committedTerminalInWindow ? 1 : 0,
        result.invalidated ? 1 : 0);
}

STORAGE_PERSIST_MEM void finishObservation(uint32_t nowUs) {
    LibraryObservationState& state = libraryObservationState_;
    state.windowEndUs = nowUs;
    LibraryObservation::Window window;
    window.startUs = state.windowStartUs;
    window.endUs = state.windowEndUs;
    window.turns = state.turns;
    window.admissionComplete = bootBackgroundFillAdmissionComplete();
    window.everyInitialPlaybackSlotObserved = state.everyInitialPlaybackObserved;
    window.consistentView = state.consistentView;
    const LibraryObservation::Report report =
        LibraryObservation::summarize(state.results, kSlotCount, window);
    SC_LIBRARY_OBSERVATION_END(window.startUs, window.endUs, state.turns, state.restarts,
                               report.slotsObserved, report.counts.payloadInventory,
                               report.counts.initialPlaybackRequired,
                               report.counts.observedRestoreCompletions,
                               report.counts.committedAtCheckpoint, window.admissionComplete ? 1 : 0,
                               window.everyInitialPlaybackSlotObserved ? 1 : 0,
                               window.consistentView ? 1 : 0, report.complete ? 1 : 0);
    state.phase = LibraryObservationPhase::Done;
    state.requested = false;
    state.reportReady = true;
}

STORAGE_PERSIST_MEM void requestImpl() {
    LibraryObservationState& state = libraryObservationState_;
    beginCollection(micros());
    state.requested = true;
    state.turns = 0;
    state.restarts = 0;
    state.lastInvocationSlots = 0;
    state.lastInvocationSdProbes = 0;
}

STORAGE_PERSIST_MEM void serviceImpl() {
    LibraryObservationState& state = libraryObservationState_;
    if (!state.requested || state.phase == LibraryObservationPhase::Idle ||
        state.phase == LibraryObservationPhase::Done) {
        return;
    }

    // A set-epoch change invalidates the whole observation: restart it under the new epoch, bounded.
    if (restoreSetEpoch() != state.setEpochAtStart) {
        if (state.restarts < kMaxRestarts) {
            const uint16_t restarts = static_cast<uint16_t>(state.restarts + 1);
            const uint32_t turns = state.turns;
            beginCollection(micros());
            state.restarts = restarts;
            state.turns = turns;
        } else {
            for (uint16_t i = 0; i < kSlotCount; ++i) {
                if (i < state.collected && !state.results[i].invalidated) {
                    LibraryObservation::invalidateSlotResult(state.results[i]);
                }
            }
            beginEmission();
        }
        return;
    }

    state.lastInvocationSlots = 0;
    state.lastInvocationSdProbes = 0;
    ++state.turns;

    if (state.phase == LibraryObservationPhase::Collecting) {
        const bool sdAdmitted = sdAdmissionGranted();
        uint16_t sdProbes = 0;
        uint8_t observedThisInvocation = 0;
        while (observedThisInvocation < kSlotsPerInvocation &&
               state.cursorTrack < Config::NUM_TRACKS) {
            observeSlot(state.cursorTrack, state.cursorSlot, sdAdmitted, sdProbes);
            ++observedThisInvocation;
            ++state.cursorSlot;
            if (state.cursorSlot >= Config::MAX_LOOPS_PER_TRACK) {
                state.cursorSlot = 0;
                ++state.cursorTrack;
            }
        }
        state.lastInvocationSlots = observedThisInvocation;
        state.lastInvocationSdProbes = static_cast<uint8_t>(sdProbes);
        if (state.turns > 1) {
            state.consistentView = false;
        }
        if (state.cursorTrack >= Config::NUM_TRACKS) {
            beginEmission();
        }
        return;
    }

    if (state.phase == LibraryObservationPhase::Emitting) {
        if (state.emitted == 0) {
            SC_LIBRARY_OBSERVATION_BEGIN(state.windowStartUs, state.setEpochAtStart, kSlotCount);
        }
        uint8_t emittedThisInvocation = 0;
        while (emittedThisInvocation < kSlotsPerInvocation && state.emitted < kSlotCount) {
            (void)recordStillCurrent(state.emitted);
            emitSlotRecord(state.results[state.emitted]);
            ++state.emitted;
            ++emittedThisInvocation;
        }
        state.lastInvocationSlots = emittedThisInvocation;
        if (state.emitted >= kSlotCount) {
            finishObservation(micros());
        }
    }
}

}  // namespace

STORAGE_PERSIST_MEM LibraryObservationView libraryObservationView() {
    LibraryObservationView view;
    const LibraryObservationState& state = libraryObservationState_;
    view.requested = state.requested;
    view.reportReady = state.reportReady;
    view.phase = static_cast<uint8_t>(state.phase);
    view.cursorTrack = state.cursorTrack;
    view.cursorSlot = state.cursorSlot;
    view.turns = state.turns;
    view.restarts = state.restarts;
    view.collected = state.collected;
    view.slotsPerInvocation = kSlotsPerInvocation;
    view.sdProbesPerInvocation = kSdProbesPerInvocation;
    view.lastInvocationSlots = state.lastInvocationSlots;
    view.lastInvocationSdProbes = state.lastInvocationSdProbes;
    view.windowStartUs = state.windowStartUs;
    view.windowEndUs = state.windowEndUs;
    view.setEpochAtStart = state.setEpochAtStart;
    view.emittedSlots = state.emitted;
    return view;
}

STORAGE_PERSIST_MEM const LibraryObservation::SlotResult& libraryObservationResultAt(uint8_t trackIndex, uint8_t slot) {
    return libraryObservationState_.results[flatSlotIndex(trackIndex, slot)];
}

STORAGE_PERSIST_MEM void resetLibraryObservationState() { libraryObservationState_ = LibraryObservationState{}; }

STORAGE_PERSIST_MEM void requestLibraryObservationImpl() { requestImpl(); }

STORAGE_PERSIST_MEM void serviceLibraryObservationImpl() { serviceImpl(); }

}  // namespace StorageManagerInternal

STORAGE_PERSIST_MEM void StorageManager::requestLibraryObservation() {
    StorageManagerInternal::requestLibraryObservationImpl();
}

STORAGE_PERSIST_MEM void StorageManager::serviceLibraryObservation() {
    StorageManagerInternal::serviceLibraryObservationImpl();
}
