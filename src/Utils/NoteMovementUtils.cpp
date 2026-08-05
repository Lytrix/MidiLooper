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
#include "Utils/NoteMovementUtils.h"
#include "RunEditSessionGeometryPipelineDriver.h"
#include "Utils/IntervalProjection.h"
#include "Globals.h"
#include "Utils/DebugSessionCapture.h"
#include <algorithm>
#include <map>
#include "Utils/NoteEditMem.h"

namespace NoteMovementUtils {

namespace {

uint32_t storageTickToDisplayPhase(uint32_t tick, uint32_t loopLength) {
    return IntervalProjection::tickPhaseInLoop(tick, 0, loopLength);
}

uint32_t bracketDisplayTickFromStorage(uint32_t storageTick, uint32_t loopStartTick,
                                       uint32_t loopLength) {
    return NoteEditDisplaySnapshot::displayStartTickFromStorage(storageTick, loopStartTick,
                                                                loopLength);
}

}  // namespace

MidiEvent* findNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick);

namespace {

NOTE_EDIT_MEM void stampPairedNoteId(MidiEvent* noteOn, MidiEvent* noteOff) {
    if (noteOn != nullptr && noteOff != nullptr && noteOn->noteId != kInvalidNoteId &&
        noteOff->noteId == kInvalidNoteId) {
        noteOff->noteId = noteOn->noteId;
    }
}

NOTE_EDIT_MEM NoteEditFocus& editFocus(EditManager& manager) {
    return manager.getEditSession().focus;
}

MidiEvent* findNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick);

} // namespace

NOTE_EDIT_MEM bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2, uint32_t loopLength) {
    // Convert to unwrapped positions for comparison
    uint32_t unwrappedEnd1 = end1;
    uint32_t unwrappedEnd2 = end2;
    
    // Check if notes are wrapped (end < start means wrapped)
    bool wrapped1 = (end1 < start1);
    bool wrapped2 = (end2 < start2);
    
    if (wrapped1) {
        unwrappedEnd1 = end1 + loopLength;
    }
    if (wrapped2) {
        unwrappedEnd2 = end2 + loopLength;
    }
    
    // Now check overlap using unwrapped positions
    // Note1: [start1, unwrappedEnd1], Note2: [start2, unwrappedEnd2]
    bool overlap = (start1 < unwrappedEnd2) && (start2 < unwrappedEnd1);
    
    // If both notes are unwrapped, also check for loop-wrapped overlaps
    if (!wrapped1 && !wrapped2) {
        // Check if note1 wraps around and overlaps with note2
        bool note1WrapsAndOverlaps = (start1 + loopLength < unwrappedEnd2) && (start2 < end1 + loopLength);
        // Check if note2 wraps around and overlaps with note1
        bool note2WrapsAndOverlaps = (start2 + loopLength < unwrappedEnd1) && (start1 < end2 + loopLength);
        overlap = overlap || note1WrapsAndOverlaps || note2WrapsAndOverlaps;
    }
    
    return overlap;
}

NOTE_EDIT_MEM bool linearStorageSpansOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2) {
    return start1 < end2 && start2 < end1;
}

template <typename Alloc>
NOTE_EDIT_MEM MidiEvent* findCorrespondingNoteOff(std::vector<MidiEvent, Alloc>& midiEvents,
                                                  MidiEvent* noteOnEvent, uint8_t pitch,
                                                  std::uint32_t startTick, std::uint32_t endTick) {
    (void)startTick;
    (void)endTick;
    if (noteOnEvent == nullptr) {
        return nullptr;
    }
    std::vector<MidiEvent*> activeNoteOnStack;

    for (auto& evt : midiEvents) {
        bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                         evt.data.noteData.note == pitch);
        bool isNoteOff = ((evt.type == midi::NoteOff ||
                           (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) &&
                          evt.data.noteData.note == pitch);

        if (isNoteOn) {
            activeNoteOnStack.push_back(&evt);
        } else if (isNoteOff) {
            for (int stackIndex = static_cast<int>(activeNoteOnStack.size()) - 1; stackIndex >= 0;
                 --stackIndex) {
                MidiEvent* candidateOn = activeNoteOnStack[static_cast<size_t>(stackIndex)];
                if (evt.tick <= candidateOn->tick) {
                    continue;
                }
                MidiEvent* correspondingNoteOn = candidateOn;
                activeNoteOnStack.erase(activeNoteOnStack.begin() + stackIndex);
                if (correspondingNoteOn == noteOnEvent) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "Found corresponding note-off: pitch=%d, noteOn@%lu -> noteOff@%lu",
                              pitch, correspondingNoteOn->tick, evt.tick);
                    return &evt;
                }
                break;
            }
        }
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "No corresponding note-off found for pitch=%d, start=%lu",
              pitch, startTick);
    return nullptr;
}

