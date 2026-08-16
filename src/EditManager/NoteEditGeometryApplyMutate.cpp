//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Logger.h"
#include "Globals.h"
#ifndef PIO_UNIT_TEST_NATIVE
#include "DisplayManager.h"
#endif
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/MidiEventUtils.h"
#include "NoteEditGeometryApply.h"
#include "NoteEditGeometryApplyInternal.h"
#include "NoteGeometryResolver.h"
#if defined(SESSION_CAPTURE)
#include "EditManagerInternal.h"
#endif
#include "Utils/IntervalProjection.h"
#include "Utils/DebugSessionCapture.h"
#include <algorithm>
#include <map>
#include "Utils/NoteEditMem.h"

namespace NoteEditGeometryApply {


NOTE_EDIT_MEM void finalReconstructAndSelect(Track& track,
                              MidiEventVec& midiEvents,
                              EditManager& manager,
                              uint8_t movingNotePitch,
                              uint32_t newStart,
                              uint32_t newEnd,
                              uint32_t loopLength,
                              uint32_t selectedTick,
                              bool refreshPlaybackPreview) {
    NoteUtils::sortMidiEventsChronologically(midiEvents);
    int newSelectedIdx = -1;
    const uint32_t displayNewEnd = noteEditGeometryApplyStorageTickToDisplayPhase(newEnd, loopLength);

    if (manager.isNoteEditActive()) {
        const NoteEditFocus& focus = manager.getEditSession().focus;
        const EditorSelection& selection = manager.getNoteEditSessionState().selection;
        const NoteUtils::DisplayNoteVec filtered =
            manager.selectableDisplayNotesAtEditSelect(track);

        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        const bool lengthBracket = manager.isLengthBracketEditActive();
        const NoteEditKind sessionKind = manager.getNoteEditSessionState().kind;
        const bool geometryMutation =
            isGeometryEditKind(sessionKind) && sessionKind != NoteEditKind::Select;
        if (editorSelectionHasNote(selection)) {
            if (geometryMutation && focus.active &&
                focus.movingNoteId == selection.primaryNote) {
                const uint32_t storageBracketTick =
                    lengthBracket ? focus.last.endTick : focus.last.startTick;
                const uint32_t displayBracket = noteEditGeometryApplyBracketDisplayTickFromStorage(
                    storageBracketTick, loopStartTick, loopLength);
                newSelectedIdx = filteredDisplayNoteIndexForNoteIdAndStart(
                    filtered, focus.movingNoteId, displayBracket, loopStartTick, loopLength);
            }
            if (newSelectedIdx < 0) {
                newSelectedIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
                    selection, filtered, loopStartTick, loopLength, lengthBracket);
            }
            if (newSelectedIdx < 0 && focus.active &&
                focus.movingNoteId == selection.primaryNote) {
                EditorSelection retrySelection = selection;
                const bool lengthBracket = manager.isLengthBracketEditActive();
                const uint32_t storageBracketTick =
                    lengthBracket ? focus.last.endTick : focus.last.startTick;
                retrySelection.selectedTick = noteEditGeometryApplyBracketDisplayTickFromStorage(
                    storageBracketTick, loopStartTick, loopLength);
                newSelectedIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
                    retrySelection, filtered, loopStartTick, loopLength, lengthBracket);
                if (newSelectedIdx >= 0) {
                    manager.applySelectionFromGeometryEdit(track, retrySelection.selectedTick,
                                                           retrySelection.primaryNote);
                }
            }
        }
    } else {
        const std::vector<NoteUtils::DisplayNote> finalNotes =
            NoteUtils::reconstructNotes(midiEvents, loopLength);
        for (int i = 0; i < static_cast<int>(finalNotes.size()); ++i) {
            if (finalNotes[static_cast<size_t>(i)].note == movingNotePitch &&
                finalNotes[static_cast<size_t>(i)].startTick == newStart &&
                finalNotes[static_cast<size_t>(i)].endTick == displayNewEnd) {
                newSelectedIdx = i;
                break;
            }
        }
    }

    const bool noteEditActive = manager.isNoteEditActive();
    bool geometrySelectionFromFocus = false;
    if (noteEditActive) {
        const NoteEditKind sessionKind = manager.getNoteEditSessionState().kind;
        const bool geometryMutation =
            isGeometryEditKind(sessionKind) && sessionKind != NoteEditKind::Select;
        const NoteEditFocus& focus = manager.getEditSession().focus;
        const EditorSelection& selection = manager.getNoteEditSessionState().selection;
        geometrySelectionFromFocus =
            geometryMutation && focus.active && editorSelectionHasNote(selection) &&
            focus.movingNoteId == selection.primaryNote;
    }

    if (!geometrySelectionFromFocus) {
        if (newSelectedIdx >= 0) {
            const int oldSelectedIdx = manager.getSelectedNoteIdx();
            manager.setSelectedNoteIdx(newSelectedIdx);
            logger.log(CAT_MIDI, LOG_DEBUG, "Updated selectedNoteIdx: %d -> %d (note at new position)",
                       oldSelectedIdx, newSelectedIdx);
            if (noteEditActive &&
                editorSelectionHasNote(manager.getNoteEditSessionState().selection)) {
                manager.setSelectedTick(manager.getNoteEditSessionState().selection.selectedTick);
            } else {
                manager.setSelectedTick(selectedTick);
            }
        } else if (noteEditActive) {
            if (editorSelectionHasNote(manager.getNoteEditSessionState().selection)) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Keeping selectedNoteIdx %d (moving note not in filtered list)",
                           manager.getSelectedNoteIdx());
                manager.setSelectedTick(manager.getNoteEditSessionState().selection.selectedTick);
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find moved note in filtered list");
                manager.setSelectedTick(selectedTick);
            }
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find moved note in filtered list");
            manager.setSelectedTick(selectedTick);
        }
        manager.syncSelectedNoteIdxToFilteredInventory(track);
    } else {
        const NoteEditFocus& focus = manager.getEditSession().focus;
        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        const bool lengthBracket = manager.isLengthBracketEditActive();
        const uint32_t storageBracketTick =
            lengthBracket ? focus.last.endTick : focus.last.startTick;
        const uint32_t displayBracket = noteEditGeometryApplyBracketDisplayTickFromStorage(
            storageBracketTick, loopStartTick, loopLength);
        manager.applySelectionFromGeometryEdit(track, displayBracket, focus.movingNoteId);
    }
    track.invalidateCaches(refreshPlaybackPreview);
