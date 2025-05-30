//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackUndo.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "ClockManager.h"
#include "Globals.h"

// Undo overdub
void TrackUndo::pushUndoSnapshot(Track& track) {
    track.midiHistory.push_back(track.midiEvents);
    track.midiEventCountAtLastSnapshot = track.midiEvents.size();
}

void TrackUndo::undoOverdub(Track& track) {
    if (!canUndo(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo overdub right now");
        return;
    }
    track.midiEvents = peekLastMidiSnapshot(track);
    track.midiEventCountAtLastSnapshot = track.midiEvents.size();
    popLastUndo(track);
    logger.debug("Undo restored snapshot: midiEvents=%d snapshotSize=%d",
                 track.midiEvents.size(), getUndoCount(track));
    logger.logTrackEvent("Overdub undone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState.getLooperState());
}

size_t TrackUndo::getUndoCount(const Track& track) {
    return track.midiHistory.size();
}

bool TrackUndo::canUndo(const Track& track) {
    return !track.midiHistory.empty();
}

void TrackUndo::popLastUndo(Track& track) {
    if (track.midiHistory.empty()) {
        logger.log(CAT_TRACK, LOG_WARNING, "Attempted to pop undo snapshot, but none exist");
        return;
    }
    track.midiHistory.pop_back();
}

const std::vector<MidiEvent>& TrackUndo::peekLastMidiSnapshot(const Track& track) {
    return track.midiHistory.back();
}

std::deque<std::vector<MidiEvent>>& TrackUndo::getMidiHistory(Track& track) {
    return track.midiHistory;
}

const std::vector<MidiEvent>& TrackUndo::getCurrentMidiSnapshot(const Track& track) {
    return track.midiEvents;
}

// Undo clear
void TrackUndo::pushClearTrackSnapshot(Track& track) {
    track.clearMidiHistory.push_back(track.midiEvents);
    track.clearStateHistory.push_back(track.trackState);
    track.clearLengthHistory.push_back(track.loopLengthTicks);
    if (track.clearMidiHistory.size() > Config::MAX_UNDO_HISTORY) track.clearMidiHistory.pop_front();
    if (track.clearStateHistory.size() > Config::MAX_UNDO_HISTORY) track.clearStateHistory.pop_front();
    if (track.clearLengthHistory.size() > Config::MAX_UNDO_HISTORY) track.clearLengthHistory.pop_front();
}

void TrackUndo::undoClearTrack(Track& track) {
    if (!track.clearMidiHistory.empty()) {
        track.midiEvents = track.clearMidiHistory.back();
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
    if (!track.midiEvents.empty() && (track.trackState == TRACK_EMPTY)) {
        track.setState(TRACK_STOPPED);
    }
}

bool TrackUndo::canUndoClearTrack(const Track& track) {
    return (!track.clearMidiHistory.empty());
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