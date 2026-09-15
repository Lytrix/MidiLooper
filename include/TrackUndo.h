//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <vector>
#include <deque>
#include "Track.h"
#include "TrackStateMachine.h"
#include "MidiEvent.h"
#include "GlobalUndoStack.h"
#include "LoopSnapshot.h"
#include "Utils/MemoryPool.h"

class TrackUndo {
public:
    friend class Track;
    static void pushRecordPassAdded(Track& track, uint8_t slotIndex, PassId passId);
    static void pushOverdubPassAdded(Track& track, uint8_t slotIndex, PassIdList passIds,
                                     EditPassIdList companionEditPassIds = {});
    /// Stop: one OverdubPassAdded for sealed session wraps plus the last wrap when committed.
    static void pushOverdubSessionOnStop(Track& track, uint8_t slotIndex, PassId lastPassId,
                                         EditPassIdList lastCompanionIds, bool includeLastPass);
    /// Consume completed private IDs into an unpublished entry. Caller supplies
    /// the existing session-before root (or published fallback); no global push.
    static bool prepareOverdubSessionEntry(Loop& loop, Loop::SessionUndoCollection& collection,
        uint8_t slotIndex, const ImmutableLoopSnapshotRef& beforePublished, UndoEntry& entry);
    static void pushNoteEditPassClosed(Track& track, uint8_t noteEditPassIndex,
                                       EditPassIdList editPassIds,
                                       ImmutableLoopSnapshotRef beforePublishedSnapshot = {});
    static void pushEditPassClosed(Track& track, uint8_t editPassIndex,
                                   EditPassIdList editPassIds, EditPassType editPassType,
                                   uint8_t slotIndex,
                                   ImmutableLoopSnapshotRef beforePublishedSnapshot = {});
    /// OpenSpec 5.1: attach `after` onto the GUS entry named by `originatingEntryId`
    /// (Pin A — never tip/slot inference).
    static void attachAfterPublishedSnapshot(Track& track, Loop& loop,
                                             ImmutableLoopSnapshotRef after,
                                             UndoEntryId originatingEntryId);
    /// Resolves the owning track via `trackManager`, then attaches.
    static void attachAfterPublishedSnapshot(Loop& loop,
                                             ImmutableLoopSnapshotRef after,
                                             UndoEntryId originatingEntryId);
    /// Bind deferred after-attach to the current GUS tip when session-exit is queued.
    static void bindTipSessionExitUndoAttach(Track& track, Loop& loop);
    /// Friend-capable published-root select used by undo/redo apply (Pin C).
    static void selectPublishedSnapshotForEntry(Loop& loop, ImmutableLoopSnapshotRef ref);
    static void beginOverdubSession(Track& track);
    static bool undoOverdubSession(Track& track, Loop& loop);
    static bool redoOverdubSession(Track& track, Loop& loop);
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
                                               uint32_t beforeLoopLengthTicks,
                                               PassId geometryId = kInvalidPassId);
    static void rebuildSlotFromLoopContent(Track& track, uint8_t slotIndex);
    static void rebuildTrackFromLoopContent(Track& track, uint8_t focusSlotIndex);
    static void undoLoopStart(Track& track);
};
