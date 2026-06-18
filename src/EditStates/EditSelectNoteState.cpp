//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditStates/EditSelectNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include "Logger.h"
#include "TrackUndo.h"
#include "ClockManager.h"
#include "MidiHandler.h"
#include "MidiButtonManager.h"
#include "MidiFaderManager.h"
#include "Globals.h"
#include "MidiConfig.h"
#include "Utils/NoteUtils.h"
#include "Utils/ValidationUtils.h"
#include "NoteEditManager.h"
#include <algorithm>

void EditSelectNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("EditSelectNoteState::onEnter at tick %lu", startTick);
    
    // Initialize MIDI event count for overdub tracking
    lastMidiEventCount = track.getMidiEvents().size();
    
    // Use the existing selectClosestNote logic which properly finds nearest notes
    // or snaps to the current position if no notes exist
    manager.selectClosestNote(track, startTick);
    
    uint32_t bracketTick = manager.getBracketTick();
    int selectedIdx = manager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0) {
        logger.info("EditSelectNoteState: Found and selected note %d at tick %lu", 
                   selectedIdx, bracketTick);
    } else {
        logger.info("EditSelectNoteState: No note selected, bracket at tick %lu", bracketTick);
    }
    
    // Note: Pitchbend will be sent by MidiButtonManager after program change
    
    logger.info("MIDI Encoder: Entered SELECT mode (bracket=%lu)", bracketTick);
}

void EditSelectNoteState::onExit(EditManager& manager, Track& track) {
    logger.debug("EditSelectNoteState::onExit");
}

void EditSelectNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditSelectNoteState::onEncoderTurn called with delta=%d", delta);
    
    if (!ValidationUtils::validateLoopLength(track.getLoopLength())) return;
    //uint32_t loopLength = track.getLoopLength();
    
    // In SELECT mode, we want to navigate sequentially through notes by start position
    // rather than using grid-based movement with snap windows
    if (delta > 0) {
        selectNextNoteSequential(manager, track);
    } else if (delta < 0) {
        selectPreviousNoteSequential(manager, track);
    }
    
    uint32_t bracketTick = manager.getBracketTick();
    int selectedIdx = manager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0) {
        logger.debug("EditSelectNoteState: Moved to tick %lu, selected note %d", 
                    bracketTick, selectedIdx);
    } else {
        logger.debug("EditSelectNoteState: Moved to tick %lu, no note selected", bracketTick);
    }
}

void EditSelectNoteState::onButtonPress(EditManager& manager, Track& track) {
    logger.debug("EditSelectNoteState::onButtonPress");
    
    uint32_t bracketTick = manager.getBracketTick();
    
    if (manager.getSelectedNoteIdx() >= 0) {
        // There's a note at this position - enter start note editing
        logger.info("EditSelectNoteState: Note exists, entering start note edit mode");
        manager.setState(manager.getStartNoteState(), track, bracketTick);
    } else {
        // No note at this position - create a 32nd note
        logger.info("EditSelectNoteState: No note found, creating 32nd note at tick %lu", bracketTick);
        
        // Push undo snapshot before creating note
        TrackUndo::pushUndoSnapshot(track);
        createDefaultNote(track, bracketTick);
        track.getActiveLoop().markEditFlatDirty();
        track.invalidateCaches();

        // Select the newly created note and enter start note editing
        manager.selectClosestNote(track, bracketTick);
        manager.setState(manager.getStartNoteState(), track, bracketTick);
    }
}

