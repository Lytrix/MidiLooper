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
#include <stdexcept>

/**
 * @class EditStartNoteState
 * @brief Implements:
 *   1. onEnter() – set up moving note identity and bracket.
 *   2. onEncoderTurn() – move a note's start/end (detailed in Flow of onEncoderTurn).
 *   3. onExit() – clear move mode state.
 *   4. onButtonPress() – exit move mode and return to NoteState.
 *
 * Flow of onEncoderTurn():
 *   1. Read current moving-note identity (pitch, lastStart, lastEnd).
 *   2. Compute newStart = lastStart + δ (wrapping at loopLength), then newEnd = newStart + noteLen.
 *   3. Reconstruct all DisplayNotes from MIDI events into currentNotes.
 *   4. From manager.movingNote.deletedNotes decide which notes to restore (no longer overlapping).
 *   5. Scan currentNotes for ones that overlap the moved note; for right-to-left motion try to shorten them, otherwise queue them for deletion.
 *   6. Move your NoteOn/NoteOff events to newStart/newEnd.
 *   7. Apply shortening, then deletion, and record all DeletedNote entries into manager.movingNote.deletedNotes.
 *   8. Re-insert any notes that no longer overlap.
 *   9. Sort the MIDI event list, update manager.movingNote.lastStart/lastEnd and bracket, and reselect the moved note index.
 */

// 1. onEnter(): set up moving note identity and bracket.
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
            // CRITICAL: We need to store the actual MIDI event positions, not display positions
            // For wrapped notes, the display shows endTick < startTick, but MIDI events 
            // are stored at their actual tick positions (may be > loopLength)
            manager.movingNote.note = notes[idx].note;
            manager.movingNote.origStart = notes[idx].startTick;
            manager.movingNote.origEnd = notes[idx].endTick;
            
            // Find the actual MIDI event positions for tracking
            uint8_t selectedPitch = notes[idx].note;
            uint32_t selectedStart = notes[idx].startTick;
            uint32_t selectedDisplayEnd = notes[idx].endTick;
            
            // Find the actual NoteOff event position in MIDI events
            uint32_t actualMidiEndTick = selectedDisplayEnd;
            if (selectedDisplayEnd < selectedStart) {
                // This is a wrapped note - find the actual NoteOff position
                for (const auto& evt : midiEvents) {
                    if ((evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && 
                        evt.data.noteData.note == selectedPitch && 
                        evt.tick > selectedStart) {
                        actualMidiEndTick = evt.tick;
                        logger.debug("Found actual MIDI end position %lu for wrapped note (display end=%lu)", 
                                   actualMidiEndTick, selectedDisplayEnd);
                        break;
                    }
                }
            }
            
            // Store the actual MIDI event positions for reliable tracking
            manager.movingNote.lastStart = selectedStart;
            manager.movingNote.lastEnd = actualMidiEndTick;  // Store actual MIDI position, not display position
            manager.movingNote.wrapCount = 0;
            manager.movingNote.active = true;
            manager.movingNote.movementDirection = 0;
            manager.movingNote.deletedEvents.clear();
            manager.movingNote.deletedEventIndices.clear();
            // Set bracket to this note's start
            manager.setBracketTick(notes[idx].startTick);
            logger.debug("Set up moving note identity: note=%d, start=%lu, actualEnd=%lu, displayEnd=%lu", 
                         selectedPitch, selectedStart, actualMidiEndTick, selectedDisplayEnd);
        }
    } else {
        manager.movingNote.active = false;
        manager.selectClosestNote(track, startTick);
    }
}

