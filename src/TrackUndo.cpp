//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackUndo.h"
#include "Track.h"
#include "EditManager.h"
#include "LoopEditManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "ClockManager.h"
#include "Globals.h"
#include "PassReclaim.h"
#include "TrackManager.h"
#include "Utils/MemoryPool.h"
#include "Utils/MidiEventVecFnvHash.h"
#include "Utils/TrackMem.h"
#include "UndoLoopGeometry.h"

extern TrackManager trackManager;

namespace {

constexpr size_t kMaxStaleUndoRedoSkipPerPress = 32;

TRACK_COLD_MEM UndoLoopGeometry captureGeometry(const Loop& loop) {
    return {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick};
}

TRACK_COLD_MEM void applyGeometry(Loop& loop, const UndoLoopGeometry& geometry) {
    loop.loopLengthTicks =
        loop.reconcileLoopLengthWithCommittedPasses(geometry.loopLengthTicks);
    loop.startLoopTick = geometry.startLoopTick;
    loop.loopStartTick = geometry.loopStartTick;
}

TRACK_COLD_MEM void dropRedoBranch(GlobalUndoStack& stack) {
    if (stack.cursor >= stack.entries.size()) {
        return;
    }
    stack.entries.erase(stack.entries.begin() + static_cast<std::ptrdiff_t>(stack.cursor), stack.entries.end());
    trackManager.reclaimUnreferencedDisabledPasses();
}

TRACK_COLD_MEM void trimUndoStackForMemory(Track& track) {
    if (trimGlobalUndoStackForMemory(track.getGlobalUndoStack()) > 0) {
        trackManager.reclaimUnreferencedDisabledPasses();
    }
}

TRACK_COLD_MEM uint8_t resolveTrackIndexForPersistence(const Track& track) {
    for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
        if (&trackManager.getTrack(i) == &track) {
            return i;
        }
    }
    return trackManager.getSelectedTrackIndex();
}

TRACK_COLD_MEM void pushUndoEntry(Track& track, UndoEntry&& entry) {
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    dropRedoBranch(stack);
    entry.id = stack.nextEntryId++;
    stack.entries.push_back(std::move(entry));
    stack.cursor = stack.entries.size();
    trimUndoStackForMemory(track);
}

