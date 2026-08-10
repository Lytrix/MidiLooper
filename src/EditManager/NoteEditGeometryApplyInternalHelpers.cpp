//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditGeometryApplyInternal.h"
#include "NoteEditGeometryApply.h"

#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/IntervalProjection.h"

NOTE_EDIT_MEM uint32_t noteEditGeometryApplyStorageTickToDisplayPhase(uint32_t tick,
                                                                      uint32_t loopLength) {
    return IntervalProjection::tickPhaseInLoop(tick, 0, loopLength);
}

NOTE_EDIT_MEM uint32_t noteEditGeometryApplyBracketDisplayTickFromStorage(uint32_t storageTick,
                                                                          uint32_t loopStartTick,
                                                                          uint32_t loopLength) {
    return NoteEditDisplaySnapshot::displayStartTickFromStorage(storageTick, loopStartTick,
                                                                loopLength);
}

NOTE_EDIT_MEM void noteEditGeometryApplyStampPairedNoteId(MidiEvent* noteOn, MidiEvent* noteOff) {
    if (noteOn != nullptr && noteOff != nullptr && noteOn->noteId != kInvalidNoteId &&
        noteOff->noteId == kInvalidNoteId) {
        noteOff->noteId = noteOn->noteId;
    }
}

NOTE_EDIT_MEM NoteEditFocus& noteEditGeometryApplyEditFocus(EditManager& manager) {
    return manager.getEditSession().focus;
}

NOTE_EDIT_MEM MidiEvent* noteEditGeometryApplyFindNoteOnAtStart(MidiEventVec& midiEvents,
                                                                uint8_t channel, uint8_t pitch,
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

NOTE_EDIT_MEM MidiEvent* noteEditGeometryApplyResolveMovingNoteOffForEdit(
    MidiEventVec& midiEvents, MidiEvent* noteOnEvent, NoteId movingNoteId, uint8_t channel,
    uint8_t pitch, uint32_t startTick, uint32_t displayEndTick, uint32_t loopLength) {
    if (noteOnEvent == nullptr) {
        return nullptr;
    }
    MidiEvent* noteOffEvent = nullptr;
    if (movingNoteId != kInvalidNoteId && noteOnEvent->noteId == movingNoteId) {
        noteOffEvent = findLinearOffForNoteId(midiEvents, *noteOnEvent, movingNoteId, loopLength);
    }
    if (noteOffEvent == nullptr) {
        noteOffEvent = NoteEditGeometryApply::resolveNoteOffForEditSpan(
            midiEvents, noteOnEvent, channel, pitch, startTick, displayEndTick, loopLength);
    }
    noteEditGeometryApplyStampPairedNoteId(noteOnEvent, noteOffEvent);
    return noteOffEvent;
}
