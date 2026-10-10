// Copyright (c) 2025 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"
#include "EditManager.h"
#include "TrackManager.h"
#include "TickPhase.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/SnapshotNoteConversion.h"

#include <algorithm>
#include <climits>

EDIT_MANAGER_IMPL_MEM bool EditManager::defaultSelectionPreparationContextMatches(
    const Track& track) const {
    const auto& p = selectionPreparation_;
    if (&track != &trackManager.getSelectedTrack() ||
        p.track != trackManager.getSelectedTrackIndex() ||
        p.slot != trackManager.getSelectedSlotIndex(p.track)) return false;
    if (p.loop) {
        if (!track.loopsAllocated()) return false;
        const Loop& selected = trackManager.getSelectedLoop(track);
        if (p.loop != &selected || p.loopId != selected.loopId) return false;
    }
    return p.sessionActive == editSession.active && p.sessionType == editSession.sessionType &&
        p.preview == sessionPreviewRevision_ && p.playbackPreview == sessionPlaybackPreviewRevision_ &&
        p.priorNote == sessionState.selection.primaryNote && p.priorTick == sessionState.selection.selectedTick &&
        p.priorIndex == selectedNoteIdx && p.priorState == currentState;
}

EDIT_MANAGER_IMPL_MEM bool EditManager::defaultSelectionPreparationInputSupported(
    const Track& track) const {
    if (!editSession.store.empty() || editSession.store.hasDirtyEventValues() ||
        !editSession.noteEditCurrentState.empty() || editSession.focus.active ||
        !editSession.applyOwnedEditPassRows.empty()) return false;
    if (!track.loopsAllocated()) return true;
    const Loop& loop = trackManager.getSelectedLoop(track);
    // A stopped dirty-cache getter would rebuild. This primitive only consumes
    // prepared sources; it must not smuggle that rebuild into an owner unit.
    return !loop.captureActive() && (track.isPlaying() || !loop.visualCacheDirty);
}

EDIT_MANAGER_IMPL_MEM bool EditManager::defaultSelectionPreparationSourceMatches(
    const Track& track) const {
    const auto& p = selectionPreparation_;
    if (!track.loopsAllocated() || !p.root) return false;
    const Loop& loop = trackManager.getSelectedLoop(track);
    return loop.publishedSnapshotIsCurrent() && loop.publishedSnapshotIdentity() == p.root &&
        loop.publishedSnapshotRevision() == p.rootRevision &&
        track.getPlaybackGeneration() == p.trackGeneration && loop.playbackRevision == p.playbackRevision &&
        loop.visualCache.revision == p.visualRevision && loop.visualCacheDirty == p.visualDirty &&
        loop.captureContentGeneration() == p.captureGeneration && loop.loopLengthTicks == p.length &&
        loop.loopStartTick == p.origin && loop.startLoopTick == p.start;
}

EDIT_MANAGER_IMPL_MEM void EditManager::bindDefaultSelectionPreparationSource(Track& track) {
    auto& p = selectionPreparation_;
    const Loop& loop = trackManager.getSelectedLoop(track);
    p.loop = &loop;
    p.loopId = loop.loopId;
    p.root = loop.publishedSnapshotIdentity();
    p.rootRevision = loop.publishedSnapshotRevision();
    p.trackGeneration = track.getPlaybackGeneration();
    p.playbackRevision = loop.playbackRevision;
    p.visualRevision = loop.visualCache.revision;
    p.visualDirty = loop.visualCacheDirty;
    p.captureGeneration = loop.captureContentGeneration();
    p.length = loop.loopLengthTicks;
    p.origin = loop.loopStartTick;
    p.start = loop.startLoopTick;
    p.snapshotSource = DisplayWindowUtils::shouldResolveNoteEditCommittedBaseFromSnapshot(
        p.visualDirty, true);
    p.hasCommitted = false;
    p.reselecting = false;
    p.pass = p.chunk = p.visual = p.ordinal = 0;
    p.lastCommittedTick = p.maxVisualEnd = p.navLength = 0;
    p.bracket = p.nearestTarget = 0;
    p.bestDistance = p.length;
    p.exactIndex = p.nearestIndex = -1;
    p.exact = p.nearest = p.candidate = {};
    p.rows = p.prior = p.candidatePosition = {};
    p.prepared = {};
    p.phase = SelectionPreparationPhase::Chunks;
}

