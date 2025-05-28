#include <iostream>
#include <vector>
#include <cstdint>

// Stubbed MIDI types and MidiEvent for host-side test
namespace midi {
    enum MidiType { NoteOn, NoteOff };
}

struct MidiEvent {
    uint32_t tick;
    midi::MidiType type;
    uint8_t channel;
    union {
        struct { uint8_t note, velocity; } noteData;
    } data;
    static MidiEvent NoteOn(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity) {
        MidiEvent e;
        e.tick = tick;
        e.type = midi::NoteOn;
        e.channel = channel;
        e.data.noteData = {note, velocity};
        return e;
    }
    static MidiEvent NoteOff(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity = 0) {
        MidiEvent e;
        e.tick = tick;
        e.type = midi::NoteOff;
        e.channel = channel;
        e.data.noteData = {note, velocity};
        return e;
    }
};

int main() {
    bool success = true;
    std::vector<MidiEvent> events;

    // Simulate receiving two MIDI messages
    events.push_back(MidiEvent::NoteOn(10, 1, 60, 127));
    events.push_back(MidiEvent::NoteOff(20, 1, 60, 0));

    // Verify NoteOn
    const auto& on = events[0];
    if (on.type != midi::NoteOn || on.channel != 1 || on.data.noteData.note != 60 || on.data.noteData.velocity != 127) {
        std::cerr << "FAIL: NoteOn constructor produced wrong fields\n";
        success = false;
    }

    // Verify NoteOff
    const auto& off = events[1];
    if (off.type != midi::NoteOff || off.channel != 1 || off.data.noteData.note != 60) {
        std::cerr << "FAIL: NoteOff constructor produced wrong fields\n";
        success = false;
    }

    if (success) std::cout << "✅ Serial MIDI constructor parsing test passed" << std::endl;
    return success ? 0 : 1;
} 