//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditStates/EditSelectNoteState.h"
#include "EditManager.h"
#include "NoteEditSessionState.h"
#include "EditPass.h"
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
#include "Utils/SelectNavigation.h"
#include "Utils/ValidationUtils.h"
#include "Utils/SelectNavigation.h"
#include "Utils/ValidationUtils.h"

void EditSelectNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("EditSelectNoteState::onEnter at tick %lu", startTick);
    
    // Initialize MIDI event count for overdub tracking
    lastMidiEventCount = track.editAwareMidiEvents().size();
    
    // Use the existing selectClosestNote logic which properly finds nearest notes
    // or snaps to the current position if no notes exist
    manager.selectClosestNote(track, startTick);
    
    uint32_t selectedTick = manager.getSelectedTick();
    int selectedIdx = manager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0) {
        logger.info("EditSelectNoteState: Found and selected note %d at tick %lu", 
                   selectedIdx, selectedTick);
    } else {
        logger.info("EditSelectNoteState: No note selected, bracket at tick %lu", selectedTick);
    }
    
    // Note: Pitchbend will be sent by MidiButtonManager after program change
    
    logger.info("MIDI Encoder: Entered SELECT mode (bracket=%lu)", selectedTick);
}

void EditSelectNoteState::onExit(EditManager& manager, Track& track) {
    logger.debug("EditSelectNoteState::onExit");
}

void EditSelectNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditSelectNoteState::onEncoderTurn called with delta=%d", delta);
    
    if (!ValidationUtils::validateLoopLength(track.getLoopLength())) return;
    
    if (delta > 0) {
        manager.stepSelectNavSlot(track, 1);
    } else if (delta < 0) {
        manager.stepSelectNavSlot(track, -1);
    }
    
    uint32_t selectedTick = manager.getSelectedTick();
    int selectedIdx = manager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0) {
        logger.debug("EditSelectNoteState: Moved to tick %lu, selected note %d", 
                    selectedTick, selectedIdx);
    } else {
        logger.debug("EditSelectNoteState: Moved to tick %lu, no note selected", selectedTick);
    }
}

void EditSelectNoteState::onButtonPress(EditManager& manager, Track& track) {
    logger.debug("EditSelectNoteState::onButtonPress");
    
    uint32_t selectedTick = manager.getSelectedTick();
    
    if (manager.getSelectedNoteIdx() >= 0) {
        // There's a note at this position - enter start note editing
        logger.info("EditSelectNoteState: Note exists, entering start note edit mode");
        manager.setState(manager.getStartNoteState(), track, selectedTick);
    } else {
        // No note at this position - create a 32nd note
        logger.info("EditSelectNoteState: No note found, creating 32nd note at tick %lu", selectedTick);
        
        const uint32_t loopLength = track.getLoopLength();
        const uint32_t loopStartTick = track.getLoopStartTick();
        const uint32_t bracketRel =
            SelectNavigation::displayPhaseTick(selectedTick, loopLength);
        const uint32_t storageTick =
            SelectNavigation::noteStorageTick(bracketRel, loopStartTick, loopLength);

        // Push undo snapshot before creating note
        manager.beginGeometryMutation(track, NoteEditKind::Add, false);
        const std::array<MidiEvent, 2> created = createDefaultNote(track, storageTick);
        NoteUtils::DisplayNote createdDisplay{};
        createdDisplay.noteId = created[0].noteId;
        createdDisplay.note = created[0].data.noteData.note;
        createdDisplay.velocity = created[0].data.noteData.velocity;
        createdDisplay.startTick = created[0].tick;
        createdDisplay.endTick = created[1].tick;
        manager.applyCreatedNoteOverlapGeometry(track, createdDisplay);
        EditPass add{};
        add.passType = EditPassType::Note;
        add.actionType = EditActionType::Create;
        add.propertyType = EditPropertyType::None;
        add.addedEvents.push_back(created[0]);
        add.addedEvents.push_back(created[1]);
        manager.commitEditAction(track, EditPassVec{add});
        track.invalidateCaches();

        // Select the newly created note and enter start note editing
        manager.selectNoteAtBracket(track, bracketRel);
        manager.setState(manager.getStartNoteState(), track, selectedTick);
    }
}

void EditSelectNoteState::updateForOverdubbing(EditManager& manager, Track& track) {
    // Only update during overdubbing
    if (!track.isOverdubbing()) {
        return;
    }
    
    const auto& midiEvents = track.editAwareMidiEvents();
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
                manager.setSelectedTick(newBracketTick);
                manager.setSelectedNoteIdx(mostRecentIdx);
                
                logger.debug("EditSelectNoteState: Updated bracket to new note at tick %lu (idx=%d)", 
                            newBracketTick, mostRecentIdx);
            }
        }
        
        lastMidiEventCount = currentEventCount;
    }
}

