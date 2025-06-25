//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <vector>
#include <deque>
#include "Track.h"
#include "TrackStateMachine.h"
#include "MidiEvent.h"

/**
 * @class TrackUndo
 * @brief Manages undo history for track MIDI events.
 *
 * This class maintains snapshots of a Track's MIDI event list to support
 * undoable operations. Typical usage:
 *   - pushUndoSnapshot(track): record current MIDI state before an overdub or edit.
 *   - undoOverdub(track): restore the last snapshot and remove it from history.
 *   - popLastUndo(track): discard the last snapshot without restoring (e.g., if an edit
 *     yields no net changes using a hash).
 *   - getUndoCount / canUndo: query available undo snapshots.
 *
 * For full-track clear operations, separate clear-track snapshots are managed via
 * pushClearTrackSnapshot and undoClearTrack.
 *
 * Internally, snapshots are stored in deques of std::vector<MidiEvent> for efficient
 * push/pop operations.
 */
class TrackUndo {
public:
    friend class Track;
    // Undo overdub
    static void pushUndoSnapshot(Track& track);
    static void undoOverdub(Track& track);
    static size_t getUndoCount(const Track& track);
    static bool canUndo(const Track& track);
    static void popLastUndo(Track& track);
    static const std::vector<MidiEvent>& peekLastMidiSnapshot(const Track& track);
    static std::deque<std::vector<MidiEvent>>& getMidiHistory(Track& track);
    static const std::vector<MidiEvent>& getCurrentMidiSnapshot(const Track& track);
    // Undo clear
    static void pushClearTrackSnapshot(Track& track);
    static void undoClearTrack(Track& track);
    static bool canUndoClearTrack(const Track& track);
    /**
     * @brief Compute a simple rolling hash (FNV-1a) over the track's current MIDI events
     * @param track The track to hash
     * @return 32-bit hash of current midiEvents
     */
    static uint32_t computeMidiHash(const Track& track);
}; 