//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <Arduino.h>

#include "EditManager.h"
#include "Globals.h"
#include "LoopContentResolution.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "TrackManager.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteUtils.h"
#include "Utils/SnapshotNoteConversion.h"

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

EDIT_MANAGER_IMPL_MEM const NoteUtils::DisplayNoteVec& EditManager::noteEditCommittedDisplayNotes(
    const Track& track) const {
    ensureNoteEditDisplayProjectionCachesBuilt(track);
    return noteEditCommittedDisplayCacheNotes_;
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
        noteEditCommittedDisplayCacheNotes_.clear();
        noteEditSelectableDisplayCacheNotes_.clear();
        return;
    }
    const NoteEditFocus& focus = editSession.focus;
    Loop& loop = const_cast<Loop&>(trackManager.getSelectedLoop(track));
    const uint8_t slot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    const uint32_t playbackRevision = loop.playbackRevision;
    const uint32_t visualRevision = loop.visualCache.revision;
    const uint16_t captureRevision = loop.captureDisplayRevision;
    const uint32_t previewRevision = sessionPreviewRevision_;
    const uint32_t displayFingerprint =
        noteEditDisplayCacheFingerprint(focus, &editSession.noteEditCurrentState);
    const bool committedBaseKeysMatch =
        previewRevision == noteEditSelectableDisplayCachePreviewRevision_ &&
        loopLength == noteEditSelectableDisplayCacheLoopLength_ &&
        playbackRevision == noteEditSelectableDisplayCachePlaybackRevision_ &&
        visualRevision == noteEditSelectableDisplayCacheVisualRevision_;
    const bool paintKeysMatch =
        displayFingerprint == noteEditSelectableDisplayCacheFingerprint_ &&
        selectedNoteIdx == noteEditSelectableDisplayCacheSelectedNoteIdx_ &&
        captureRevision == noteEditSelectableDisplayCacheCaptureRevision_;
    const DisplayWindowUtils::NoteEditProjectionCacheReuse cacheReuse =
        DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
            !noteEditCommittedDisplayCacheNotes_.empty(), committedBaseKeysMatch,
            !noteEditPaintDisplayCacheNotes_.empty(), paintKeysMatch);
    if (cacheReuse == DisplayWindowUtils::NoteEditProjectionCacheReuse::FullHit) {
        return;
    }

    if (cacheReuse == DisplayWindowUtils::NoteEditProjectionCacheReuse::RebuildCommittedBase) {
        // Snapshot / visual cache / pass materialize only. Live Capture is
        // capturePreview overlay on paint and overlap analyze — do not
        // rebuildDisplayNotesInWindow([0, loopLength)) here (`170818`).
        // Do not rematerialize EditSession.store.
        NoteUtils::DisplayNoteVec materializedCommittedBase;
        const NoteUtils::DisplayNoteVec* committedBase = nullptr;
        const DisplayWindowUtils::NoteEditCommittedBaseKind baseKind =
            DisplayWindowUtils::selectNoteEditCommittedBaseKind(
                loop.captureActive(), loop.visualCacheDirty, loop.publishedSnapshotIsCurrent());
        switch (baseKind) {
        case DisplayWindowUtils::NoteEditCommittedBaseKind::CaptureAwareGather:
        case DisplayWindowUtils::NoteEditCommittedBaseKind::Snapshot:
            if (const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot()) {
                LoopContentResolution::resolveDisplayNotes(*snapshot, materializedCommittedBase);
                committedBase = &materializedCommittedBase;
            }
            break;
        case DisplayWindowUtils::NoteEditCommittedBaseKind::PassMaterialize: {
            MidiEventVec flat;
            loop.passes.materializeToEventVector(flat, loopLength);
            materializedCommittedBase =
                NoteUtils::reconstructDisplayNotes(flat, loopLength, false, false);
            committedBase = &materializedCommittedBase;
            break;
        }
        case DisplayWindowUtils::NoteEditCommittedBaseKind::VisualCache:
            committedBase = &track.getVisualNotesForSlot(slot);
            break;
        }
        if (committedBase == nullptr) {
            committedBase = &track.getVisualNotesForSlot(slot);
        }
        noteEditCommittedDisplayCacheNotes_ = *committedBase;
        noteEditSelectableDisplayCachePreviewRevision_ = previewRevision;
        noteEditSelectableDisplayCacheLoopLength_ = loopLength;
        noteEditSelectableDisplayCachePlaybackRevision_ = playbackRevision;
        noteEditSelectableDisplayCacheVisualRevision_ = loop.visualCache.revision;
    }

    noteEditSelectableDisplayCacheFingerprint_ = displayFingerprint;
    noteEditSelectableDisplayCacheSelectedNoteIdx_ = selectedNoteIdx;
    noteEditSelectableDisplayCacheCaptureRevision_ = captureRevision;
    const NoteUtils::DisplayNoteVec* paintSource = &noteEditCommittedDisplayCacheNotes_;
    NoteUtils::DisplayNoteVec captureOverlaySource;
    if (loop.captureActive() && !loop.capturePreview.notes.empty()) {
        captureOverlaySource = noteEditCommittedDisplayCacheNotes_;
        for (const NoteUtils::DisplayNote& preview : loop.capturePreview.notes) {
            if (preview.noteId == kInvalidNoteId) {
                continue;
            }
            bool alreadyPresent = false;
            for (const NoteUtils::DisplayNote& existing : captureOverlaySource) {
                if (existing.noteId == preview.noteId) {
                    alreadyPresent = true;
                    break;
                }
            }
            if (!alreadyPresent) {
                captureOverlaySource.push_back(preview);
            }
        }
        paintSource = &captureOverlaySource;
    }
    noteEditPaintDisplayCacheNotes_ =
        projectNoteEditDisplayNotes(*paintSource,
                                    track.editAwareMidiEvents(), focus,
                                    track.getMidiChannel(), loopLength,
                                    &editSession.noteEditCurrentState);
    noteEditSelectableDisplayCacheNotes_ =
        filterSelectableDisplayNotes(noteEditPaintDisplayCacheNotes_,
                                               &editSession.noteEditCurrentState, focus,
                                               selectedNoteIdx);
}

