//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditLengthNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include "Logger.h"
#include "TrackUndo.h"
#include "Utils/NoteUtils.h"
#include "Globals.h"
#include <algorithm>

void EditLengthNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("EditLengthNoteState::onEnter");
    
    // Store initial hash for commit-on-exit
    initialHash = TrackUndo::computeMidiHash(track);
    
    // Push undo snapshot for length editing
    TrackUndo::pushUndoSnapshot(track);
    
    // Select the closest note to edit
    manager.selectClosestNote(track, startTick);
    
    if (manager.getSelectedNoteIdx() >= 0) {
        // Move bracket to the end of the selected note for length editing
          uint32_t loopLength = track.getLoopLength();
  const auto& notes = track.getCachedNotes();
        
        if (manager.getSelectedNoteIdx() < (int)notes.size()) {
            auto& selectedNote = notes[manager.getSelectedNoteIdx()];
            uint32_t noteEnd = selectedNote.endTick;
            manager.setBracketTick(noteEnd % loopLength);
            
            logger.info("EditLengthNoteState: Selected note %d for length editing, moved bracket to end position %lu", 
                       manager.getSelectedNoteIdx(), noteEnd % loopLength);
        } else {
            logger.info("EditLengthNoteState: Selected note %d for length editing", manager.getSelectedNoteIdx());
        }
    } else {
        logger.info("EditLengthNoteState: No note selected, will create new note");
    }
}

void EditLengthNoteState::onExit(EditManager& manager, Track& track) {
    logger.debug("EditLengthNoteState::onExit");
    // Note: commit-on-exit hash checking is handled in EditManager::setState
}

void EditLengthNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditLengthNoteState::onEncoderTurn called with delta=%d", delta);
    
    if (manager.getSelectedNoteIdx() < 0) {
        logger.debug("EditLengthNoteState: No note selected, selectedNoteIdx=%d", manager.getSelectedNoteIdx());
        return;
    }
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    
    logger.debug("EditLengthNoteState: Loop length=%lu, MIDI events count=%zu", loopLength, midiEvents.size());
    
    if (loopLength == 0) {
        logger.debug("EditLengthNoteState: Loop length is 0, cannot edit");
        return;
    }
    
    // Use cached notes to find the selected one
    const auto& notes = track.getCachedNotes();
    if (manager.getSelectedNoteIdx() >= (int)notes.size()) {
        logger.debug("EditLengthNoteState: Selected note index %d out of range (notes size: %zu)", 
                     manager.getSelectedNoteIdx(), notes.size());
        return;
    }
    
    auto& selectedNote = notes[manager.getSelectedNoteIdx()];
    uint8_t notePitch = selectedNote.note;
    uint32_t noteStart = selectedNote.startTick;
    uint32_t currentEnd = selectedNote.endTick;
    
    logger.debug("EditLengthNoteState: Selected note - pitch=%d, start=%lu, end=%lu", 
                 notePitch, noteStart, currentEnd);
    
    // Calculate length change using single tick increments (same as EditStartNoteState)
    // Each delta step represents one tick for fine-grained control
    int lengthDelta = delta;
    
    logger.debug("EditLengthNoteState: Length delta calculation - delta=%d, lengthDelta=%d", 
                 delta, lengthDelta);
    
    // Calculate current note length
    uint32_t currentLength;
    if (currentEnd >= noteStart) {
        currentLength = currentEnd - noteStart;
    } else {
        // Wrapped note
        currentLength = (loopLength - noteStart) + currentEnd;
    }
    
    // Calculate new length
    int32_t newLengthInt = (int32_t)currentLength + lengthDelta;
    
    // Constrain to minimum length (one tick for fine control)
    if (newLengthInt < 1) {
        newLengthInt = 1;
    }
    
    // Constrain to maximum length (one loop)
    if (newLengthInt > (int32_t)loopLength) {
        newLengthInt = loopLength;
    }
    
    uint32_t newLength = (uint32_t)newLengthInt;
    uint32_t newEnd = (noteStart + newLength) % loopLength;
    
    logger.debug("EditLengthNoteState: Changing note length from %lu to %lu ticks (delta=%d)", 
                 currentLength, newLength, lengthDelta);
    
    // Find and update the NoteOff event
    bool foundOff = false;
    for (auto& event : midiEvents) {
        if ((event.type == midi::NoteOff || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
            event.data.noteData.note == notePitch && 
            event.tick == currentEnd) {
            
            event.tick = newEnd;
            foundOff = true;
            logger.debug("EditLengthNoteState: Updated NoteOff event to tick %lu", newEnd);
            break;
        }
    }
    
    if (!foundOff) {
        logger.debug("EditLengthNoteState: Warning - could not find NoteOff event to update");
    }
    
    // Sort events to maintain order
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    
    // Update bracket position to the new end position
    manager.setBracketTick(newEnd % loopLength);
    
    // Re-select the note by finding it again in the updated note list
    const auto& updatedNotes = track.getCachedNotes();
    for (int i = 0; i < (int)updatedNotes.size(); ++i) {
        if (updatedNotes[i].note == notePitch && updatedNotes[i].startTick == noteStart) {
            manager.setSelectedNoteIdx(i);
            break;
        }
    }
}

void EditLengthNoteState::onButtonPress(EditManager& manager, Track& track) {
    logger.debug("EditLengthNoteState::onButtonPress - exiting length edit mode");
    // This will be handled by the MidiButtonManager cycling logic
} 