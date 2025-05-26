#include "EditManager.h"
#include "Track.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include "LooperState.h"
#include "Globals.h"
#include "TrackManager.h"
#include "MidiEvent.h"
#include <map>

EditManager editManager;

// Helper struct for display/edit only
struct DisplayNote {
    uint8_t note;
    uint8_t velocity;
    uint32_t startTick;
    uint32_t endTick;
};

// Reconstruct notes from midiEvents for the current loop
static std::vector<DisplayNote> reconstructNotes(const std::vector<MidiEvent>& midiEvents, uint32_t loopLength) {
    std::vector<DisplayNote> notes;
    std::map<uint8_t, DisplayNote> activeNotes; // note -> DisplayNote
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
            // Start a new note
            DisplayNote dn{evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick};
            activeNotes[evt.data.noteData.note] = dn;
        } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
            // End a note
            auto it = activeNotes.find(evt.data.noteData.note);
            if (it != activeNotes.end()) {
                it->second.endTick = evt.tick;
                notes.push_back(it->second);
                activeNotes.erase(it);
            }
        }
    }
    // Any notes still active wrap to end of loop
    for (auto& kv : activeNotes) {
        kv.second.endTick = loopLength;
        notes.push_back(kv.second);
    }
    return notes;
}

EditManager::EditManager() {}

void EditManager::enterEditMode(EditContext ctx, uint32_t startTick) {
    context = ctx;
    uint32_t loopLength = trackManager.getSelectedTrack().getLength();
    uint32_t ticksPerStep = Config::TICKS_PER_16TH_STEP;
    const uint32_t SNAP_WINDOW = 24; // ticks
    if (loopLength == 0) {
        bracketTick = 0;
        selectedNoteIdx = -1;
    } else {
        // Find the closest note to startTick (wrapping)
        const auto& midiEvents = trackManager.getSelectedTrack().getEvents();
        auto notes = reconstructNotes(midiEvents, loopLength);
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < (int)notes.size(); ++i) {
            uint32_t noteTick = notes[i].startTick % loopLength;
            uint32_t dist = std::min((noteTick + loopLength - (startTick % loopLength)) % loopLength,
                                     ((startTick % loopLength) + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            bracketTick = notes[snapIdx].startTick % loopLength;
            selectedNoteIdx = snapIdx;
        } else {
            // Snap to nearest 16th step from tick 0
            bracketTick = ((startTick + ticksPerStep / 2) / ticksPerStep) * ticksPerStep % loopLength;
            selectedNoteIdx = -1;
        }
    }
    hasMovedBracket = true;
    Serial.print("bracketTick: "); Serial.print(bracketTick);
    Serial.print(" / loopLength: "); Serial.println(trackManager.getSelectedTrack().getLength());
}

void EditManager::exitEditMode() {
    context = EDIT_NONE;
    selectedNoteIdx = -1;
    hasMovedBracket = false;
}

const uint32_t SNAP_WINDOW = 24; // ticks

void EditManager::moveBracket(int delta, const Track& track, uint32_t ticksPerStep) {
    const auto& midiEvents = track.getEvents();
    auto notes = reconstructNotes(midiEvents, track.getLength());
    uint32_t loopLength = track.getLength();
    if (loopLength == 0) return;

    if (delta > 0) {
        // Move to next 16th step (with wrapping)
        uint32_t targetTick = (bracketTick + ticksPerStep) % loopLength;
        // Find closest note to targetTick within SNAP_WINDOW
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < (int)notes.size(); ++i) {
            uint32_t noteTick = notes[i].startTick % loopLength;
            uint32_t dist = std::min((noteTick + loopLength - targetTick) % loopLength,
                                     (targetTick + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            bracketTick = notes[snapIdx].startTick % loopLength;
            selectedNoteIdx = snapIdx;
        } else {
            bracketTick = targetTick;
            selectedNoteIdx = -1;
        }
    } else if (delta < 0) {
        // Move to previous 16th step (with wrapping)
        uint32_t targetTick = (bracketTick + loopLength - (ticksPerStep % loopLength)) % loopLength;
        // Find closest note to targetTick within SNAP_WINDOW
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < (int)notes.size(); ++i) {
            uint32_t noteTick = notes[i].startTick % loopLength;
            uint32_t dist = std::min((noteTick + loopLength - targetTick) % loopLength,
                                     (targetTick + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            bracketTick = notes[snapIdx].startTick % loopLength;
            selectedNoteIdx = snapIdx;
        } else {
            bracketTick = targetTick;
            selectedNoteIdx = -1;
        }
    }
    bracketTick = bracketTick % loopLength;
    Serial.print("bracketTick: "); Serial.print(bracketTick);
    Serial.print(" / loopLength: "); Serial.println(loopLength);
}

void EditManager::selectNextNote(const Track& track) {
    moveBracket(1, track, 1); // Use 1 as ticksPerStep to just move to next note in chord
}

void EditManager::selectPrevNote(const Track& track) {
    moveBracket(-1, track, 1);
}

EditContext EditManager::getContext() const { return context; }
uint32_t EditManager::getBracketTick() const { return bracketTick; }
int EditManager::getSelectedNoteIdx() const { return selectedNoteIdx; }
void EditManager::resetSelection() { selectedNoteIdx = -1; }
 