template MidiEvent* findCorrespondingNoteOff<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, MidiEvent*, uint8_t, std::uint32_t, std::uint32_t);
template MidiEvent* findCorrespondingNoteOff<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, MidiEvent*, uint8_t, std::uint32_t, std::uint32_t);

NOTE_EDIT_MEM MidiEvent* findNoteOffPairedAt(MidiEventVec& midiEvents, uint8_t pitch, uint32_t startTick,
                               uint32_t endTick) {
    std::vector<MidiEvent*> activeNoteOnStack;
    for (auto& evt : midiEvents) {
        const bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                             evt.data.noteData.note == pitch);
        const bool isNoteOff =
            ((evt.type == midi::NoteOff ||
              (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) &&
             evt.data.noteData.note == pitch);

        if (isNoteOn) {
            activeNoteOnStack.push_back(&evt);
        } else if (isNoteOff) {
            if (activeNoteOnStack.empty()) {
                continue;
            }
            MidiEvent* correspondingNoteOn = activeNoteOnStack.back();
            activeNoteOnStack.pop_back();
            if (correspondingNoteOn->tick == startTick && evt.tick == endTick) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Found paired note-off: pitch=%d, start=%lu, end=%lu",
                          pitch, startTick, endTick);
                return &evt;
            }
        }
    }

    logger.log(CAT_MIDI, LOG_DEBUG,
              "No paired note-off for pitch=%d, start=%lu, end=%lu",
              pitch, startTick, endTick);
    return nullptr;
}

NOTE_EDIT_MEM MidiEvent* findNoteOffForNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel,
                                       uint8_t pitch, uint32_t startTick, NoteId noteId,
                                       uint32_t loopLength) {
    if (noteId != kInvalidNoteId) {
        for (auto& evt : midiEvents) {
            if (evt.noteId != noteId || evt.channel != channel || !evt.isNoteOn() ||
                evt.data.noteData.velocity == 0 || evt.data.noteData.note != pitch) {
                continue;
            }
            if (evt.tick != startTick) {
                continue;
            }
            if (MidiEvent* linearOff =
                    findLinearOffForNoteId(midiEvents, evt, noteId, loopLength)) {
                return linearOff;
            }
            return findCorrespondingNoteOff(midiEvents, &evt, pitch, evt.tick, 0);
        }
    }
    for (auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.channel == channel &&
            evt.tick == startTick) {
            return findCorrespondingNoteOff(midiEvents, &evt, pitch, startTick, 0);
        }
    }
    return nullptr;
}

NOTE_EDIT_MEM MidiEvent* resolveNoteOffForEditSpan(MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                                   uint8_t channel, uint8_t pitch, uint32_t startTick,
                                   uint32_t displayEndTick, uint32_t loopLength) {
    if (!noteOnEvent) {
        return nullptr;
    }

    if (MidiEvent* noteOffEvent = findCorrespondingNoteOff(
            midiEvents, noteOnEvent, pitch, startTick, displayEndTick)) {
        return noteOffEvent;
    }
    if (MidiEvent* noteOffEvent =
            findNoteOffPairedAt(midiEvents, pitch, startTick, displayEndTick)) {
        return noteOffEvent;
    }

    for (auto& evt : midiEvents) {
        if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != pitch) {
            continue;
        }
        const uint32_t headOffTick = storageTickToDisplayPhase(evt.tick, loopLength);
        if (NoteUtils::isPreferredWrapTailForHeadOff(startTick, headOffTick, midiEvents, pitch,
                                                     channel, loopLength)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Found wrap-head note-off: pitch=%d, tailOn@%lu -> headOff@%lu",
                       pitch, startTick, headOffTick);
            return &evt;
        }
    }
    return nullptr;
}