EDIT_MANAGER_IMPL_MEM EditManager::SelectionPreparationResult
EditManager::beginDefaultNoteEditSelectionPreparation(Track& track, uint32_t transportTick) {
    if (&track != &trackManager.getSelectedTrack()) return SelectionPreparationResult::Superseded;
    cancelDefaultNoteEditSelectionPreparation();
    auto& p = selectionPreparation_;
    p.track = trackManager.getSelectedTrackIndex();
    p.slot = trackManager.getSelectedSlotIndex(p.track);
    p.loop = track.loopsAllocated() ? &trackManager.getSelectedLoop(track) : nullptr;
    p.loopId = p.loop ? p.loop->loopId : kInvalidLoopId;
    p.transportTick = transportTick;
    p.sessionActive = editSession.active;
    p.sessionType = editSession.sessionType;
    p.preview = sessionPreviewRevision_;
    p.playbackPreview = sessionPlaybackPreviewRevision_;
    p.priorNote = sessionState.selection.primaryNote;
    p.priorTick = sessionState.selection.selectedTick;
    p.priorIndex = selectedNoteIdx;
    p.priorState = currentState;
    p.phase = SelectionPreparationPhase::Source;
    if (!defaultSelectionPreparationInputSupported(track)) {
        p.phase = SelectionPreparationPhase::Unsupported;
        return SelectionPreparationResult::Unsupported;
    }
    return track.loopsAllocated() && trackManager.getSelectedLoop(track).publishedSnapshotIsCurrent()
        ? SelectionPreparationResult::Progress : SelectionPreparationResult::WaitingForSource;
}

EDIT_MANAGER_IMPL_MEM EditManager::SelectionSnapshotRead EditManager::stepSelectionSnapshotPosition(
    const LoopSnapshot& root, SelectionSnapshotPosition& position, NoteUtils::DisplayNote& out) {
    const uint32_t length = root.loopLengthTicks();
    const uint32_t bars = length ? (length - 1) / Config::TICKS_PER_BAR + 1 : 0;
    if (position.bar >= bars) return SelectionSnapshotRead::Complete;
    if (position.pitch >= kLoopSnapshotPitchCount) {
        ++position.bar;
        position.pitch = 0;
        position.pageIndex = 0;
        position.entry = 0;
        position.loaded = false;
        return SelectionSnapshotRead::Progress;
    }
    // Reacquire from the root every time. The existing membership cap bounds
    // this prefix to at most 255 links; no borrowed page survives a yield.
    if (position.pageIndex >= kSnapshotMembershipMaxPages) return SelectionSnapshotRead::Unsupported;
    const auto head = root.noteMembership(static_cast<uint8_t>(position.pitch), position.bar);
    const auto* page = head.get();
    for (uint16_t hop = 0; hop < position.pageIndex && page; ++hop) page = page->next.get();
    if (!position.loaded || !page) {
        position.entry = 0;
        position.loaded = true;
        if (!page) {
            ++position.pitch;
            position.pageIndex = 0;
            position.loaded = false;
        }
        return SelectionSnapshotRead::Progress;
    }
    if (page->count > kSnapshotMembershipPageCapacity) return SelectionSnapshotRead::Unsupported;
    if (position.entry >= page->count) {
        position.entry = 0;
        if (page->next) ++position.pageIndex;
        else {
            ++position.pitch;
            position.pageIndex = 0;
            position.loaded = false;
        }
        return SelectionSnapshotRead::Progress;
    }
    const auto& reference = page->references[position.entry++];
    if (!reference) return SelectionSnapshotRead::Progress;
    out = displayNoteFromSnapshotNote(*reference);
    return SelectionSnapshotRead::Row;
}

EDIT_MANAGER_IMPL_MEM void EditManager::reduceDefaultSelectionCandidate() {
    auto& p = selectionPreparation_;
    const uint32_t tick = SelectNavigation::noteRelativeTick(p.candidate.startTick, p.origin, p.length);
    if (p.exactIndex < 0 && tick == p.bracket) {
        p.exactIndex = static_cast<int>(p.ordinal);
        p.exact = p.candidate;
    }
    const uint32_t distance = std::min((tick + p.length - p.nearestTarget) % p.length,
                                       (p.nearestTarget + p.length - tick) % p.length);
    if (p.nearestIndex < 0 || distance < p.bestDistance) {
        p.nearestIndex = static_cast<int>(p.ordinal);
        p.nearest = p.candidate;
        p.bestDistance = distance;
    }
    ++p.ordinal;
    p.phase = SelectionPreparationPhase::Rows;
}