#ifndef PIO_UNIT_TEST_NATIVE
    displayManager.requestNoteInfoRefresh(track);
#endif
}

namespace {

// Non-session pitch path: loop view or before NOTE_EDIT session uses NoteGeometryResolver.
// Active NOTE_EDIT pitch uses NoteGeometryResolver::resolveForCausingNote (Phase 4.10g).
NOTE_EDIT_MEM bool applySimplePitchChange(MidiEventVec& midiEvents, EditManager& manager,
                                          Track& track, NoteEditFocus& focus, uint8_t channel,
                                          uint8_t currentNoteValue, uint8_t newNoteValue,
                                          uint32_t noteStart, uint32_t noteEnd,
                                          uint32_t loopLength) {
  if (focus.movingNoteId != kInvalidNoteId) {
    NoteBaseline moverSpan;
    if (findLinearNoteSpanForNoteId(midiEvents, focus.movingNoteId, channel, moverSpan,
                                    focus.last.startTick, loopLength)) {
      noteStart = moverSpan.startTick;
      noteEnd = moverSpan.endTick;
      focus.last.startTick = moverSpan.startTick;
      focus.last.endTick = moverSpan.endTick;
    }
  }

  const uint32_t displayEndForResolve = noteEditGeometryApplyStorageTickToDisplayPhase(noteEnd, loopLength);
  MidiEvent* noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, focus, channel, currentNoteValue,
                                                       noteStart, loopLength);
  if (noteOnEvent == nullptr) {
    return false;
  }
  MidiEvent* noteOffEvent = noteEditGeometryApplyResolveMovingNoteOffForEdit(
      midiEvents, noteOnEvent, focus.movingNoteId, channel, currentNoteValue, noteOnEvent->tick,
      displayEndForResolve, loopLength);
  const bool openTailOnly =
      noteOffEvent == nullptr &&
      isOpenTailNoteAtLoopEnd(midiEvents, channel, currentNoteValue, noteOnEvent->tick,
                              displayEndForResolve, loopLength);
  if (!noteOffEvent && !openTailOnly) {
    return false;
  }

  noteOnEvent->data.noteData.note = newNoteValue;
  if (noteOffEvent != nullptr) {
    noteOffEvent->data.noteData.note = newNoteValue;
  }