TRACK_COLD_MEM size_t eraseUndoEntriesForSlot(GlobalUndoStack& stack, uint8_t slotIndex,
                                              bool preserveClearSlot) {
    const size_t oldSize = stack.entries.size();
    if (oldSize == 0) {
        return 0;
    }

    UndoEntryVec kept;
    kept.reserve(oldSize);
    size_t removedBeforeCursor = 0;
    size_t removedTotal = 0;

    for (size_t i = 0; i < oldSize; ++i) {
        const bool remove = stack.entries[i].slotIndex == slotIndex &&
                            (!preserveClearSlot ||
                             stack.entries[i].kind != UndoEntryKind::ClearSlot);
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

TRACK_COLD_MEM void restoreLoopSnapshot(Loop& loop, const LoopSnapshotRef& snapshot, const UndoLoopGeometry& geometry) {
    if (snapshot) {
        loop.restorePassesSnapshot(*snapshot);
    } else {
        loop.resetPassTimeline();
    }
    applyGeometry(loop, geometry);
    if (!loop.hasCommittedPasses()) {
        loop.nextEventIndex = 0;
        loop.lastTickInLoop = 0;
    }
    loop.invalidateCaches();
}

TRACK_COLD_MEM bool disableCapturePass(Loop& loop, PassId passId) {
    return loop.setCapturePassState(passId, CapturePassState::Disabled);
}

TRACK_COLD_MEM bool enableCapturePass(Loop& loop, PassId passId) {
    return loop.setCapturePassState(passId, CapturePassState::Active);
}

TRACK_COLD_MEM bool setEditPassState(Loop& loop, const EditPassIdList& ids, EditPassState state,
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

TRACK_COLD_MEM bool applyUndoEntry(Track& track, UndoEntry& entry) {
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
            loop.loopLengthTicks =
                loop.reconcileLoopLengthWithCommittedPasses(entry.beforeLoopLengthTicks);
            loop.invalidateCaches();
            track.invalidateCaches();
            loopEditManager.onGlobalGeometryRestored(track);
            entry.hasRedoPayload = true;
            return true;
        case UndoEntryKind::RecordPassAdded:
            entry.afterGeometry = captureGeometry(loop);
            entry.hasRedoPayload = true;
            if (!disableCapturePass(loop, entry.passId)) {
                logger.log(CAT_TRACK, LOG_WARNING, "Undo failed: missing pass %lu in slot %u",
                           static_cast<unsigned long>(entry.passId),
                           static_cast<unsigned>(entry.slotIndex));
                return false;
            }
            applyGeometry(loop, entry.beforeGeometry);
            // Keep disabled capture passes on the timeline so redo can re-enable them.
            loop.rebuildVisualCacheFromPasses();
            loop.invalidateCaches();
            track.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getEditSession().store.mutStore());
                editManager.getEditSession().store.discardEventsCache();
                editManager.getEditSession().undoStack.clear();
            }
            return true;
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
                editManager.getEditSession().store.discardEventsCache();
                editManager.getEditSession().undoStack.clear();
            }
            entry.hasRedoPayload = true;
            return true;
        case UndoEntryKind::NoteEditPassClosed:
        case UndoEntryKind::ControlChangeEditPassClosed:
            if (entry.editPassIds.empty()) {
                logger.log(CAT_TRACK, LOG_WARNING,
                           "Undo failed: note edit pass undo has no editPassIds (slot=%u kind=%d)",
                           static_cast<unsigned>(entry.slotIndex),
                           static_cast<int>(entry.kind));
                return false;
            }
            if (!setEditPassState(loop, entry.editPassIds, EditPassState::Disabled,
                                  entry.editPassType)) {
                logger.log(CAT_TRACK, LOG_WARNING,
                           "Undo failed: missing edit pass id(s) in slot %u (editPass=%u count=%u)",
                           static_cast<unsigned>(entry.slotIndex),
                           static_cast<unsigned>(entry.editPassIndex),
                           static_cast<unsigned>(entry.editPassIds.size()));
                return false;
            }
            loop.rebuildVisualCacheFromPasses();
            loop.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getEditSession().store.mutStore());
                editManager.getEditSession().store.discardEventsCache();
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

TRACK_COLD_MEM uint8_t resolveSlotIndexForLoop(const Track& track, const Loop& loop) {
    for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
        if (&track.getLoop(slotIndex) == &loop) {
            return slotIndex;
        }
    }
    return Config::INVALID_LOOP_SLOT;
}

TRACK_COLD_MEM bool loopHasLiveOverdubCapture(const Loop& loop) {
    return loop.capture.phase == CapturePhase::Overdub && !loop.capture.store.empty();
}

