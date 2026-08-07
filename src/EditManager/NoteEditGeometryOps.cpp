//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <unordered_map>

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#endif

#include "EditEvent.h"
#include "EditManager.h"
#include "EditPass.h"
#include "Logger.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "NoteGeometryResolver.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/NoteUtils.h"

using DisplayNote = NoteUtils::DisplayNote;

EDIT_MANAGER_IMPL_MEM void EditManager::applyCreatedNoteOverlapGeometry(Track& track,
                                                                        const DisplayNote& createdNote) {
    if (!editSession.active || createdNote.noteId == kInvalidNoteId) {
        return;
    }
    const uint32_t loopLength = noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return;
    }

    rebuildNoteEditFocusForDisplayNote(track, createdNote);
    const uint32_t bracketTick = createdNote.startTick % loopLength;
    applySelectionFromGeometryEdit(track, bracketTick, createdNote.noteId);

    const uint8_t channel = track.getMidiChannel();
    NoteBaseline editedSpan{};
    if (!findLinearNoteSpanForNoteId(sessionMidiEvents(), createdNote.noteId, channel, editedSpan,
                                     createdNote.startTick, loopLength)) {
        editedSpan = {createdNote.note, createdNote.velocity, createdNote.startTick,
                      createdNote.endTick};
    }

    NoteBaseline priorLatch{};
    NoteGeometryResolver::resolveForCausingNote(track, *this, createdNote.noteId, editedSpan,
                                                priorLatch, bracketTick, createdNote.note);
}

EDIT_MANAGER_IMPL_MEM void EditManager::applyDeleteNoteOverlapRestore(Track& track) {
    if (!editSession.active || !editSession.focus.active) {
        return;
    }
    if (editSession.focus.changedOverlapNoteIds.empty()) {
        return;
    }

    EditedGeometry editedGeometry{};
    editedGeometry.selection = sessionState.selection;

    std::unordered_map<NoteId, NoteBaseline, NoteIdHash> priorLatchByNoteId;
    NoteGeometryResolver::resolve(track, *this, editedGeometry, priorLatchByNoteId, std::nullopt,
                                  true);
}