NOTE_EDIT_MEM bool isOpenTailNoteAtLoopEnd(const MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick, uint32_t displayEndTick, uint32_t loopLength) {
    if (loopLength == 0 || displayEndTick != loopLength - 1) {
        return false;
    }

    const MidiEvent* noteOnEvent = nullptr;
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.channel == channel &&
            evt.tick == startTick) {
            noteOnEvent = &evt;
            break;
        }
    }
    if (!noteOnEvent) {
        return false;
    }

    MidiEventVec& mutableEvents = const_cast<MidiEventVec&>(midiEvents);
    return resolveNoteOffForEditSpan(mutableEvents, const_cast<MidiEvent*>(noteOnEvent), channel,
                                     pitch, startTick, displayEndTick,
                                     loopLength) == nullptr;
}

namespace {

NOTE_EDIT_MEM MidiEvent* resolveMovingNoteOffForEdit(MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                                       NoteId movingNoteId, uint8_t channel, uint8_t pitch,
                                       uint32_t startTick, uint32_t displayEndTick,
                                       uint32_t loopLength) {
    if (noteOnEvent == nullptr) {
        return nullptr;
    }
    MidiEvent* noteOffEvent = nullptr;
    if (movingNoteId != kInvalidNoteId && noteOnEvent->noteId == movingNoteId) {
        noteOffEvent =
            findLinearOffForNoteId(midiEvents, *noteOnEvent, movingNoteId, loopLength);
    }
    if (noteOffEvent == nullptr) {
        noteOffEvent = resolveNoteOffForEditSpan(midiEvents, noteOnEvent, channel, pitch,
                                                 startTick, displayEndTick, loopLength);
    }
    stampPairedNoteId(noteOnEvent, noteOffEvent);
    return noteOffEvent;
}

NOTE_EDIT_MEM MidiEvent* findNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick) {
    for (auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.channel == channel &&
            evt.tick == startTick) {
            return &evt;
        }
    }
    return nullptr;
}

NOTE_EDIT_MEM MidiEvent& appendNoteOffForOpenTail(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                                    uint32_t offTick, NoteId noteId) {
    MidiEvent offEvent = MidiEvent::NoteOff(offTick, channel, pitch, 0);
    offEvent.noteId = noteId;
    midiEvents.push_back(offEvent);
    NoteUtils::orderSamePitchNoteOffsForLifo(midiEvents, channel, pitch);
    return midiEvents.back();
}

NOTE_EDIT_MEM uint32_t storageOffTickForSpanEnd(uint32_t startTick, uint32_t noteLen, uint32_t loopLength) {
    (void)loopLength;
    return NoteMovementUtils::linearStorageOffTickForSpanEnd(startTick, noteLen);
}

NOTE_EDIT_MEM uint32_t displayFocusEndTickForMove(uint32_t startTick, uint32_t noteLen, uint32_t loopLength) {
    if (loopLength == 0) {
        return startTick + noteLen;
    }
    const uint32_t rawEnd = startTick + noteLen;
    if (rawEnd >= loopLength) {
        return loopLength - 1;
    }
    return rawEnd;
}

