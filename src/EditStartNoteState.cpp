#include "EditStartNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include "Logger.h"
#include <map>

void EditStartNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    // Optionally log or highlight
    logger.debug("Entered EditStartNoteState");
}

void EditStartNoteState::onExit(EditManager& manager, Track& track) {
    // Optionally log or cleanup
    logger.debug("Exited EditStartNoteState");
}

void EditStartNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    int noteIdx = manager.getSelectedNoteIdx();
    if (noteIdx < 0) return;
    auto& midiEvents = track.getMidiEvents(); // ensure non-const
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
    // Move start tick by delta, keep length constant
    int32_t newStart = (int32_t)dn.startTick + delta;
    if (newStart < 0) newStart = 0;
    if (newStart >= (int32_t)loopLength) newStart = loopLength - 1;
    int32_t noteLen = (int32_t)dn.endTick - (int32_t)dn.startTick;
    int32_t newEnd = newStart + noteLen;
    if (newEnd > (int32_t)loopLength) newEnd = loopLength;
    // Update events
    onIt->tick = newStart;
    offIt->tick = newEnd;
    // Update selection in manager
    manager.selectClosestNote(track, newStart);
}

void EditStartNoteState::onButtonPress(EditManager& manager, Track& track) {
    // Switch back to note state
    manager.setState(manager.getNoteState(), track, manager.getBracketTick());
} 