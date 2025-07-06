//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/NoteUtils.h"
#include "Logger.h"
#include <set>
#include <tuple>

// CachedNoteList implementation
uint32_t NoteUtils::CachedNoteList::computeMidiHash(const std::vector<MidiEvent>& midiEvents) {
    uint32_t hash = 2166136261u; // FNV-1a initial value
    for (const auto& evt : midiEvents) {
        hash ^= static_cast<uint32_t>(evt.type); hash *= 16777619u;
        hash ^= evt.tick; hash *= 16777619u;
        hash ^= evt.data.noteData.note; hash *= 16777619u;
        hash ^= evt.data.noteData.velocity; hash *= 16777619u;
    }
    return hash;
}

const std::vector<NoteUtils::DisplayNote>& NoteUtils::CachedNoteList::getNotes(const std::vector<MidiEvent>& midiEvents, uint32_t loopLength) {
    uint32_t currentHash = computeMidiHash(midiEvents);
    
    if (isValid && currentHash == lastMidiHash && loopLength == lastLoopLength) {
        return cachedNotes; // Return cached result
    }
    
    // Cache miss - rebuild notes
    cachedNotes = reconstructNotes(midiEvents, loopLength);
    lastMidiHash = currentHash;
    lastLoopLength = loopLength;
    isValid = true;
    
    return cachedNotes;
}

std::vector<NoteUtils::DisplayNote> NoteUtils::reconstructNotes(const std::vector<MidiEvent>& midiEvents, uint32_t loopLength) {
    using DisplayNote = NoteUtils::DisplayNote;
    std::vector<DisplayNote> notes;
    std::map<uint8_t, std::vector<DisplayNote>> activeNoteStacks;
    
    logger.log(CAT_TRACK, LOG_DEBUG, "Reconstructing notes with loop length: %lu ticks", loopLength);

    // Process ALL MIDI events to handle notes that extend beyond current loop
    for (const auto& evt : midiEvents) {
        bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        bool isNoteOff = (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        uint8_t pitch = evt.data.noteData.note;
        
        if (isNoteOn) {
            uint32_t noteOnTick = evt.tick;
            
            // If note-on is beyond current loop boundary, only wrap it if it's from original loop extension
            // For loop shortening: discard notes that start beyond the new boundary
            if (noteOnTick >= loopLength) {
                logger.log(CAT_TRACK, LOG_DEBUG, "Discarding note-on beyond loop boundary: pitch=%d, tick=%lu, loop=%lu", 
                           pitch, noteOnTick, loopLength);
                continue;
            }
            
            logger.log(CAT_TRACK, LOG_DEBUG, "Note-on: pitch=%d, tick=%lu", pitch, noteOnTick);
            
                         DisplayNote note;
             note.note = pitch;
             note.startTick = noteOnTick;
             note.endTick = noteOnTick; // Will be updated when note-off is found
             note.velocity = evt.data.noteData.velocity;
            
            activeNoteStacks[pitch].push_back(note);
            
        } else if (isNoteOff) {
            uint32_t noteOffTick = evt.tick;
            
            // Handle note-off that might be beyond current loop boundary
            if (noteOffTick >= loopLength) {
                // Wrap the note-off position for notes that extend beyond loop
                noteOffTick = noteOffTick % loopLength;
                logger.log(CAT_TRACK, LOG_DEBUG, "Wrapped note-off: pitch=%d, original_tick=%lu -> wrapped_tick=%lu", 
                           pitch, evt.tick, noteOffTick);
            }
            
            logger.log(CAT_TRACK, LOG_DEBUG, "Note-off: pitch=%d, tick=%lu", pitch, noteOffTick);
            
            if (activeNoteStacks[pitch].empty()) {
                logger.log(CAT_TRACK, LOG_DEBUG, "Note-off without matching note-on: pitch=%d", pitch);
                continue;
            }
            
            // Complete the most recent note-on for this pitch
            DisplayNote& note = activeNoteStacks[pitch].back();
            note.endTick = noteOffTick;
            
                         logger.log(CAT_TRACK, LOG_DEBUG, "Final note: pitch=%d, start=%lu, end=%lu", 
                       note.note, note.startTick, note.endTick);
            
            notes.push_back(note);
            activeNoteStacks[pitch].pop_back();
        }
    }
    
    // Handle any remaining active notes (notes without explicit note-offs)
    for (auto& [pitch, noteStack] : activeNoteStacks) {
        for (const auto& note : noteStack) {
            DisplayNote completedNote = note;
            completedNote.endTick = loopLength - 1; // End at loop boundary
            
                         logger.log(CAT_TRACK, LOG_DEBUG, "Active note at loop end: pitch=%d, start=%lu, end=%lu", 
                       completedNote.note, completedNote.startTick, completedNote.endTick);
            
            notes.push_back(completedNote);
        }
    }
    
    // Deduplicate notes with same pitch, start, and end
    std::set<std::tuple<uint8_t, uint32_t, uint32_t>> seenNotes;
    std::vector<DisplayNote> finalNotes;
    
    size_t originalCount = notes.size();
    for (const auto& note : notes) {
        auto key = std::make_tuple(note.note, note.startTick, note.endTick);
        if (seenNotes.find(key) == seenNotes.end()) {
            seenNotes.insert(key);
            finalNotes.push_back(note);
        } else {
            logger.log(CAT_TRACK, LOG_DEBUG, "Deduplicated note: pitch=%d, start=%lu, end=%lu", 
                       note.note, note.startTick, note.endTick);
        }
    }
    
    logger.log(CAT_TRACK, LOG_DEBUG, "Reconstruction complete: %zu notes total (%zu duplicates removed)", 
               finalNotes.size(), originalCount - finalNotes.size());
    
    return finalNotes;
}

// Build a fast lookup index for NoteOn/NoteOff events
NoteUtils::EventIndex NoteUtils::buildEventIndex(const std::vector<MidiEvent>& midiEvents) {
    using Key = NoteUtils::Key;
    EventIndexMap onIndex;
    EventIndexMap offIndex;
    onIndex.reserve(midiEvents.size());
    offIndex.reserve(midiEvents.size());
    for (size_t i = 0; i < midiEvents.size(); ++i) {
        const auto& evt = midiEvents[i];
        bool isOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        bool isOff = (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        if (isOn || isOff) {
            Key key = ((Key)evt.data.noteData.note << 32) | evt.tick;
            if (isOn) onIndex[key] = i;
            else offIndex[key] = i;
        }
    }
    return {std::move(onIndex), std::move(offIndex)};
} 