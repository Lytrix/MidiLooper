//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditPitchNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include "Logger.h"
#include <map>
#include "TrackUndo.h"
#include "Utils/NoteUtils.h"

using DisplayNote = NoteUtils::DisplayNote;

void EditPitchNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("Entered EditPitchNoteState");
    // Commit-on-enter: snapshot and hash initial MIDI events
    initialHash = TrackUndo::computeMidiHash(track);
    TrackUndo::pushUndoSnapshot(track);
    logger.debug("Snapshot on enter (pitch), initial hash: %u", initialHash);
    
    // Move bracket to the start of the selected note for pitch editing
    if (manager.getSelectedNoteIdx() >= 0) {
          uint32_t loopLength = track.getLoopLength();
  const auto& notes = track.getCachedNotes();
        
        if (manager.getSelectedNoteIdx() < (int)notes.size()) {
            auto& selectedNote = notes[manager.getSelectedNoteIdx()];
            uint32_t noteStart = selectedNote.startTick;
            manager.setBracketTick(noteStart % loopLength);
            
            logger.debug("EditPitchNoteState: Moved bracket to note start position %lu", noteStart % loopLength);
        }
    }
}

void EditPitchNoteState::onExit(EditManager& manager, Track& track) {
    logger.debug("Exited EditPitchNoteState");
}

void EditPitchNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    int noteIdx = manager.getSelectedNoteIdx();
    if (noteIdx < 0) return;
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    // Use cached notes for optimal performance
    const auto& notes = track.getCachedNotes();
    
    if (noteIdx >= (int)notes.size()) return;
    // Find the corresponding MidiEvent indices for this note
    const auto& dn = notes[noteIdx];
    // Find the NoteOn and NoteOff events in midiEvents using non-const iterators
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn && evt.data.noteData.note == dn.note && evt.tick == dn.startTick;
    });
    auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && evt.data.noteData.note == dn.note && evt.tick == dn.endTick;
    });
    if (onIt == midiEvents.end() || offIt == midiEvents.end()) return;
    // Change pitch, wrap 0-127
    int newPitch = ((int)dn.note + delta + 128) % 128;
    onIt->data.noteData.note = newPitch;
    offIt->data.noteData.note = newPitch;
    // Update selection in manager
    manager.selectClosestNote(track, onIt->tick);
}

void EditPitchNoteState::onButtonPress(EditManager& manager, Track& track) {
    // No-op for pitch edit
} 