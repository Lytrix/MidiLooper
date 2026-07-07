//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "MidiEvent.h"
#include <cstdint>
#include <vector>

/// FNV-1a over note-relevant fields of each MIDI event (shared by undo + note cache).
template <typename Alloc>
inline uint32_t midiEventVecFnv1aHash(const std::vector<MidiEvent, Alloc>& midiEvents) {
    uint32_t hash = 2166136261u;
    for (const auto& evt : midiEvents) {
        hash ^= static_cast<uint32_t>(evt.type);
        hash *= 16777619u;
        hash ^= evt.tick;
        hash *= 16777619u;
        hash ^= evt.data.noteData.note;
        hash *= 16777619u;
        hash ^= evt.data.noteData.velocity;
        hash *= 16777619u;
    }
    return hash;
}
