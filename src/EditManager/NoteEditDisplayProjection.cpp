//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <Arduino.h>

#include "EditManager.h"
#include "Globals.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "TrackManager.h"
#include "Utils/NoteUtils.h"

using DisplayNote = NoteUtils::DisplayNote;

EDIT_MANAGER_IMPL_MEM NoteUtils::DisplayNoteVec EditManager::selectableDisplayNotesAtEditSelect(
    const Track& track) const {
    if (isNoteEditActive()) {
        return filteredSelectableDisplayNotesForNoteEdit(track);
    }
    return track.getCachedNotes();
}

EDIT_MANAGER_IMPL_MEM NoteUtils::DisplayNoteVec EditManager::projectedNoteEditDisplayNotes(
    const Track& track) const {
    ensureNoteEditDisplayProjectionCachesBuilt(track);
    return noteEditPaintDisplayCacheNotes_;
}

EDIT_MANAGER_IMPL_MEM NoteUtils::DisplayNoteVec EditManager::filteredSelectableDisplayNotesForNoteEdit(
    const Track& track) const {
    ensureNoteEditDisplayProjectionCachesBuilt(track);
    return noteEditSelectableDisplayCacheNotes_;
}

EDIT_MANAGER_IMPL_MEM void EditManager::ensureNoteEditDisplayProjectionCachesBuilt(
    const Track& track) const {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (!editSession.active || loopLength == 0) {
        noteEditPaintDisplayCacheNotes_.clear();
        noteEditSelectableDisplayCacheNotes_.clear();
        return;
    }
    const NoteEditFocus& focus = editSession.focus;
    Loop& loop = const_cast<Loop&>(trackManager.getSelectedLoop(track));
    const uint32_t playbackRevision = loop.playbackRevision;
    const uint32_t previewRevision = sessionPreviewRevision_;
    const uint32_t displayFingerprint =
        noteEditDisplayCacheFingerprint(focus, &editSession.noteEditCurrentState);
    if (previewRevision == noteEditSelectableDisplayCachePreviewRevision_ &&
        displayFingerprint == noteEditSelectableDisplayCacheFingerprint_ &&
        loopLength == noteEditSelectableDisplayCacheLoopLength_ &&
        playbackRevision == noteEditSelectableDisplayCachePlaybackRevision_ &&
        !noteEditPaintDisplayCacheNotes_.empty()) {
        return;
    }

    NoteUtils::DisplayNoteVec committedBase;
    if (!loop.visualCache.notes.empty()) {
        committedBase.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
    } else {
        loop.ensureVisualCacheBuilt();
        committedBase.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
    }

    noteEditSelectableDisplayCachePreviewRevision_ = previewRevision;
    noteEditSelectableDisplayCacheFingerprint_ = displayFingerprint;
    noteEditSelectableDisplayCacheLoopLength_ = loopLength;
    noteEditSelectableDisplayCachePlaybackRevision_ = playbackRevision;
    noteEditPaintDisplayCacheNotes_ =
        projectNoteEditDisplayNotes(committedBase, track.editAwareMidiEvents(), focus,
                                    track.getMidiChannel(), loopLength,
                                    &editSession.noteEditCurrentState);
    noteEditSelectableDisplayCacheNotes_ =
        filterProjectingSelectableDisplayNotes(noteEditPaintDisplayCacheNotes_,
                                               &editSession.noteEditCurrentState);
}

EDIT_MANAGER_IMPL_MEM void EditManager::invalidateProjectedNoteEditDisplayCache() const {
    ++noteEditDisplayInvalidateEpoch_;
    noteEditDisplayImmediatePaintRequested_ = true;
    noteEditSelectableDisplayCachePreviewRevision_ = UINT32_MAX;
    noteEditSelectableDisplayCacheFingerprint_ = static_cast<uint32_t>(-1);
    noteEditSelectableDisplayCacheLoopLength_ = 0;
    noteEditSelectableDisplayCachePlaybackRevision_ = UINT32_MAX;
    noteEditPaintDisplayCacheNotes_.clear();
    noteEditSelectableDisplayCacheNotes_.clear();
}

EDIT_MANAGER_IMPL_MEM void EditManager::markNoteEditDisplayPainted() {
    noteEditDisplayPaintedEpoch_ = noteEditDisplayInvalidateEpoch_;
    noteEditDisplayImmediatePaintRequested_ = false;
}