std::array<MidiEvent, 2> EditSelectNoteState::createDefaultNote(Track& track, uint32_t tick) const {
    // Create a 32nd note (TICKS_PER_16TH_STEP / 2 = 24 ticks for a 32nd note)
    uint32_t noteLength = Config::TICKS_PER_16TH_STEP / 2; // 32nd note
    uint32_t endTick = (tick + noteLength) % track.getLoopLength();
    
    // Use a default note (C3 = MIDI note 60) with moderate velocity
    uint8_t defaultNote = 60; // C3
    uint8_t defaultVelocity = 80;
    
    auto& midiEvents = track.editAwareMidiEvents();
    
    const uint8_t outCh = track.getMidiChannel();
    Loop& loop = track.getActiveLoop();
    // Create Note On event
    MidiEvent noteOn;
    noteOn.type = midi::NoteOn;
    noteOn.tick = tick;
    noteOn.channel = outCh;
    noteOn.data.noteData.note = defaultNote;
    noteOn.data.noteData.velocity = defaultVelocity;
    noteOn.noteId = loop.allocateNoteId();
    midiEvents.push_back(noteOn);
    
    // Create Note Off event
    MidiEvent noteOff;
    noteOff.type = midi::NoteOff;
    noteOff.tick = endTick;
    noteOff.channel = outCh;
    noteOff.data.noteData.note = defaultNote;
    noteOff.data.noteData.velocity = 0;
    noteOff.noteId = noteOn.noteId;
    midiEvents.push_back(noteOff);
    
    // Sort events to maintain order
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    track.invalidateCaches();

    logger.info(
        "EditSelectNoteState: Created 32nd note (noteId=%lu, pitch=%d, tick=%lu-%lu, length=%lu)",
        static_cast<unsigned long>(noteOn.noteId), defaultNote, tick, endTick, noteLength);
    // Return the exact created events: after the sort above they are not necessarily the
    // last two entries, so callers must use these to record an AddNote edit.
    return {noteOn, noteOff};
}

std::array<MidiEvent, 2> EditSelectNoteState::createNoteAtTick(Track& track, uint32_t tick) {
    EditSelectNoteState helper;
    return helper.createDefaultNote(track, tick);
}

bool EditSelectNoteState::resolveTargetPitchbend(EditManager& manager, Track& track,
                                                 int16_t& outPitchbend) {
    const uint32_t loopLength = track.getLoopLength();
    const uint32_t selectedTick = manager.getSelectedTick();

    if (!ValidationUtils::validateLoopLength(loopLength)) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Target pitchbend: No loop length, cannot calculate");
        return false;
    }

    const uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Target pitchbend calculation: loopLength=%lu, numSteps=%lu, selectedTick=%lu",
               loopLength, numSteps, selectedTick);

    if (numSteps == 0) {
        return false;
    }

    const std::vector<SelectNavigation::SelectNavSlot> slots =
        manager.buildSelectNavigationSlots(track, selectedTick, true);

    logger.log(CAT_MIDI, LOG_DEBUG, "Target pitchbend: Final navigation slots: %lu", slots.size());

    if (slots.empty()) {
        return false;
    }

    const EditorSelection& sel = manager.getNoteEditSessionState().selection;
    const auto navNotes = manager.selectableDisplayNotesForEditUi(track);
    int currentPosIndex = -1;
    if (editorSelectionHasNote(sel)) {
        currentPosIndex = SelectNavigation::findSlotIndexForNoteId(
            slots, navNotes, sel.primaryNote, selectedTick, loopLength);
    } else {
        currentPosIndex =
            SelectNavigation::findSlotIndexForSelection(slots, -1, selectedTick, loopLength);
    }

    if (currentPosIndex < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Target pitchbend: Current selection not found in navigation slots");
        return false;
    }

    const float normalizedPos =
        static_cast<float>(currentPosIndex) / static_cast<float>(slots.size() - 1);
    int16_t targetPitchbend = static_cast<int16_t>(
        MidiConfig::Pitchbend::MIN +
        normalizedPos * static_cast<float>(MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
    outPitchbend = constrain(targetPitchbend, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX);

    logger.log(CAT_MIDI, LOG_DEBUG,
               "SENDING PITCHBEND: Position %d/%lu at tick %lu = value %d (range: %d to %d)",
               currentPosIndex, slots.size(), selectedTick, outPitchbend, MidiConfig::Pitchbend::MIN,
               MidiConfig::Pitchbend::MAX);
    return true;
}

void EditSelectNoteState::sendTargetPitchbend(EditManager& manager, Track& track) {
    int16_t targetPitchbend = 0;
    if (!resolveTargetPitchbend(manager, track, targetPitchbend)) {
        return;
    }

    midiHandler.sendPitchBend(MidiConfig::Fader::SELECT_MOTOR_CHANNEL, targetPitchbend);
    midiHandler.sendNoteOn(MidiConfig::Fader::SELECT_MOTOR_CHANNEL, 0, 127);
    midiHandler.sendNoteOff(MidiConfig::Fader::SELECT_MOTOR_CHANNEL, 0, 0);
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT).lastSentPitchbend =
        targetPitchbend;
} 