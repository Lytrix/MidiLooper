//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManager.h"
#include "EditManagerInternal.h"
#include "EditNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "Loop.h"
#include "LooperState.h"
#include "Globals.h"
#include "TrackManager.h"
#include "MidiEvent.h"
#include "TrackUndo.h"
#include "StorageManager.h"
#include "Logger.h"
#include "Utils/NoteUtils.h"
#include "MidiConfig.h"
#include "ControlSurfaceManager.h"
#include "LoopEditManager.h"
#include "NoteEditFocus.h"
#include "EditApply.h"
#include "EditSession.h"
#include "NoteEditSessionState.h"
#include "NoteEditCurrentState.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/LoopTickNormalize.h"
#include "Utils/LoopEventValidation.h"
#include "ClockManager.h"
#include "DisplayManager.h"
#include "ApplyOwnedEditPassRows.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/SelectNavigation.h"
#include "TickPhase.h"
#include "Utils/ValidationUtils.h"
#include "Utils/Diagnostics.h"
#include "Utils/DiagnosticsEvents.h"
#include "Utils/NoteEditMem.h"
#include <algorithm>
#include <map>
#include <utility>
#include <vector>
#include <unordered_set>
#include <cmath>

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#endif

using DisplayNote = NoteUtils::DisplayNote;

#include "NoteEditFocus.h"
#include "EditPass.h"
#include "LoopPasses.h"

#if defined(__IMXRT1062__)
DMAMEM EditManager editManager;
#else
EditManager editManager;
#endif

EDIT_MANAGER_IMPL_MEM bool EditManager::isLengthBracketEditActive() const {
    return sessionState.kind == NoteEditKind::Length || isLengthEditingMode();
}

EDIT_MANAGER_IMPL_MEM uint32_t EditManager::noteEditLoopLengthTicks(const Track& track) const {
    if (isNoteEditActive()) {
        return trackManager.getSelectedLoop(track).loopLengthTicks;
    }
    return track.getLoopLength();
}

EDIT_MANAGER_IMPL_MEM uint32_t EditManager::noteEditLoopStartTick(const Track& track) const {
    if (isNoteEditActive()) {
        const Loop& loop = trackManager.getSelectedLoop(track);
        const uint32_t loopLength = loop.loopLengthTicks;
        return loopLength > 0 ? loop.loopStartTick % loopLength : 0;
    }
    const uint32_t loopLength = track.getLoopLength();
    return loopLength > 0 ? track.getLoopStartTick() % loopLength : 0;
}

EDIT_MANAGER_IMPL_MEM uint32_t EditManager::noteEditSelectNavigationLengthTicks(
    const Track& track) const {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (!isNoteEditActive() || loopLength == 0) {
        return loopLength;
    }
    const Loop& loop = trackManager.getSelectedLoop(track);
    uint32_t navLength = loopLength;
    const uint32_t contentLength = loop.committedBarAlignedContentLengthTicks();
    if (contentLength > 0 && contentLength < loopLength) {
        navLength = contentLength;
    }
    const uint8_t slot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    const NoteUtils::DisplayNoteVec& visualNotes = track.getVisualNotesForSlot(slot);
    uint32_t maxCommittedEndTick = 0;
    for (const NoteUtils::DisplayNote& note : visualNotes) {
        if (note.endTick > maxCommittedEndTick) {
            maxCommittedEndTick = note.endTick;
        }
    }
    if (maxCommittedEndTick > navLength && maxCommittedEndTick <= loopLength) {
        navLength = maxCommittedEndTick;
    }
    return navLength;
}

EDIT_MANAGER_IMPL_MEM uint32_t EditManager::clampNoteEditBracketPhaseTick(const Track& track,
                                                                          uint32_t phaseTick) const {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return 0;
    }
    const uint32_t navLength = noteEditSelectNavigationLengthTicks(track);
    const uint32_t modPhase = phaseTick % loopLength;
    if (navLength > 0 && navLength < loopLength && modPhase >= navLength) {
        return navLength > Config::TICKS_PER_16TH_STEP ? navLength - Config::TICKS_PER_16TH_STEP : 0;
    }
    return modPhase;
}

EDIT_MANAGER_IMPL_MEM const MidiEventVec& EditManager::noteEditSessionProjectionEvents() const {
    return editSession.store.readEvents();
}

EDIT_MANAGER_IMPL_MEM void EditManager::refreshNoteEditSessionProjection(uint8_t channel) {
    if (!editSession.active || editSession.sessionType != EditSessionType::Note) {
        return;
    }
    MidiEventVec& projection = editSession.store.mutEvents();
    editSession.noteEditCurrentState.projectToSessionStore(projection, channel);
    editSession.store.syncEventsToStore();
}

