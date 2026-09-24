//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Owner of the slot-clear playback restore, shared by the two routes that need it.
//
// `MidiButtonActions::handleClearTrack` and `TrackUndo`'s clear-redo side effects both need the same
// "after this slot was cleared, play whatever enabled slot still has content" step. It used to be
// defined twice -- `MidiButtonActions.cpp` in an anonymous namespace and `TrackUndo.cpp` at namespace
// scope. Separate translation units never collide, but the native harness includes production `.cpp`
// files into one concatenated TU, so any suite that links **both** routes (Packet C / A3.4a's
// MIDI-dispatch fixtures) got an ambiguous call.
//
// A3.4a moved it here: one definition, one owner, external linkage, no include-order or macro seam. It
// stays `TRACK_COLD_MEM` because a slot clear is not a MIDI clock hot path.

#pragma once

#include <cstdint>

#include "Utils/TrackMem.h"

class Track;

/// Select and restart the first enabled, unmuted slot that still has content after a clear; otherwise
/// silence the track and drop it out of TRACK_PLAYING (clear of the last enabled playing slot must not
/// leave TRACK_PLAYING on empty RAM -- otherwise a record press takes the mute-toggle path,
/// session_20260803_170251).
TRACK_COLD_MEM void restorePlaybackAfterSlotClear(uint8_t trackIndex, Track& track, uint32_t now);
