#include "TrackUndo.h"
#include "StorageManager.h"
#include "Logger.h"
#include "ClockManager.h"
#include "DisplayManager.h"
#include "Globals.h"

// Undo overdub
void TrackUndo::pushUndoSnapshot(Track& track) {
    track.midiHistory.push_back(track.midiEvents);
    track.noteHistory.push_back(track.noteEvents);
    track.midiEventCountAtLastSnapshot = track.midiEvents.size();
    track.noteEventCountAtLastSnapshot = track.noteEvents.size();
}

void TrackUndo::undoOverdub(Track& track) {
    if (!canUndo(track)) {
        logger.log(CAT_TRACK, LOG_WARNING, "Cannot undo overdub right now");
        return;
    }
    track.midiEvents = peekLastMidiSnapshot(track);
    track.noteEvents = peekLastNoteSnapshot(track);
    track.midiEventCountAtLastSnapshot = track.midiEvents.size();
    track.noteEventCountAtLastSnapshot = track.noteEvents.size();
    popLastUndo(track);
    track.hasNewEventsSinceSnapshot = false;
    logger.debug("Undo restored snapshot: midiEvents=%d noteEvents=%d  snapshotSize=%d",
                 track.midiEvents.size(), track.noteEvents.size(), getUndoCount(track));
    logger.logTrackEvent("Overdub undone", clockManager.getCurrentTick());
    StorageManager::saveState(looperState);
}

size_t TrackUndo::getUndoCount(const Track& track) {
    return track.midiHistory.size();
}

bool TrackUndo::canUndo(const Track& track) {
    return !track.midiHistory.empty();
}

void TrackUndo::popLastUndo(Track& track) {
    if (track.midiHistory.empty() || track.noteHistory.empty()) {
        logger.log(CAT_TRACK, LOG_WARNING, "Attempted to pop undo snapshot, but none exist");
        return;
    }
    track.midiHistory.pop_back();
    track.noteHistory.pop_back();
}

const std::vector<MidiEvent>& TrackUndo::peekLastMidiSnapshot(const Track& track) {
    return track.midiHistory.back();
}

const std::vector<NoteEvent>& TrackUndo::peekLastNoteSnapshot(const Track& track) {
    return track.noteHistory.back();
}

std::deque<std::vector<MidiEvent>>& TrackUndo::getMidiHistory(Track& track) {
    return track.midiHistory;
}

std::deque<std::vector<NoteEvent>>& TrackUndo::getNoteHistory(Track& track) {
    return track.noteHistory;
}

const std::vector<MidiEvent>& TrackUndo::getCurrentMidiSnapshot(const Track& track) {
    return track.midiEvents;
}

const std::vector<NoteEvent>& TrackUndo::getCurrentNoteSnapshot(const Track& track) {
    return track.noteEvents;
}

// Undo clear
void TrackUndo::pushClearTrackSnapshot(Track& track) {
    track.clearMidiHistory.push_back(track.midiEvents);
    track.clearNoteHistory.push_back(track.noteEvents);
    track.clearStateHistory.push_back(track.trackState);
    track.clearLengthHistory.push_back(track.loopLengthTicks);
    if (track.clearMidiHistory.size() > Config::MAX_UNDO_HISTORY) track.clearMidiHistory.pop_front();
    if (track.clearNoteHistory.size() > Config::MAX_UNDO_HISTORY) track.clearNoteHistory.pop_front();
    if (track.clearStateHistory.size() > Config::MAX_UNDO_HISTORY) track.clearStateHistory.pop_front();
    if (track.clearLengthHistory.size() > Config::MAX_UNDO_HISTORY) track.clearLengthHistory.pop_front();
}

void TrackUndo::undoClearTrack(Track& track) {
    if (!track.clearMidiHistory.empty() && !track.clearNoteHistory.empty()) {
        track.midiEvents = track.clearMidiHistory.back();
        track.noteEvents = track.clearNoteHistory.back();
        track.clearMidiHistory.pop_back();
        track.clearNoteHistory.pop_back();
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
    displayManager.update();
}

bool TrackUndo::canUndoClearTrack(const Track& track) {
    return (!track.clearMidiHistory.empty() && !track.clearNoteHistory.empty());
} 