// 3. onExit(): clear move mode state.
void EditStartNoteState::onExit(EditManager& manager, Track& track) {
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

// 2. onEncoderTurn(): move a note's start/end based on encoder spinning.
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
    
    // currentEnd is now the actual MIDI event position, we need to calculate display length
    uint32_t displayEnd = currentEnd;
    if (currentEnd >= loopLength) {
        // MIDI event is stored beyond loop boundary - calculate wrapped display position
        displayEnd = currentEnd % loopLength;
    }
    
    if (displayEnd >= currentStart) {
        noteLen = displayEnd - currentStart;
    } else {
        // Wrapped note
        noteLen = (loopLength - currentStart) + displayEnd;
    }
    
    if (noteLen == 0) {
        noteLen = 1;
    }
    
    logger.debug("Note length calculation: actualEnd=%lu, displayEnd=%lu, length=%lu", 
                 currentEnd, displayEnd, noteLen);
    
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
    
    // Calculate new end position
    // We need to maintain whether the note will wrap or not based on its length
    uint32_t newEnd = (uint32_t)newStart + noteLen;
    
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
    {
        uint32_t displayMoveStart = (uint32_t)newStart;
        uint32_t displayMoveEnd = (newEnd >= loopLength) ? (newEnd % loopLength) : newEnd;
        for (const auto& deletedNote : manager.movingNote.deletedNotes) {
            if (!notesOverlap(displayMoveStart, displayMoveEnd, deletedNote.startTick, deletedNote.endTick)) {
                notesToRestore.push_back(deletedNote);
                logger.debug("Will restore note: pitch=%d, start=%lu, end=%lu", 
                             deletedNote.note, deletedNote.startTick, deletedNote.endTick);
            }
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
            // Calculate display positions for overlap checking
            uint32_t moveStart = (uint32_t)newStart;
            uint32_t moveEnd = (newEnd >= loopLength) ? (newEnd % loopLength) : newEnd;
            bool overlaps;
            
            // Handle wrapped moving note case
            if (moveEnd < moveStart) {
                // Moving note wraps around loop boundary
                // It overlaps if note overlaps with either part of the wrapped note
                overlaps = (note.startTick < moveEnd) || (moveStart < note.endTick);
            } else {
                // Normal case - moving note doesn't wrap
                overlaps = (moveStart < note.endTick) && (note.startTick < moveEnd);
            }
            
            if (overlaps) {
                // For right-to-left movement (negative delta), try to shorten the note instead of deleting
                if (delta < 0 && note.startTick < moveStart && moveEnd >= moveStart) {
                    // Only try shortening if moving note doesn't wrap (to avoid complex cases)
                    // Calculate what the new end would be if we shorten it
                    uint32_t newNoteEnd = moveStart;
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
    
    // Step 7: Move the selected note's MIDI events
    // Find and update the actual MIDI events for the note we're moving
    
    logger.debug("Looking for NoteOn at tick %lu for pitch %d", currentStart, movingNotePitch);
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn && 
               evt.data.noteData.note == movingNotePitch && 
               evt.tick == currentStart;
    });
    
    // Now currentEnd is the actual MIDI event position, so we can search directly
    logger.debug("Looking for NoteOff at tick %lu for pitch %d", currentEnd, movingNotePitch);
    auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && 
               evt.data.noteData.note == movingNotePitch && 
               evt.tick == currentEnd;
    });
    
    if (onIt == midiEvents.end() || offIt == midiEvents.end()) {
        logger.debug("Could not find MIDI events for selected note (looking for start=%lu, end=%lu)", 
                     currentStart, currentEnd);
        return;
    }
    
    logger.debug("Found MIDI events: NoteOn at %lu, NoteOff at %lu", onIt->tick, offIt->tick);
    
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
        // Remove only the original NoteOn event for this restored note
        midiEvents.erase(std::remove_if(midiEvents.begin(), midiEvents.end(), [&](const MidiEvent& evt) {
            return evt.type == midi::NoteOn &&
                   evt.data.noteData.note == noteToRestore.note &&
                   evt.data.noteData.velocity == noteToRestore.velocity &&
                   evt.tick == noteToRestore.startTick;
        }), midiEvents.end());
        // Remove only the original NoteOff event for this restored note
        midiEvents.erase(std::remove_if(midiEvents.begin(), midiEvents.end(), [&](const MidiEvent& evt) {
            return ((evt.type == midi::NoteOff) ||
                    (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) &&
                   evt.data.noteData.note == noteToRestore.note &&
                   evt.tick == noteToRestore.endTick;
        }), midiEvents.end());
        
        // Create the note with its original stored positions
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
        
        logger.debug("Restored note with original positions: pitch=%d, start=%lu, end=%lu", 
                   noteToRestore.note, noteToRestore.startTick, noteToRestore.endTick);
        
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
    manager.movingNote.lastEnd = newEnd;  // Store actual MIDI position
    
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
    uint32_t displayNewEnd = (newEnd >= loopLength) ? (newEnd % loopLength) : newEnd;
    for (int i = 0; i < (int)currentNotes.size(); i++) {
        if (currentNotes[i].note == movingNotePitch && 
            currentNotes[i].startTick == (uint32_t)newStart &&
            currentNotes[i].endTick == displayNewEnd) {
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

// 4. onButtonPress(): exit move mode and return to NoteState.
void EditStartNoteState::onButtonPress(EditManager& manager, Track& track) {
    // Switch back to note state
    manager.setState(manager.getNoteState(), track, manager.getBracketTick());
}

// Private helper method implementations

bool EditStartNoteState::validatePreconditions(EditManager& manager, Track& track) {
    // Validate basic conditions required for note movement
    // These early checks prevent crashes and undefined behavior
    uint32_t loopLength = track.getLength();
    if (loopLength == 0) {
        logger.debug("Loop length is 0, cannot move notes");
        return false;
    }
    
    if (!manager.movingNote.active) {
        logger.debug("No active moving note, cannot move");
        return false;
    }
    
    return true;
}

void EditStartNoteState::updateMovementDirection(EditManager& manager, int delta) {
    // Track movement direction for different overlap handling strategies
    // Positive delta = moving left (earlier in time)
    // Negative delta = moving right (later in time)
    // Direction affects whether we try to shorten vs delete overlapping notes
    if (delta > 0) {
        manager.movingNote.movementDirection = 1; // Moving left (positive delta)
    } else if (delta < 0) {
        manager.movingNote.movementDirection = -1; // Moving right (negative delta)
    }
}

uint32_t EditStartNoteState::calculateNoteLength(uint32_t start, uint32_t end, uint32_t loopLength) {
    // Calculate note length handling wrapped notes that cross loop boundary
    // Wrapped notes occur when start > end due to loop wraparound
    uint32_t noteLen;
    if (end >= start) {
        // Normal case: note doesn't wrap around loop boundary
        noteLen = end - start;
    } else {
        // Wrapped note: spans across loop boundary (e.g. start=950, end=50 in 1000-tick loop)
        // Length = distance to end of loop + distance from start of loop
        noteLen = (loopLength - start) + end;
    }
    
    return (noteLen == 0) ? 1 : noteLen; // Ensure minimum length of 1 tick to prevent zero-length notes
}

uint32_t EditStartNoteState::wrapPosition(int32_t position, uint32_t loopLength) {
    // Handle position wrapping around loop boundaries
    // This is critical for seamless looping behavior
    if (position < 0) {
        // Moving backwards past start of loop - wrap to end
        position = (int32_t)loopLength + position;
        while (position < 0) {
            // Handle multiple loop wraps for large negative deltas
            position += (int32_t)loopLength;
        }
    } else if (position >= (int32_t)loopLength) {
        // Moving forwards past end of loop - wrap to start
        position = position % (int32_t)loopLength;
    }
    
    return (uint32_t)position;
}

bool EditStartNoteState::notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2) {
    // Comprehensive overlap detection that handles wrapped notes
    // This is complex because notes can wrap around the loop boundary
    // Standard overlap logic fails when end < start due to wrapping
    bool note1Wrapped = (end1 < start1);
    bool note2Wrapped = (end2 < start2);
    
    if (note1Wrapped && note2Wrapped) {
        // Both notes wrap around loop boundary - they always overlap somewhere
        // This is because wrapped notes span the majority of the loop
        return true;
    } else if (note1Wrapped) {
        // Note 1 wraps around loop boundary, note 2 is normal
        // Note 1 occupies [start1, loopEnd] and [0, end1]
        // Check if note 2 intersects either segment
        return (start2 < end1) || (start1 < end2);
    } else if (note2Wrapped) {
        // Note 2 wraps around loop boundary, note 1 is normal
        // Note 2 occupies [start2, loopEnd] and [0, end2]
        // Check if note 1 intersects either segment
        return (start1 < end2) || (start2 < end1);
    } else {
        // Neither note wraps - use standard interval overlap check
        // Two intervals [a,b] and [c,d] overlap if a < d AND c < b
        return (start1 < end2) && (start2 < end1);
    }
} 