#include "EditStartNoteState.h"
#include "EditManager.h"
#include "MidiEvent.h"
#include "Track.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include "Logger.h"
#include <map>

void EditStartNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("Entered EditStartNoteState");
    int idx = manager.getSelectedNoteIdx();
    if (idx >= 0) {
        // Don't rebuild notes here - just preserve the current selection
        // and set up the moving note identity for tracking
        uint32_t loopLength = track.getLength();
        
        // Use the same note reconstruction as DisplayManager to get consistent indices
        auto& midiEvents = track.getMidiEvents();
        struct DisplayNote { uint8_t note, velocity; uint32_t startTick, endTick; };
        std::vector<DisplayNote> notes;
        std::vector<DisplayNote> activeNotes; // Changed from map to vector to handle multiple notes of same pitch
        
        for (const auto& evt : midiEvents) {
            if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
                // Start a new note
                DisplayNote dn{evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick};
                activeNotes.push_back(dn);
            } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
                // End a note - find the matching active note (FIFO for same pitch)
                auto it = std::find_if(activeNotes.begin(), activeNotes.end(), [&](const DisplayNote& dn) {
                    return dn.note == evt.data.noteData.note;
                });
                if (it != activeNotes.end()) {
                    it->endTick = evt.tick;
                    
                    // Handle wrap-around: if note crosses loop boundary, make it wrapped
                    if (it->startTick < loopLength && evt.tick >= loopLength) {
                        // Note crosses loop boundary - make it wrapped
                        it->endTick = evt.tick % loopLength;
                    }
                    
                    notes.push_back(*it);
                    activeNotes.erase(it);
                }
            }
        }
        // Any notes still active wrap to end of loop
        for (auto& dn : activeNotes) {
            dn.endTick = loopLength;
            notes.push_back(dn);
        }
        
        if (idx < (int)notes.size()) {
            // Record persistent identity for the currently selected note
            manager.movingNote.note = notes[idx].note;
            manager.movingNote.origStart = notes[idx].startTick;
            manager.movingNote.origEnd = notes[idx].endTick;
            manager.movingNote.wrapCount = 0;
            manager.movingNote.active = true;
            // Set bracket to this note's start
            manager.setBracketTick(notes[idx].startTick);
            logger.debug("Set up moving note identity: note=%d, start=%lu, end=%lu", 
                         notes[idx].note, notes[idx].startTick, notes[idx].endTick);
        }
    } else {
        manager.movingNote.active = false;
        manager.selectClosestNote(track, startTick);
    }
}

void EditStartNoteState::onExit(EditManager& manager, Track& track) {
    // Optionally log or cleanup
    logger.debug("Exited EditStartNoteState");
}

void EditStartNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditStartNoteState::onEncoderTurn called with delta=%d", delta);
    
    int noteIdx = manager.getSelectedNoteIdx();
    if (noteIdx < 0) {
        logger.debug("No note selected, returning early");
        return;
    }
    
    auto& midiEvents = track.getMidiEvents(); // ensure non-const
    uint32_t loopLength = track.getLength();
    
    // Validate loop length to prevent division by zero or invalid calculations
    if (loopLength == 0) {
        logger.debug("Loop length is 0, cannot move notes");
        return;
    }
    
    logger.debug("Processing note idx=%d, loopLength=%lu, midiEvents.size()=%zu", 
                 noteIdx, loopLength, midiEvents.size());
    
    // If we don't have an active moving note, we can't proceed safely
    if (!manager.movingNote.active) {
        logger.debug("No active moving note, cannot move");
        return;
    }
    
    // --- Compute new start/end for moved note using persistent identity ---
    int32_t currentStart = (int32_t)manager.movingNote.origStart;
    int32_t newStart = currentStart + delta;
    
    // Handle wrap-around: ensure newStart is always in [0, loopLength) range
    while (newStart < 0) {
        newStart += (int32_t)loopLength;
    }
    newStart = newStart % (int32_t)loopLength;
    
    logger.debug("Movement calculation: currentStart=%d, delta=%d, newStart=%d", 
                 currentStart, delta, newStart);
    
    // Calculate original note length using persistent identity
    uint32_t currentEnd = manager.movingNote.origEnd;
    int32_t noteLen;
    if (currentEnd >= (uint32_t)currentStart) {
        noteLen = currentEnd - currentStart;
        logger.debug("Note length calculation (normal): end=%lu - start=%d = %d", 
                     currentEnd, currentStart, noteLen);
    } else {
        // Note was already wrapped: endTick < startTick
        noteLen = (loopLength - currentStart) + currentEnd;
        logger.debug("Note length calculation (wrapped): (loopLen=%lu - start=%d) + end=%lu = %d", 
                     loopLength, currentStart, currentEnd, noteLen);
    }
    
    // Calculate new end position - PRESERVE the raw end tick to allow wrapped notes
    int32_t newEndRaw = newStart + noteLen;
    uint32_t newEnd;
    
    // Store the raw end tick value to preserve wrap relationship
    if (newEndRaw >= (int32_t)loopLength) {
        // Note extends beyond loop boundary - this creates a wrapped note
        newEnd = newEndRaw; // Keep the raw value, don't apply modulo
        logger.debug("Note extends beyond loop: rawEnd=%d (wrapped note)", newEndRaw);
    } else {
        newEnd = newEndRaw;
        logger.debug("Note stays within loop: end=%d", newEndRaw);
    }
    
    // Ensure we have a minimum note length of 1 tick
    if (noteLen == 0) {
        newEnd = newStart + 1;
    }
    
    // Validate the calculated start value is within bounds
    if (newStart < 0 || newStart >= (int32_t)loopLength) {
        logger.debug("Invalid start tick calculation: newStart=%d, loopLength=%lu", 
                     newStart, loopLength);
        return;
    }
    
    logger.debug("Note movement: start %lu->%d, end %lu->%lu, length=%d, wrapped=%s", 
                 manager.movingNote.origStart, newStart, manager.movingNote.origEnd, newEnd, noteLen,
                 (newEnd >= loopLength) ? "YES" : "NO");

    // Find the NoteOn and NoteOff events in midiEvents using persistent identity
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn && evt.data.noteData.note == manager.movingNote.note && evt.tick == manager.movingNote.origStart;
    });
    auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && evt.data.noteData.note == manager.movingNote.note && evt.tick == manager.movingNote.origEnd;
    });
    
    if (onIt == midiEvents.end() || offIt == midiEvents.end()) {
        logger.debug("Could not find MIDI events for moving note");
        return;
    }

    logger.debug("Found MIDI events: NoteOn at tick=%lu, NoteOff at tick=%lu", 
                 onIt->tick, offIt->tick);

    // Move the note to its new position
    onIt->tick = newStart;
    offIt->tick = newEnd;
    
    logger.debug("Moved MIDI events: NoteOn to tick=%d, NoteOff to tick=%lu", 
                 newStart, newEnd);

    // Sort events by tick
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](auto const &a, auto const &b){ return a.tick < b.tick; });

    // Update the moving note identity for next move
    manager.movingNote.origStart = newStart;
    manager.movingNote.origEnd = newEnd;
    
    // Set bracket to the new start position
    manager.setBracketTick(newStart);
    
    // Find the moved note in the reconstructed list and update selectedNoteIdx
    // This prevents other notes from getting highlighted when they overlap
    struct DisplayNote { uint8_t note, velocity; uint32_t startTick, endTick; };
    std::vector<DisplayNote> notes;
    std::map<uint8_t, std::vector<DisplayNote>> activeNoteStacks; // Stack per note pitch
    
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
            DisplayNote dn{evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick};
            activeNoteStacks[evt.data.noteData.note].push_back(dn);
        } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
            // End a note - pop from stack for this pitch (LIFO for overlapping notes)
            auto& stack = activeNoteStacks[evt.data.noteData.note];
            if (!stack.empty()) {
                DisplayNote& dn = stack.back();
                dn.endTick = evt.tick;
                notes.push_back(dn);
                stack.pop_back();
            }
        }
    }
    for (auto& [pitch, stack] : activeNoteStacks) {
        for (auto& dn : stack) {
            dn.endTick = loopLength;
            notes.push_back(dn);
        }
    }
    
    // Find the moved note in the reconstructed list
    int newSelectedIdx = -1;
    for (int i = 0; i < (int)notes.size(); i++) {
        if (notes[i].note == manager.movingNote.note && 
            notes[i].startTick == manager.movingNote.origStart &&
            notes[i].endTick == manager.movingNote.origEnd) {
            newSelectedIdx = i;
            break;
        }
    }
    
    if (newSelectedIdx >= 0) {
        manager.setSelectedNoteIdx(newSelectedIdx);
        logger.debug("Updated selectedNoteIdx to %d for moved note", newSelectedIdx);
    } else {
        logger.debug("Warning: Could not find moved note in reconstructed list");
    }
    
    logger.debug("Updated moving note identity: start=%d, end=%lu", newStart, newEnd);
    logger.debug("EditStartNoteState::onEncoderTurn completed successfully");
}

void EditStartNoteState::onButtonPress(EditManager& manager, Track& track) {
    // Switch back to note state
    manager.setState(manager.getNoteState(), track, manager.getBracketTick());
} 