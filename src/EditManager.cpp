#include "EditManager.h"
#include "EditNoteState.h"
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

EditManager::EditManager() {
    currentState = nullptr; // Start with no state
}

void EditManager::setState(EditState* newState, Track& track, uint32_t startTick) {
    if (currentState) currentState->onExit(*this, track);
    currentState = newState;
    if (currentState) currentState->onEnter(*this, track, startTick);
}

void EditManager::onEncoderTurn(Track& track, int delta) {
    if (currentState) {
        int step = (delta > 0) ? 1 : -1;
        for (int i = 0; i < abs(delta); ++i) {
            currentState->onEncoderTurn(*this, track, step);
        }
    }
}

void EditManager::onButtonPress(Track& track) {
    if (currentState) currentState->onButtonPress(*this, track);
}

void EditManager::selectClosestNote(Track& track, uint32_t startTick) {
    uint32_t loopLength = track.getLength();
    uint32_t ticksPerStep = Config::TICKS_PER_16TH_STEP;
    const uint32_t SNAP_WINDOW = 24; // ticks
    const uint32_t NOTE_SELECT_WINDOW = 24; // ticks, for close note selection
    if (loopLength == 0) {
        bracketTick = 0;
        selectedNoteIdx = -1;
        notesAtBracketTick.clear();
        notesAtBracketIdx = 0;
    } else {
        const auto& midiEvents = track.getEvents();
        struct DisplayNote {
            uint8_t note;
            uint8_t velocity;
            uint32_t startTick;
            uint32_t endTick;
        };
        std::vector<DisplayNote> notes;
        std::map<uint8_t, DisplayNote> activeNotes;
        for (const auto& evt : midiEvents) {
            if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
                DisplayNote dn{evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick};
                activeNotes[evt.data.noteData.note] = dn;
            } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
                auto it = activeNotes.find(evt.data.noteData.note);
                if (it != activeNotes.end()) {
                    it->second.endTick = evt.tick;
                    notes.push_back(it->second);
                    activeNotes.erase(it);
                }
            }
        }
        for (auto& kv : activeNotes) {
            kv.second.endTick = loopLength;
            notes.push_back(kv.second);
        }
        // Find all notes within NOTE_SELECT_WINDOW ticks of the bracket
        notesAtBracketTick.clear();
        for (int i = 0; i < (int)notes.size(); ++i) {
            uint32_t noteTick = notes[i].startTick % loopLength;
            uint32_t bracketMod = startTick % loopLength;
            uint32_t dist = (noteTick > bracketMod)
                ? noteTick - bracketMod
                : bracketMod - noteTick;
            // Account for wrap-around
            dist = std::min(dist, loopLength - dist);
            if (dist <= NOTE_SELECT_WINDOW) {
                notesAtBracketTick.push_back(i);
            }
        }
        // Sort by tick, then by pitch (lowest to highest)
        std::sort(notesAtBracketTick.begin(), notesAtBracketTick.end(), [&](int a, int b) {
            if (notes[a].startTick != notes[b].startTick)
                return notes[a].startTick < notes[b].startTick;
            return notes[a].note < notes[b].note;
        });
        // Sticky selection: keep selectedNoteIdx if possible
        int stickyIdx = -1;
        for (int i = 0; i < (int)notesAtBracketTick.size(); ++i) {
            if (notesAtBracketTick[i] == selectedNoteIdx) {
                stickyIdx = i;
                break;
            }
        }
        if (!notesAtBracketTick.empty()) {
            if (stickyIdx != -1) {
                notesAtBracketIdx = stickyIdx;
                selectedNoteIdx = notesAtBracketTick[stickyIdx];
            } else {
                notesAtBracketIdx = 0;
                selectedNoteIdx = notesAtBracketTick[0];
            }
            bracketTick = notes[selectedNoteIdx].startTick % loopLength;
        } else {
            // No note in window, find closest as before
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
                notesAtBracketTick = {snapIdx};
                notesAtBracketIdx = 0;
            } else {
                bracketTick = ((startTick + ticksPerStep / 2) / ticksPerStep) * ticksPerStep % loopLength;
                selectedNoteIdx = -1;
                notesAtBracketTick.clear();
                notesAtBracketIdx = 0;
            }
        }
    }
    hasMovedBracket = true;
}

void EditManager::moveBracket(Track& track, int delta) {
    if (!notesAtBracketTick.empty() && notesAtBracketTick.size() > 1) {
        if (delta > 0) {
            notesAtBracketIdx++;
            if (notesAtBracketIdx >= (int)notesAtBracketTick.size()) {
                // Move to next tick group
                moveBracket(1, track, Config::TICKS_PER_16TH_STEP);
                return;
            }
        } else if (delta < 0) {
            notesAtBracketIdx--;
            if (notesAtBracketIdx < 0) {
                // Move to previous tick group
                moveBracket(-1, track, Config::TICKS_PER_16TH_STEP);
                return;
            }
        }
        selectedNoteIdx = notesAtBracketTick[notesAtBracketIdx];
        return;
    }
    // Otherwise, move bracket as before
    moveBracket(delta, track, Config::TICKS_PER_16TH_STEP);
}

void EditManager::switchToNextState(Track& track) {
    // Example: cycle between noteState and startNoteState
    if (currentState == &noteState) {
        setState(&startNoteState, track, bracketTick);
    } else {
        setState(&noteState, track, bracketTick);
    }
}

void EditManager::enterEditMode(EditState* newState, uint32_t startTick) {
    auto& track = trackManager.getSelectedTrack();
    setState(newState, track, startTick);
}

void EditManager::exitEditMode(Track& track) {
    selectedNoteIdx = -1;
    hasMovedBracket = false;
    if (currentState) currentState->onExit(*this, track);
    currentState = nullptr;
}

void EditManager::moveBracket(int delta, const Track& track, uint32_t ticksPerStep) {
    const auto& midiEvents = track.getEvents();
    struct DisplayNote {
        uint8_t note;
        uint8_t velocity;
        uint32_t startTick;
        uint32_t endTick;
    };
    std::vector<DisplayNote> notes;
    std::map<uint8_t, DisplayNote> activeNotes;
    uint32_t loopLength = track.getLength();
    if (loopLength == 0) return;
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
            DisplayNote dn{evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick};
            activeNotes[evt.data.noteData.note] = dn;
        } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
            auto it = activeNotes.find(evt.data.noteData.note);
            if (it != activeNotes.end()) {
                it->second.endTick = evt.tick;
                notes.push_back(it->second);
                activeNotes.erase(it);
            }
        }
    }
    for (auto& kv : activeNotes) {
        kv.second.endTick = loopLength;
        notes.push_back(kv.second);
    }
    const uint32_t SNAP_WINDOW = 24;
    if (delta > 0) {
        uint32_t targetTick = (bracketTick + ticksPerStep) % loopLength;
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
        uint32_t targetTick = (bracketTick + loopLength - (ticksPerStep % loopLength)) % loopLength;
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
}

void EditManager::selectNextNote(const Track& track) {
    moveBracket(1, track, 1);
}

void EditManager::selectPrevNote(const Track& track) {
    moveBracket(-1, track, 1);
}

