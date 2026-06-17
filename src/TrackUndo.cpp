//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackUndo.h"
#include "Track.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "ClockManager.h"
#include "Globals.h"
#include "Utils/MemoryPool.h"
#include "Utils/MidiEventVecFnvHash.h"
#include "Utils/HotPathTelemetry.h"

namespace {

void trimOverdubUndoHistory(Loop& loop) {
    if (loop.getMidiHistory().size() <= Config::MAX_UNDO_HISTORY) {
        return;
    }
    loop.getMidiHistory().pop_front();
    if (!loop.overdubGeomHistoryEmpty()) {
        loop.getOverdubGeomHistory().pop_front();
    }
}

void logLargeSnapshotIfNeeded(size_t eventCount) {
    if (eventCount > Config::SNAPSHOT_DEGRADED_UNDO_EVENT_THRESHOLD) {
        logger.log(CAT_TRACK, LOG_WARNING,
                   "Large loop snapshot ref (%zu events); undo restore remains O(N)",
                   eventCount);
    }
}

}  // namespace

void TrackUndo::pushUndoSnapshot(Track& track) {
#if BYPASS_STOP_UNDO_SAVE
    (void)track;
    logger.log(CAT_TRACK, LOG_INFO, "BYPASS_STOP_UNDO_SAVE: skip pushUndoSnapshot");
    return;
#endif
    const uint32_t telemetryStartUs = micros();
    Loop& loop = track.getActiveLoop();
    const size_t eventCount = loop.committedEvents.size();
    logLargeSnapshotIfNeeded(eventCount);
    loop.getMidiHistory().push_back(loop.committedEvents.shareForSnapshot());
    trimOverdubUndoHistory(loop);
    loop.getOverdubGeomHistory().push_back(
        {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick});
    loop.midiEventCountAtLastSnapshot = eventCount;
    loop.getMidiRedoHistory().clear();
    loop.getOverdubGeomRedoHistory().clear();
    HotPathTelemetry::recordUndoSnapshot(micros() - telemetryStartUs,
                                         static_cast<uint32_t>(eventCount), 0);
}

void TrackUndo::establishRecordStopBaseline(Track& track) {
    Loop& loop = track.getActiveLoop();
#if BYPASS_STOP_UNDO_SAVE
    loop.overdubSessionBaselineEventCount = loop.committedEvents.size();
    loop.overdubSessionBaselineHash = 0;
    loop.midiEventCountAtLastSnapshot = loop.committedEvents.size();
    logger.log(CAT_TRACK, LOG_INFO, "BYPASS_STOP_UNDO_SAVE: skip establishRecordStopBaseline snapshot");
#else
    if (!loop.midiHistoryEmpty()) {
        const auto& top = loop.getMidiHistory().back();
        // Keep empty preroll snapshot from record start so a second undo can revert to empty.
        if (!top || !top->empty()) {
            popLastUndo(track);
        }
    }
    pushUndoSnapshot(track);
    loop.overdubSessionBaselineEventCount = loop.committedEvents.size();
    loop.overdubSessionBaselineHash = 0;
    loop.midiEventCountAtLastSnapshot = loop.committedEvents.size();
#endif
}

void TrackUndo::beginOverdubSession(Track& track) {
    const uint32_t telemetryStartUs = micros();
    Loop& loop = track.getActiveLoop();
    loop.overdubSessionOpen = true;
    loop.overdubSessionBaselineEventCount = loop.committedEvents.size();
    loop.overdubSessionBaselineHash = 0;
    loop.midiEventCountAtLastSnapshot = loop.committedEvents.size();
    loop.getMidiRedoHistory().clear();
    loop.getOverdubGeomRedoHistory().clear();
    const uint32_t sourceEvents = static_cast<uint32_t>(loop.committedEvents.size());
    HotPathTelemetry::recordOverdubSessionOpen(micros() - telemetryStartUs, sourceEvents);
    logger.debug("Overdub session opened: events=%d, snapshots=%d",
                 static_cast<int>(loop.committedEvents.size()),
                 static_cast<int>(loop.midiHistorySize()));
}

