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
#include "CurrentWorkspaceStorage.h"
#include "ClockManager.h"
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
#include <cstdint>
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
    /// The published-root **publication identity** observed for each slot: validity plus a hash of the
    /// non-owning root identity, its publication revision and the committed-pass revision it covers.
    /// `contentRevisionIdentity` reads the pass side, so a root replaced under an unchanged pass
    /// identity would otherwise be invisible.
    uint8_t observedRootValid[kSlotCount]{};
    uint8_t observedRootCurrent[kSlotCount]{};
    uint32_t observedRootIdentityHash[kSlotCount]{};
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
///
/// This reads the **pass** side only. A published-root-backed slot has none of these fields, so its root
/// publication identity is tracked separately (`rootIdentityHashFor`) — otherwise a root replaced
/// between two slices would leave an unchanged revision hash behind it.
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

/// True when a published root is **installed** on the loop, regardless of whether it currently covers
/// the live committed-pass state. This is the LSV1 restore's content representation (it leaves
/// `Loop::passes` empty), and an installed-but-not-current root is still read by playback through the
/// stale-root overlay path, so it is content. Currency is reported separately (`rootIsCurrent`).
STORAGE_PERSIST_MEM bool rootContentInstalled(const Loop& loop) {
    return static_cast<bool>(loop.publishedSnapshot());
}

/// True when the installed root currently represents the live committed-pass state (DEC-073).
STORAGE_PERSIST_MEM bool rootContentIsCurrent(const Loop& loop) {
    return loop.publishedSnapshotIsCurrent();
}

/// A stable per-slot identity for the published root actually installed, over the presence, the root
/// object's non-owning identity and its publication revision. Zero means "no root". The revision is
/// mixed in beside the pointer because a freed root's address can be reused by a replacement. An
/// installed root is fingerprinted whether or not it is current: replacing a stale root is still a
/// change to the observed content.
STORAGE_PERSIST_MEM uint32_t rootIdentityHashFor(const Loop& loop) {
    if (!rootContentInstalled(loop)) {
        return 0;
    }
    uint32_t hash = 2166136261u;  // FNV-1a
    const auto mix = [&hash](uint32_t value) {
        hash ^= value;
        hash *= 16777619u;
    };
    mix(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(loop.publishedSnapshot().get())));
    mix(loop.publishedSnapshotRevision());
    mix(loop.committedPassRevision());
    mix(rootContentIsCurrent(loop) ? 1u : 0u);
    return hash != 0 ? hash : 1u;
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

    // Committed content lives in RAM, so it is observable even when SD admission is denied. It is
    // reported as two separate representations and one union: the pass-backed identity below, and the
    // valid published root installed by the LSV1 restore path (`publishedSnapshotIsCurrent()`).
    input.contentObserved = true;
    const Track& track = trackManager.getTrack(trackIndex);
    uint32_t observedRootIdentity = 0;
    if (track.loopsAllocated()) {
        const Loop& loop = track.getLoop(slot);
        input.hasPassContent = loop.passes.hasRecordPass() || !loop.passes.overdubPasses.empty();
        input.hasRootContent = rootContentInstalled(loop);
        input.rootIsCurrent = input.hasRootContent && rootContentIsCurrent(loop);
        input.hasCommittedContent = input.hasPassContent || input.hasRootContent;
        input.committedPassCount = static_cast<uint16_t>(loop.passes.capturePassCount());
        input.highestCommittedPassId = highestCommittedPassIdFor(loop);
        input.loopLengthTicks = loop.loopLengthTicks;
        input.committedEventCount = committedEventCountFor(loop);
        // Taken in the same observation as the content fields, from the same loop reference.
        observedRootIdentity = rootIdentityHashFor(loop);
    }

    input.selectionObserved = true;
    input.requiredForInitialPlayback = requiredForInitialPlayback(trackIndex, slot);

    libraryObservationState_.results[index] = LibraryObservation::classifySlot(input);
    libraryObservationState_.observedGeneration[index] = input.requestGeneration;
    libraryObservationState_.observedContentRevision[index] = contentRevisionIdentity(trackIndex, slot);
    libraryObservationState_.observedRootValid[index] = input.hasRootContent ? 1u : 0u;
    libraryObservationState_.observedRootCurrent[index] = input.rootIsCurrent ? 1u : 0u;
    libraryObservationState_.observedRootIdentityHash[index] = observedRootIdentity;
    ++libraryObservationState_.collected;
}

