#include "EditPitchNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include "Logger.h"
#include <map>

void EditPitchNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("Entered EditPitchNoteState");
}

void EditPitchNoteState::onExit(EditManager& manager, Track& track) {
    logger.debug("Exited EditPitchNoteState");
}

void EditPitchNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    int noteIdx = manager.getSelectedNoteIdx();
    if (noteIdx < 0) return;
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLength();
    // Reconstruct notes
    struct DisplayNote {
        uint8_t note;
        uint8_t velocity;
        uint32_t startTick;
        uint32_t endTick;
    };
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