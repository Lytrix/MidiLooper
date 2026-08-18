//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <algorithm>

#include "Globals.h"
#include "NoteEditGeometryApply.h"
#include "NoteEditGeometryApplyInternal.h"
#include "Utils/NoteUtils.h"

NOTE_EDIT_MEM void noteEditGeometryApplyScrubStaleWrapHeadOffsForMovedNote(
    MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch, uint32_t tailOnTick,
    uint32_t linearOffTick, uint32_t loopLength, NoteId movingNoteId) {
    if (loopLength == 0 || movingNoteId == kInvalidNoteId) {
        return;
    }
    MidiEvent* moverOn =
        noteEditGeometryApplyFindNoteOnAtStart(midiEvents, channel, pitch, tailOnTick);
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

NOTE_EDIT_MEM uint32_t noteEditGeometryApplyResolveMovingNoteLengthTicks(
    MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch, uint32_t startTick,
    uint32_t fallbackEndTick, uint32_t loopLength, NoteId movingNoteId) {
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
        return NoteEditGeometryApply::calculateNoteLength(linearSpan.startTick, linearSpan.endTick,
                                                          loopLength);
        }
    }
    if (MidiEvent* noteOffEvent = NoteEditGeometryApply::findNoteOffForNoteOnAtStart(
            midiEvents, channel, pitch, startTick, movingNoteId, loopLength)) {
        const uint32_t pairedEnd = noteOffEvent->tick;
        if (pairedEnd > startTick && pairedEnd <= startTick + loopLength) {
            return pairedEnd - startTick;
        }
        return NoteEditGeometryApply::calculateNoteLength(startTick, pairedEnd, loopLength);
    }
    const uint32_t displayFallbackEnd =
        noteEditGeometryApplyStorageTickToDisplayPhase(fallbackEndTick, loopLength);
    if (fallbackEndTick > startTick && fallbackEndTick <= startTick + loopLength) {
        return fallbackEndTick - startTick;
    }
    return NoteEditGeometryApply::calculateNoteLength(startTick, displayFallbackEnd, loopLength);
}
