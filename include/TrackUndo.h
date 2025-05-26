#pragma once
#include <vector>
#include <deque>
#include "Track.h"
#include "TrackStateMachine.h"
#include "MidiEvent.h"

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
}; 