TRACK_COLD_MEM bool applyRedoEntry(Track& track, UndoEntry& entry) {
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
            loop.loopLengthTicks =
                loop.reconcileLoopLengthWithCommittedPasses(entry.afterLoopLengthTicks);
            loop.invalidateCaches();
            track.invalidateCaches();
            loopEditManager.onGlobalGeometryRestored(track);
            return true;
        case UndoEntryKind::RecordPassAdded:
            if (!entry.hasRedoPayload) {
                logger.log(CAT_TRACK, LOG_WARNING, "Redo payload missing for record pass entry %lu",
                           static_cast<unsigned long>(entry.id));
                return false;
            }
            if (!enableCapturePass(loop, entry.passId)) {
                logger.log(CAT_TRACK, LOG_WARNING, "Redo failed: missing pass %lu in slot %u",
                           static_cast<unsigned long>(entry.passId),
                           static_cast<unsigned>(entry.slotIndex));
                return false;
            }
            applyGeometry(loop, entry.afterGeometry);
            loop.rebuildVisualCacheFromPasses();
            loop.invalidateCaches();
            track.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getEditSession().store.mutStore());
                editManager.getEditSession().store.discardEventsCache();
                editManager.getEditSession().undoStack.clear();
            }
            return true;
        case UndoEntryKind::OverdubPassAdded:
            if (!entry.hasRedoPayload) {
                logger.log(CAT_TRACK, LOG_WARNING, "Redo payload missing for overdub pass entry %lu",
                           static_cast<unsigned long>(entry.id));
                return false;
            }
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
                editManager.getEditSession().store.discardEventsCache();
                editManager.getEditSession().undoStack.clear();
            }
            return true;
        case UndoEntryKind::NoteEditPassClosed:
        case UndoEntryKind::ControlChangeEditPassClosed:
            if (!entry.hasRedoPayload || entry.editPassIds.empty()) {
                logger.log(CAT_TRACK, LOG_WARNING,
                           "Redo failed: edit pass redo payload missing (slot=%u kind=%d)",
                           static_cast<unsigned>(entry.slotIndex),
                           static_cast<int>(entry.kind));
                return false;
            }
            if (!setEditPassState(loop, entry.editPassIds, EditPassState::Active,
                                  entry.editPassType)) {
                logger.log(CAT_TRACK, LOG_WARNING,
                           "Redo failed: missing edit pass id(s) in slot %u (editPass=%u count=%u)",
                           static_cast<unsigned>(entry.slotIndex),
                           static_cast<unsigned>(entry.editPassIndex),
                           static_cast<unsigned>(entry.editPassIds.size()));
                return false;
            }
            loop.invalidateCaches();
            if (editManager.isNoteEditActive()) {
                loop.rematerializeEditView(editManager.getEditSession().store.mutStore());
                editManager.getEditSession().store.discardEventsCache();
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

TRACK_COLD_MEM void restorePlaybackAfterSlotClear(uint8_t trackIndex, Track& track, uint32_t now) {
    bool foundPlaybackSlot = false;
    uint8_t newActiveSlot = 0;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        if (trackManager.isSlotEnabled(trackIndex, s) &&
            !trackManager.isSlotMuted(trackIndex, s) &&
            track.hasDataInSlot(s)) {
            foundPlaybackSlot = true;
            newActiveSlot = s;
            break;
        }
    }
    if (foundPlaybackSlot) {
        trackManager.setActiveLoopIndex(trackIndex, newActiveSlot);
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (trackManager.isSlotEnabled(trackIndex, s) &&
                !trackManager.isSlotMuted(trackIndex, s) &&
                track.hasDataInSlot(s)) {
                track.resetPlaybackStateForSlot(s, now);
            }
        }
        track.forceSetState(TRACK_STOPPED);
        track.startPlaying(now);
    } else {
        track.sendAllNotesOff();
        if (track.isPlaying()) {
            track.forceSetState(TRACK_STOPPED);
        }
    }
}

TRACK_COLD_MEM void applyClearSlotUndoSideEffects(Track& track, const UndoEntry& entry, uint8_t slotIndex) {
    const uint8_t trackIndex = resolveTrackIndexForPersistence(track);
    if (entry.hasSlotFlags) {
        trackManager.setSlotEnabled(trackIndex, slotIndex, entry.beforeSlotEnabled);
        trackManager.setSlotMuted(trackIndex, slotIndex, entry.beforeSlotMuted);
    }
    trackManager.setActiveLoopIndex(trackIndex, slotIndex);
    track.invalidateCaches();
    const uint32_t now = clockManager.getCurrentTick();
    if (entry.hasTrackState && entry.beforeTrackState == TRACK_PLAYING) {
        track.resetPlaybackStateForSlot(slotIndex, now);
        if (!track.isPlaying() && !track.isOverdubbing()) {
            track.startPlaying(now);
        }
    }
    trackManager.forceMidiLedUpdate(now);
}

