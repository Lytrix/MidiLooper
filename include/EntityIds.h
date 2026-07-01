//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// Stable logical-note identity (stored on note-on **MidiEvent** in phase B).
/// **0** means invalid / unassigned.
using NoteId = uint32_t;
constexpr NoteId kInvalidNoteId = 0;

/// Stable track identity for **EditorSelection** (phase B).
/// Invalid sentinel matches **LoopId** — **UINT32_MAX** (not **0**, which is a valid pool index).
using TrackId = uint32_t;
constexpr TrackId kInvalidTrackId = UINT32_MAX;