void EditSelectNoteState::updateForOverdubbing(EditManager& manager, Track& track) {
    // Only update during overdubbing
    if (!track.isOverdubbing()) {
        return;
    }
    
    const auto& midiEvents = track.getMidiEvents();
    size_t currentEventCount = midiEvents.size();
    
    // Check if new MIDI events have been added
    if (currentEventCount > lastMidiEventCount) {
        // Find the most recent NoteOn event
        if (!ValidationUtils::validateLoopLength(track.getLoopLength())) return;
        uint32_t loopLength = track.getLoopLength();
        
        const auto& notes = track.getCachedNotes();
        if (!notes.empty()) {
            // Find the most recently added note by looking for the highest start tick
            // in the current loop position range
            uint32_t currentTick = clockManager.getCurrentTick();
            uint32_t tickInLoop = (currentTick - track.getStartLoopTick()) % loopLength;
            
            // Find notes that were just added (within a small window of current position)
            const uint32_t RECENT_WINDOW = 48; // About 16th note window
            
            int mostRecentIdx = -1;
            uint32_t closestDistance = RECENT_WINDOW + 1;
            
            for (int i = 0; i < (int)notes.size(); ++i) {
                uint32_t noteStart = notes[i].startTick % loopLength;
                uint32_t distance = (tickInLoop + loopLength - noteStart) % loopLength;
                
                if (distance <= RECENT_WINDOW && distance < closestDistance) {
                    closestDistance = distance;
                    mostRecentIdx = i;
                }
            }
            
            // Update bracket to the most recent note
            if (mostRecentIdx >= 0) {
                uint32_t newBracketTick = notes[mostRecentIdx].startTick % loopLength;
                manager.setBracketTick(newBracketTick);
                manager.setSelectedNoteIdx(mostRecentIdx);
                
                logger.debug("EditSelectNoteState: Updated bracket to new note at tick %lu (idx=%d)", 
                            newBracketTick, mostRecentIdx);
            }
        }
        
        lastMidiEventCount = currentEventCount;
    }
}

void EditSelectNoteState::createDefaultNote(Track& track, uint32_t tick) const {
    // Create a 32nd note (TICKS_PER_16TH_STEP / 2 = 24 ticks for a 32nd note)
    uint32_t noteLength = Config::TICKS_PER_16TH_STEP / 2; // 32nd note
    uint32_t endTick = (tick + noteLength) % track.getLoopLength();
    
    // Use a default note (C3 = MIDI note 60) with moderate velocity
    uint8_t defaultNote = 60; // C3
    uint8_t defaultVelocity = 80;
    
    auto& midiEvents = track.getMidiEvents();
    
    const uint8_t outCh = track.getMidiChannel();
    // Create Note On event
    MidiEvent noteOn;
    noteOn.type = midi::NoteOn;
    noteOn.tick = tick;
    noteOn.channel = outCh;
    noteOn.data.noteData.note = defaultNote;
    noteOn.data.noteData.velocity = defaultVelocity;
    midiEvents.push_back(noteOn);
    
    // Create Note Off event
    MidiEvent noteOff;
    noteOff.type = midi::NoteOff;
    noteOff.tick = endTick;
    noteOff.channel = outCh;
    noteOff.data.noteData.note = defaultNote;
    noteOff.data.noteData.velocity = 0;
    midiEvents.push_back(noteOff);
    
    // Sort events to maintain order
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    
    logger.info("EditSelectNoteState: Created 32nd note (pitch=%d, tick=%lu-%lu, length=%lu)", 
               defaultNote, tick, endTick, noteLength);
}

void EditSelectNoteState::createNoteAtTick(Track& track, uint32_t tick) {
    EditSelectNoteState helper;
    helper.createDefaultNote(track, tick);
}

