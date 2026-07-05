//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackUndo.h"
#include "Track.h"
#include "EditManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "ClockManager.h"
#include "Globals.h"
#include "PassReclaim.h"
#include "TrackManager.h"
#include "Utils/MemoryPool.h"
#include "Utils/MidiEventVecFnvHash.h"

extern TrackManager trackManager;

namespace {

UndoLoopGeometry captureGeometry(const Loop& loop) {
    return {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick};
}

void applyGeometry(Loop& loop, const UndoLoopGeometry& geometry) {
    loop.loopLengthTicks = geometry.loopLengthTicks;
    loop.startLoopTick = geometry.startLoopTick;
    loop.loopStartTick = geometry.loopStartTick;
}

void dropRedoBranch(GlobalUndoStack& stack) {
    if (stack.cursor >= stack.entries.size()) {
        return;
    }
    stack.entries.erase(stack.entries.begin() + static_cast<std::ptrdiff_t>(stack.cursor), stack.entries.end());
    trackManager.reclaimUnreferencedDisabledPasses();
}

void trimUndoStackForMemory(Track& track) {
    if (trimGlobalUndoStackForMemory(track.getGlobalUndoStack()) > 0) {
        trackManager.reclaimUnreferencedDisabledPasses();
    }
}

uint8_t resolveTrackIndexForPersistence(const Track& track) {
    for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
        if (&trackManager.getTrack(i) == &track) {
            return i;
        }
    }
    return trackManager.getSelectedTrackIndex();
}

void pushUndoEntry(Track& track, UndoEntry&& entry) {
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    dropRedoBranch(stack);
    entry.id = stack.nextEntryId++;
    stack.entries.push_back(std::move(entry));
    stack.cursor = stack.entries.size();
    trimUndoStackForMemory(track);
}

size_t eraseUndoEntriesForSlot(GlobalUndoStack& stack, uint8_t slotIndex) {
    const size_t oldSize = stack.entries.size();
    if (oldSize == 0) {
        return 0;
    }

    UndoEntryVec kept;
    kept.reserve(oldSize);
    size_t removedBeforeCursor = 0;
    size_t removedTotal = 0;

    for (size_t i = 0; i < oldSize; ++i) {
        const bool remove = stack.entries[i].slotIndex == slotIndex;
        if (remove) {
            ++removedTotal;
            if (i < stack.cursor) {
                ++removedBeforeCursor;
            }
            continue;
        }
        kept.push_back(std::move(stack.entries[i]));
    }

    stack.entries = std::move(kept);
    if (removedBeforeCursor > stack.cursor) {
        stack.cursor = 0;
    } else {
        stack.cursor -= removedBeforeCursor;
    }
    if (stack.cursor > stack.entries.size()) {
        stack.cursor = stack.entries.size();
    }
    if (stack.entries.empty()) {
        stack.nextEntryId = 1;
    }
    trackManager.reclaimUnreferencedDisabledPasses();
    return removedTotal;
}

void restoreLoopSnapshot(Loop& loop, const LoopSnapshotRef& snapshot, const UndoLoopGeometry& geometry) {
    if (snapshot) {
        loop.restorePassesSnapshot(*snapshot);
    } else {
        loop.resetPassTimeline();
    }
    applyGeometry(loop, geometry);
    if (!loop.hasPublishedEvents()) {
        loop.nextEventIndex = 0;
        loop.lastTickInLoop = 0;
    }
    loop.invalidateCaches();
}

bool disableCapturePass(Loop& loop, PassId passId) {
    return loop.setCapturePassState(passId, CapturePassState::Disabled);
}

bool enableCapturePass(Loop& loop, PassId passId) {
    return loop.setCapturePassState(passId, CapturePassState::Active);
}

bool setEditPassState(Loop& loop, const EditPassIdList& ids, EditPassState state,
                      EditPassType passType) {
    if (ids.empty()) {
        return false;
    }

    bool touched = false;
    for (const EditPassId id : ids) {
        for (EditPass& editPass : loop.passes.editPasses) {
            if (editPass.id != id) {
                continue;
            }
            if (editPass.passType != passType) {
                continue;
            }
            if (editPass.state != state) {
                editPass.state = state;
                touched = true;
            }
        }
    }

    if (touched) {
        ++loop.playbackRevision;
        loop.discardPassesMaterializedCache();
    }
    return touched;
}

