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
#include "Utils/MemoryPool.h"
#include "Utils/MidiEventVecFnvHash.h"

namespace {

UndoLoopGeometry captureGeometry(const Loop& loop) {
    return {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick};
}

void applyGeometry(Loop& loop, const UndoLoopGeometry& geometry) {
    loop.loopLengthTicks = geometry.loopLengthTicks;
    loop.startLoopTick = geometry.startLoopTick;
    loop.loopStartTick = geometry.loopStartTick;
}

void trimGlobalUndoHistory(GlobalUndoStack& stack) {
    while (stack.entries.size() > Config::MAX_UNDO_HISTORY) {
        stack.entries.erase(stack.entries.begin());
        if (stack.cursor > 0) {
            --stack.cursor;
        }
    }
}

void dropRedoBranch(GlobalUndoStack& stack) {
    if (stack.cursor >= stack.entries.size()) {
        return;
    }
    stack.entries.erase(stack.entries.begin() + static_cast<std::ptrdiff_t>(stack.cursor), stack.entries.end());
}

void pushUndoEntry(Track& track, UndoEntry&& entry) {
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    dropRedoBranch(stack);
    entry.id = stack.nextEntryId++;
    stack.entries.push_back(std::move(entry));
    stack.cursor = stack.entries.size();
    trimGlobalUndoHistory(stack);
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
                loop.rematerializeEditView(editManager.getNoteEditSession().store.mutStore());
                editManager.getNoteEditSession().store.discardFlatCache();
                editManager.getNoteEditSession().undoStack.clear();
            }
            return true;
        case UndoEntryKind::NoteEditPassClosed:
            if (entry.noteEditPassIds.empty()) {
                return false;
            }
            loop.disableEditPasses(entry.noteEditPassIds);
            loop.rebuildVisualCacheFromPasses();
            loop.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getNoteEditSession().store.mutStore());
                editManager.getNoteEditSession().store.discardFlatCache();
                editManager.getNoteEditSession().undoStack.clear();
            }
            entry.hasRedoPayload = true;
            logger.log(CAT_TRACK, LOG_INFO, "Note edit pass undone editPass=%u edits=%u",
                       static_cast<unsigned>(entry.noteEditPassIndex),
                       static_cast<unsigned>(entry.noteEditPassIds.size()));
            return true;
        case UndoEntryKind::ControlChangeEditPassClosed:
            return false;
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
                loop.rematerializeEditView(editManager.getNoteEditSession().store.mutStore());
                editManager.getNoteEditSession().store.discardFlatCache();
                editManager.getNoteEditSession().undoStack.clear();
            }
            return true;
        case UndoEntryKind::NoteEditPassClosed:
            if (!entry.hasRedoPayload || entry.noteEditPassIds.empty()) {
                return false;
            }
            for (const EditPassId id : entry.noteEditPassIds) {
                for (EditPass& editPass : loop.passes.editPasses) {
                    if (editPass.id == id) {
                        editPass.state = EditPassState::Active;
                    }
                }
            }
            ++loop.playbackRevision;
            loop.discardEditFlatMaterialization();
            loop.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getNoteEditSession().store.mutStore());
                editManager.getNoteEditSession().store.discardFlatCache();
                editManager.getNoteEditSession().undoStack.clear();
            }
            logger.log(CAT_TRACK, LOG_INFO, "Note edit pass redone editPass=%u edits=%u",
                       static_cast<unsigned>(entry.noteEditPassIndex),
                       static_cast<unsigned>(entry.noteEditPassIds.size()));
            return true;
        case UndoEntryKind::ControlChangeEditPassClosed:
            return false;
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
    if (editPassIds.empty()) {
        return;
    }
    const Loop& loop = track.getActiveLoop();
    UndoEntry entry;
    entry.kind = UndoEntryKind::NoteEditPassClosed;
    entry.slotIndex = track.getActiveLoopIndex();
    entry.loopId = loop.loopId;
    entry.noteEditPassIndex = noteEditPassIndex;
    entry.noteEditPassIds = std::move(editPassIds);
    pushUndoEntry(track, std::move(entry));
}

void TrackUndo::beginOverdubSession(Track& track) {
    if (editManager.isNoteEditActive()) {
        editManager.commitAllPendingNoteEditActions(track);
        editManager.closeNoteEditPass(track);
    }
    (void)track;
}

void TrackUndo::endOverdubSession(Track& track) {
    if (!editManager.isNoteEditActive()) {
        return;
    }
    Loop& loop = track.getActiveLoop();
    loop.rematerializeEditView(editManager.getNoteEditSession().store.mutStore());
    editManager.getNoteEditSession().store.discardFlatCache();
    editManager.getNoteEditSession().undoStack.clear();
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

void TrackUndo::pushLoopStartSnapshot(Track& track) {
    Loop& loop = track.getActiveLoop();
    UndoEntry entry;
    entry.kind = UndoEntryKind::LoopBoundaryChange;
    entry.slotIndex = track.getActiveLoopIndex();
    entry.loopId = loop.loopId;
    entry.beforeLoopStartTick = loop.loopStartTick;
    entry.beforeLoopLengthTicks = loop.loopLengthTicks;
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