TRACK_COLD_MEM void applyClearSlotRedoSideEffects(Track& track, uint8_t slotIndex) {
    const uint8_t trackIndex = resolveTrackIndexForPersistence(track);
    trackManager.setSlotEnabled(trackIndex, slotIndex, false);
    trackManager.setSlotMuted(trackIndex, slotIndex, false);
    trackManager.clearQueuedRecordingTrack(trackIndex, slotIndex);
    trackManager.setLayeredSlotHeld(trackIndex, slotIndex, false);
    const uint32_t now = clockManager.getCurrentTick();
    restorePlaybackAfterSlotClear(trackIndex, track, now);
    trackManager.forceMidiLedUpdate(now);
}

}  // namespace

TRACK_COLD_MEM void TrackUndo::pushRecordPassAdded(Track& track, uint8_t slotIndex, PassId passId) {
    if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || passId == kInvalidPassId) {
        return;
    }
    // Fresh record on a cleared slot supersedes prior pass and clear-slot undo checkpoints.
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    const size_t pruned = eraseUndoEntriesForSlot(stack, slotIndex, false);
    if (pruned > 0) {
        logger.log(CAT_TRACK, LOG_INFO,
                   "Record pass undo reset for slot %u: removed=%u",
                   static_cast<unsigned>(slotIndex), static_cast<unsigned>(pruned));
    }
    const Loop& loop = track.getLoop(slotIndex);
    UndoEntry entry;
    entry.kind = UndoEntryKind::RecordPassAdded;
    entry.slotIndex = slotIndex;
    entry.loopId = loop.loopId;
    entry.passId = passId;
    if (track.hasRecordCaptureBaselineGeometry_) {
        entry.beforeGeometry = track.recordCaptureBaselineGeometry_;
    } else {
        entry.beforeGeometry = captureGeometry(loop);
    }
    entry.afterGeometry = captureGeometry(loop);
    track.hasRecordCaptureBaselineGeometry_ = false;
    track.recordCaptureBaselineGeometry_ = {};
    pushUndoEntry(track, std::move(entry));
}