void TrackUndo::endOverdubSession(Track& track) {
    Loop& loop = track.getActiveLoop();
    if (!loop.overdubSessionOpen) {
        return;
    }
    loop.overdubSessionOpen = false;
    const bool unchanged =
        loop.committedEvents.size() == loop.overdubSessionBaselineEventCount;
    if (!unchanged) {
        loop.overdubSessionBaselineHash = computeMidiHash(track);
    }
    logger.debug("Overdub session closed: events=%d, snapshots=%d, unchanged=%d",
                 static_cast<int>(loop.committedEvents.size()),
                 static_cast<int>(loop.midiHistorySize()),
                 unchanged ? 1 : 0);
}

void TrackUndo::undoOverdub(Track& track) {
    Loop& loop = track.getActiveLoop();
    if (loop.overdubSessionOpen && loop.capture.phase == CapturePhase::Overdub) {
        loop.discardCapture();
        track.invalidateCaches();
        logger.logTrackEvent("Overdub capture undone", clockManager.getCurrentTick());
        return;
    }
    if (!canUndo(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo overdub right now");
        return;
    }
    loop.getMidiRedoHistory().push_back(loop.committedEvents.shareForSnapshot());
    loop.getOverdubGeomRedoHistory().push_back(
        {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick});

    const auto& lastSnapshot = loop.getMidiHistory().back();
    loop.committedEvents.restoreFromSnapshot(lastSnapshot);
    if (!loop.overdubGeomHistoryEmpty()) {
        const auto& lastGeom = loop.getOverdubGeomHistory().back();
        loop.loopLengthTicks = lastGeom.loopLengthTicks;
        loop.startLoopTick = lastGeom.startLoopTick;
        loop.loopStartTick = lastGeom.loopStartTick;
    }
    if (loop.committedEvents.empty()) {
        loop.loopLengthTicks = 0;
        loop.loopStartTick = 0;
        loop.nextEventIndex = 0;
        loop.lastTickInLoop = 0;
        if (track.getState() != TRACK_RECORDING && track.getState() != TRACK_OVERDUBBING &&
            track.getState() != TRACK_ARMED) {
            track.forceSetState(TRACK_EMPTY);
        }
    } else if (track.getState() == TRACK_EMPTY) {
        track.forceSetState(TRACK_STOPPED);
    }
    loop.midiEventCountAtLastSnapshot = loop.committedEvents.size();
    popLastUndo(track);
    track.invalidateCaches();
    logger.debug("Undo restored snapshot: midiEvents=%d snapshotSize=%d",
                 static_cast<int>(loop.committedEvents.size()), getUndoCount(track));
    logger.logTrackEvent("Overdub undone", clockManager.getCurrentTick());
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

void TrackUndo::redoOverdub(Track& track) {
    if (!canRedo(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo overdub right now");
        return;
    }
    Loop& loop = track.getActiveLoop();
    loop.getMidiHistory().push_back(loop.committedEvents.shareForSnapshot());
    trimOverdubUndoHistory(loop);
    loop.getOverdubGeomHistory().push_back(
        {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick});

    const auto& redoSnapshot = loop.getMidiRedoHistory().back();
    loop.committedEvents.restoreFromSnapshot(redoSnapshot);
    if (!loop.overdubGeomRedoHistoryEmpty()) {
        const auto& redoGeom = loop.getOverdubGeomRedoHistory().back();
        loop.loopLengthTicks = redoGeom.loopLengthTicks;
        loop.startLoopTick = redoGeom.startLoopTick;
        loop.loopStartTick = redoGeom.loopStartTick;
    }
    loop.getMidiRedoHistory().pop_back();
    loop.getOverdubGeomRedoHistory().pop_back();
    loop.midiEventCountAtLastSnapshot = loop.committedEvents.size();
    track.invalidateCaches();
    logger.debug("Redo restored snapshot: midiEvents=%d redoSize=%d",
                 static_cast<int>(loop.committedEvents.size()), getRedoCount(track));
    logger.logTrackEvent("Overdub redone", clockManager.getCurrentTick());
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

size_t TrackUndo::getUndoCount(const Track& track) {
    return track.getActiveLoop().midiHistorySize();
}

size_t TrackUndo::getRedoCount(const Track& track) {
    return track.getActiveLoop().midiRedoHistorySize();
}

bool TrackUndo::canUndo(const Track& track) {
    const Loop& loop = track.getActiveLoop();
    if (loop.overdubSessionOpen && loop.capture.phase == CapturePhase::Overdub &&
        !loop.capture.store.empty()) {
        return true;
    }
    return !loop.midiHistoryEmpty();
}

bool TrackUndo::canRedo(const Track& track) {
    return !track.getActiveLoop().midiRedoHistoryEmpty();
}

void TrackUndo::popLastUndo(Track& track) {
    Loop& loop = track.getActiveLoop();
    if (loop.midiHistoryEmpty()) {
        logger.log(CAT_TRACK, LOG_WARNING, "Attempted to pop undo snapshot, but none exist");
        return;
    }
    loop.getMidiHistory().pop_back();
    if (!loop.overdubGeomHistoryEmpty()) {
        loop.getOverdubGeomHistory().pop_back();
    }
}

const MidiEventVec& TrackUndo::peekLastMidiSnapshot(const Track& track) {
    static MidiEventVec tempSnapshot;
    tempSnapshot.clear();
    const Loop& loop = track.getActiveLoop();
    if (!loop.midiHistoryEmpty()) {
        loop.getMidiHistory().back()->flatten(tempSnapshot);
    }
    return tempSnapshot;
}

MidiSnapshotDeque& TrackUndo::getMidiHistory(Track& track) {
    return track.getActiveLoop().getMidiHistory();
}

const MidiEventVec& TrackUndo::getCurrentMidiSnapshot(const Track& track) {
    return track.getMidiEvents();
}

void TrackUndo::pushClearTrackSnapshot(Track& track) {
    Loop& loop = track.getActiveLoop();
    MemoryPool::PooledMidiEventVector pooledEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : loop.midiEvents()) {
        pooledEvents.push_back(event);
    }
    loop.getClearMidiHistory().push_back(std::move(pooledEvents));
    loop.getClearStateHistory().push_back(track.getState());
    loop.getClearLengthHistory().push_back(loop.loopLengthTicks);
    loop.getClearStartHistory().push_back(loop.loopStartTick);
    if (loop.getClearMidiHistory().size() > Config::MAX_UNDO_HISTORY) loop.getClearMidiHistory().pop_front();
    if (loop.getClearStateHistory().size() > Config::MAX_UNDO_HISTORY) loop.getClearStateHistory().pop_front();
    if (loop.getClearLengthHistory().size() > Config::MAX_UNDO_HISTORY) loop.getClearLengthHistory().pop_front();
    if (loop.getClearStartHistory().size() > Config::MAX_UNDO_HISTORY) loop.getClearStartHistory().pop_front();
    loop.getClearMidiRedoHistory().clear();
    loop.getClearStateRedoHistory().clear();
    loop.getClearLengthRedoHistory().clear();
    loop.getClearStartRedoHistory().clear();
}

void TrackUndo::undoClearTrack(Track& track) {
    Loop& loop = track.getActiveLoop();
    if (!loop.clearMidiHistoryEmpty()) {
        MemoryPool::PooledMidiEventVector redoEvents(MemoryPool::globalMidiEventPool);
        for (const auto& event : loop.midiEvents()) {
            redoEvents.push_back(event);
        }
        loop.getClearMidiRedoHistory().push_back(std::move(redoEvents));
        loop.getClearStateRedoHistory().push_back(track.getState());
        loop.getClearLengthRedoHistory().push_back(loop.loopLengthTicks);
        loop.getClearStartRedoHistory().push_back(loop.loopStartTick);

        const auto& lastSnapshot = loop.getClearMidiHistory().back();
        auto snapStore = std::make_shared<LoopEventStore>();
        for (const auto& event : lastSnapshot) {
            snapStore->append(*event);
        }
        loop.committedEvents.restoreFromSnapshot(snapStore);
        loop.getClearMidiHistory().pop_back();
    }
    if (!loop.getClearStateHistory().empty()) {
        track.forceSetState(loop.getClearStateHistory().back());
        loop.getClearStateHistory().pop_back();
    }
    if (!loop.getClearLengthHistory().empty()) {
        loop.loopLengthTicks = loop.getClearLengthHistory().back();
        loop.getClearLengthHistory().pop_back();
    }
    if (!loop.getClearStartHistory().empty()) {
        loop.loopStartTick = loop.getClearStartHistory().back();
        loop.getClearStartHistory().pop_back();
    }
    if (!loop.midiEvents().empty() && (track.getState() == TRACK_EMPTY)) {
        track.setState(TRACK_STOPPED);
    }
    track.invalidateCaches();
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

void TrackUndo::redoClearTrack(Track& track) {
    if (!canRedoClearTrack(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo clear track right now");
        return;
    }
    Loop& loop = track.getActiveLoop();

    MemoryPool::PooledMidiEventVector undoEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : loop.midiEvents()) {
        undoEvents.push_back(event);
    }
    loop.getClearMidiHistory().push_back(std::move(undoEvents));
    loop.getClearStateHistory().push_back(track.getState());
    loop.getClearLengthHistory().push_back(loop.loopLengthTicks);
    loop.getClearStartHistory().push_back(loop.loopStartTick);

    if (!loop.clearMidiRedoHistoryEmpty()) {
        const auto& redoSnapshot = loop.getClearMidiRedoHistory().back();
        auto snapStore = std::make_shared<LoopEventStore>();
        for (const auto& event : redoSnapshot) {
            snapStore->append(*event);
        }
        loop.committedEvents.restoreFromSnapshot(snapStore);
        loop.getClearMidiRedoHistory().pop_back();
    }
    if (!loop.getClearStateRedoHistory().empty()) {
        track.forceSetState(loop.getClearStateRedoHistory().back());
        loop.getClearStateRedoHistory().pop_back();
    }
    if (!loop.getClearLengthRedoHistory().empty()) {
        loop.loopLengthTicks = loop.getClearLengthRedoHistory().back();
        loop.getClearLengthRedoHistory().pop_back();
    }
    if (!loop.getClearStartRedoHistory().empty()) {
        loop.loopStartTick = loop.getClearStartRedoHistory().back();
        loop.getClearStartRedoHistory().pop_back();
    }

    logger.logTrackEvent("Clear track redone", clockManager.getCurrentTick());
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

void TrackUndo::pushLoopStartSnapshot(Track& track) {
    Loop& loop = track.getActiveLoop();
    loop.getLoopStartHistory().push_back(loop.loopStartTick);
    if (loop.getLoopStartHistory().size() > Config::MAX_UNDO_HISTORY) {
        loop.getLoopStartHistory().pop_front();
    }
    loop.getLoopStartRedoHistory().clear();
    logger.log(CAT_TRACK, LOG_DEBUG, "Loop start snapshot pushed: %lu ticks", loop.loopStartTick);
}

void TrackUndo::undoLoopStart(Track& track) {
    if (!canUndoLoopStart(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo loop start change right now");
        return;
    }
    Loop& loop = track.getActiveLoop();
    loop.getLoopStartRedoHistory().push_back(loop.loopStartTick);

    uint32_t previousStartTick = loop.getLoopStartHistory().back();
    loop.getLoopStartHistory().pop_back();

    logger.log(CAT_TRACK, LOG_INFO, "Loop start undo: %lu -> %lu ticks", loop.loopStartTick, previousStartTick);
    loop.loopStartTick = previousStartTick;
    track.invalidateCaches();

    logger.logTrackEvent("Loop start undone", clockManager.getCurrentTick());
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

void TrackUndo::redoLoopStart(Track& track) {
    if (!canRedoLoopStart(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo loop start change right now");
        return;
    }
    Loop& loop = track.getActiveLoop();
    loop.getLoopStartHistory().push_back(loop.loopStartTick);

    uint32_t nextStartTick = loop.getLoopStartRedoHistory().back();
    loop.getLoopStartRedoHistory().pop_back();

    logger.log(CAT_TRACK, LOG_INFO, "Loop start redo: %lu -> %lu ticks", loop.loopStartTick, nextStartTick);
    loop.loopStartTick = nextStartTick;
    track.invalidateCaches();

    logger.logTrackEvent("Loop start redone", clockManager.getCurrentTick());
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

bool TrackUndo::canUndoLoopStart(const Track& track) {
    return !track.getActiveLoop().loopStartHistoryEmpty();
}

bool TrackUndo::canRedoLoopStart(const Track& track) {
    return !track.getActiveLoop().loopStartRedoHistoryEmpty();
}

bool TrackUndo::canRedoClearTrack(const Track& track) {
    return !track.getActiveLoop().clearMidiRedoHistoryEmpty();
}

bool TrackUndo::canUndoClearTrack(const Track& track) {
    return !track.getActiveLoop().clearMidiHistoryEmpty();
}

uint32_t TrackUndo::computeMidiHash(const Track& track) {
    return midiEventVecFnv1aHash(track.getMidiEvents());
}