NOTE_EDIT_MEM void scrubStaleWrapHeadOffsForMovedNote(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                                        uint32_t tailOnTick, uint32_t linearOffTick,
                                        uint32_t loopLength, NoteId movingNoteId) {
    if (loopLength == 0 || movingNoteId == kInvalidNoteId) {
        return;
    }
    MidiEvent* moverOn = findNoteOnAtStart(midiEvents, channel, pitch, tailOnTick);
    if (moverOn == nullptr || moverOn->noteId != movingNoteId) {
        return;
    }
    const auto isStaleWrapHead = [&](const MidiEvent& evt) {
        if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != pitch) {
            return false;
        }
        if (evt.tick == linearOffTick) {
            return false;
        }
        uint32_t headOffTick = evt.tick;
        if (headOffTick >= loopLength) {
            if (headOffTick == linearOffTick) {
                return false;
            }
            headOffTick %= loopLength;
        }
        if (headOffTick >= loopLength - 1) {
            return false;
        }
        if (linearOffTick >= loopLength) {
            return NoteUtils::isPreferredWrapTailForHeadOff(tailOnTick, headOffTick, midiEvents,
                                                            pitch, channel, loopLength);
        }
        return NoteUtils::isPreferredWrapTailForHeadOff(tailOnTick, headOffTick, midiEvents, pitch,
                                                        channel, loopLength);
    };
    midiEvents.erase(std::remove_if(midiEvents.begin(), midiEvents.end(), isStaleWrapHead),
                     midiEvents.end());
}

NOTE_EDIT_MEM bool stillOpenTailAfterMove(uint32_t newStart, uint32_t noteLen, uint32_t loopLength) {
    if (loopLength == 0) {
        return false;
    }
    return (newStart + noteLen) >= loopLength;
}

NOTE_EDIT_MEM bool isWrapHeadOffForTailOn(const MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                            MidiEvent* noteOffEvent, uint8_t channel, uint8_t pitch,
                            uint32_t loopLength) {
    if (!noteOnEvent || !noteOffEvent || loopLength == 0) {
        return false;
    }
    const uint32_t headOffTick = storageTickToDisplayPhase(noteOffEvent->tick, loopLength);
    return NoteUtils::isPreferredWrapTailForHeadOff(noteOnEvent->tick, headOffTick, midiEvents,
                                                    pitch, channel, loopLength);
}