bool applyUndoEntry(Track& track, UndoEntry& entry) {
    Loop& loop = track.getLoop(entry.slotIndex);
    switch (entry.kind) {
        case UndoEntryKind::ClearSlot:
            entry.afterSnapshot = loop.sharePassesSnapshot();
            entry.afterGeometry = captureGeometry(loop);
            if (entry.hasTrackState) {
                entry.afterTrackState = track.getState();
            }
            restoreLoopSnapshot(loop, entry.beforeSnapshot, entry.beforeGeometry);
            if (entry.hasTrackState) {
                track.forceSetState(entry.beforeTrackState);
            }
            entry.hasRedoPayload = true;
            return true;
        case UndoEntryKind::LoopBoundaryChange:
            entry.afterLoopStartTick = loop.loopStartTick;
            entry.afterLoopLengthTicks = loop.loopLengthTicks;
            loop.loopStartTick = entry.beforeLoopStartTick;
            loop.loopLengthTicks = entry.beforeLoopLengthTicks;
            loop.invalidateCaches();
            entry.hasRedoPayload = true;
            return true;
        case UndoEntryKind::RecordPassAdded:
        case UndoEntryKind::OverdubPassAdded:
            if (!disableCapturePass(loop, entry.passId)) {
                logger.log(CAT_TRACK, LOG_WARNING, "Undo failed: missing pass %lu in slot %u",
                           static_cast<unsigned long>(entry.passId),
                           static_cast<unsigned>(entry.slotIndex));
                return false;
            }
            loop.rebuildVisualCacheFromPasses();
            loop.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getEditSession().store.mutStore());
                editManager.getEditSession().store.discardFlatCache();
                editManager.getEditSession().undoStack.clear();
            }
            return true;
        case UndoEntryKind::NoteEditPassClosed:
        case UndoEntryKind::ControlChangeEditPassClosed:
            if (entry.editPassIds.empty()) {
                return false;
            }
            if (!setEditPassState(loop, entry.editPassIds, EditPassState::Disabled,
                                  entry.editPassType)) {
                return false;
            }
            loop.rebuildVisualCacheFromPasses();
            loop.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getEditSession().store.mutStore());
                editManager.getEditSession().store.discardFlatCache();
                editManager.getEditSession().undoStack.clear();
            }
            entry.hasRedoPayload = true;
            logger.log(CAT_TRACK, LOG_INFO, "Scoped edit pass undone session=%u editPass=%u edits=%u",
                       static_cast<unsigned>(entry.editPassType),
                       static_cast<unsigned>(entry.editPassIndex),
                       static_cast<unsigned>(entry.editPassIds.size()));
            return true;
    }
    return false;
}

bool applyRedoEntry(Track& track, UndoEntry& entry) {
    Loop& loop = track.getLoop(entry.slotIndex);
    switch (entry.kind) {
        case UndoEntryKind::ClearSlot:
            if (!entry.hasRedoPayload) {
                logger.log(CAT_TRACK, LOG_WARNING, "Redo payload missing for entry %lu",
                           static_cast<unsigned long>(entry.id));
                return false;
            }
            restoreLoopSnapshot(loop, entry.afterSnapshot, entry.afterGeometry);
            if (entry.hasTrackState) {
                track.forceSetState(entry.afterTrackState);
            }
            return true;
        case UndoEntryKind::LoopBoundaryChange:
            if (!entry.hasRedoPayload) {
                logger.log(CAT_TRACK, LOG_WARNING, "Redo boundary payload missing for entry %lu",
                           static_cast<unsigned long>(entry.id));
                return false;
            }
            loop.loopStartTick = entry.afterLoopStartTick;
            loop.loopLengthTicks = entry.afterLoopLengthTicks;
            loop.invalidateCaches();
            return true;
        case UndoEntryKind::RecordPassAdded:
        case UndoEntryKind::OverdubPassAdded:
            if (!enableCapturePass(loop, entry.passId)) {
                logger.log(CAT_TRACK, LOG_WARNING, "Redo failed: missing pass %lu in slot %u",
                           static_cast<unsigned long>(entry.passId),
                           static_cast<unsigned>(entry.slotIndex));
                return false;
            }
            loop.rebuildVisualCacheFromPasses();
            loop.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getEditSession().store.mutStore());
                editManager.getEditSession().store.discardFlatCache();
                editManager.getEditSession().undoStack.clear();
            }
            return true;
        case UndoEntryKind::NoteEditPassClosed:
        case UndoEntryKind::ControlChangeEditPassClosed:
            if (!entry.hasRedoPayload || entry.editPassIds.empty()) {
                return false;
            }
            if (!setEditPassState(loop, entry.editPassIds, EditPassState::Active,
                                  entry.editPassType)) {
                return false;
            }
            loop.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getEditSession().store.mutStore());
                editManager.getEditSession().store.discardFlatCache();
                editManager.getEditSession().undoStack.clear();
            }
            logger.log(CAT_TRACK, LOG_INFO, "Scoped edit pass redone session=%u editPass=%u edits=%u",
                       static_cast<unsigned>(entry.editPassType),
                       static_cast<unsigned>(entry.editPassIndex),
                       static_cast<unsigned>(entry.editPassIds.size()));
            return true;
    }
    return false;
}

}  // namespace