  const uint32_t linearEnd = noteOffEvent != nullptr ? noteOffEvent->tick : noteEnd;
  noteEditFocusApplyPitch(focus, newNoteValue, noteStart, linearEnd, loopLength);
  focus.movingNoteRange.start = focus.last.startTick;
  focus.movingNoteRange.end = focus.last.endTick;
  focus.overlapNotes.erase(focus.movingNoteId);
  track.invalidateCaches(true);
  logger.log(CAT_MIDI, LOG_DEBUG, "Note value changed successfully (simple path): %d -> %d",
             currentNoteValue, newNoteValue);
  return true;
}

}  // namespace

NOTE_EDIT_MEM bool applyPitchChange(Track& track, EditManager& manager,
                      uint8_t currentNoteValue, uint8_t newNoteValue,
                      uint32_t& noteStart, uint32_t& noteEnd, bool refreshPlaybackPreview) {
    if (currentNoteValue == newNoteValue) {
        return true;
    }

    // Session store when a note-edit session is active (matches move/length live paths).
    auto& midiEvents = track.editAwareMidiEvents();
    const uint32_t loopLength = manager.noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return false;
    }

    NoteEditFocus& focus = noteEditGeometryApplyEditFocus(manager);
    if (focus.active) {
        manager.syncNoteEditFocusLastFromSessionStore(track);
        noteStart = focus.last.startTick;
        noteEnd = focus.last.endTick;
    }

    const uint8_t channel = track.getMidiChannel();
    const bool activeNoteEditSession =
        manager.isNoteSessionStoreOpen() && focus.active && focus.movingNoteId != kInvalidNoteId;

    if (activeNoteEditSession) {
        evictOverlapScratchForSelectedNote(focus, focus.movingNoteId);

        if (focus.movingNoteId != kInvalidNoteId) {
            NoteBaseline moverSpan;
            if (findLinearNoteSpanForNoteId(midiEvents, focus.movingNoteId, channel, moverSpan,
                                            focus.last.startTick, loopLength)) {
                noteStart = moverSpan.startTick;
                noteEnd = moverSpan.endTick;
                focus.last.startTick = moverSpan.startTick;
                focus.last.endTick = moverSpan.endTick;
            }
        }

        const NoteBaseline priorLatch{currentNoteValue, focus.last.velocity, noteStart, noteEnd};
        const NoteBaseline editedSpan{newNoteValue, focus.last.velocity, noteStart, noteEnd};
        const bool geometryResolved = NoteGeometryResolver::resolveForCausingNote(
            track, manager, focus.movingNoteId, editedSpan, priorLatch, std::nullopt, newNoteValue,
            refreshPlaybackPreview);
        if (!geometryResolved) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Warning: NoteGeometryResolver did not apply pitch change for "
                       "pitch=%u start=%lu",
                       static_cast<unsigned>(newNoteValue),
                       static_cast<unsigned long>(noteStart));
            return false;
        }

        noteStart = focus.last.startTick;
        noteEnd = focus.last.endTick;

        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        const uint32_t bracketDisplay =
            noteEditGeometryApplyBracketDisplayTickFromStorage(noteStart, loopStartTick, loopLength);
        manager.applySelectionFromGeometryEdit(track, bracketDisplay, focus.movingNoteId);
#if defined(SESSION_CAPTURE)
        const uint32_t reconstructStartUs = micros();
#endif
        finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                                  focus.last.endTick, loopLength, bracketDisplay,
                                  refreshPlaybackPreview);
#if defined(SESSION_CAPTURE)
        logGeomApplyPhase("reconstruct", micros() - reconstructStartUs, 0, 0);
