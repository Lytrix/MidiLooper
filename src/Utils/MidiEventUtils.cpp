//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MidiEventUtils.h"
#include "Utils/NoteMovementUtils.h"
#include <algorithm>

namespace MidiEventUtils {

EditManager::MovingNoteIdentity::DeletedNote createDeletedNote(
    const NoteUtils::DisplayNote& note, 
    uint32_t loopLength,
    bool wasShortened, 
    uint32_t shortenedToTick) {
    
    EditManager::MovingNoteIdentity::DeletedNote deleted;
    deleted.note = note.note;
    deleted.velocity = note.velocity;
    deleted.startTick = note.startTick;
    deleted.endTick = note.endTick;
    deleted.originalLength = NoteMovementUtils::calculateNoteLength(note.startTick, note.endTick, loopLength);
    deleted.wasShortened = wasShortened;
    deleted.shortenedToTick = shortenedToTick;
    
    return deleted;
}

std::vector<MidiEvent>::iterator findNoteOnEvent(
    std::vector<MidiEvent>& midiEvents,
    uint8_t pitch,
    uint32_t tick) {
    
    return std::find_if(midiEvents.begin(), midiEvents.end(), 
        [pitch, tick](const MidiEvent& evt) {
            return evt.type == midi::NoteOn && 
                   evt.data.noteData.velocity > 0 &&
                   evt.data.noteData.note == pitch && 
                   evt.tick == tick;
        });
}

std::vector<MidiEvent>::iterator findNoteOffEvent(
    std::vector<MidiEvent>& midiEvents,
    uint8_t pitch,
    uint32_t tick) {
    
    return std::find_if(midiEvents.begin(), midiEvents.end(), 
        [pitch, tick](const MidiEvent& evt) {
            bool isNoteOff = (evt.type == midi::NoteOff) || 
                           (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0);
            return isNoteOff && 
                   evt.data.noteData.note == pitch && 
                   evt.tick == tick;
        });
}

std::pair<std::vector<MidiEvent>::iterator, std::vector<MidiEvent>::iterator> 
findNoteEventPair(
    std::vector<MidiEvent>& midiEvents,
    uint8_t pitch,
    uint32_t startTick,
    uint32_t endTick) {
    
    auto onIt = findNoteOnEvent(midiEvents, pitch, startTick);
    auto offIt = findNoteOffEvent(midiEvents, pitch, endTick);
    
    return std::make_pair(onIt, offIt);
}

MidiEvent createNoteEvent(bool isNoteOn, uint8_t pitch, uint8_t velocity, uint32_t tick) {
    MidiEvent event;
    event.tick = tick;
    event.type = isNoteOn ? midi::NoteOn : midi::NoteOff;
    event.data.noteData.note = pitch;
    event.data.noteData.velocity = isNoteOn ? velocity : 0;
    
    return event;
}

} // namespace MidiEventUtils 