NOTE_EDIT_MEM uint32_t resolveMovingNoteLengthTicks(MidiEventVec& midiEvents, uint8_t channel,
                                      uint8_t pitch, uint32_t startTick,
                                      uint32_t fallbackEndTick, uint32_t loopLength,
                                      NoteId movingNoteId) {
    if (loopLength == 0) {
        return 0;
    }
    if (movingNoteId != kInvalidNoteId) {
        NoteBaseline linearSpan;
        if (findLinearNoteSpanForNoteId(midiEvents, movingNoteId, channel, linearSpan, startTick,
                                        loopLength) &&
            linearSpan.endTick > linearSpan.startTick) {
            if (linearSpan.endTick <= linearSpan.startTick + loopLength) {
                return linearSpan.endTick - linearSpan.startTick;
            }
            return calculateNoteLength(linearSpan.startTick, linearSpan.endTick, loopLength);
        }
    }
    if (MidiEvent* noteOffEvent = findNoteOffForNoteOnAtStart(
            midiEvents, channel, pitch, startTick, movingNoteId, loopLength)) {
        const uint32_t pairedEnd = noteOffEvent->tick;
        if (pairedEnd > startTick && pairedEnd <= startTick + loopLength) {
            return pairedEnd - startTick;
        }
        return calculateNoteLength(startTick, pairedEnd, loopLength);
    }
    const uint32_t displayFallbackEnd = storageTickToDisplayPhase(fallbackEndTick, loopLength);
    if (fallbackEndTick > startTick && fallbackEndTick <= startTick + loopLength) {
        return fallbackEndTick - startTick;
    }
    return calculateNoteLength(startTick, displayFallbackEnd, loopLength);
}

}  // namespace

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
    const uint32_t displayNewEnd = storageTickToDisplayPhase(newEnd, loopLength);

    if (manager.isNoteEditActive()) {
        const NoteEditFocus& focus = manager.getEditSession().focus;
        const EditorSelection& selection = manager.getNoteEditSessionState().selection;
        const NoteUtils::DisplayNoteVec filtered = manager.projectedNoteEditDisplayNotes(track);

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
                const uint32_t displayBracket = bracketDisplayTickFromStorage(
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
                retrySelection.selectedTick = bracketDisplayTickFromStorage(
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
        const uint32_t displayBracket = bracketDisplayTickFromStorage(
            storageBracketTick, loopStartTick, loopLength);
        manager.applySelectionFromGeometryEdit(track, displayBracket, focus.movingNoteId);
    }
    track.invalidateCaches(refreshPlaybackPreview);
#ifndef PIO_UNIT_TEST_NATIVE
    displayManager.requestNoteInfoRefresh(track);
#endif
}

namespace {

// Non-session pitch path: loop view or before NOTE_EDIT session applies overlap pipeline.
// Active NOTE_EDIT pitch uses runEditSessionGeometryPipelineForCausingNote (Phase 4.10g).
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

  const uint32_t displayEndForResolve = storageTickToDisplayPhase(noteEnd, loopLength);
  MidiEvent* noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, focus, channel, currentNoteValue,
                                                       noteStart, loopLength);
  if (noteOnEvent == nullptr) {
    return false;
  }
  MidiEvent* noteOffEvent = resolveMovingNoteOffForEdit(
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
  manager.bumpSessionPreviewRevision();
  manager.scheduleDeferredNoteEditDisplayRefresh();
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

    // Use the note-edit session store (when active) so live pitch edits share the same
    // source-of-truth buffer as move/length edits; getMidiEvents() re-materializes from
    // takes+edits and would diverge from the live session flat.
    auto& midiEvents = track.editAwareMidiEvents();
    const uint32_t loopLength = manager.noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return false;
    }

    NoteEditFocus& focus = editFocus(manager);
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

        recordBaselinePitchLaneRestoreOverlapCandidates(focus, midiEvents, channel,
                                                        currentNoteValue);

        const NoteBaseline priorLatch{currentNoteValue, focus.last.velocity, noteStart, noteEnd};
        const NoteBaseline editedSpan{newNoteValue, focus.last.velocity, noteStart, noteEnd};
        const bool pipelineApplied = runEditSessionGeometryPipelineForCausingNote(
            track, manager, focus.movingNoteId, editedSpan, priorLatch, std::nullopt, newNoteValue,
            refreshPlaybackPreview);
        if (!pipelineApplied) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Warning: edit session geometry pipeline did not apply pitch change for "
                       "pitch=%u start=%lu",
                       static_cast<unsigned>(newNoteValue),
                       static_cast<unsigned long>(noteStart));
            return false;
        }

        noteStart = focus.last.startTick;
        noteEnd = focus.last.endTick;

        NoteUtils::removeDuplicateNotePairsAtSpan(midiEvents, newNoteValue, noteStart, noteEnd);
        NoteUtils::ensureNoteOffsBeforeNoteOnsAtTick(midiEvents, newNoteValue, noteStart);

        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        const uint32_t bracketDisplay =
            bracketDisplayTickFromStorage(noteStart, loopStartTick, loopLength);
        manager.applySelectionFromGeometryEdit(track, bracketDisplay, focus.movingNoteId);
        finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                                  focus.last.endTick, loopLength, bracketDisplay,
                                  refreshPlaybackPreview);
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value changed via geometry pipeline: %d -> %d",
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

    recordBaselinePitchLaneRestoreOverlapCandidates(focus, midiEvents, channel,
                                                    currentNoteValue);

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
        runEditSessionGeometryPipelineForCausingNote(track, manager, focus.movingNoteId, editedSpan,
                                                     priorLatch, std::nullopt, newNoteValue, false);
    if (overlapStructureChanged) {
      noteStart = focus.last.startTick;
      noteEnd = focus.last.endTick;
    }

    const uint32_t displayEndForResolve = storageTickToDisplayPhase(noteEnd, loopLength);

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
        noteOffEvent = resolveMovingNoteOffForEdit(
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
            bracketDisplayTickFromStorage(storageBracket, loopStartTick, loopLength);
        manager.applySelectionFromGeometryEdit(track, bracketDisplay, focus.movingNoteId);
        if (!overlapStructureChanged) {
            NoteUtils::sortMidiEventsChronologically(midiEvents);
            track.invalidateCaches(refreshPlaybackPreview);
#ifndef PIO_UNIT_TEST_NATIVE
            displayManager.requestNoteInfoRefresh(track);
#endif
        } else {
            finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                                      focus.last.endTick, loopLength, bracketDisplay,
                                      refreshPlaybackPreview);
        }
        return true;
    }
    finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                              focus.last.endTick, loopLength, noteStart, refreshPlaybackPreview);
    return true;
}

