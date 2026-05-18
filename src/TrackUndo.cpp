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

// Undo overdub (operates on active loop)
void TrackUndo::pushUndoSnapshot(Track& track) {
    Loop& loop = track.getActiveLoop();
    MemoryPool::PooledMidiEventVector pooledEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : loop.midiEvents) {
        pooledEvents.push_back(event);
    }
    loop.getMidiHistory().push_back(std::move(pooledEvents));
    loop.getOverdubGeomHistory().push_back(
        {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick});
    loop.midiEventCountAtLastSnapshot = loop.midiEvents.size();
    loop.getMidiRedoHistory().clear();
    loop.getOverdubGeomRedoHistory().clear();
}

void TrackUndo::undoOverdub(Track& track) {
    if (!canUndo(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo overdub right now");
        return;
    }
    Loop& loop = track.getActiveLoop();
    MemoryPool::PooledMidiEventVector redoEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : loop.midiEvents) {
        redoEvents.push_back(event);
    }
    loop.getMidiRedoHistory().push_back(std::move(redoEvents));
    loop.getOverdubGeomRedoHistory().push_back(
        {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick});

    const auto& lastSnapshot = loop.getMidiHistory().back();
    loop.midiEvents.clear();
    for (const auto& event : lastSnapshot) {
        loop.midiEvents.push_back(*event);
    }
    if (!loop.overdubGeomHistoryEmpty()) {
        const auto& lastGeom = loop.getOverdubGeomHistory().back();
        loop.loopLengthTicks = lastGeom.loopLengthTicks;
        loop.startLoopTick = lastGeom.startLoopTick;
        loop.loopStartTick = lastGeom.loopStartTick;
    }
    loop.midiEventCountAtLastSnapshot = loop.midiEvents.size();
    popLastUndo(track);
    track.invalidateCaches();
    logger.debug("Undo restored snapshot: midiEvents=%d snapshotSize=%d",
                 loop.midiEvents.size(), getUndoCount(track));
    logger.logTrackEvent("Overdub undone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState.getLooperState());
}

void TrackUndo::redoOverdub(Track& track) {
    if (!canRedo(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo overdub right now");
        return;
    }
    Loop& loop = track.getActiveLoop();
    MemoryPool::PooledMidiEventVector undoEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : loop.midiEvents) {
        undoEvents.push_back(event);
    }
    loop.getMidiHistory().push_back(std::move(undoEvents));
    loop.getOverdubGeomHistory().push_back(
        {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick});

    const auto& redoSnapshot = loop.getMidiRedoHistory().back();
    loop.midiEvents.clear();
    for (const auto& event : redoSnapshot) {
        loop.midiEvents.push_back(*event);
    }
    if (!loop.overdubGeomRedoHistoryEmpty()) {
        const auto& redoGeom = loop.getOverdubGeomRedoHistory().back();
        loop.loopLengthTicks = redoGeom.loopLengthTicks;
        loop.startLoopTick = redoGeom.startLoopTick;
        loop.loopStartTick = redoGeom.loopStartTick;
    }
    loop.getMidiRedoHistory().pop_back();
    loop.getOverdubGeomRedoHistory().pop_back();
    loop.midiEventCountAtLastSnapshot = loop.midiEvents.size();
    track.invalidateCaches();
    logger.debug("Redo restored snapshot: midiEvents=%d redoSize=%d",
                 loop.midiEvents.size(), getRedoCount(track));
    logger.logTrackEvent("Overdub redone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState.getLooperState());
}

size_t TrackUndo::getUndoCount(const Track& track) {
    return track.getActiveLoop().midiHistorySize();
}

size_t TrackUndo::getRedoCount(const Track& track) {
    return track.getActiveLoop().midiRedoHistorySize();
}

bool TrackUndo::canUndo(const Track& track) {
    return !track.getActiveLoop().midiHistoryEmpty();
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

const std::vector<MidiEvent, ExtMemAllocator<MidiEvent>>& TrackUndo::peekLastMidiSnapshot(const Track& track) {
    static std::vector<MidiEvent, ExtMemAllocator<MidiEvent>> tempSnapshot;
    tempSnapshot.clear();
    const Loop& loop = track.getActiveLoop();
    if (!loop.midiHistoryEmpty()) {
        const auto& pooledSnapshot = loop.getMidiHistory().back();
        for (const auto& event : pooledSnapshot) {
            tempSnapshot.push_back(*event);
        }
    }
    return tempSnapshot;
}

std::deque<MemoryPool::PooledMidiEventVector, ExtMemAllocator<MemoryPool::PooledMidiEventVector>>& TrackUndo::getMidiHistory(Track& track) {
    return track.getActiveLoop().getMidiHistory();
}

const std::vector<MidiEvent, ExtMemAllocator<MidiEvent>>& TrackUndo::getCurrentMidiSnapshot(const Track& track) {
    return track.getMidiEvents();
}

// Undo clear (operates on active loop)
void TrackUndo::pushClearTrackSnapshot(Track& track) {
    Loop& loop = track.getActiveLoop();
    MemoryPool::PooledMidiEventVector pooledEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : loop.midiEvents) {
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
        for (const auto& event : loop.midiEvents) {
            redoEvents.push_back(event);
        }
        loop.getClearMidiRedoHistory().push_back(std::move(redoEvents));
        loop.getClearStateRedoHistory().push_back(track.getState());
        loop.getClearLengthRedoHistory().push_back(loop.loopLengthTicks);
        loop.getClearStartRedoHistory().push_back(loop.loopStartTick);

        const auto& lastSnapshot = loop.getClearMidiHistory().back();
        loop.midiEvents.clear();
        for (const auto& event : lastSnapshot) {
            loop.midiEvents.push_back(*event);
        }
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
    if (!loop.midiEvents.empty() && (track.getState() == TRACK_EMPTY)) {
        track.setState(TRACK_STOPPED);
    }
    track.invalidateCaches();
    StorageManager::saveState(looperState.getLooperState());
}

void TrackUndo::redoClearTrack(Track& track) {
    if (!canRedoClearTrack(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo clear track right now");
        return;
    }
    Loop& loop = track.getActiveLoop();

    MemoryPool::PooledMidiEventVector undoEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : loop.midiEvents) {
        undoEvents.push_back(event);
    }
    loop.getClearMidiHistory().push_back(std::move(undoEvents));
    loop.getClearStateHistory().push_back(track.getState());
    loop.getClearLengthHistory().push_back(loop.loopLengthTicks);
    loop.getClearStartHistory().push_back(loop.loopStartTick);

    if (!loop.clearMidiRedoHistoryEmpty()) {
        const auto& redoSnapshot = loop.getClearMidiRedoHistory().back();
        loop.midiEvents.clear();
        for (const auto& event : redoSnapshot) {
            loop.midiEvents.push_back(*event);
        }
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
    StorageManager::saveState(looperState.getLooperState());
}

// -------------------------
// Loop start point undo/redo
// -------------------------

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
    StorageManager::saveState(looperState.getLooperState());
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
    StorageManager::saveState(looperState.getLooperState());
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

// Compute a rolling FNV-1a hash of the track's current midiEvents
uint32_t TrackUndo::computeMidiHash(const Track& track) {
    uint32_t hash = 2166136261u;
    for (auto const& evt : track.getMidiEvents()) {
        hash ^= static_cast<uint32_t>(evt.type); hash *= 16777619u;
        hash ^= evt.tick;                    hash *= 16777619u;
        hash ^= evt.data.noteData.note;      hash *= 16777619u;
        hash ^= evt.data.noteData.velocity;  hash *= 16777619u;
    }
    return hash;
} 