#endif
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value changed via NoteGeometryResolver: %d -> %d",
                   currentNoteValue, newNoteValue);
        return true;
    }

    if (canApplySimplePitchChange(midiEvents, focus, channel, currentNoteValue, newNoteValue,
                                  noteStart, noteEnd, loopLength) &&
        applySimplePitchChange(midiEvents, manager, track, focus, channel, currentNoteValue,
                               newNoteValue, noteStart, noteEnd, loopLength)) {
        (void)refreshPlaybackPreview;
        return true;
    }

    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        NoteBaseline moverSpan;
        if (findLinearNoteSpanForNoteId(midiEvents, focus.movingNoteId, channel, moverSpan,
                                        focus.last.startTick, loopLength)) {
            noteStart = moverSpan.startTick;
            noteEnd = moverSpan.endTick;
            focus.last.startTick = moverSpan.startTick;
            focus.last.endTick = moverSpan.endTick;
        }
    }

    const NoteBaseline priorLatch{currentNoteValue, focus.last.velocity, noteStart, noteEnd};
    const NoteBaseline editedSpan{newNoteValue, focus.last.velocity, noteStart, noteEnd};
    const bool overlapStructureChanged =
        NoteGeometryResolver::resolveForCausingNote(track, manager, focus.movingNoteId, editedSpan,
                                                     priorLatch, std::nullopt, newNoteValue, false);
    if (overlapStructureChanged) {
      noteStart = focus.last.startTick;
      noteEnd = focus.last.endTick;
    }

    const uint32_t displayEndForResolve = noteEditGeometryApplyStorageTickToDisplayPhase(noteEnd, loopLength);

    // Resolve the note-on, then its STRUCTURALLY PAIRED note-off (not an independent
    // tick scan): two same-pitch notes can share a start or end tick when overlapping,
    // and an independent scan would repitch one event of an overlap note and orphan a note.
    bool noteOnUpdated = false;
    bool noteOffUpdated = false;
    MidiEvent* noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, focus, channel, newNoteValue,
                                                         noteStart, loopLength);
    if (noteOnEvent == nullptr) {
      noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, focus, channel, currentNoteValue,
                                                noteStart, loopLength);
    }
    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent) {
        noteOffEvent = noteEditGeometryApplyResolveMovingNoteOffForEdit(
            midiEvents, noteOnEvent, focus.movingNoteId, channel, newNoteValue, noteOnEvent->tick,
            displayEndForResolve, loopLength);
        const bool openTailOnly =
            !noteOffEvent &&
            isOpenTailNoteAtLoopEnd(midiEvents, channel, currentNoteValue, noteOnEvent->tick,
                                    displayEndForResolve, loopLength);
        if (noteOffEvent || openTailOnly) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Updating note-on: pitch %d -> %d at tick %lu",
                      currentNoteValue, newNoteValue, noteOnEvent->tick);
            noteOnEvent->data.noteData.note = newNoteValue;
            noteOnUpdated = true;
            if (noteOffEvent) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Updating note-off: pitch %d -> %d at tick %lu",
                          currentNoteValue, newNoteValue, noteOffEvent->tick);
                noteOffEvent->data.noteData.note = newNoteValue;
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Pitch edit on open-tail note: pitch %d -> %d (note-on only)",
                          currentNoteValue, newNoteValue);
            }
            noteOffUpdated = true;
        }
    }

    if (!noteOnUpdated || !noteOffUpdated) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Failed to update note value: noteOn=%s noteOff=%s",
                  noteOnUpdated ? "OK" : "FAILED",
                  noteOffUpdated ? "OK" : "FAILED");
        return false;
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "Note value changed successfully: %d -> %d",
              currentNoteValue, newNoteValue);
    NoteUtils::removeDuplicateNotePairsAtSpan(midiEvents, newNoteValue, noteStart, noteEnd);
    NoteUtils::ensureNoteOffsBeforeNoteOnsAtTick(midiEvents, newNoteValue, noteStart);

    const uint32_t linearEnd = noteOffEvent != nullptr ? noteOffEvent->tick : noteEnd;
    noteEditFocusApplyPitch(focus, newNoteValue, noteStart, linearEnd, loopLength);
    focus.movingNoteRange.start = focus.last.startTick;
    focus.movingNoteRange.end = focus.last.endTick;
    if (focus.movingNoteId != kInvalidNoteId) {
        focus.overlapNotes.erase(focus.movingNoteId);
    }
    if (focus.movingNoteId != kInvalidNoteId) {
        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        const bool lengthBracket = manager.isLengthBracketEditActive();
        const uint32_t storageBracket =
            lengthBracket ? focus.last.endTick : focus.last.startTick;
        const uint32_t bracketDisplay =
            noteEditGeometryApplyBracketDisplayTickFromStorage(storageBracket, loopStartTick, loopLength);
        manager.applySelectionFromGeometryEdit(track, bracketDisplay, focus.movingNoteId);
        if (!overlapStructureChanged) {
            NoteUtils::sortMidiEventsChronologically(midiEvents);
            track.invalidateCaches(refreshPlaybackPreview);
#ifndef PIO_UNIT_TEST_NATIVE
            displayManager.requestNoteInfoRefresh(track);
#endif
        } else {
#if defined(SESSION_CAPTURE)
            const uint32_t reconstructStartUs = micros();
#endif
            finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                                      focus.last.endTick, loopLength, bracketDisplay,
                                      refreshPlaybackPreview);
#if defined(SESSION_CAPTURE)
            logGeomApplyPhase("reconstruct", micros() - reconstructStartUs, 0, 0);
#endif
        }
        return true;
    }