EDIT_MANAGER_IMPL_MEM void EditManager::invalidateNoteEditDerivedCaches() {
    noteEditFocusMaterializeLoopRevision_ = UINT32_MAX;
    noteEditFocusMaterializeSlot_ = 255;
    noteEditFocusMaterializeLoopLength_ = 0;
    noteEditFocusMaterializedLoopEvents_.clear();
    invalidateProjectedNoteEditDisplayCache();
}

EDIT_MANAGER_IMPL_MEM const MidiEventVec& EditManager::materializedLoopEventsForNoteEditFocus(
    Track& track) {
    Loop& loop = trackManager.getSelectedLoop(track);
    const uint8_t slot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    const uint32_t revision = loop.playbackRevision;
    if (revision == noteEditFocusMaterializeLoopRevision_ && slot == noteEditFocusMaterializeSlot_ &&
        loopLength == noteEditFocusMaterializeLoopLength_ &&
        !noteEditFocusMaterializedLoopEvents_.empty()) {
        return noteEditFocusMaterializedLoopEvents_;
    }
    noteEditFocusMaterializeLoopRevision_ = revision;
    noteEditFocusMaterializeSlot_ = slot;
    noteEditFocusMaterializeLoopLength_ = loopLength;
    noteEditFocusMaterializedLoopEvents_.clear();
    loop.passes.materializeToEventVector(noteEditFocusMaterializedLoopEvents_, loopLength);
    return noteEditFocusMaterializedLoopEvents_;
}

EDIT_MANAGER_IMPL_MEM DisplayNote EditManager::liveEditDisplayNoteAtSelect(const Track& track) const {
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (isNoteEditActive() && editSession.focus.active && loopLength > 0) {
        const bool driverValid = !editSession.noteEditCurrentState.empty()
                                     ? isLiveEditDriverValidFromCurrentState(
                                           sessionState.selection, editSession.focus,
                                           editSession.noteEditCurrentState)
                                     : isLiveEditDriverValid(sessionState.selection,
                                                             editSession.focus, sessionMidiEvents(),
                                                             track.getMidiChannel(), loopLength);
        if (driverValid &&
            editorSelectionMatchesDriverNote(sessionState.selection, editSession.focus.movingNoteId)) {
            const NoteBaseline& last = editSession.focus.last;
            return {editSession.focus.movingNoteId, last.pitch, last.velocity, last.startTick,
                    last.endTick};
        }
    }

    // Stage 8 / C5: fader + snapshot consumers use paint-cache participant span (same as grid).
    if (isNoteEditActive() && editorSelectionHasNote(sessionState.selection)) {
        const NoteUtils::DisplayNoteVec& paint = projectedNoteEditDisplayNotes(track);
        for (const NoteUtils::DisplayNote& dn : paint) {
            if (dn.noteId == sessionState.selection.primaryNote) {
                return dn;
            }
        }
    }

    const int idx = getSelectedNoteIdx();
    const NoteUtils::DisplayNoteVec& notes = selectableDisplayNotesAtEditSelect(track);
    if (idx < 0 || idx >= static_cast<int>(notes.size())) {
        return {};
    }
    return notes[static_cast<size_t>(idx)];
}

EDIT_MANAGER_IMPL_MEM void EditManager::scheduleDeferredNoteEditDisplayRefresh() {
    deferredNoteEditDisplayRefreshPending_ = true;
    deferredNoteEditDisplayRefreshArmedAtMs_ = millis();
}

EDIT_MANAGER_IMPL_MEM void EditManager::flushDeferredNoteEditDisplayRefresh(Track& track) {
    if (!deferredNoteEditDisplayRefreshPending_) {
        return;
    }
    deferredNoteEditDisplayRefreshPending_ = false;
    bumpSessionPlaybackPreviewRevision();
    track.getActiveLoop().playbackOrderDirty = true;
    track.invalidatePlaybackCaches();
}

EDIT_MANAGER_IMPL_MEM void EditManager::processDeferredNoteEditDisplayRefresh(Track& track) {
    if (!deferredNoteEditDisplayRefreshPending_) {
        return;
    }
    const uint32_t now = millis();
    if (now - deferredNoteEditDisplayRefreshArmedAtMs_ < kDeferredNoteEditPlaybackRefreshIdleMs) {
        return;
    }
    flushDeferredNoteEditDisplayRefresh(track);
}