TRACK_COLD_MEM void TrackUndo::pushOverdubPassAdded(Track& track, uint8_t slotIndex, PassId passId) {
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

TRACK_COLD_MEM void TrackUndo::pushNoteEditPassClosed(Track& track, uint8_t noteEditPassIndex,
                                       EditPassIdList editPassIds) {
    pushEditPassClosed(track, noteEditPassIndex, std::move(editPassIds), EditPassType::Note,
                       track.getActiveLoopIndex());
}

TRACK_COLD_MEM void TrackUndo::pushEditPassClosed(Track& track, uint8_t editPassIndex,
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

TRACK_COLD_MEM void TrackUndo::beginOverdubSession(Track& track) {
    if (editManager.isNoteEditActive()) {
        editManager.commitAllPendingNoteEditActions(track);
    }
    (void)track;
}

TRACK_COLD_MEM void TrackUndo::undoForLoop(Track& track, Loop& loop) {
    if (loopEditManager.hasPendingGeometry()) {
        loopEditManager.flushAllPendingGeometry(track);
    }
    const uint8_t slotIndex = resolveSlotIndexForLoop(track, loop);
    if (slotIndex == Config::INVALID_LOOP_SLOT) {
        return;
    }
    if (loopHasLiveOverdubCapture(loop)) {
        loop.discardCapture();
        loop.invalidateCaches();
        logger.logTrackEvent("Overdub capture undone", clockManager.getCurrentTick());
        return;
    }
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    if (!stack.canUndo()) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo for loop slot %u right now",
                   static_cast<unsigned>(slotIndex));
        return;
    }
    if (stack.entries[stack.cursor - 1].slotIndex != slotIndex) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo for loop slot %u right now",
                   static_cast<unsigned>(slotIndex));
        return;
    }

    bool applied = false;
    size_t staleSkipped = 0;
    while (stack.canUndo() && staleSkipped < kMaxStaleUndoRedoSkipPerPress) {
        UndoEntry& entry = stack.entries[stack.cursor - 1];
        if (entry.slotIndex != slotIndex) {
            break;
        }
        const UndoEntryKind entryKind = entry.kind;
        if (applyUndoEntry(track, entry)) {
            --stack.cursor;
            applied = true;
            logger.debug("Undo applied: kind=%d undo_count=%d",
                         static_cast<int>(entry.kind),
                         static_cast<int>(getUndoCount(track)));
            if (entryKind == UndoEntryKind::ClearSlot) {
                applyClearSlotUndoSideEffects(track, entry, slotIndex);
                logger.logTrackEvent("Clear slot undone", clockManager.getCurrentTick());
            } else {
                logger.logTrackEvent("Overdub undone", clockManager.getCurrentTick());
            }
            const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(track);
            StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, slotIndex);
            StorageManager::admitLoopSlotPersist(persistTrackIndex, slotIndex);
            StorageManager::admitLoopUndoHistory(resolveTrackIndexForPersistence(track), slotIndex);
            StorageManager::requestDeferredSaveState(looperState.getLooperState(), UINT32_MAX,
                                                     true);
            break;
        }
        logger.log(CAT_TRACK, LOG_WARNING,
                   "Undo skipped stale entry kind=%d slot=%u entry_id=%lu",
                   static_cast<int>(entry.kind), static_cast<unsigned>(entry.slotIndex),
                   static_cast<unsigned long>(entry.id));
        stack.entries.erase(stack.entries.begin() +
                            static_cast<std::ptrdiff_t>(stack.cursor - 1));
        --stack.cursor;
        ++staleSkipped;
        trackManager.reclaimUnreferencedDisabledPasses();
    }

    if (!applied && staleSkipped == 0) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo for loop slot %u right now",
                   static_cast<unsigned>(slotIndex));
    }
}

TRACK_COLD_MEM void TrackUndo::redoForLoop(Track& track, Loop& loop) {
    if (loopEditManager.hasPendingGeometry()) {
        loopEditManager.cancelPendingGeometryPreview(track);
    }
    const uint8_t slotIndex = resolveSlotIndexForLoop(track, loop);
    if (slotIndex == Config::INVALID_LOOP_SLOT) {
        return;
    }
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    if (!stack.canRedo()) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo for loop slot %u right now",
                   static_cast<unsigned>(slotIndex));
        return;
    }
    if (stack.entries[stack.cursor].slotIndex != slotIndex) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo for loop slot %u right now",
                   static_cast<unsigned>(slotIndex));
        return;
    }

    bool applied = false;
    size_t staleSkipped = 0;
    while (stack.canRedo() && staleSkipped < kMaxStaleUndoRedoSkipPerPress) {
        UndoEntry& entry = stack.entries[stack.cursor];
        if (entry.slotIndex != slotIndex) {
            break;
        }
        const UndoEntryKind entryKind = entry.kind;
        if (applyRedoEntry(track, entry)) {
            ++stack.cursor;
            applied = true;
            logger.debug("Redo applied: kind=%d redo_count=%d",
                         static_cast<int>(entry.kind),
                         static_cast<int>(getRedoCount(track)));
            if (entryKind == UndoEntryKind::ClearSlot) {
                applyClearSlotRedoSideEffects(track, slotIndex);
                logger.logTrackEvent("Clear slot redone", clockManager.getCurrentTick());
            } else {
                logger.logTrackEvent("Overdub redone", clockManager.getCurrentTick());
            }
            const uint8_t persistTrackIndex = resolveTrackIndexForPersistence(track);
            StorageManager::markLoopSlotMaterialDirty(persistTrackIndex, slotIndex);
            StorageManager::admitLoopSlotPersist(persistTrackIndex, slotIndex);
            StorageManager::admitLoopUndoHistory(resolveTrackIndexForPersistence(track), slotIndex);
            StorageManager::requestDeferredSaveState(looperState.getLooperState(), UINT32_MAX,
                                                     true);
            break;
        }
        logger.log(CAT_TRACK, LOG_WARNING,
                   "Redo skipped stale entry kind=%d slot=%u entry_id=%lu",
                   static_cast<int>(entry.kind), static_cast<unsigned>(entry.slotIndex),
                   static_cast<unsigned long>(entry.id));
        stack.entries.erase(stack.entries.begin() +
                            static_cast<std::ptrdiff_t>(stack.cursor));
        ++staleSkipped;
        trackManager.reclaimUnreferencedDisabledPasses();
    }

    if (!applied && staleSkipped == 0) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo for loop slot %u right now",
                   static_cast<unsigned>(slotIndex));
    }
}

