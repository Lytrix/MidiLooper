#include "EditStartNoteState.h"
#include "EditManager.h"
#include "MidiEvent.h"
#include "Track.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include "Logger.h"
#include <map>
#include "Globals.h"

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
            manager.movingNote.lastStart = notes[idx].startTick;
            manager.movingNote.lastEnd = notes[idx].endTick;
            manager.movingNote.wrapCount = 0;
            manager.movingNote.active = true;
            manager.movingNote.movementDirection = 0;
            manager.movingNote.deletedEvents.clear();
            manager.movingNote.deletedEventIndices.clear();
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
    
    // Clear any stored deleted events when exiting edit mode
    if (!manager.movingNote.deletedEvents.empty()) {
        logger.debug("Clearing %zu deleted events on exit", manager.movingNote.deletedEvents.size());
        manager.movingNote.deletedEvents.clear();
        manager.movingNote.deletedEventIndices.clear();
    }
    
    // Reset movement tracking
    manager.movingNote.movementDirection = 0;
    manager.movingNote.active = false;
}

void EditStartNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditStartNoteState::onEncoderTurn called with delta=%d", delta);
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLength();
    
    if (loopLength == 0) {
        logger.debug("Loop length is 0, cannot move notes");
        return;
    }
    
    // If we don't have an active moving note, set it up
    if (!manager.movingNote.active) {
        logger.debug("No active moving note, cannot move");
        return;
    }
    
    // Update movement direction
    if (delta > 0) {
        manager.movingNote.movementDirection = 1; // Moving left (positive delta)
    } else if (delta < 0) {
        manager.movingNote.movementDirection = -1; // Moving right (negative delta)
    }
    
    // Use the moving note identity instead of reconstructing from selected index
    // This ensures we always track the same note even after MIDI events change
    uint8_t movingNotePitch = manager.movingNote.note;
    uint32_t currentStart = manager.movingNote.lastStart;
    uint32_t currentEnd = manager.movingNote.lastEnd;
    
    logger.debug("Moving note: pitch=%d, start=%lu, end=%lu", 
                 movingNotePitch, currentStart, currentEnd);
    
    // Calculate note length
    uint32_t noteLen;
    if (currentEnd >= currentStart) {
        noteLen = currentEnd - currentStart;
    } else {
        // Wrapped note
        noteLen = (loopLength - currentStart) + currentEnd;
    }
    
    if (noteLen == 0) {
        noteLen = 1;
    }
    
    // Calculate new position
    int32_t newStart = (int32_t)currentStart + delta;
    
    // Handle wrap-around
    if (newStart < 0) {
        newStart = (int32_t)loopLength + newStart;
        while (newStart < 0) {
            newStart += (int32_t)loopLength;
        }
    } else if (newStart >= (int32_t)loopLength) {
        newStart = newStart % (int32_t)loopLength;
    }
    
    uint32_t newEnd = ((uint32_t)newStart + noteLen) % loopLength;
    
    logger.debug("Movement: start %lu->%d, end %lu->%lu, length=%lu", 
                 currentStart, newStart, currentEnd, newEnd, noteLen);
    
    // Reconstruct current notes to find overlaps
    struct DisplayNote { uint8_t note, velocity; uint32_t startTick, endTick; };
    std::vector<DisplayNote> currentNotes;
    std::map<uint8_t, std::vector<DisplayNote>> activeNoteStacks;
    
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
            DisplayNote dn{evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick};
            activeNoteStacks[evt.data.noteData.note].push_back(dn);
        } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
            auto& stack = activeNoteStacks[evt.data.noteData.note];
            if (!stack.empty()) {
                DisplayNote& dn = stack.back();
                dn.endTick = evt.tick;
                currentNotes.push_back(dn);
                stack.pop_back();
            }
        }
    }
    for (auto& [pitch, stack] : activeNoteStacks) {
        for (auto& dn : stack) {
            dn.endTick = loopLength;
            currentNotes.push_back(dn);
        }
    }
    
    // Store notes to delete and restore
    std::vector<DisplayNote> notesToDelete;
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> notesToRestore;
    
    // Check if we should restore any previously deleted/shortened notes
    for (const auto& deletedNote : manager.movingNote.deletedNotes) {
        // Check if the deleted note no longer overlaps with the moving note
        bool overlaps;
        
        // Handle wrapped moving note case
        if (newEnd < (uint32_t)newStart) {
            // Moving note wraps around loop boundary
            // It overlaps if deleted note overlaps with either part of the wrapped note
            overlaps = (deletedNote.startTick < newEnd) || ((uint32_t)newStart < deletedNote.endTick);
        } else {
            // Normal case - moving note doesn't wrap
            overlaps = ((uint32_t)newStart < deletedNote.endTick) && (deletedNote.startTick < newEnd);
        }
        
        if (!overlaps) {
            notesToRestore.push_back(deletedNote);
            logger.debug("Will restore note: pitch=%d, start=%lu, end=%lu", 
                       deletedNote.note, deletedNote.startTick, deletedNote.endTick);
        }
    }
    
    logger.debug("Found %zu notes to restore, %zu total deleted notes", 
                 notesToRestore.size(), manager.movingNote.deletedNotes.size());
    
    // Store notes to shorten
    std::vector<std::pair<DisplayNote, uint32_t>> notesToShorten; // note and new end tick
    
    // Find notes that will overlap with the moved note
    for (const auto& note : currentNotes) {
        if (note.note == movingNotePitch && 
            (note.startTick != currentStart || note.endTick != currentEnd)) {
            
            // Check if this note overlaps with the new position
            bool overlaps;
            
            // Handle wrapped moving note case
            if (newEnd < (uint32_t)newStart) {
                // Moving note wraps around loop boundary
                // It overlaps if note overlaps with either part of the wrapped note
                overlaps = (note.startTick < newEnd) || ((uint32_t)newStart < note.endTick);
            } else {
                // Normal case - moving note doesn't wrap
                overlaps = ((uint32_t)newStart < note.endTick) && (note.startTick < newEnd);
            }
            
            if (overlaps) {
                // For right-to-left movement (negative delta), try to shorten the note instead of deleting
                if (delta < 0 && note.startTick < (uint32_t)newStart && newEnd >= (uint32_t)newStart) {
                    // Only try shortening if moving note doesn't wrap (to avoid complex cases)
                    // Calculate what the new end would be if we shorten it
                    uint32_t newNoteEnd = (uint32_t)newStart;
                    uint32_t shortenedLength = newNoteEnd - note.startTick;
                    
                    // Only shorten if the result would be at least 48 ticks (1/16th note)
                    if (shortenedLength >= Config::TICKS_PER_16TH_STEP) {
                        notesToShorten.push_back({note, newNoteEnd});
                        logger.debug("Will shorten note: pitch=%d, start=%lu, end=%lu->%lu, length=%lu", 
                                   note.note, note.startTick, note.endTick, newNoteEnd, shortenedLength);
                    } else {
                        // Too short after shortening, delete it
                        notesToDelete.push_back(note);
                        logger.debug("Will delete note (too short after shortening): pitch=%d, start=%lu, end=%lu", 
                                   note.note, note.startTick, note.endTick);
                    }
                } else {
                    // For left-to-right movement, wrapped notes, or other cases, delete the note
                    notesToDelete.push_back(note);
                    logger.debug("Will delete overlapping note: pitch=%d, start=%lu, end=%lu", 
                               note.note, note.startTick, note.endTick);
                }
            }
        }
    }
    
    logger.debug("Found %zu notes to delete", notesToDelete.size());
    
    // Move the selected note's MIDI events
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn && 
               evt.data.noteData.note == movingNotePitch && 
               evt.tick == currentStart;
    });
    auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && 
               evt.data.noteData.note == movingNotePitch && 
               evt.tick == currentEnd;
    });
    
    if (onIt == midiEvents.end() || offIt == midiEvents.end()) {
        logger.debug("Could not find MIDI events for selected note");
        return;
    }
    
    // Move the note
    onIt->tick = newStart;
    offIt->tick = newEnd;
    
    // Shorten overlapping notes
    for (const auto& [noteToShorten, newEndTick] : notesToShorten) {
        // Find and update the note's end event
        auto endIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
            return (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && 
                   evt.data.noteData.note == noteToShorten.note && 
                   evt.tick == noteToShorten.endTick;
        });
        
        if (endIt != midiEvents.end()) {
            // Store the original note for potential restoration
            EditManager::MovingNoteIdentity::DeletedNote originalNote;
            originalNote.note = noteToShorten.note;
            originalNote.velocity = noteToShorten.velocity;
            originalNote.startTick = noteToShorten.startTick;
            originalNote.endTick = noteToShorten.endTick;
            manager.movingNote.deletedNotes.push_back(originalNote);
            
            // Update the end tick to shorten the note
            endIt->tick = newEndTick;
            
            logger.debug("Shortened note: pitch=%d, start=%lu, end=%lu->%lu", 
                       noteToShorten.note, noteToShorten.startTick, noteToShorten.endTick, newEndTick);
        } else {
            logger.debug("Warning: Could not find end event for note to shorten");
        }
    }
    
    // Delete overlapping notes from MIDI events
    for (const auto& noteToDelete : notesToDelete) {
        auto it = midiEvents.begin();
        while (it != midiEvents.end()) {
            if ((it->type == midi::NoteOn && it->data.noteData.velocity > 0 &&
                 it->data.noteData.note == noteToDelete.note && it->tick == noteToDelete.startTick) ||
                ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) &&
                 it->data.noteData.note == noteToDelete.note && it->tick == noteToDelete.endTick)) {
                it = midiEvents.erase(it);
            } else {
                ++it;
            }
        }
        
        // Store deleted note for potential restoration
        EditManager::MovingNoteIdentity::DeletedNote deletedNote;
        deletedNote.note = noteToDelete.note;
        deletedNote.velocity = noteToDelete.velocity;
        deletedNote.startTick = noteToDelete.startTick;
        deletedNote.endTick = noteToDelete.endTick;
        manager.movingNote.deletedNotes.push_back(deletedNote);
    }
    
    // Restore notes that no longer overlap
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> notesToRemoveFromDeleted;
    
    for (const auto& noteToRestore : notesToRestore) {
        // Check if this is a shortened note (has a NoteOn event but different end)
        auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](const MidiEvent& evt) {
            return evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                   evt.data.noteData.note == noteToRestore.note && 
                   evt.tick == noteToRestore.startTick;
        });
        
        if (onIt != midiEvents.end()) {
            // This is a shortened note - find its current end event
            // We need to find the CLOSEST end event after the start that belongs to this note
            auto endIt = midiEvents.end();
            uint32_t closestEndTick = UINT32_MAX;
            
            for (auto it = midiEvents.begin(); it != midiEvents.end(); ++it) {
                if ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) && 
                    it->data.noteData.note == noteToRestore.note && 
                    it->tick > noteToRestore.startTick && 
                    it->tick < closestEndTick) {
                    endIt = it;
                    closestEndTick = it->tick;
                }
            }
            
            if (endIt != midiEvents.end()) {
                // Only extend if the current end is shorter than the original
                if (endIt->tick < noteToRestore.endTick) {
                    endIt->tick = noteToRestore.endTick;
                    logger.debug("Extended shortened note: pitch=%d, start=%lu, end=%lu->%lu", 
                               noteToRestore.note, noteToRestore.startTick, closestEndTick, noteToRestore.endTick);
                } else {
                    logger.debug("Note already at correct length: pitch=%d, start=%lu, end=%lu", 
                               noteToRestore.note, noteToRestore.startTick, endIt->tick);
                }
            } else {
                logger.debug("Warning: Could not find shortened note end event to extend");
            }
        } else {
            // This is a completely deleted note - recreate it
            MidiEvent onEvent;
            onEvent.type = midi::NoteOn;
            onEvent.tick = noteToRestore.startTick;
            onEvent.data.noteData.note = noteToRestore.note;
            onEvent.data.noteData.velocity = noteToRestore.velocity;
            midiEvents.push_back(onEvent);
            
            MidiEvent offEvent;
            offEvent.type = midi::NoteOff;
            offEvent.tick = noteToRestore.endTick;
            offEvent.data.noteData.note = noteToRestore.note;
            offEvent.data.noteData.velocity = 0;
            midiEvents.push_back(offEvent);
            
            logger.debug("Restored deleted note: pitch=%d, start=%lu, end=%lu", 
                       noteToRestore.note, noteToRestore.startTick, noteToRestore.endTick);
        }
        
        // Mark for removal from deleted notes list
        notesToRemoveFromDeleted.push_back(noteToRestore);
    }
    
    // Remove restored notes from deleted notes list (do this after restoration to avoid iterator issues)
    for (const auto& noteToRemove : notesToRemoveFromDeleted) {
        auto it = std::find_if(manager.movingNote.deletedNotes.begin(), 
                             manager.movingNote.deletedNotes.end(),
                             [&](const auto& dn) {
            return dn.note == noteToRemove.note && 
                   dn.startTick == noteToRemove.startTick && 
                   dn.endTick == noteToRemove.endTick;
        });
        if (it != manager.movingNote.deletedNotes.end()) {
            manager.movingNote.deletedNotes.erase(it);
            logger.debug("Removed restored note from deleted list: pitch=%d, start=%lu, end=%lu", 
                       noteToRemove.note, noteToRemove.startTick, noteToRemove.endTick);
        }
    }
    
    // Sort events by tick
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](auto const &a, auto const &b){ return a.tick < b.tick; });
    
    // Update moving note tracking
    manager.movingNote.lastStart = newStart;
    manager.movingNote.lastEnd = newEnd;
    
    // Set bracket to new position
    manager.setBracketTick(newStart);
    
    // Reconstruct notes to find the new selected index
    currentNotes.clear();
    activeNoteStacks.clear();
    
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
            DisplayNote dn{evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick};
            activeNoteStacks[evt.data.noteData.note].push_back(dn);
        } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
            auto& stack = activeNoteStacks[evt.data.noteData.note];
            if (!stack.empty()) {
                DisplayNote& dn = stack.back();
                dn.endTick = evt.tick;
                currentNotes.push_back(dn);
                stack.pop_back();
            }
        }
    }
    for (auto& [pitch, stack] : activeNoteStacks) {
        for (auto& dn : stack) {
            dn.endTick = loopLength;
            currentNotes.push_back(dn);
        }
    }
    
    // Find the moved note's new index
    int newSelectedIdx = -1;
    for (int i = 0; i < (int)currentNotes.size(); i++) {
        if (currentNotes[i].note == movingNotePitch && 
            currentNotes[i].startTick == (uint32_t)newStart &&
            currentNotes[i].endTick == newEnd) {
            newSelectedIdx = i;
            break;
        }
    }
    
    if (newSelectedIdx >= 0) {
        manager.setSelectedNoteIdx(newSelectedIdx);
        logger.debug("Updated selectedNoteIdx to %d for moved note", newSelectedIdx);
    } else {
        logger.debug("Warning: Could not find moved note in final note list");
    }
    
    logger.debug("EditStartNoteState::onEncoderTurn completed successfully");
}

void EditStartNoteState::onButtonPress(EditManager& manager, Track& track) {
    // Switch back to note state
    manager.setState(manager.getNoteState(), track, manager.getBracketTick());
} 