void TrackUndo::pushRecordPassAdded(Track& track, uint8_t slotIndex, PassId passId) {
    if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || passId == kInvalidPassId) {
        return;
    }
    const Loop& loop = track.getLoop(slotIndex);
    UndoEntry entry;
    entry.kind = UndoEntryKind::RecordPassAdded;
    entry.slotIndex = slotIndex;
    entry.loopId = loop.loopId;
    entry.passId = passId;
    pushUndoEntry(track, std::move(entry));
}

void TrackUndo::pushOverdubPassAdded(Track& track, uint8_t slotIndex, PassId passId) {
    if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || passId == kInvalidPassId) {
        return;
    }
    const Loop& loop = track.getLoop(slotIndex);
    UndoEntry entry;
    entry.kind = UndoEntryKind::OverdubPassAdded;
    entry.slotIndex = slotIndex;
    entry.loopId = loop.loopId;
    entry.passId = passId;
    pushUndoEntry(track, std::move(entry));
}

void TrackUndo::pushNoteEditPassClosed(Track& track, uint8_t noteEditPassIndex,
                                       EditPassIdList editPassIds) {
    pushEditPassClosed(track, noteEditPassIndex, std::move(editPassIds), EditPassType::Note,
                       track.getActiveLoopIndex());
}

void TrackUndo::pushEditPassClosed(Track& track, uint8_t editPassIndex,
                                   EditPassIdList editPassIds, EditPassType editPassType,
                                   uint8_t slotIndex) {
    if (editPassIds.empty() || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    const Loop& loop = track.getLoop(slotIndex);
    UndoEntry entry;
    entry.kind = (editPassType == EditPassType::ControlChange)
                     ? UndoEntryKind::ControlChangeEditPassClosed
                     : UndoEntryKind::NoteEditPassClosed;
    entry.slotIndex = slotIndex;
    entry.loopId = loop.loopId;
    entry.editPassIndex = editPassIndex;
    entry.editPassType = editPassType;
    entry.editPassIds = std::move(editPassIds);
    pushUndoEntry(track, std::move(entry));
}

void TrackUndo::beginOverdubSession(Track& track) {
    if (editManager.isNoteEditActive()) {
        editManager.commitAllPendingNoteEditActions(track);
    }
    (void)track;
}

void TrackUndo::undoOverdub(Track& track) {
    Loop& loop = track.getActiveLoop();
    if (loop.capture.phase == CapturePhase::Overdub && !loop.capture.store.empty()) {
        loop.discardCapture();
        loop.invalidateCaches();
        logger.logTrackEvent("Overdub capture undone", clockManager.getCurrentTick());
        return;
    }
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    if (!stack.canUndo()) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo overdub right now");
        return;
    }
    UndoEntry& entry = stack.entries[stack.cursor - 1];
    if (!applyUndoEntry(track, entry)) {
        return;
    }
    --stack.cursor;
    logger.debug("Undo applied: kind=%d undo_count=%d",
                 static_cast<int>(entry.kind),
                 static_cast<int>(getUndoCount(track)));
    logger.logTrackEvent("Overdub undone", clockManager.getCurrentTick());
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(track),
                                                track.getActiveLoopIndex());
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