void EditSelectNoteState::selectNextNoteSequential(EditManager& manager, Track& track) {
    if (!ValidationUtils::validateLoopLength(track.getLoopLength())) return;
    uint32_t loopLength = track.getLoopLength();
    
    // Get cached notes and make a mutable copy for sorting
    auto notes = track.getCachedNotes();  // Copy for sorting
    
    if (notes.empty()) {
        // No notes - move to next 16th note grid position
        uint32_t currentTick = manager.getBracketTick();
        uint32_t nextTick = (currentTick + Config::TICKS_PER_16TH_STEP) % loopLength;
        manager.setBracketTick(nextTick);
        manager.resetSelection();
        return;
    }
    
    // Sort notes by start tick, then by pitch for stable ordering
    std::sort(notes.begin(), notes.end(), 
              [](const NoteUtils::DisplayNote& a, const NoteUtils::DisplayNote& b) {
                  if (a.startTick != b.startTick) return a.startTick < b.startTick;
                  return a.note < b.note;  // Secondary sort by pitch
              });
    
    uint32_t currentTick = manager.getBracketTick();
    int currentSelectedIdx = manager.getSelectedNoteIdx();
    
    // Find current note in sorted list (if any)
    int currentSortedIdx = -1;
    if (currentSelectedIdx >= 0) {
        const auto& originalNotes = track.getCachedNotes();
        if (currentSelectedIdx < (int)originalNotes.size()) {
            const auto& currentNote = originalNotes[currentSelectedIdx];
            
            // Find this note in the sorted list
            for (int i = 0; i < (int)notes.size(); i++) {
                if (notes[i].note == currentNote.note && 
                    notes[i].startTick == currentNote.startTick &&
                    notes[i].endTick == currentNote.endTick) {
                    currentSortedIdx = i;
                    break;
                }
            }
        }
    }
    
    int nextIdx = -1;
    
    // If we have a current note, try to find the next note
    if (currentSortedIdx >= 0) {
        uint32_t currentNoteStartTick = notes[currentSortedIdx].startTick;
        
        // Count notes at the same tick position for user feedback
        int notesAtSameTick = 0;
        for (int i = 0; i < (int)notes.size(); i++) {
            if (notes[i].startTick == currentNoteStartTick) {
                notesAtSameTick++;
            }
        }
        
        // First, look for another note at the same tick position
        for (int i = currentSortedIdx + 1; i < (int)notes.size(); i++) {
            if (notes[i].startTick == currentNoteStartTick) {
                nextIdx = i;
                logger.log(CAT_MIDI, LOG_INFO, "Cycling through notes at tick %lu (%d notes total)", 
                           currentNoteStartTick, notesAtSameTick);
                break;
            }
        }
        
        // If no more notes at the same tick, find next tick position
        if (nextIdx == -1) {
            for (int i = 0; i < (int)notes.size(); i++) {
                if (notes[i].startTick > currentNoteStartTick) {
                    nextIdx = i;
                    logger.log(CAT_MIDI, LOG_INFO, "Moving to next tick position: %lu -> %lu", 
                               currentNoteStartTick, notes[i].startTick);
                    break;
                }
            }
        }
    } else {
        // No current selection, find first note at or after current tick
        for (int i = 0; i < (int)notes.size(); i++) {
            if (notes[i].startTick >= currentTick) {
                nextIdx = i;
                break;
            }
        }
    }
    
    // If no note found after current position, wrap to first note
    if (nextIdx == -1 && !notes.empty()) {
        nextIdx = 0;
    }
    
    if (nextIdx >= 0) {
        manager.setBracketTick(notes[nextIdx].startTick % loopLength);
        
        // Find the index of this note in the original unsorted notes list for display highlighting
        const auto& originalNotes = track.getCachedNotes();
        int originalIdx = findNoteIndexInOriginalList(notes[nextIdx], originalNotes);
        manager.setSelectedNoteIdx(originalIdx);
        
        // Log selection for debugging
        logger.log(CAT_MIDI, LOG_DEBUG, "Selected note: pitch=%d, tick=%lu, original_idx=%d", 
                   notes[nextIdx].note, notes[nextIdx].startTick, originalIdx);
    } else {
        // No notes found - move to next 16th note grid position
        uint32_t nextTick = (currentTick + Config::TICKS_PER_16TH_STEP) % loopLength;
        manager.setBracketTick(nextTick);
        manager.resetSelection();
    }
}