EDIT_MANAGER_IMPL_MEM void EditManager::invalidateProjectedNoteEditDisplayCache() const {
    ++noteEditDisplayInvalidateEpoch_;
    noteEditDisplayImmediatePaintRequested_ = true;
    noteEditSelectableDisplayCachePreviewRevision_ = UINT32_MAX;
    noteEditSelectableDisplayCacheFingerprint_ = static_cast<uint32_t>(-1);
    noteEditSelectableDisplayCacheLoopLength_ = 0;
    noteEditSelectableDisplayCachePlaybackRevision_ = UINT32_MAX;
    noteEditSelectableDisplayCacheVisualRevision_ = UINT32_MAX;
    noteEditSelectableDisplayCacheCaptureRevision_ = 0xFFFF;
    noteEditSelectableDisplayCacheSelectedNoteIdx_ = -2;
    noteEditPaintDisplayCacheNotes_.clear();
    noteEditCommittedDisplayCacheNotes_.clear();
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

EDIT_MANAGER_IMPL_MEM bool EditManager::tryCollectPreparedOverlapHoldNoteIds(
    const Track& track, uint8_t pitch, uint32_t startTick, uint32_t endTick, NoteId excludeId,
    OverlapNoteIdSet& out, OverlapNoteIdObservation::OverlapCandidateQueryStats* stats) const {
    out.clear();
    const Loop& loop = trackManager.getSelectedLoop(track);
    const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
    if (!loop.publishedSnapshotIsCurrent() || !snapshot) {
        return false;
    }
    collectSnapshotAnalyzeOverlapNoteIds(*snapshot, pitch, startTick, endTick,
                                         noteEditLoopLengthTicks(track), excludeId, out, stats);
    if (stats != nullptr) {
        ++stats->identityLookupCount;
        stats->preparedHit = true;
    }
    return true;
}

EDIT_MANAGER_IMPL_MEM const NoteUtils::DisplayNoteVec& EditManager::visualCacheNotesForSelectedSlot(
    const Track& track) const {
    const uint8_t slot = trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex());
    return track.getVisualNotesForSlot(slot);
}

EDIT_MANAGER_IMPL_MEM void EditManager::ensureCurrentStateVisibleRowsFromVisualCache(Track& track) {
    if (!editSession.active) {
        return;
    }
    editSession.noteEditCurrentState.ensureVisibleRowsForDisplayNotes(
        visualCacheNotesForSelectedSlot(track));
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
    loop.copyNoteEditCommitBase(editSession.editPassIds, noteEditFocusMaterializedLoopEvents_,
                                editSession.openedOnCurrentSnapshot,
                                editSession.openedSnapshotRevision);
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