EDIT_MANAGER_IMPL_MEM bool EditManager::normalizeNoteEditClosureProjection(Track& track) {
    if (!editSession.active || editSession.sessionType != EditSessionType::Note ||
        editSession.noteEditCurrentState.empty() || !editSession.focus.active) {
        return false;
    }
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return false;
    }
    refreshNoteEditSessionProjection(channel);
    MidiEventVec& store = editSession.store.mutEvents();
    const std::unordered_set<NoteId> closure =
        buildEditClosureNoteIds(editSession.focus, store, channel, loopLength);
    if (closure.empty()) {
        return false;
    }
    LoopTickNormalize::NormalizeOptions microOptions;
    microOptions.closeOpenTails = false;
    const LoopTickNormalize::NormalizeResult normResult = LoopTickNormalize::normalize(
        store, loopLength, LoopTickNormalize::NormalizeScope::noteIds(closure), microOptions);
    if (normResult.wrapPairsMerged == 0 && normResult.synthOffsPromoted == 0 &&
        normResult.openTailsClosed == 0) {
        return false;
    }
    editSession.noteEditCurrentState.syncProjectingRowsFromSessionStore(store, channel);
    editSession.store.syncEventsToStore();
    return true;
}

EDIT_MANAGER_IMPL_MEM void EditManager::normalizeNoteEditSessionProjectionForCommit(Track& track) {
    if (!editSession.active || editSession.sessionType != EditSessionType::Note ||
        editSession.noteEditCurrentState.empty()) {
        return;
    }
    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }
    refreshNoteEditSessionProjection(channel);
    MidiEventVec& store = editSession.store.mutEvents();
    const std::unordered_set<NoteId> closure =
        buildEditClosureNoteIds(editSession.focus, store, channel, loopLength);
    if (!closure.empty()) {
        LoopTickNormalize::NormalizeOptions microOptions;
        microOptions.closeOpenTails = false;
        LoopTickNormalize::normalize(store, loopLength,
                                     LoopTickNormalize::NormalizeScope::noteIds(closure),
                                     microOptions);
    }
    LoopTickNormalize::normalizeAll(store, loopLength);
    editSession.noteEditCurrentState.syncProjectingRowsFromSessionStore(store, channel);
    editSession.store.syncEventsToStore();
}

EDIT_MANAGER_IMPL_MEM NoteEditCurrentState& EditManager::noteEditCurrentStateMut() {
    return editSession.noteEditCurrentState;
}

EDIT_MANAGER_IMPL_MEM const NoteEditCurrentState& EditManager::noteEditCurrentState() const {
    return editSession.noteEditCurrentState;
}

EDIT_MANAGER_IMPL_MEM MidiEventVec& EditManager::sessionMidiEvents() {
    return editSession.store.mutEvents();
}

EDIT_MANAGER_IMPL_MEM const MidiEventVec& EditManager::sessionMidiEvents() const {
    return noteEditSessionProjectionEvents();
}

EDIT_MANAGER_IMPL_MEM void EditManager::bumpSessionPreviewRevision() {
    ++sessionPreviewRevision_;
    invalidateProjectedNoteEditDisplayCache();
}

EDIT_MANAGER_IMPL_MEM void EditManager::bumpSessionPlaybackPreviewRevision() {
    ++sessionPlaybackPreviewRevision_;
}

EDIT_MANAGER_IMPL_MEM MidiEventVec& EditManager::editMidiEvents(Track& track) {
    if (editSession.active) {
        return sessionMidiEvents();
    }
    return track.legacyMidiEventsFromCommitted();
}

EDIT_MANAGER_IMPL_MEM const MidiEventVec& EditManager::editMidiEvents(const Track& track) const {
    if (editSession.active) {
        return noteEditSessionProjectionEvents();
    }
    return track.legacyMidiEventsFromCommitted();
}

EDIT_MANAGER_IMPL_MEM EditManager::EditManager() {
    currentState = nullptr; // Start with no state
}

EDIT_MANAGER_IMPL_MEM void EditManager::setSelectedNoteIdx(int idx) {
    if (selectedNoteIdx != idx) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection changed: %d -> %d", selectedNoteIdx, idx);
    }
    selectedNoteIdx = idx;
}

EDIT_MANAGER_IMPL_MEM void EditManager::setLastFader1SelectNoteId(NoteId noteId) {
    lastFader1SelectNoteId = noteId;
}

EDIT_MANAGER_IMPL_MEM void EditManager::clearLastFader1SelectNoteId() {
    lastFader1SelectNoteId = kInvalidNoteId;
}

EDIT_MANAGER_IMPL_MEM void EditManager::resetSelection() {
    if (selectedNoteIdx != -1) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note selection reset: %d -> -1", selectedNoteIdx);
    }
    selectedNoteIdx = -1;
    clearLastFader1SelectNoteId();
}

EDIT_MANAGER_IMPL_MEM void EditManager::emitEditEvent(EditEvent event) {
    if (editEventListener_ != nullptr) {
        editEventListener_->onEditEvent(event);
    }
}