int EditSelectNoteState::findNoteIndexInOriginalList(const NoteUtils::DisplayNote& targetNote,
                                                     const std::vector<NoteUtils::DisplayNote>& originalNotes) const {
    // Find the note in the original unsorted notes list by matching note properties
    for (int i = 0; i < (int)originalNotes.size(); ++i) {
        const auto& note = originalNotes[i];
        // Match by note pitch, start tick, and end tick for exact identification
        if (note.note == targetNote.note && 
            note.startTick == targetNote.startTick && 
            note.endTick == targetNote.endTick &&
            note.velocity == targetNote.velocity) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Found exact match: note=%d, start=%lu, end=%lu, idx=%d", 
                       targetNote.note, targetNote.startTick, targetNote.endTick, i);
            return i;
        }
    }
    
    // If exact match not found, try matching by pitch and start tick only
    // (useful for cases where end tick might have slight differences)
    for (int i = 0; i < (int)originalNotes.size(); ++i) {
        const auto& note = originalNotes[i];
        if (note.note == targetNote.note && 
            note.startTick == targetNote.startTick) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Found partial match: note=%d, start=%lu, idx=%d", 
                       targetNote.note, targetNote.startTick, i);
            return i;
        }
    }
    
    // If still no match, return -1 to indicate no selection
    logger.log(CAT_MIDI, LOG_WARNING, "No match found for note: pitch=%d, start=%lu, end=%lu", 
               targetNote.note, targetNote.startTick, targetNote.endTick);
    return -1;
} 

void EditSelectNoteState::selectPreviousNoteSequential(EditManager& manager, Track& track) {
    if (!ValidationUtils::validateLoopLength(track.getLoopLength())) return;
    uint32_t loopLength = track.getLoopLength();
    
    // Get cached notes and make a mutable copy for sorting
    auto notes = track.getCachedNotes();  // Copy for sorting
    
    if (notes.empty()) {
        // No notes - move to previous 16th note grid position
        uint32_t currentTick = manager.getBracketTick();
        uint32_t prevTick = (currentTick + loopLength - Config::TICKS_PER_16TH_STEP) % loopLength;
        manager.setBracketTick(prevTick);
        manager.resetSelection();
        return;
    }
    
    // Sort notes by start tick, then by pitch for stable ordering
    std::sort(notes.begin(), notes.end(), 
              [](const NoteUtils::DisplayNote& a, const NoteUtils::DisplayNote& b) {
                  if (a.startTick != b.startTick) return a.startTick < b.startTick;
                  return a.note < b.note;  // Secondary sort by pitch
              });
    
    uint32_t currentTick = manager.getBracketTick();
    int currentSelectedIdx = manager.getSelectedNoteIdx();
    
    // Find current note in sorted list (if any)
    int currentSortedIdx = -1;
    if (currentSelectedIdx >= 0) {
        const auto& originalNotes = track.getCachedNotes();
        if (currentSelectedIdx < (int)originalNotes.size()) {
            const auto& currentNote = originalNotes[currentSelectedIdx];
            
            // Find this note in the sorted list
            for (int i = 0; i < (int)notes.size(); i++) {
                if (notes[i].note == currentNote.note && 
                    notes[i].startTick == currentNote.startTick &&
                    notes[i].endTick == currentNote.endTick) {
                    currentSortedIdx = i;
                    break;
                }
            }
        }
    }
    
    int prevIdx = -1;
    
    // If we have a current note, try to find the previous note
    if (currentSortedIdx >= 0) {
        uint32_t currentNoteStartTick = notes[currentSortedIdx].startTick;
        
        // Count notes at the same tick position for user feedback
        int notesAtSameTick = 0;
        for (int i = 0; i < (int)notes.size(); i++) {
            if (notes[i].startTick == currentNoteStartTick) {
                notesAtSameTick++;
            }
        }
        
        // First, look for another note at the same tick position (previous in sort order)
        for (int i = currentSortedIdx - 1; i >= 0; i--) {
            if (notes[i].startTick == currentNoteStartTick) {
                prevIdx = i;
                logger.log(CAT_MIDI, LOG_INFO, "Cycling through notes at tick %lu (%d notes total)", 
                           currentNoteStartTick, notesAtSameTick);
                break;
            }
        }
        
        // If no more notes at the same tick, find previous tick position
        if (prevIdx == -1) {
            for (int i = (int)notes.size() - 1; i >= 0; i--) {
                if (notes[i].startTick < currentNoteStartTick) {
                    prevIdx = i;
                    logger.log(CAT_MIDI, LOG_INFO, "Moving to previous tick position: %lu -> %lu", 
                               currentNoteStartTick, notes[i].startTick);
                    break;
                }
            }
        }
    } else {
        // No current selection, find last note at or before current tick
        for (int i = (int)notes.size() - 1; i >= 0; i--) {
            if (notes[i].startTick <= currentTick) {
                prevIdx = i;
                break;
            }
        }
    }
    
    // If no note found before current position, wrap to last note
    if (prevIdx == -1 && !notes.empty()) {
        prevIdx = (int)notes.size() - 1;
    }
    
    if (prevIdx >= 0) {
        manager.setBracketTick(notes[prevIdx].startTick % loopLength);
        
        // Find the index of this note in the original unsorted notes list for display highlighting
        const auto& originalNotes = track.getCachedNotes();
        int originalIdx = findNoteIndexInOriginalList(notes[prevIdx], originalNotes);
        manager.setSelectedNoteIdx(originalIdx);
        
        // Log selection for debugging
        logger.log(CAT_MIDI, LOG_DEBUG, "Selected note: pitch=%d, tick=%lu, original_idx=%d", 
                   notes[prevIdx].note, notes[prevIdx].startTick, originalIdx);
    } else {
        // No notes found - move to previous 16th note grid position
        uint32_t prevTick = (currentTick + loopLength - Config::TICKS_PER_16TH_STEP) % loopLength;
        manager.setBracketTick(prevTick);
        manager.resetSelection();
    }
} 

