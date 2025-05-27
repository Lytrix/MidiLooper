#pragma once
#include <MIDI.h>
#include <cstdint>
#include <algorithm> // For std::clamp

struct MidiEvent {
    uint32_t tick;           // When this event occurs
    midi::MidiType type;     // What kind of MIDI event, Note, CC, Pitch Bend, etc.
    uint8_t channel;         // MIDI channel (1-16)
    union {
        // Channel Voice Messages
        struct { uint8_t note, velocity; } noteData;      // NoteOn/NoteOff
        struct { uint8_t note, pressure; } polyATData;    // Polyphonic Aftertouch
        struct { uint8_t cc, value; } ccData;            // Control Change
        uint8_t program;                                 // Program Change
        uint8_t channelPressure;                        // Channel Aftertouch (mono)
        int16_t pitchBend;                              // Pitch Bend (-8192 to +8191)

        // System Common Messages
        struct {                                        // System Exclusive
            const uint8_t* data;                        // Pointer to SysEx data
            uint16_t length;                            // Length of SysEx data
        } sysexData;
        uint8_t timeCode;                               // MIDI Time Code Quarter Frame
        uint16_t songPosition;                          // Song Position Pointer (14-bit)
        uint8_t songNumber;                             // Song Select (0-127)

        // No additional data needed for:
        // - TuneRequest
        // - Clock
        // - Start
        // - Continue
        // - Stop
        // - ActiveSensing
        // - SystemReset
    } data;

    // Default constructor
    MidiEvent() : tick(0), type(midi::InvalidType), channel(0) {}

    // Helper for clamping values to MIDI range with runtime error reporting
    static uint8_t clampChannel(uint8_t channel) {
        if (channel < 1 || channel > 16) {
            Serial.print("[MidiEvent] WARNING: Channel out of range: ");
            Serial.print(channel);
            Serial.println(" (clamped to 1-16)");
        }
        return std::clamp(channel, (uint8_t)1, (uint8_t)16);
    }
    static uint8_t clamp7bit(uint8_t v, const char* label = nullptr) {
        if (v > 127) {
            if (label) {
                Serial.print("[MidiEvent] WARNING: ");
                Serial.print(label);
                Serial.print(" out of range: ");
                Serial.print(v);
                Serial.println(" (clamped to 0-127)");
            } else {
                Serial.print("[MidiEvent] WARNING: 7-bit value out of range: ");
                Serial.print(v);
                Serial.println(" (clamped to 0-127)");
            }
        }
        return std::clamp(v, (uint8_t)0, (uint8_t)127);
    }
    static int16_t clampPitchBend(int16_t v) {
        if (v < -8192 || v > 8191) {
            Serial.print("[MidiEvent] WARNING: Pitch bend out of range: ");
            Serial.print(v);
            Serial.println(" (clamped to -8192 to 8191)");
        }
        return std::clamp(v, (int16_t)-8192, (int16_t)8191);
    }
    static uint16_t clamp14bit(uint16_t v) {
        if (v > 0x3FFF) {
            Serial.print("[MidiEvent] WARNING: 14-bit value out of range: ");
            Serial.print(v);
            Serial.println(" (clamped to 0-16383)");
        }
        return std::clamp(v, (uint16_t)0, (uint16_t)0x3FFF);
    }

    // Channel Voice Message Constructors
    static MidiEvent NoteOn(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::NoteOn;
        evt.channel = clampChannel(channel); // 1-16
        evt.data.noteData = {clamp7bit(note, "Note"), clamp7bit(velocity, "Velocity")};
        return evt;
    }

    static MidiEvent NoteOff(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity = 0) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::NoteOff;
        evt.channel = clampChannel(channel); // 1-16
        evt.data.noteData = {clamp7bit(note, "Note"), clamp7bit(velocity, "Velocity")};
        return evt;
    }

    static MidiEvent PolyAftertouch(uint32_t tick, uint8_t channel, uint8_t note, uint8_t pressure) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::AfterTouchPoly;
        evt.channel = clampChannel(channel); // 1-16
        evt.data.polyATData = {clamp7bit(note, "Note"), clamp7bit(pressure, "Pressure")};
        return evt;
    }

    static MidiEvent ControlChange(uint32_t tick, uint8_t channel, uint8_t cc, uint8_t value) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::ControlChange;
        evt.channel = clampChannel(channel); // 1-16
        evt.data.ccData = {clamp7bit(cc, "CC"), clamp7bit(value, "CC Value")};
        return evt;
    }

    static MidiEvent ProgramChange(uint32_t tick, uint8_t channel, uint8_t program) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::ProgramChange;
        evt.channel = clampChannel(channel); // 1-16
        evt.data.program = clamp7bit(program, "Program");
        return evt;
    }

    static MidiEvent ChannelAftertouch(uint32_t tick, uint8_t channel, uint8_t pressure) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::AfterTouchChannel;
        evt.channel = clampChannel(channel); // 1-16
        evt.data.channelPressure = clamp7bit(pressure, "Channel Pressure");
        return evt;
    }

    static MidiEvent PitchBend(uint32_t tick, uint8_t channel, int16_t value) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::PitchBend;
        evt.channel = clampChannel(channel); // 1-16
        evt.data.pitchBend = clampPitchBend(value);
        return evt;
    }

    // System Common Message Constructors
    static MidiEvent SysEx(uint32_t tick, const uint8_t* data, uint16_t length) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::SystemExclusive;
        evt.channel = 0;  // System messages don't use channel
        evt.data.sysexData = {data, length};
        return evt;
    }

    static MidiEvent TimeCode(uint32_t tick, uint8_t data) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::TimeCodeQuarterFrame;
        evt.channel = 0;
        evt.data.timeCode = clamp7bit(data, "TimeCode");
        return evt;
    }

    static MidiEvent SongPosition(uint32_t tick, uint16_t beats) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::SongPosition;
        evt.channel = 0;
        evt.data.songPosition = clamp14bit(beats);
        return evt;
    }

    static MidiEvent SongSelect(uint32_t tick, uint8_t song) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::SongSelect;
        evt.channel = 0;
        evt.data.songNumber = clamp7bit(song, "Song Number");
        return evt;
    }

    // System Real-Time Message Constructors (no data payload)
    static MidiEvent Clock(uint32_t tick) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::Clock;
        evt.channel = 0;
        return evt;
    }

    static MidiEvent Start(uint32_t tick) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::Start;
        evt.channel = 0;
        return evt;
    }

    static MidiEvent Continue(uint32_t tick) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::Continue;
        evt.channel = 0;
        return evt;
    }

    static MidiEvent Stop(uint32_t tick) {
        MidiEvent evt;
        evt.tick = tick;
        evt.type = midi::Stop;
        evt.channel = 0;
        return evt;
    }

    // Helper methods for type checking
    bool isNoteOn() const { return type == midi::NoteOn && data.noteData.velocity > 0; }
    bool isNoteOff() const { 
        return type == midi::NoteOff || 
               (type == midi::NoteOn && data.noteData.velocity == 0); 
    }
    bool isChannelVoice() const { 
        return type >= midi::NoteOff && type <= midi::PitchBend;
    }
    bool isSystemCommon() const {
        return type >= midi::SystemExclusive && type <= midi::TuneRequest;
    }
    bool isRealTime() const {
        return type >= midi::Clock && type <= midi::SystemReset;
    }
};