#if defined(SESSION_CAPTURE)
    const uint32_t reconstructStartUs = micros();
#endif
    finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                              focus.last.endTick, loopLength, noteStart, refreshPlaybackPreview);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("reconstruct", micros() - reconstructStartUs, 0, 0);
#endif
    return true;
}

NOTE_EDIT_MEM bool moveNoteWithOverlapHandling(Track& track, EditManager& manager,
                                const NoteUtils::DisplayNote& currentNote,
                                uint32_t targetTick, int delta,
                                bool refreshPlaybackPreview) {
    // Session store when a note-edit session is active (matches move/length live paths).
    auto& midiEvents = track.editAwareMidiEvents();
    const uint32_t loopLength = manager.noteEditLoopLengthTicks(track);

    logger.log(CAT_MIDI, LOG_DEBUG, "NoteEditGeometryApply::moveNoteWithOverlapHandling called: targetTick=%lu, delta=%d", targetTick, delta);

    manager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    const uint8_t channel = track.getMidiChannel();
    const NoteEditFocus& focus = noteEditGeometryApplyEditFocus(manager);
    
    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length is 0, cannot move notes");
        return false;
    }

    // If there's no actual movement, just update the bracket position and return
    if (delta == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No movement (delta=0), just updating bracket position to %lu", targetTick);
        manager.setSelectedTick(targetTick);
        return false;
    }
    
    uint8_t movingNotePitch = currentNote.note;
    uint32_t currentStart = currentNote.startTick;
    uint32_t currentEnd = currentNote.endTick;
    if (focus.active) {
        movingNotePitch = focus.last.pitch;
        currentStart = focus.last.startTick;
        currentEnd = focus.last.endTick;
    }
    const uint32_t originalStart =
        focus.active ? focus.commitBaseline.startTick : currentNote.startTick;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Moving note: pitch=%d, start=%lu, end=%lu", 
              movingNotePitch, currentStart, currentEnd);
    
    uint32_t displayCurrentEnd = noteEditGeometryApplyStorageTickToDisplayPhase(currentEnd, loopLength);
    if (focus.active && focus.last.pitch == movingNotePitch &&
        focus.last.startTick == currentStart &&
        focus.last.endTick > currentEnd) {
        currentEnd = focus.last.endTick;
        displayCurrentEnd = noteEditGeometryApplyStorageTickToDisplayPhase(currentEnd, loopLength);
    }
    const NoteId movingNoteId =
        focus.active ? focus.movingNoteId : kInvalidNoteId;
    if (focus.active) {
        evictOverlapScratchForSelectedNote(noteEditGeometryApplyEditFocus(manager), movingNoteId);
    }
    // Prefer focus.last length during an active edit driver. Live-store pairing can briefly
    // mis-resolve same-pitch neighbor offs (noteId lives on note-on only) before Shorten
    // applies; session_20260804_210819 collapsed 190 → 4 at neighbor end 815.
    // Wrap spans are end < start (192259: 2592–96). `currentEnd > currentStart` skipped
    // those and resolve returned the head length 96 instead of calculateNoteLength 576.
    uint32_t noteLen = 0;
    if (focus.active && currentEnd != currentStart) {
        noteLen = calculateNoteLength(currentStart, currentEnd, loopLength);
    } else {
        noteLen = noteEditGeometryApplyResolveMovingNoteLengthTicks(midiEvents, channel, movingNotePitch, currentStart,
                                               currentEnd, loopLength, movingNoteId);
    }
    uint32_t newStart = targetTick;
    uint32_t newEnd = newStart + noteLen;
    uint32_t displayNewEnd = noteEditGeometryApplyStorageTickToDisplayPhase(newEnd, loopLength);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Movement: start %lu->%lu, end actual %lu (display %lu), length=%lu", 
              currentStart, newStart, newEnd, displayNewEnd, noteLen);

    const uint32_t linearNewEnd =
        NoteEditGeometryApply::linearStorageOffTickForSpanEnd(newStart, noteLen);
    const uint32_t displayEndForBracket =
        noteEditGeometryApplyStorageTickToDisplayPhase(linearNewEnd, loopLength);

    const NoteBaseline editedSpan{movingNotePitch, focus.last.velocity, newStart, linearNewEnd};
    // Overlap scope is the mover's own lane — a move never changes pitch (Q14).
    const bool geometryResolved =
        NoteGeometryResolver::resolveForCausingNote(track, manager, focus.movingNoteId, editedSpan,
                                                     focus.last, targetTick, movingNotePitch,
                                                     refreshPlaybackPreview);

    bool movedNoteEvents = geometryResolved;
    if (geometryResolved) {
      MidiEvent* noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, noteEditGeometryApplyEditFocus(manager), channel,
                                                           movingNotePitch, newStart, loopLength);
      if (noteOnEvent != nullptr) {
        noteEditGeometryApplyScrubStaleWrapHeadOffsForMovedNote(midiEvents, channel, movingNotePitch, newStart,
                                           linearNewEnd, loopLength, noteOnEvent->noteId);
      }
    } else {
      logger.log(CAT_MIDI, LOG_DEBUG,
                  "Warning: NoteGeometryResolver did not apply move for pitch=%u "
                  "start=%lu",
                  movingNotePitch, newStart);
    }

    const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
    const uint32_t bracketDisplay =
        noteEditGeometryApplyBracketDisplayTickFromStorage(newStart, loopStartTick, loopLength);
    if (movedNoteEvents && focus.active && focus.movingNoteId != kInvalidNoteId) {
        manager.applySelectionFromGeometryEdit(track, bracketDisplay, focus.movingNoteId);
    }