TRACK_COLD_MEM void TrackUndo::undoOverdub(Track& track) {
    undoForLoop(track, track.getActiveLoop());
}

TRACK_COLD_MEM void TrackUndo::redoOverdub(Track& track) {
    redoForLoop(track, track.getActiveLoop());
}

TRACK_COLD_MEM size_t TrackUndo::undoDepthForLoop(const Track& track, const Loop& loop) {
    const uint8_t slotIndex = resolveSlotIndexForLoop(track, loop);
    if (slotIndex == Config::INVALID_LOOP_SLOT) {
        return 0;
    }
    if (loopHasLiveOverdubCapture(loop)) {
        return 1u + countAppliedPassUndoEntriesForSlot(track.getGlobalUndoStack(), slotIndex);
    }
    // Cleared slot: pass undo entries may remain for restore-on-undo but sidebar U: shows none.
    if (!loop.hasCommittedPasses()) {
        return 0;
    }
    return countAppliedPassUndoEntriesForSlot(track.getGlobalUndoStack(), slotIndex);
}

TRACK_COLD_MEM size_t TrackUndo::redoDepthForLoop(const Track& track, const Loop& loop) {
    const uint8_t slotIndex = resolveSlotIndexForLoop(track, loop);
    if (slotIndex == Config::INVALID_LOOP_SLOT) {
        return 0;
    }
    return countRedoEntriesForSlot(track.getGlobalUndoStack(), slotIndex);
}

TRACK_COLD_MEM bool TrackUndo::canUndoForLoop(const Track& track, const Loop& loop) {
    const uint8_t slotIndex = resolveSlotIndexForLoop(track, loop);
    if (slotIndex == Config::INVALID_LOOP_SLOT) {
        return false;
    }
    if (loopHasLiveOverdubCapture(loop)) {
        return true;
    }
    const GlobalUndoStack& stack = track.getGlobalUndoStack();
    return stack.canUndo() && stack.entries[stack.cursor - 1].slotIndex == slotIndex;
}

TRACK_COLD_MEM bool TrackUndo::canRedoForLoop(const Track& track, const Loop& loop) {
    const uint8_t slotIndex = resolveSlotIndexForLoop(track, loop);
    if (slotIndex == Config::INVALID_LOOP_SLOT) {
        return false;
    }
    const GlobalUndoStack& stack = track.getGlobalUndoStack();
    return stack.canRedo() && stack.entries[stack.cursor].slotIndex == slotIndex;
}

TRACK_COLD_MEM bool TrackUndo::canUndoClearTrackForLoop(const Track& track, const Loop& loop) {
    const uint8_t slotIndex = resolveSlotIndexForLoop(track, loop);
    if (slotIndex == Config::INVALID_LOOP_SLOT) {
        return false;
    }
    const GlobalUndoStack& stack = track.getGlobalUndoStack();
    return stack.canUndo() &&
           stack.entries[stack.cursor - 1].kind == UndoEntryKind::ClearSlot &&
           stack.entries[stack.cursor - 1].slotIndex == slotIndex;
}

