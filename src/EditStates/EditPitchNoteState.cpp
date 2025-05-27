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
    std::map<uint8_t, DisplayNote> activeNotes;
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
            DisplayNote dn{evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick};
            activeNotes[evt.data.noteData.note] = dn;
        } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
            auto it = activeNotes.find(evt.data.noteData.note);
            if (it != activeNotes.end()) {
                it->second.endTick = evt.tick;
                notes.push_back(it->second);
                activeNotes.erase(it);
            }
        }
    }
    for (auto& kv : activeNotes) {
        kv.second.endTick = loopLength;
        notes.push_back(kv.second);
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