STORAGE_PERSIST_MEM void clearResults() {
    for (uint16_t i = 0; i < kSlotCount; ++i) {
        libraryObservationState_.results[i] = LibraryObservation::SlotResult{};
        libraryObservationState_.observedContentRevision[i] = 0;
        libraryObservationState_.observedGeneration[i] = 0;
        libraryObservationState_.observedRootValid[i] = 0;
        libraryObservationState_.observedRootCurrent[i] = 0;
        libraryObservationState_.observedRootIdentityHash[i] = 0;
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

/// Re-check one record immediately before it is emitted. A request-generation advance, a set-epoch change,
/// a content-revision change (a restore publication under an unchanged request identity, or an ordinary
/// capture/overdub/edit/clear commit) or a **published-root publication identity change** invalidates it,
/// so no emitted record mixes fields from two versions. The root identity is checked in addition to the
/// pass revision because a root-backed slot has no pass fields: replacing its root leaves
/// `contentRevisionIdentity` unchanged.
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
    // The root check must not allocate: `Track::getLoop` calls `ensureLoopsAllocated()` (even on the
    // const overload), which would itself change the observed content revision of an unallocated track
    // between slices and invalidate a record for the observation's own side effect.
    const bool rootChanged = [&]() {
        if (!trackManager.getTrack(trackIndex).loopsAllocated()) {
            return state.observedRootIdentityHash[index] != 0;
        }
        const Loop& loop = trackManager.getTrack(trackIndex).getLoop(slot);
        if (rootIdentityHashFor(loop) != state.observedRootIdentityHash[index]) {
            return true;
        }
        const bool rootNow = rootContentInstalled(loop);
        if ((rootNow ? 1u : 0u) != static_cast<uint32_t>(state.observedRootValid[index])) {
            return true;
        }
        // Currency is a separate observed bit (an installed root can stop covering the live
        // committed-pass state without being replaced).
        return rootNow && (rootContentIsCurrent(loop) ? 1u : 0u) !=
                              static_cast<uint32_t>(state.observedRootCurrent[index]);
    }();
    if (generationChanged || contentChanged || rootChanged) {
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
        result.contentObserved && result.hasCommittedContent ? 1 : 0,
        result.contentObserved && result.hasPassContent ? 1 : 0,
        result.contentObserved && result.hasRootContent ? 1 : 0,
        result.contentObserved && result.rootIsCurrent ? 1 : 0, result.committedPassCount,
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
                               report.counts.committedAtCheckpoint,
                               report.counts.passBackedAtCheckpoint,
                               report.counts.rootBackedAtCheckpoint,
                               window.admissionComplete ? 1 : 0,
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

#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
STORAGE_PERSIST_MEM const char* StorageManager::readSlotPayloadPageForHitl(
    uint8_t track, uint8_t slot, uint32_t offset, SlotPayloadPage& page) {
    using namespace StorageManagerInternal;
    page = SlotPayloadPage{};
    if (track >= Config::NUM_TRACKS || slot >= Config::MAX_LOOPS_PER_TRACK) return PSTR("invalid_slot");
    if (clockManager.isTransportRunning()) return PSTR("transport_active");
    if (!sdAdmissionGranted() || hasDeferredSaveWork()) return PSTR("busy");
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        const Track& candidate = trackManager.getTrack(t);
        if (candidate.isArmed() || candidate.isOverdubbing() || candidate.hasStopInFlight()) return PSTR("busy");
    }
    char path[64];
    if (!CurrentSetStorage::formatLoopSlotPath(path, sizeof(path), track, slot)) return PSTR("invalid_slot");
    if (!SD.exists(path)) return PSTR("missing");
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(path)) return PSTR("bad_token");
    File file = SD.open(path, FILE_READ);
    if (!file) return PSTR("io_error");
    page.fileSize = file.size();
    if (offset >= page.fileSize) { file.close(); return PSTR("range"); }
    if (CurrentWorkspaceStorage::fileStartsWithEpochHeader(file)) {
        CurrentWorkspaceStorage::EpochFileHeader header{};
        if (!CurrentWorkspaceStorage::readEpochFileHeader(storageIoFromFileRead(file), header)) {
            file.close(); return PSTR("bad_header");
        }
        page.bodyOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
    }
    page.setEpoch = restoreSetEpoch();
    const Track& owner = trackManager.getTrack(track);
    page.contentIdentity = owner.loopsAllocated() ? owner.getLoop(slot).committedPassRevision() : 0;
    page.rootIdentity = owner.loopsAllocated() ? rootIdentityHashFor(owner.getLoop(slot)) : 0;
    const size_t count = std::min(sizeof(page.bytes), static_cast<size_t>(page.fileSize - offset));
    const bool ok = file.seek(offset) && file.read(page.bytes, count) == static_cast<int>(count);
    file.close();
    if (!ok) return PSTR("io_error");
    page.count = static_cast<uint8_t>(count);
    return PSTR("ok");
}
#endif