void EditSelectNoteState::sendTargetPitchbend(EditManager& manager, Track& track) {
    //auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    uint32_t bracketTick = manager.getBracketTick();
    
    if (!ValidationUtils::validateLoopLength(loopLength)) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Target pitchbend: No loop length, cannot calculate");
        return;
    }

    // Calculate total number of 16th steps in the loop
    uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
    logger.log(CAT_MIDI, LOG_DEBUG, "Target pitchbend calculation: loopLength=%lu, numSteps=%lu, bracketTick=%lu", 
               loopLength, numSteps, bracketTick);
    
    if (numSteps > 0) {
        const std::vector<SelectNavigation::SelectNavSlot> slots =
            NoteEditManager::buildSelectNavigationSlots(track, bracketTick, true);

        logger.log(CAT_MIDI, LOG_DEBUG, "Target pitchbend: Final navigation slots: %lu", slots.size());

        if (!slots.empty()) {
            const int currentPosIndex = SelectNavigation::findSlotIndexForSelection(
            slots, manager.getSelectedNoteIdx(), bracketTick, 0, loopLength);

            if (currentPosIndex >= 0) {
                // Calculate what pitchbend value corresponds to this position
                float normalizedPos = (float)currentPosIndex / (float)(slots.size() - 1);  // 0.0 to 1.0
                int16_t targetPitchbend = (int16_t)(MidiConfig::Pitchbend::MIN + normalizedPos * (MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
                targetPitchbend = constrain(targetPitchbend, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX);
                
                logger.log(CAT_MIDI, LOG_DEBUG, "SENDING PITCHBEND: Position %d/%lu at tick %lu = value %d (range: %d to %d)",
                           currentPosIndex, slots.size(), bracketTick, targetPitchbend,
                           MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX);
                
                // Send the pitchbend value to external device (select fader channel)
                midiHandler.sendPitchBend(MidiConfig::Fader::SELECT_CHANNEL, targetPitchbend);
                
                // Send note trigger to help motorized fader update (similar to fader 3)
                midiHandler.sendNoteOn(MidiConfig::Fader::SELECT_CHANNEL, 0, 127);
                midiHandler.sendNoteOff(MidiConfig::Fader::SELECT_CHANNEL, 0, 0);
                
                // Record the value we sent for smart feedback detection
                midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT).lastSentPitchbend = targetPitchbend;
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Target pitchbend: Current selection not found in navigation slots");
            }
        }
    }
} 