EDIT_MANAGER_IMPL_MEM bool EditManager::deleteSelectedNote(Track& track,
                                                           const NoteUtils::DisplayNoteVec& filteredNotes) {
    if (selectedNoteIdx < 0 && lastFader1SelectNoteId == kInvalidNoteId) {
        logger.info("MIDI Encoder: No note selected for deletion");
        return false;
    }

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const NoteEditFocus& focus = editSession.focus;
    const EditorSelection& selection = sessionState.selection;

    int resolvedIdx = selectedNoteIdx;
    NoteId deleteTargetNoteId = kInvalidNoteId;
    if (editorSelectionHasNote(selection)) {
        deleteTargetNoteId = selection.primaryNote;
    } else if (lastFader1SelectNoteId != kInvalidNoteId) {
        deleteTargetNoteId = lastFader1SelectNoteId;
    }
    if (deleteTargetNoteId != kInvalidNoteId) {
        const uint32_t selectedTick = editorSelectionHasNote(selection) ? selection.selectedTick
                                                                        : UINT32_MAX;
        resolvedIdx =
            selectedTick != UINT32_MAX
                ? filteredDisplayNoteIndexForNoteIdAndStart(
                      filteredNotes, deleteTargetNoteId, selectedTick, noteEditLoopStartTick(track),
                      noteEditLoopLengthTicks(track))
                : -1;
    }
    if (resolvedIdx < 0 || resolvedIdx >= static_cast<int>(filteredNotes.size())) {
        logger.info("MIDI Encoder: Selected note index out of range");
        return false;
    }
    if (deleteTargetNoteId == kInvalidNoteId) {
        deleteTargetNoteId = noteIdFromFilteredDisplayNote(filteredNotes, resolvedIdx);
    }

    const NoteUtils::DisplayNote selectedNote = filteredNotes[static_cast<size_t>(resolvedIdx)];

    const bool noteEditActive = editSession.active;
    beginGeometryMutation(track, NoteEditKind::Delete, false);
    const bool deleteTargetDiffersFromFocus =
        noteEditActive && focus.active && focus.movingNoteId != deleteTargetNoteId;
    if (deleteTargetDiffersFromFocus) {
        commitPendingOverlapNoteEdits(track);
        rebuildNoteEditFocusForDisplayNote(track, selectedNote);
    } else if (noteEditActive && !focus.active) {
        rebuildNoteEditFocusForDisplayNote(track, selectedNote);
    }
    commitAllPendingNoteEditActions(track);

    uint8_t notePitch = selectedNote.note;
    uint32_t noteStart = selectedNote.startTick;
    uint32_t noteEnd = selectedNote.endTick;
    const NoteUtils::DisplayNoteVec notesAfter = selectableDisplayNotesAtEditSelect(track);
    const int refreshedIdx =
        editorSelectionHasNote(selection)
            ? filteredDisplayNoteIndexForNoteIdAndStart(
                  notesAfter, deleteTargetNoteId, selection.selectedTick,
                  noteEditLoopStartTick(track), noteEditLoopLengthTicks(track))
            : -1;
    if (refreshedIdx >= 0 && refreshedIdx < static_cast<int>(notesAfter.size())) {
        const NoteUtils::DisplayNote& refreshed = notesAfter[static_cast<size_t>(refreshedIdx)];
        notePitch = refreshed.note;
        noteStart = refreshed.startTick;
        noteEnd = refreshed.endTick;
    } else {
        for (const NoteUtils::DisplayNote& n : notesAfter) {
            if (n.noteId == deleteTargetNoteId) {
                notePitch = n.note;
                noteStart = n.startTick;
                noteEnd = n.endTick;
                break;
            }
        }
    }

    logger.info("MIDI Encoder: Deleting note noteId=%lu pitch=%d, start=%lu, end=%lu",
                static_cast<unsigned long>(deleteTargetNoteId), notePitch, noteStart, noteEnd);

    const uint8_t channel = track.getMidiChannel();
  NoteEditCurrentState& currentState = editSession.noteEditCurrentState;
  if (!currentState.empty()) {
    const NoteEditCurrentNoteState* row = currentState.find(deleteTargetNoteId);
    if (row != nullptr && row->presence == NoteEditPresenceType::Added) {
      currentState.removeRow(deleteTargetNoteId);
    } else {
      currentState.markRowDeleted(deleteTargetNoteId);
    }
    refreshNoteEditSessionProjection(channel);
  } else {
    // NOTE_EDIT_PROJECTED_STORE_COMPAT: legacy projected-store delete until open always builds current state.
    auto& midiEvents = track.editAwareMidiEvents();
    MidiEvent* noteOnEvent = nullptr;
    for (MidiEvent& e : midiEvents) {
        if (e.type == midi::NoteOn && e.data.noteData.velocity > 0 &&
            e.data.noteData.note == notePitch && e.tick == noteStart) {
            noteOnEvent = &e;
            break;
        }
    }

    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent != nullptr) {
        noteOffEvent = NoteMovementUtils::findCorrespondingNoteOff(midiEvents, noteOnEvent,
                                                                   notePitch, noteStart, noteEnd);
    }

    int deletedCount = 0;
    auto eraseByPointer = [&](MidiEvent* needle) {
        if (needle == nullptr) {
            return;
        }
        for (auto it = midiEvents.begin(); it != midiEvents.end(); ++it) {
            if (&(*it) == needle) {
                midiEvents.erase(it);
                ++deletedCount;
                return;
            }
        }
    };
    if (noteOnEvent != nullptr || noteOffEvent != nullptr) {
        eraseByPointer(noteOffEvent);
        eraseByPointer(noteOnEvent);
    } else {
        auto it = midiEvents.begin();
        while (it != midiEvents.end()) {
            const bool matchOn =
                (it->type == midi::NoteOn && it->data.noteData.velocity > 0 &&
                 it->data.noteData.note == notePitch && it->tick == noteStart);
            const bool matchOff =
                ((it->type == midi::NoteOff ||
                  (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) &&
                 it->data.noteData.note == notePitch && it->tick == noteEnd);
            if (matchOn || matchOff) {
                it = midiEvents.erase(it);
                ++deletedCount;
            } else {
                ++it;
            }
        }
    }

    logger.info("MIDI Encoder: Deleted %d MIDI events for note", deletedCount);
  }

    applyDeleteNoteOverlapRestore(track);

    EditPass del{};
    del.passType = EditPassType::Note;
    del.actionType = EditActionType::Delete;
    del.propertyType = EditPropertyType::None;
    del.targetNoteId = deleteTargetNoteId;
    commitEditAction(track, EditPassVec{del});
    track.invalidateCaches();

    setSelectedNoteIdx(-1);
    rebuildNoteEditFocusAtSelect(track, -1);

    logger.info("MIDI Encoder: Note deleted, maintaining current edit mode");
    emitEditEvent(EditEvent::GeometryChanged);
    return true;
}

