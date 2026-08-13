//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MidiEventUtils.h"

namespace MidiEventUtils {

MidiEvent createNoteEvent(bool isNoteOn, uint8_t pitch, uint8_t velocity, uint32_t tick) {
    MidiEvent event;
    event.tick = tick;
    event.type = isNoteOn ? midi::NoteOn : midi::NoteOff;
    event.data.noteData.note = pitch;
    event.data.noteData.velocity = isNoteOn ? velocity : 0;
    
    return event;
}

} // namespace MidiEventUtils