#if defined(SESSION_CAPTURE)
    const uint32_t reconstructStartUs = micros();
#endif
    finalReconstructAndSelect(track, midiEvents, manager, movingNotePitch, newStart,
                              displayEndForBracket, loopLength, bracketDisplay,
                              refreshPlaybackPreview);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("reconstruct", micros() - reconstructStartUs, 0, 0);
#endif
    return movedNoteEvents;
}

NOTE_EDIT_MEM void changeLengthWithOverlapHandling(Track& track, EditManager& manager,
                                     const NoteUtils::DisplayNote& currentNote,
                                     uint32_t targetEndTick,
                                     bool refreshPlaybackPreview) {
    auto& midiEvents = track.editAwareMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    manager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    manager.syncNoteEditFocusLastFromSessionStore(track);
    const uint8_t channel = track.getMidiChannel();
    const NoteEditFocus& focus = noteEditGeometryApplyEditFocus(manager);

    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        evictOverlapScratchForSelectedNote(noteEditGeometryApplyEditFocus(manager), focus.movingNoteId);
    }

    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length is 0, cannot change note length");
        return;
    }

    uint8_t notePitch = currentNote.note;
    uint32_t noteStart = currentNote.startTick;
    uint32_t currentEnd = currentNote.endTick;
    if (focus.active) {
        notePitch = focus.last.pitch;
        noteStart = focus.last.startTick;
        currentEnd = focus.last.endTick;
    }
    if (loopLength > 0) {
        noteStart %= loopLength;
        currentEnd %= loopLength;
        targetEndTick %= loopLength;
    }
    uint32_t displayCurrentEnd = currentEnd;

    if (targetEndTick == currentEnd) {
        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        manager.setSelectedTick(
            noteEditGeometryApplyBracketDisplayTickFromStorage(targetEndTick, loopStartTick, loopLength));
        return;
    }

    const uint32_t minNoteDuration = Config::TICKS_PER_16TH_STEP;
    const bool nonWrapMovingNote =
        currentEnd > noteStart && (currentEnd - noteStart) < loopLength;

    uint32_t newStart = noteStart;
    uint32_t newEnd = targetEndTick;

    if (nonWrapMovingNote && newEnd < noteStart + minNoteDuration) {
        newEnd = noteStart + minNoteDuration;
    }

    const int delta = (newEnd > currentEnd) ? 1 : -1;
    const uint32_t baselineStart =
        focus.active ? focus.commitBaseline.startTick : noteStart;

    const uint32_t displayNewEnd = noteEditGeometryApplyStorageTickToDisplayPhase(newEnd, loopLength);

    logger.log(CAT_MIDI, LOG_DEBUG,
              "Length change with overlap: pitch=%d, start=%lu, end %lu->%lu",
              notePitch, noteStart, currentEnd, targetEndTick);

    const uint32_t noteLen =
        NoteEditGeometryApply::calculateNoteLength(newStart, newEnd, loopLength);
    const uint32_t linearNewEnd =
        NoteEditGeometryApply::linearStorageOffTickForSpanEnd(newStart, noteLen);

    const NoteBaseline editedSpan{notePitch, focus.last.velocity, newStart, linearNewEnd};
    const bool geometryResolved =
        NoteGeometryResolver::resolveForCausingNote(track, manager, focus.movingNoteId,
                                                     editedSpan, focus.last, std::nullopt,
                                                     notePitch, refreshPlaybackPreview);

    if (!geometryResolved) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Warning: NoteGeometryResolver did not apply length for pitch=%d "
                  "start=%lu end=%lu",
                  notePitch, noteStart, newEnd);
    }

    const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
    const uint32_t lengthBracketDisplay =
        noteEditGeometryApplyBracketDisplayTickFromStorage(newEnd, loopStartTick, loopLength);
  if (geometryResolved && focus.active && focus.movingNoteId != kInvalidNoteId) {
    manager.applySelectionFromGeometryEdit(track, lengthBracketDisplay, focus.movingNoteId);
  }
  manager.setSelectedTick(lengthBracketDisplay);

