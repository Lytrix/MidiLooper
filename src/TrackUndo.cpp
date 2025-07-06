//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackUndo.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "ClockManager.h"
#include "Globals.h"
#include "Utils/MemoryPool.h"

// Undo overdub
void TrackUndo::pushUndoSnapshot(Track& track) {
    // Create a pooled vector and copy current events to it
    MemoryPool::PooledMidiEventVector pooledEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : track.midiEvents) {
        pooledEvents.push_back(event);
    }
    track.midiHistory.push_back(std::move(pooledEvents));
    track.midiEventCountAtLastSnapshot = track.midiEvents.size();
    // Clear redo history when new undo is pushed
    track.midiRedoHistory.clear();
}

void TrackUndo::undoOverdub(Track& track) {
    if (!canUndo(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo overdub right now");
        return;
    }
    // Save current state to redo history before undoing
    MemoryPool::PooledMidiEventVector redoEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : track.midiEvents) {
        redoEvents.push_back(event);
    }
    track.midiRedoHistory.push_back(std::move(redoEvents));
    
    // Restore from undo history
    const auto& lastSnapshot = track.midiHistory.back();
    track.midiEvents.clear();
    for (const auto& event : lastSnapshot) {
        track.midiEvents.push_back(*event);
    }
    track.midiEventCountAtLastSnapshot = track.midiEvents.size();
    popLastUndo(track);
    logger.debug("Undo restored snapshot: midiEvents=%d snapshotSize=%d",
                 track.midiEvents.size(), getUndoCount(track));
    logger.logTrackEvent("Overdub undone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState.getLooperState());
}

void TrackUndo::redoOverdub(Track& track) {
    if (!canRedo(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo overdub right now");
        return;
    }
    // Save current state back to undo history
    MemoryPool::PooledMidiEventVector undoEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : track.midiEvents) {
        undoEvents.push_back(event);
    }
    track.midiHistory.push_back(std::move(undoEvents));
    
    // Restore from redo history
    const auto& redoSnapshot = track.midiRedoHistory.back();
    track.midiEvents.clear();
    for (const auto& event : redoSnapshot) {
        track.midiEvents.push_back(*event);
    }
    track.midiRedoHistory.pop_back();
    track.midiEventCountAtLastSnapshot = track.midiEvents.size();
    logger.debug("Redo restored snapshot: midiEvents=%d redoSize=%d",
                 track.midiEvents.size(), getRedoCount(track));
    logger.logTrackEvent("Overdub redone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState.getLooperState());
}

size_t TrackUndo::getUndoCount(const Track& track) {
    return track.midiHistory.size();
}

size_t TrackUndo::getRedoCount(const Track& track) {
    return track.midiRedoHistory.size();
}

bool TrackUndo::canUndo(const Track& track) {
    return !track.midiHistory.empty();
}

bool TrackUndo::canRedo(const Track& track) {
    return !track.midiRedoHistory.empty();
}

void TrackUndo::popLastUndo(Track& track) {
    if (track.midiHistory.empty()) {
        logger.log(CAT_TRACK, LOG_WARNING, "Attempted to pop undo snapshot, but none exist");
        return;
    }
    track.midiHistory.pop_back();
}

const std::vector<MidiEvent>& TrackUndo::peekLastMidiSnapshot(const Track& track) {
    // Convert pooled vector to regular vector for compatibility
    static std::vector<MidiEvent> tempSnapshot;
    tempSnapshot.clear();
    if (!track.midiHistory.empty()) {
        const auto& pooledSnapshot = track.midiHistory.back();
        for (const auto& event : pooledSnapshot) {
            tempSnapshot.push_back(*event);
        }
    }
    return tempSnapshot;
}

std::deque<MemoryPool::PooledMidiEventVector>& TrackUndo::getMidiHistory(Track& track) {
    return track.midiHistory;
}

const std::vector<MidiEvent>& TrackUndo::getCurrentMidiSnapshot(const Track& track) {
    return track.midiEvents;
}

// Undo clear
void TrackUndo::pushClearTrackSnapshot(Track& track) {
    // Create a pooled vector and copy current events to it
    MemoryPool::PooledMidiEventVector pooledEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : track.midiEvents) {
        pooledEvents.push_back(event);
    }
    track.clearMidiHistory.push_back(std::move(pooledEvents));
    track.clearStateHistory.push_back(track.trackState);
    track.clearLengthHistory.push_back(track.loopLengthTicks);
    track.clearStartHistory.push_back(track.loopStartTick);  // Save loop start point
    if (track.clearMidiHistory.size() > Config::MAX_UNDO_HISTORY) track.clearMidiHistory.pop_front();
    if (track.clearStateHistory.size() > Config::MAX_UNDO_HISTORY) track.clearStateHistory.pop_front();
    if (track.clearLengthHistory.size() > Config::MAX_UNDO_HISTORY) track.clearLengthHistory.pop_front();
    if (track.clearStartHistory.size() > Config::MAX_UNDO_HISTORY) track.clearStartHistory.pop_front();
    // Clear redo history when new clear undo is pushed
    track.clearMidiRedoHistory.clear();
    track.clearStateRedoHistory.clear();
    track.clearLengthRedoHistory.clear();
    track.clearStartRedoHistory.clear();
}

void TrackUndo::undoClearTrack(Track& track) {
    if (!track.clearMidiHistory.empty()) {
        // Save current state to redo history before undoing
        MemoryPool::PooledMidiEventVector redoEvents(MemoryPool::globalMidiEventPool);
        for (const auto& event : track.midiEvents) {
            redoEvents.push_back(event);
        }
        track.clearMidiRedoHistory.push_back(std::move(redoEvents));
        track.clearStateRedoHistory.push_back(track.trackState);
        track.clearLengthRedoHistory.push_back(track.loopLengthTicks);
        track.clearStartRedoHistory.push_back(track.loopStartTick);
        
        // Restore from undo history
        const auto& lastSnapshot = track.clearMidiHistory.back();
        track.midiEvents.clear();
        for (const auto& event : lastSnapshot) {
            track.midiEvents.push_back(*event);
        }
        track.clearMidiHistory.pop_back();
    }
    if (!track.clearStateHistory.empty()) {
        track.forceSetState(track.clearStateHistory.back());
        track.clearStateHistory.pop_back();
    }
    if (!track.clearLengthHistory.empty()) {
        track.loopLengthTicks = track.clearLengthHistory.back();
        track.clearLengthHistory.pop_back();
    }
    if (!track.clearStartHistory.empty()) {
        track.loopStartTick = track.clearStartHistory.back();
        track.clearStartHistory.pop_back();
    }
    if (!track.midiEvents.empty() && (track.trackState == TRACK_EMPTY)) {
        track.setState(TRACK_STOPPED);
    }
}

void TrackUndo::redoClearTrack(Track& track) {
    if (!canRedoClearTrack(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo clear track right now");
        return;
    }
    
    // Save current state back to undo history
    MemoryPool::PooledMidiEventVector undoEvents(MemoryPool::globalMidiEventPool);
    for (const auto& event : track.midiEvents) {
        undoEvents.push_back(event);
    }
    track.clearMidiHistory.push_back(std::move(undoEvents));
    track.clearStateHistory.push_back(track.trackState);
    track.clearLengthHistory.push_back(track.loopLengthTicks);
    track.clearStartHistory.push_back(track.loopStartTick);
    
    // Restore from redo history
    if (!track.clearMidiRedoHistory.empty()) {
        const auto& redoSnapshot = track.clearMidiRedoHistory.back();
        track.midiEvents.clear();
        for (const auto& event : redoSnapshot) {
            track.midiEvents.push_back(*event);
        }
        track.clearMidiRedoHistory.pop_back();
    }
    if (!track.clearStateRedoHistory.empty()) {
        track.forceSetState(track.clearStateRedoHistory.back());
        track.clearStateRedoHistory.pop_back();
    }
    if (!track.clearLengthRedoHistory.empty()) {
        track.loopLengthTicks = track.clearLengthRedoHistory.back();
        track.clearLengthRedoHistory.pop_back();
    }
    if (!track.clearStartRedoHistory.empty()) {
        track.loopStartTick = track.clearStartRedoHistory.back();
        track.clearStartRedoHistory.pop_back();
    }
    
    logger.logTrackEvent("Clear track redone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState.getLooperState());
}

// -------------------------
// Loop start point undo/redo
// -------------------------

void TrackUndo::pushLoopStartSnapshot(Track& track) {
    track.loopStartHistory.push_back(track.loopStartTick);
    if (track.loopStartHistory.size() > Config::MAX_UNDO_HISTORY) {
        track.loopStartHistory.pop_front();
    }
    // Clear redo history when new undo is pushed
    track.loopStartRedoHistory.clear();
    
    logger.log(CAT_TRACK, LOG_DEBUG, "Loop start snapshot pushed: %lu ticks", track.loopStartTick);
}

void TrackUndo::undoLoopStart(Track& track) {
    if (!canUndoLoopStart(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo loop start change right now");
        return;
    }
    
    // Save current state to redo history before undoing
    track.loopStartRedoHistory.push_back(track.loopStartTick);
    
    // Restore from undo history
    uint32_t previousStartTick = track.loopStartHistory.back();
    track.loopStartHistory.pop_back();
    
    logger.log(CAT_TRACK, LOG_INFO, "Loop start undo: %lu -> %lu ticks", 
               track.loopStartTick, previousStartTick);
    
    track.loopStartTick = previousStartTick;
    
    // Invalidate caches when loop start changes
    track.invalidateCaches();
    
    logger.logTrackEvent("Loop start undone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState.getLooperState());
}

void TrackUndo::redoLoopStart(Track& track) {
    if (!canRedoLoopStart(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot redo loop start change right now");
        return;
    }
    
    // Save current state back to undo history
    track.loopStartHistory.push_back(track.loopStartTick);
    
    // Restore from redo history
    uint32_t nextStartTick = track.loopStartRedoHistory.back();
    track.loopStartRedoHistory.pop_back();
    
    logger.log(CAT_TRACK, LOG_INFO, "Loop start redo: %lu -> %lu ticks", 
               track.loopStartTick, nextStartTick);
    
    track.loopStartTick = nextStartTick;
    
    // Invalidate caches when loop start changes
    track.invalidateCaches();
    
    logger.logTrackEvent("Loop start redone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState.getLooperState());
}

bool TrackUndo::canUndoLoopStart(const Track& track) {
    return !track.loopStartHistory.empty();
}

bool TrackUndo::canRedoLoopStart(const Track& track) {
    return !track.loopStartRedoHistory.empty();
}

bool TrackUndo::canRedoClearTrack(const Track& track) {
    return !track.clearMidiRedoHistory.empty();
}

bool TrackUndo::canUndoClearTrack(const Track& track) {
    return !track.clearMidiHistory.empty();
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