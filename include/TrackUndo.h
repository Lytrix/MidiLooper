//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <vector>
#include <deque>
#include "Track.h"
#include "TrackStateMachine.h"
#include "MidiEvent.h"
#include "Utils/MemoryPool.h"

class TrackUndo {
public:
    friend class Track;
    static void pushRecordPassAdded(Track& track, uint8_t slotIndex, PassId passId);
    static void pushOverdubPassAdded(Track& track, uint8_t slotIndex, PassId passId);
    static void pushNoteEditPassClosed(Track& track, uint8_t noteEditPassIndex,
                                       EditPassIdList editPassIds);
    static void beginOverdubSession(Track& track);
    static void endOverdubSession(Track& track);
    static void undoOverdub(Track& track);
    static void redoOverdub(Track& track);
    static size_t getUndoCount(const Track& track);
    static size_t getRedoCount(const Track& track);
    static bool canUndo(const Track& track);
    static bool canRedo(const Track& track);
    static void popLastUndo(Track& track);
    static size_t clearUndoHistoryForSlot(Track& track, uint8_t slotIndex);
    static const MidiEventVec& peekLastMidiSnapshot(const Track& track);
    static const MidiEventVec& getCurrentMidiSnapshot(const Track& track);
    static void pushClearTrackSnapshot(Track& track);
    static void undoClearTrack(Track& track);
    static void redoClearTrack(Track& track);
    static bool canUndoClearTrack(const Track& track);
    static bool canRedoClearTrack(const Track& track);
    static void pushLoopStartSnapshot(Track& track);
    static void undoLoopStart(Track& track);
    static void redoLoopStart(Track& track);
    static bool canUndoLoopStart(const Track& track);
    static bool canRedoLoopStart(const Track& track);
    static uint32_t computeMidiHash(const Track& track);
};
