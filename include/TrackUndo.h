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
    static void pushEditPassClosed(Track& track, uint8_t editPassIndex,
                                   EditPassIdList editPassIds, EditPassType editPassType,
                                   uint8_t slotIndex);
    static void beginOverdubSession(Track& track);
    static void undoOverdub(Track& track);
    static void redoOverdub(Track& track);
    static void undoForLoop(Track& track, Loop& loop);
    static void redoForLoop(Track& track, Loop& loop);
    static size_t undoDepthForLoop(const Track& track, const Loop& loop);
    static size_t redoDepthForLoop(const Track& track, const Loop& loop);
    static bool canUndoForLoop(const Track& track, const Loop& loop);
    static bool canRedoForLoop(const Track& track, const Loop& loop);
    static bool canUndoClearTrackForLoop(const Track& track, const Loop& loop);
    static bool canRedoClearTrackForLoop(const Track& track, const Loop& loop);
    static size_t getUndoCount(const Track& track);
    static size_t getRedoCount(const Track& track);
    static bool canUndo(const Track& track);
    static bool canRedo(const Track& track);
    static size_t clearUndoHistoryForSlot(Track& track, uint8_t slotIndex);
    static void pushClearTrackSnapshot(Track& track);
    static void undoClearTrack(Track& track);
    static void redoClearTrack(Track& track);
    static bool canUndoClearTrack(const Track& track);
    static bool canRedoClearTrack(const Track& track);
    static void pushLoopStartSnapshot(Track& track, uint8_t slotIndex);
    static void pushLoopGeometryDepartSnapshot(Track& track, uint8_t slotIndex,
                                               uint32_t beforeLoopStartTick,
                                               uint32_t beforeLoopLengthTicks);
    static void undoLoopStart(Track& track);
};
