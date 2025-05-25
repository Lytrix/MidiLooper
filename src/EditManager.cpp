#include "EditManager.h"
#include "Track.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include "LooperState.h"
#include "Globals.h"
#include "TrackManager.h"

EditManager editManager;

EditManager::EditManager() {}

void EditManager::enterEditMode(EditContext ctx, uint32_t startTick) {
    context = ctx;
    uint32_t loopLength = trackManager.getSelectedTrack().getLength();
    if (loopLength == 0) {
        bracketTick = 0;
    } else {
        startTick = startTick % loopLength;
        // Snap to nearest note at or before startTick, or previous 16th step
        const auto& notes = trackManager.getSelectedTrack().getNoteEvents();
        int bestIdx = -1;
        uint32_t bestTick = 0;
        for (int i = 0; i < (int)notes.size(); ++i) {
            if (notes[i].startNoteTick <= startTick && notes[i].startNoteTick >= bestTick) {
                bestTick = notes[i].startNoteTick;
                bestIdx = i;
            }
        }
        if (bestIdx != -1) {
            bracketTick = notes[bestIdx].startNoteTick % loopLength;
            selectedNoteIdx = bestIdx;
        } else {
            bracketTick = ((startTick / Config::TICKS_PER_16TH_STEP) * Config::TICKS_PER_16TH_STEP) % loopLength;
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
    const auto& notes = track.getNoteEvents();
    uint32_t loopLength = track.getLength();
    if (loopLength == 0) return;

    if (delta > 0) {
        // Move to next 16th step (with wrapping)
        uint32_t targetTick = (bracketTick + ticksPerStep) % loopLength;
        // Find closest note to targetTick within SNAP_WINDOW
        int snapIdx = -1;
        uint32_t minDist = SNAP_WINDOW + 1;
        for (int i = 0; i < (int)notes.size(); ++i) {
            uint32_t noteTick = notes[i].startNoteTick % loopLength;
            uint32_t dist = std::min((noteTick + loopLength - targetTick) % loopLength,
                                     (targetTick + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            bracketTick = notes[snapIdx].startNoteTick % loopLength;
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
            uint32_t noteTick = notes[i].startNoteTick % loopLength;
            uint32_t dist = std::min((noteTick + loopLength - targetTick) % loopLength,
                                     (targetTick + loopLength - noteTick) % loopLength);
            if (dist < minDist) {
                minDist = dist;
                snapIdx = i;
            }
        }
        if (snapIdx != -1 && minDist <= SNAP_WINDOW) {
            bracketTick = notes[snapIdx].startNoteTick % loopLength;
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
 