EDIT_MANAGER_IMPL_MEM bool EditManager::moveNoteToPosition(Track& track,
                                                           const NoteUtils::DisplayNote& currentNote,
                                                           uint32_t targetTick) {
    const NoteEditFocus& focus = editSession.focus;
    uint32_t fromStart = currentNote.startTick;
    if (focus.active && focus.last.pitch == currentNote.note &&
        focus.last.startTick == currentNote.startTick) {
        fromStart = focus.last.startTick;
    }
    if (fromStart == targetTick) {
        return false;
    }
#if defined(SESSION_CAPTURE)
    const uint32_t undoStartUs = micros();
#endif
    if (!beginGeometryMutation(track, NoteEditKind::Move, true)) {
#if defined(SESSION_CAPTURE)
        logGeomApplyUndo(false, micros() - undoStartUs, NoteEditKind::Move);
#endif
        logger.log(CAT_MIDI, LOG_WARNING,
                   "Note move aborted: session undo snapshot unavailable (heap reserve)");
        return false;
    }
#if defined(SESSION_CAPTURE)
    logGeomApplyUndo(true, micros() - undoStartUs, NoteEditKind::Move);
    const uint32_t focusStartUs = micros();
#endif
    const int32_t tickDifference =
        static_cast<int32_t>(targetTick) - static_cast<int32_t>(fromStart);

    logger.log(CAT_MIDI, LOG_DEBUG,
               "Note movement with overlap handling: from=%lu to=%lu difference=%ld overlapNotes=%zu",
               fromStart, targetTick, tickDifference, editSession.focus.overlapNotes.size());

    ensureNoteEditFocusForLiveEdit(track, currentNote);
#if defined(SESSION_CAPTURE)
    logGeomApplyFocus(micros() - focusStartUs, NoteEditKind::Move);
    const uint32_t pipelineStartUs = micros();
#endif
    if (focus.active) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Overlap move bridge: pitch=%d, start=%lu, end=%lu",
                   focus.last.pitch, static_cast<unsigned long>(focus.last.startTick),
                   static_cast<unsigned long>(focus.last.endTick));
    }

    uint32_t dummyStart = currentNote.startTick;
    uint32_t dummyEnd = currentNote.endTick;
    const bool applied = NoteMovementUtils::applyNoteEditChange(
        track, *this, NoteMovementUtils::NoteEditChangeKind::Move, currentNote, targetTick,
        static_cast<int>(tickDifference), 0, 0, 0, dummyStart, dummyEnd);
#if defined(SESSION_CAPTURE)
    logGeomApplyPipeline(micros() - pipelineStartUs, applied, NoteEditKind::Move);
#endif
    return applied;
}

EDIT_MANAGER_IMPL_MEM bool EditManager::changeNoteEndWithOverlapHandling(
    Track& track, const NoteUtils::DisplayNote& currentNote, uint32_t targetEndTick) {
    const NoteEditFocus& focus = editSession.focus;
    const uint32_t currentEnd = focus.active ? focus.last.endTick : currentNote.endTick;
    if (currentEnd == targetEndTick) {
        return false;
    }
#if defined(SESSION_CAPTURE)
    const uint32_t undoStartUs = micros();
#endif
    if (!beginGeometryMutation(track, NoteEditKind::Length, true)) {
#if defined(SESSION_CAPTURE)
        logGeomApplyUndo(false, micros() - undoStartUs, NoteEditKind::Length);
#endif
        logger.log(CAT_MIDI, LOG_WARNING,
                   "Note length change aborted: session undo snapshot unavailable (heap reserve)");
        return false;
    }
#if defined(SESSION_CAPTURE)
    logGeomApplyUndo(true, micros() - undoStartUs, NoteEditKind::Length);
    const uint32_t focusStartUs = micros();
#endif
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Note length change with overlap handling: pitch=%d, start=%lu, end %lu->%lu",
               currentNote.note, currentNote.startTick, currentNote.endTick, targetEndTick);
    ensureNoteEditFocusForLiveEdit(track, currentNote);
#if defined(SESSION_CAPTURE)
    logGeomApplyFocus(micros() - focusStartUs, NoteEditKind::Length);
    const uint32_t pipelineStartUs = micros();
#endif
    NoteMovementUtils::changeLengthWithOverlapHandling(track, *this, currentNote, targetEndTick);
#if defined(SESSION_CAPTURE)
    logGeomApplyPipeline(micros() - pipelineStartUs, true, NoteEditKind::Length);
#endif
    return true;
}
