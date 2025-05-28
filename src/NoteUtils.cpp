#include "NoteUtils.h"

std::vector<NoteUtils::DisplayNote> NoteUtils::reconstructNotes(const std::vector<MidiEvent>& midiEvents, uint32_t loopLength) {
    using DisplayNote = NoteUtils::DisplayNote;
    std::vector<DisplayNote> notes;
    std::map<uint8_t, std::vector<DisplayNote>> activeNoteStacks;

    for (const auto& evt : midiEvents) {
        bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        bool isNoteOff = (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        uint8_t pitch = evt.data.noteData.note;
        uint8_t velocity = evt.data.noteData.velocity;
        if (isNoteOn) {
            DisplayNote dn{pitch, velocity, evt.tick, evt.tick};
            activeNoteStacks[pitch].push_back(dn);
        } else if (isNoteOff) {
            auto& stack = activeNoteStacks[pitch];
            if (!stack.empty()) {
                DisplayNote dn = stack.back();
                dn.endTick = evt.tick;
                notes.push_back(dn);
                stack.pop_back();
            }
        }
    }

    // Any notes still active at loop end
    for (auto& kv : activeNoteStacks) {
        auto& stack = kv.second;
        for (auto& dn : stack) {
            dn.endTick = loopLength;
            notes.push_back(dn);
        }
    }

    return notes;
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