#if defined(SESSION_CAPTURE)
    const uint32_t reconstructStartUs = micros();
#endif
    finalReconstructAndSelect(track, midiEvents, manager, notePitch, newStart, newEnd, loopLength,
                              lengthBracketDisplay, refreshPlaybackPreview);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("reconstruct", micros() - reconstructStartUs, 0, 0);
#endif

    NoteUtils::orderSamePitchNoteOffsForLifo(midiEvents, track.getMidiChannel(), notePitch);
}

// Extend shortened notes dynamically
NOTE_EDIT_MEM void extendShortenedNotes(MidiEventVec& midiEvents,
                         const std::vector<std::pair<OverlapNoteRestore, std::uint32_t>>& notesToExtend,
                         EditManager& manager,
                         std::uint32_t loopLength) {
    logger.log(CAT_MIDI, LOG_DEBUG, "=== EXTENDING SHORTENED NOTES ===");
    logger.log(CAT_MIDI, LOG_DEBUG, "Total notes to extend: %zu", notesToExtend.size());
    
    for (const auto& [noteToExtend, newEndTick] : notesToExtend) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Extending shortened note: pitch=%d, start=%lu, from %lu to %lu", 
                  noteToExtend.pitch, noteToExtend.startTick, noteToExtend.shortenedToTick, newEndTick);
        
        // Find the note-off event at its current shortened position
        MidiEvent* noteOffEvent = nullptr;
        for (auto& event : midiEvents) {
            if ((event.type == midi::NoteOff || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
                event.data.noteData.note == noteToExtend.pitch && 
                event.tick > noteToExtend.startTick) {
                // Take the first note-off we find for this pitch after the note-on
                noteOffEvent = &event;
                break;
            }
        }
        
        if (noteOffEvent) {
            uint32_t oldTick = noteOffEvent->tick;
            noteOffEvent->tick = newEndTick;
            
            // Update the tracking in overlap notes
            for (auto& [noteId, entry] : manager.getEditSession().focus.overlapNotes) {
                (void)noteId;
                if (entry.baseline.pitch == noteToExtend.pitch &&
                    entry.baseline.startTick == noteToExtend.startTick &&
                    entry.state == OverlapNoteStoreState::Shortened) {
                    entry.shortenedEndTick = newEndTick;
                    logger.log(CAT_MIDI, LOG_DEBUG, "Updated overlap note shortened end to %lu",
                              newEndTick);
                    break;
                }
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Extended note-off event: pitch=%d, from tick=%lu to tick=%lu", 
                      noteToExtend.pitch, oldTick, newEndTick);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find note-off event to extend: pitch=%d, start=%lu", 
                      noteToExtend.pitch, noteToExtend.startTick);
        }
    }
}

}  // namespace NoteEditGeometryApply