EDIT_MANAGER_IMPL_MEM EditManager::SelectionPreparationResult
EditManager::stepDefaultNoteEditSelectionPreparation(Track& track) {
    auto& p = selectionPreparation_;
    using Phase = SelectionPreparationPhase;
    using Result = SelectionPreparationResult;
    if (p.phase == Phase::Idle) return Result::Idle;
    if (p.phase == Phase::Superseded) return Result::Superseded;
    if (p.phase == Phase::Unsupported) return Result::Unsupported;
    if (!defaultSelectionPreparationContextMatches(track)) {
        p.rows = p.prior = p.candidatePosition = {};
        p.prepared = {};
        p.phase = Phase::Superseded;
        return Result::Superseded;
    }
    if (!defaultSelectionPreparationInputSupported(track)) {
        p.rows = p.prior = p.candidatePosition = {};
        p.prepared = {};
        p.phase = Phase::Unsupported;
        return Result::Unsupported;
    }
    if (p.phase == Phase::Complete && defaultSelectionPreparationSourceMatches(track)) return Result::Complete;
#if defined(PIO_UNIT_TEST_NATIVE)
    ++p.units;
#endif
    if (p.phase == Phase::Source || !defaultSelectionPreparationSourceMatches(track)) {
        // Discard coordinates before touching any potentially retired membership.
        p.rows = p.prior = p.candidatePosition = {};
        p.prepared = {};
        p.phase = Phase::Source;
        if (!track.loopsAllocated()) return Result::WaitingForSource;
        const Loop& loop = trackManager.getSelectedLoop(track);
        if (!loop.publishedSnapshotIsCurrent()) return Result::WaitingForSource;
        const auto root = loop.publishedSnapshot();
        if (!root || !loop.loopLengthTicks || root->loopLengthTicks() != loop.loopLengthTicks ||
            root->loopId() != loop.loopId) {
            p.phase = Phase::Unsupported;
            return Result::Unsupported;
        }
        bindDefaultSelectionPreparationSource(track);
        return Result::Progress;
    }
    const Loop& loop = trackManager.getSelectedLoop(track);
    if (p.phase == Phase::Chunks) {
        const CommittedChunkIdList* chunks = nullptr;
        if (p.pass == 0) {
            if (loop.passes.hasRecordPass() && loop.passes.recordPass.state == CapturePassState::Active)
                chunks = &loop.passes.recordPass.committedChunkIds;
        } else if (p.pass <= loop.passes.overdubPasses.size()) {
            const auto& pass = loop.passes.overdubPasses[p.pass - 1];
            if (pass.state == CapturePassState::Active) chunks = &pass.committedChunkIds;
        } else {
            p.phase = Phase::VisualExtent;
            return Result::Progress;
        }
        if (chunks && !chunks->empty()) p.hasCommitted = true;
        if (!chunks || p.chunk >= chunks->size()) {
            ++p.pass;
            p.chunk = 0;
            return Result::Progress;
        }
        uint32_t first = 0, last = 0;
        if (LoopEventStore::chunkTickSpan((*chunks)[p.chunk++], first, last))
            p.lastCommittedTick = std::max(p.lastCommittedTick, last);
        return Result::Progress;
    }
    if (p.phase == Phase::VisualExtent) {
        if (p.visual < loop.visualCache.notes.size()) {
            p.maxVisualEnd = std::max(p.maxVisualEnd, loop.visualCache.notes[p.visual++].endTick);
            return Result::Progress;
        }
        p.navLength = p.length;
        if (p.hasCommitted && p.lastCommittedTick) {
            const uint32_t bars = p.lastCommittedTick / Config::TICKS_PER_BAR;
            const uint32_t rem = p.lastCommittedTick % Config::TICKS_PER_BAR;
            uint32_t contentLength = (bars + 1) * Config::TICKS_PER_BAR;
            if (rem <= Config::TICKS_PER_BAR / 6)
                contentLength = (bars ? bars : 1) * Config::TICKS_PER_BAR;
            else if (p.lastCommittedTick < Config::TICKS_PER_BAR / 2)
                contentLength = Config::TICKS_PER_BAR;
            if (contentLength && contentLength < p.navLength) p.navLength = contentLength;
        }
        if (p.maxVisualEnd > p.navLength && p.maxVisualEnd <= p.length) p.navLength = p.maxVisualEnd;
        const auto clamp = [&p](uint32_t phase) {
            phase %= p.length;
            if (p.navLength && p.navLength < p.length && phase >= p.navLength)
                return p.navLength > Config::TICKS_PER_16TH_STEP
                    ? p.navLength - Config::TICKS_PER_16TH_STEP : 0u;
            return phase;
        };
        p.bracket = SelectNavigation::noteRelativeTick(
            clamp(tickPhaseInLoop(p.transportTick, p.start, p.length)), p.origin, p.length);
        p.nearestTarget = clamp(p.bracket);
        p.visual = 0;
        p.phase = Phase::Rows;
        return Result::Progress;
    }
    if (p.phase == Phase::Reduce) {
        if (p.ordinal > static_cast<size_t>(INT_MAX)) {
            p.phase = Phase::Unsupported;
            return Result::Unsupported;
        }
        reduceDefaultSelectionCandidate();
        return Result::Progress;
    }
    const auto root = loop.publishedSnapshot(); // Loop remains the final owner throughout this unit.
    if (p.phase == Phase::Deduplicate) {
        const auto& at = p.candidatePosition;
        if (p.prior.bar == at.bar && p.prior.pitch == at.pitch && p.prior.pageIndex == at.pageIndex &&
            p.prior.entry == at.entry && p.prior.loaded == at.loaded) {
            p.phase = Phase::Reduce;
            return Result::Progress;
        }
        NoteUtils::DisplayNote prior{};
        const auto read = stepSelectionSnapshotPosition(*root, p.prior, prior);
        if (read == SelectionSnapshotRead::Row && prior.noteId == p.candidate.noteId &&
            prior.startTick == p.candidate.startTick) p.phase = Phase::Rows;
        else if (read == SelectionSnapshotRead::Complete || read == SelectionSnapshotRead::Unsupported) {
            p.phase = Phase::Unsupported; // malformed/unreachable candidate position
            return Result::Unsupported;
        }
        return Result::Progress;
    }
    bool complete = false;
    if (p.snapshotSource) {
        p.candidatePosition = p.rows;
        const auto read = stepSelectionSnapshotPosition(*root, p.rows, p.candidate);
        if (read == SelectionSnapshotRead::Progress) return Result::Progress;
        if (read == SelectionSnapshotRead::Unsupported) {
            p.phase = Phase::Unsupported;
            return Result::Unsupported;
        }
        complete = read == SelectionSnapshotRead::Complete;
        if (!complete) {
            p.prior = {};
            p.phase = p.candidate.noteId == kInvalidNoteId ? Phase::Reduce : Phase::Deduplicate;
            return Result::Progress;
        }
    } else {
        complete = p.visual >= loop.visualCache.notes.size();
        if (!complete) {
            p.candidate = loop.visualCache.notes[p.visual++];
            p.phase = Phase::Reduce;
            return Result::Progress;
        }
    }
    p.prepared.inventoryCount = p.ordinal;
    p.prepared.selectedTick = p.bracket;
    if (p.ordinal) {
        // An invalid exact identity resolves to index -1 in the legacy UI and
        // then retries nearest; do not silently exclude invalid inventory rows.
        const bool exact = !p.reselecting && p.exactIndex >= 0 && p.exact.noteId != kInvalidNoteId;
        const auto& winner = exact ? p.exact : p.nearest;
        p.prepared.selectedTick = SelectNavigation::noteRelativeTick(winner.startTick, p.origin, p.length);
        p.prepared.primaryNote = winner.noteId;
        if (winner.noteId != kInvalidNoteId)
            p.prepared.selectedNoteIdx = exact ? p.exactIndex : p.nearestIndex;
        if (!p.reselecting && p.priorState != &selectNoteState &&
            p.navLength && p.navLength < p.length && p.prepared.selectedTick >= p.navLength) {
            // The real Select onEnter reclamps this landing, then retries nearest.
            // A phase transition is one grant; no first-pass receipt may escape.
            p.nearestTarget = clampNoteEditBracketPhaseTickForNavigation(
                p.prepared.selectedTick, p.length, p.navLength);
            p.reselecting = true;
            p.visual = p.ordinal = 0;
            p.rows = p.prior = p.candidatePosition = {};
            p.exactIndex = p.nearestIndex = -1;
            p.exact = p.nearest = p.candidate = {};
            p.bestDistance = p.length;
            p.prepared = {};
            p.phase = Phase::Rows;
            return Result::Progress;
        }
    }
    p.phase = Phase::Complete;
    return Result::Complete;
}

EDIT_MANAGER_IMPL_MEM const EditManager::PreparedNoteEditSelection*
EditManager::preparedDefaultNoteEditSelection(const Track& track) const {
    if (selectionPreparation_.phase != SelectionPreparationPhase::Complete ||
        !defaultSelectionPreparationContextMatches(track) ||
        !defaultSelectionPreparationInputSupported(track) ||
        !defaultSelectionPreparationSourceMatches(track)) return nullptr;
    return &selectionPreparation_.prepared;
}