TRACK_COLD_MEM bool TrackUndo::canRedoClearTrackForLoop(const Track& track, const Loop& loop) {
    const uint8_t slotIndex = resolveSlotIndexForLoop(track, loop);
    if (slotIndex == Config::INVALID_LOOP_SLOT) {
        return false;
    }
    const GlobalUndoStack& stack = track.getGlobalUndoStack();
    return stack.canRedo() &&
           stack.entries[stack.cursor].kind == UndoEntryKind::ClearSlot &&
           stack.entries[stack.cursor].slotIndex == slotIndex;
}

TRACK_COLD_MEM size_t TrackUndo::getUndoCount(const Track& track) {
    return track.getGlobalUndoStack().undoCount();
}

TRACK_COLD_MEM size_t TrackUndo::getRedoCount(const Track& track) {
    return track.getGlobalUndoStack().redoCount();
}

TRACK_COLD_MEM bool TrackUndo::canUndo(const Track& track) {
    const Loop& loop = track.getActiveLoop();
    if (loop.capture.phase == CapturePhase::Overdub && !loop.capture.store.empty()) {
        return true;
    }
    return track.getGlobalUndoStack().canUndo();
}

TRACK_COLD_MEM bool TrackUndo::canRedo(const Track& track) {
    return track.getGlobalUndoStack().canRedo();
}

TRACK_COLD_MEM size_t TrackUndo::clearUndoHistoryForSlot(Track& track, uint8_t slotIndex) {
    if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return 0;
    }
    GlobalUndoStack& stack = track.getGlobalUndoStack();
    const size_t removed = eraseUndoEntriesForSlot(stack, slotIndex, false);
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

TRACK_COLD_MEM void TrackUndo::pushClearTrackSnapshot(Track& track) {
    Loop& loop = track.getActiveLoop();
    const uint8_t slotIndex = track.getActiveLoopIndex();
    const uint8_t trackIndex = resolveTrackIndexForPersistence(track);
    UndoEntry entry;
    entry.kind = UndoEntryKind::ClearSlot;
    entry.slotIndex = slotIndex;
    entry.loopId = loop.loopId;
    entry.beforeSnapshot = loop.sharePassesSnapshot();
    entry.beforeGeometry = captureGeometry(loop);
    entry.beforeTrackState = track.getState();
    entry.hasTrackState = true;
    entry.beforeSlotEnabled = trackManager.isSlotEnabled(trackIndex, slotIndex);
    entry.beforeSlotMuted = trackManager.isSlotMuted(trackIndex, slotIndex);
    entry.hasSlotFlags = true;
    pushUndoEntry(track, std::move(entry));
}

TRACK_COLD_MEM void TrackUndo::undoClearTrack(Track& track) {
    undoForLoop(track, track.getActiveLoop());
}

TRACK_COLD_MEM void TrackUndo::redoClearTrack(Track& track) {
    redoForLoop(track, track.getActiveLoop());
}

TRACK_COLD_MEM void TrackUndo::pushLoopStartSnapshot(Track& track, uint8_t slotIndex) {
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

TRACK_COLD_MEM void TrackUndo::pushLoopGeometryDepartSnapshot(Track& track, uint8_t slotIndex,
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

TRACK_COLD_MEM void TrackUndo::undoLoopStart(Track& track) {
    undoForLoop(track, track.getActiveLoop());
}

TRACK_COLD_MEM bool TrackUndo::canRedoClearTrack(const Track& track) {
    const GlobalUndoStack& stack = track.getGlobalUndoStack();
    return stack.canRedo() &&
           stack.entries[stack.cursor].kind == UndoEntryKind::ClearSlot;
}

TRACK_COLD_MEM bool TrackUndo::canUndoClearTrack(const Track& track) {
    const GlobalUndoStack& stack = track.getGlobalUndoStack();
    return stack.canUndo() &&
           stack.entries[stack.cursor - 1].kind == UndoEntryKind::ClearSlot;
}