void TrackUndo::redoOverdub(Track& track) {
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    if (!stack.canRedo()) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo overdub right now");
        return;
    }
    UndoEntry& entry = stack.entries[stack.cursor];
    if (!applyRedoEntry(track, entry)) {
        return;
    }
    ++stack.cursor;
    logger.debug("Redo applied: kind=%d redo_count=%d",
                 static_cast<int>(entry.kind),
                 static_cast<int>(getRedoCount(track)));
    logger.logTrackEvent("Overdub redone", clockManager.getCurrentTick());
    StorageManager::markCurrentSetLoopSlotDirty(resolveTrackIndexForPersistence(track),
                                                track.getActiveLoopIndex());
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

size_t TrackUndo::getUndoCount(const Track& track) {
    return track.getGlobalUndoStack().undoCount();
}

size_t TrackUndo::getRedoCount(const Track& track) {
    return track.getGlobalUndoStack().redoCount();
}

bool TrackUndo::canUndo(const Track& track) {
    const Loop& loop = track.getActiveLoop();
    if (loop.capture.phase == CapturePhase::Overdub && !loop.capture.store.empty()) {
        return true;
    }
    return track.getGlobalUndoStack().canUndo();
}

bool TrackUndo::canRedo(const Track& track) {
    return track.getGlobalUndoStack().canRedo();
}

size_t TrackUndo::clearUndoHistoryForSlot(Track& track, uint8_t slotIndex) {
    if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return 0;
    }
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    const size_t removed = eraseUndoEntriesForSlot(stack, slotIndex);
    if (removed > 0) {
        logger.log(CAT_TRACK, LOG_INFO,
                   "Global undo pruned for slot %u: removed=%u remaining=%u cursor=%u",
                   static_cast<unsigned>(slotIndex),
                   static_cast<unsigned>(removed),
                   static_cast<unsigned>(stack.entries.size()),
                   static_cast<unsigned>(stack.cursor));
    }
    return removed;
}

void TrackUndo::pushClearTrackSnapshot(Track& track) {
    Loop& loop = track.getActiveLoop();
    UndoEntry entry;
    entry.kind = UndoEntryKind::ClearSlot;
    entry.slotIndex = track.getActiveLoopIndex();
    entry.loopId = loop.loopId;
    entry.beforeSnapshot = loop.sharePassesSnapshot();
    entry.beforeGeometry = captureGeometry(loop);
    entry.beforeTrackState = track.getState();
    entry.hasTrackState = true;
    pushUndoEntry(track, std::move(entry));
}

void TrackUndo::undoClearTrack(Track& track) {
    undoOverdub(track);
}

void TrackUndo::redoClearTrack(Track& track) {
    redoOverdub(track);
}

void TrackUndo::pushLoopStartSnapshot(Track& track, uint8_t slotIndex) {
    if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    Loop& loop = track.getLoop(slotIndex);
    UndoEntry entry;
    entry.kind = UndoEntryKind::LoopBoundaryChange;
    entry.slotIndex = slotIndex;
    entry.loopId = loop.loopId;
    entry.beforeLoopStartTick = loop.loopStartTick;
    entry.beforeLoopLengthTicks = loop.loopLengthTicks;
    pushUndoEntry(track, std::move(entry));
}

void TrackUndo::pushLoopGeometryDepartSnapshot(Track& track, uint8_t slotIndex,
                                              uint32_t beforeLoopStartTick,
                                              uint32_t beforeLoopLengthTicks) {
    if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    Loop& loop = track.getLoop(slotIndex);
    if (loop.loopStartTick == beforeLoopStartTick &&
        loop.loopLengthTicks == beforeLoopLengthTicks) {
        return;
    }
    UndoEntry entry;
    entry.kind = UndoEntryKind::LoopBoundaryChange;
    entry.slotIndex = slotIndex;
    entry.loopId = loop.loopId;
    entry.beforeLoopStartTick = beforeLoopStartTick;
    entry.beforeLoopLengthTicks = beforeLoopLengthTicks;
    pushUndoEntry(track, std::move(entry));
}

void TrackUndo::undoLoopStart(Track& track) {
    undoOverdub(track);
}

bool TrackUndo::canRedoClearTrack(const Track& track) {
    const GlobalUndoStack& stack = track.getGlobalUndoStack();
    return stack.canRedo() &&
           stack.entries[stack.cursor].kind == UndoEntryKind::ClearSlot;
}

bool TrackUndo::canUndoClearTrack(const Track& track) {
    const GlobalUndoStack& stack = track.getGlobalUndoStack();
    return stack.canUndo() &&
           stack.entries[stack.cursor - 1].kind == UndoEntryKind::ClearSlot;
}