NOTE_EDIT_MEM bool moveNoteWithOverlapHandling(Track& track, EditManager& manager,
                                const NoteUtils::DisplayNote& currentNote,
                                uint32_t targetTick, int delta) {
    // Session store when a note-edit session is active (matches move/length live paths).
    auto& midiEvents = track.editAwareMidiEvents();
    const uint32_t loopLength = manager.noteEditLoopLengthTicks(track);

    logger.log(CAT_MIDI, LOG_DEBUG, "NoteMovementUtils::moveNoteWithOverlapHandling called: targetTick=%lu, delta=%d", targetTick, delta);

    manager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    const uint8_t channel = track.getMidiChannel();
    const NoteEditFocus& focus = editFocus(manager);
    
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
    
    uint32_t displayCurrentEnd = storageTickToDisplayPhase(currentEnd, loopLength);
    if (focus.active && focus.last.pitch == movingNotePitch &&
        focus.last.startTick == currentStart &&
        focus.last.endTick > currentEnd) {
        currentEnd = focus.last.endTick;
        displayCurrentEnd = storageTickToDisplayPhase(currentEnd, loopLength);
    }
    const NoteId movingNoteId =
        focus.active ? focus.movingNoteId : kInvalidNoteId;
    if (focus.active) {
        evictOverlapScratchForSelectedNote(editFocus(manager), movingNoteId);
    }
    // Prefer focus.last length during an active edit driver. Live-store pairing can briefly
    // mis-resolve same-pitch neighbor offs (noteId lives on note-on only) before Shorten
    // applies; session_20260804_210819 collapsed 190 → 4 at neighbor end 815.
    uint32_t noteLen = 0;
    if (focus.active && currentEnd > currentStart) {
        if (currentEnd <= currentStart + loopLength) {
            noteLen = currentEnd - currentStart;
        } else {
            noteLen = calculateNoteLength(currentStart, currentEnd, loopLength);
        }
    } else {
        noteLen = resolveMovingNoteLengthTicks(midiEvents, channel, movingNotePitch, currentStart,
                                               currentEnd, loopLength, movingNoteId);
    }
    uint32_t newStart = targetTick;
    uint32_t newEnd = newStart + noteLen;
    uint32_t displayNewEnd = storageTickToDisplayPhase(newEnd, loopLength);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Movement: start %lu->%lu, end actual %lu (display %lu), length=%lu", 
              currentStart, newStart, newEnd, displayNewEnd, noteLen);

    const uint32_t linearNewEnd =
        NoteMovementUtils::linearStorageOffTickForSpanEnd(newStart, noteLen);
    const uint32_t displayEndForBracket =
        displayFocusEndTickForMove(newStart, noteLen, loopLength);

    const NoteBaseline editedSpan{movingNotePitch, focus.last.velocity, newStart, linearNewEnd};
    // Overlap scope is the mover's own lane — a move never changes pitch (Q14).
    const bool pipelineApplied =
        runEditSessionGeometryPipelineForCausingNote(track, manager, focus.movingNoteId, editedSpan,
                                                     focus.last, targetTick, movingNotePitch,
                                                     false);

    bool movedNoteEvents = pipelineApplied;
    if (pipelineApplied) {
      MidiEvent* noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, editFocus(manager), channel,
                                                           movingNotePitch, newStart, loopLength);
      if (noteOnEvent != nullptr) {
        scrubStaleWrapHeadOffsForMovedNote(midiEvents, channel, movingNotePitch, newStart,
                                           linearNewEnd, loopLength, noteOnEvent->noteId);
      }
    } else {
      logger.log(CAT_MIDI, LOG_DEBUG,
                  "Warning: edit session geometry pipeline did not apply move for pitch=%u "
                  "start=%lu",
                  movingNotePitch, newStart);
    }

    const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
    const uint32_t bracketDisplay =
        bracketDisplayTickFromStorage(newStart, loopStartTick, loopLength);
    if (movedNoteEvents && focus.active && focus.movingNoteId != kInvalidNoteId) {
        manager.applySelectionFromGeometryEdit(track, bracketDisplay, focus.movingNoteId);
    }

    finalReconstructAndSelect(track, midiEvents, manager, movingNotePitch, newStart,
                              displayEndForBracket, loopLength, bracketDisplay, false);
    return movedNoteEvents;
}

