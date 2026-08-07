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
#include "NoteEditSessionUndo.h"
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
#include "Utils/NoteMovementUtils.h"
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

EDIT_MANAGER_IMPL_MEM NoteEditCurrentState& EditManager::noteEditCurrentStateMut() {
    return editSession.noteEditCurrentState;
}

EDIT_MANAGER_IMPL_MEM const NoteEditCurrentState& EditManager::noteEditCurrentState() const {
    return editSession.noteEditCurrentState;
}

#if NOTE_EDIT_PROJECTED_STORE_COMPAT
EDIT_MANAGER_IMPL_MEM MidiEventVec& EditManager::mutNoteEditSessionProjectionEventsCompat() {
    return editSession.store.mutEvents();
}

EDIT_MANAGER_IMPL_MEM MidiEventVec& EditManager::mutEditProjectionEventsCompat(Track& track) {
    if (editSession.active) {
        return mutNoteEditSessionProjectionEventsCompat();
    }
    return track.legacyMidiEventsFromCommitted();
}
#endif

EDIT_MANAGER_IMPL_MEM MidiEventVec& EditManager::sessionMidiEvents() {
#if NOTE_EDIT_PROJECTED_STORE_COMPAT
    return mutNoteEditSessionProjectionEventsCompat();
#else
    return editSession.store.mutEvents();
#endif
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
#if NOTE_EDIT_PROJECTED_STORE_COMPAT
        return mutEditProjectionEventsCompat(track);
#else
        return sessionMidiEvents();
#endif
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