NOTE_EDIT_MEM void changeLengthWithOverlapHandling(Track& track, EditManager& manager,
                                     const NoteUtils::DisplayNote& currentNote,
                                     uint32_t targetEndTick) {
    auto& midiEvents = track.editAwareMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    manager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    manager.syncNoteEditFocusLastFromSessionStore(track);
    const uint8_t channel = track.getMidiChannel();
    const NoteEditFocus& focus = editFocus(manager);

    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        evictOverlapScratchForSelectedNote(editFocus(manager), focus.movingNoteId);
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
            bracketDisplayTickFromStorage(targetEndTick, loopStartTick, loopLength));
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

    const uint32_t displayNewEnd = storageTickToDisplayPhase(newEnd, loopLength);

    logger.log(CAT_MIDI, LOG_DEBUG,
              "Length change with overlap: pitch=%d, start=%lu, end %lu->%lu",
              notePitch, noteStart, currentEnd, targetEndTick);

    const uint32_t noteLen =
        NoteMovementUtils::calculateNoteLength(newStart, newEnd, loopLength);
    const uint32_t linearNewEnd =
        NoteMovementUtils::linearStorageOffTickForSpanEnd(newStart, noteLen);

    const NoteBaseline editedSpan{notePitch, focus.last.velocity, newStart, linearNewEnd};
    const bool pipelineApplied =
        runEditSessionGeometryPipelineForCausingNote(track, manager, focus.movingNoteId,
                                                     editedSpan, focus.last, std::nullopt,
                                                     notePitch, false);

    if (!pipelineApplied) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Warning: edit session geometry pipeline did not apply length for pitch=%d "
                  "start=%lu end=%lu",
                  notePitch, noteStart, newEnd);
    }

    const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
    const uint32_t lengthBracketDisplay =
        bracketDisplayTickFromStorage(newEnd, loopStartTick, loopLength);
  if (pipelineApplied && focus.active && focus.movingNoteId != kInvalidNoteId) {
    manager.applySelectionFromGeometryEdit(track, lengthBracketDisplay, focus.movingNoteId);
  }
  manager.setSelectedTick(lengthBracketDisplay);

    finalReconstructAndSelect(track, midiEvents, manager, notePitch, newStart, newEnd, loopLength,
                              lengthBracketDisplay, false);

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

NOTE_EDIT_MEM bool applyNoteEditChange(Track& track, EditManager& manager, NoteEditChangeKind kind,
                         const NoteUtils::DisplayNote& currentNote, uint32_t targetTick,
                         int delta, uint32_t targetEndTick, uint8_t currentPitch,
                         uint8_t newPitch, uint32_t& inOutStart, uint32_t& inOutEnd,
                         bool refreshPlaybackPreview) {
    switch (kind) {
        case NoteEditChangeKind::Move:
            return moveNoteWithOverlapHandling(track, manager, currentNote, targetTick, delta);
        case NoteEditChangeKind::Length:
            changeLengthWithOverlapHandling(track, manager, currentNote, targetEndTick);
            return true;
        case NoteEditChangeKind::Pitch:
            inOutStart = currentNote.startTick;
            inOutEnd = currentNote.endTick;
            return applyPitchChange(track, manager, currentPitch, newPitch, inOutStart,
                                    inOutEnd, refreshPlaybackPreview);
    }
    return false;
}

} // namespace NoteMovementUtils 