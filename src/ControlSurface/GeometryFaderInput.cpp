//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"
#include "ControlSurfaceManagerInternal.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "EditManager.h"
#include "EditStates/EditLengthNoteState.h"
#include "Globals.h"
#include "Logger.h"
#include "NoteEditFocus.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/LoopTickNormalize.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/NoteUtils.h"
#include "Utils/SelectNavigation.h"

NOTE_EDIT_MEM void ControlSurfaceManager::handleCoarseFaderInput(int16_t pitchValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Start editing disabled (grace period active)");
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for coarse editing");
        return;
    }

    if (editManager.isLengthEditingMode()) {
        editManager.syncNoteEditFocusLastFromSessionStore(track);
    }

    if constexpr (kNoteEditFaderFeedbackEnabled) {
        if (!editManager.isLengthEditingMode()) {
            const Fader1SelectTarget f1Target =
                resolveFader1SelectTarget(track, lastUserSelectFaderValue);
            if (f1Target.valid) {
                const EditorSelection& sel = editManager.getNoteEditSessionState().selection;
                NoteId physicalPrimary = kInvalidNoteId;
                if (f1Target.noteIdx >= 0) {
                    const std::vector<NoteUtils::DisplayNote> navNotes =
                        editManager.selectableDisplayNotesForEditUi(track);
                    if (f1Target.noteIdx < static_cast<int>(navNotes.size())) {
                        physicalPrimary =
                            noteIdFromFilteredDisplayNote(navNotes, f1Target.noteIdx);
                    }
                }
                const bool physicalTargetDivergent =
                    NoteEditFaderSelectSync::physicalSelectTargetDivergesFromLogical(
                        sel.primaryNote, sel.selectedTick, physicalPrimary,
                        f1Target.absoluteTargetTick);
                const uint32_t coarseGuardNow = millis();
                if (NoteEditFaderSelectSync::shouldBlockCoarseForPendingSelectNavigation(
                        physicalTargetDivergent, currentDriverFader,
                        isGeometryDriverActive(coarseGuardNow))) {
#if defined(SESSION_CAPTURE)
                    logger.info("#DBG coarse_blocked pending_select_navigation bracket_tick=%lu",
                                static_cast<unsigned long>(f1Target.absoluteTargetTick));
#endif
                    logger.log(CAT_MIDI, LOG_DEBUG,
                               "Coarse fader: blocked while select fader diverges from logical "
                               "selection");
                    return;
                }
            }
        }
    }

    // Movement filtering - prevent jitter from rescheduling updates
    uint32_t now = millis();
    int16_t movementDelta = abs(pitchValue - lastUserCoarseFaderValue);
    uint32_t timeSinceLastMovement = (lastCoarseFaderTime > 0) ? (now - lastCoarseFaderTime) : COARSE_STABILITY_TIME;
    
    // Only process if movement is significant enough or enough time has passed
    if (movementDelta >= COARSE_MOVEMENT_THRESHOLD || timeSinceLastMovement >= COARSE_STABILITY_TIME) {
        // Update tracking values
        lastUserCoarseFaderValue = pitchValue;
        lastCoarseFaderTime = now;
        lastMotorSyncDriverInputMs_ = now;
        clearPendingSelectDependentMotorSync();
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader: significant movement (delta=%d, time=%lu ms) - %s mode", 
                   movementDelta, timeSinceLastMovement, editManager.isLengthEditingMode() ? "LENGTH EDIT" : "POSITION EDIT");
        releaseEditedNoteAudition();
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader: ignoring small movement (delta=%d, time=%lu ms)", 
                   movementDelta, timeSinceLastMovement);
        return; // Skip processing for small movements
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    const std::vector<NoteUtils::DisplayNote> notes = editManager.selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    if (selectedIdx >= static_cast<int>(notes.size()) && selectedIdx >= 0 &&
        editorSelectionHasNote(editManager.getNoteEditSessionState().selection)) {
        editManager.syncSelectedNoteIdxToFilteredInventory(track);
        selectedIdx = editManager.getSelectedNoteIdx();
    }
    
    if (selectedIdx >= 0 && selectedIdx < static_cast<int>(notes.size())) {
        NoteUtils::DisplayNote currentNote = editManager.liveEditDisplayNoteAtSelect(track);
        uint32_t currentNoteStartTick = currentNote.startTick;
        const uint32_t loopStartTick = editManager.noteEditLoopStartTick(track);
        const uint32_t loopStartPhase = loopStartTick % loopLength;
        const bool focusActive = editManager.getEditSession().focus.active;
        
        if (focusActive) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader using focus.last: pitch=%d, start=%lu",
                       currentNote.note,
                       static_cast<unsigned long>(currentNote.startTick));
        }
        
        bool geometryApplied = false;
        if (editManager.isLengthEditingMode()) {
            const uint32_t ticksPerStep = Config::TICKS_PER_16TH_STEP;
            const uint32_t minNoteDuration = ticksPerStep;

            const uint32_t relativeEndTick =
                focusActive
                    ? SelectNavigation::noteRelativeTick(currentNote.endTick, loopStartPhase,
                                                         loopLength)
                    : SelectNavigation::displayPhaseTick(currentNote.endTick, loopLength);
            uint32_t targetEndTick =
                lengthEditCoarsePitchbendToLoopTick(pitchValue, loopLength);
            applyLengthEndTargetRules(currentNote.startTick, currentNote.endTick, loopLength,
                                      minNoteDuration, targetEndTick);
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "LENGTH EDIT: pitchbend %d -> tick %lu (was %lu)",
                       pitchValue, targetEndTick, relativeEndTick);
            if (clockManager.isTransportRunning()) {
                queuePendingPlayingLength(currentNote, targetEndTick);
                return;
            }
            geometryApplied =
                editManager.changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
            if (geometryApplied) {
                const NoteUtils::DisplayNote liveAfterLength =
                    editManager.liveEditDisplayNoteAtSelect(track);
                editManager.setLengthFineAnchorEndTick(
                    focusActive
                        ? SelectNavigation::noteRelativeTick(liveAfterLength.endTick, loopStartPhase,
                                                             loopLength)
                        : SelectNavigation::displayPhaseTick(liveAfterLength.endTick, loopLength));
                editManager.setReferenceStep(editManager.lengthFineAnchorEndTick() /
                                             Config::TICKS_PER_16TH_STEP);
            }
        } else {
            // POSITION EDIT MODE: Move the note START position in 16th step increments
            const uint32_t relativeStartTick =
                focusActive
                    ? SelectNavigation::noteRelativeTick(currentNoteStartTick, loopStartPhase,
                                                         loopLength)
                    : SelectNavigation::displayPhaseTick(currentNoteStartTick, loopLength);
            
            // Calculate how many 16th steps are in the loop
            uint32_t totalSixteenthSteps = loopLength / Config::TICKS_PER_16TH_STEP;
            
            // Calculate the offset within the current 16th step (using relative position)
            uint32_t currentSixteenthStep = relativeStartTick / Config::TICKS_PER_16TH_STEP;
            uint32_t offsetWithinSixteenth = relativeStartTick % Config::TICKS_PER_16TH_STEP;
            
            // Map pitchbend to 16th step across entire loop
            uint32_t targetSixteenthStep = map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0, totalSixteenthSteps - 1);
            
            // Calculate target tick: new 16th step + preserved offset (relative)
            uint32_t relativeTargetTick = (targetSixteenthStep * Config::TICKS_PER_16TH_STEP) + offsetWithinSixteenth;
            
            // Constrain to valid range within the loop
            if (relativeTargetTick >= loopLength) {
                relativeTargetTick = loopLength - 1;
            }
            
            // Convert display-phase target back to storage tick for session store writes
            const uint32_t targetTick =
                SelectNavigation::noteStorageTick(relativeTargetTick, loopStartPhase, loopLength);
            
            logger.log(CAT_MIDI, LOG_DEBUG, "POSITION EDIT: Note moved from step %lu to %lu (tick %lu -> %lu, relative %lu -> %lu)", 
                       currentSixteenthStep, targetSixteenthStep, currentNoteStartTick, targetTick, relativeStartTick, relativeTargetTick);
            
            // Store the target step as reference for fine adjustments
            editManager.setReferenceStep(targetSixteenthStep);
            if (clockManager.isTransportRunning()) {
                queuePendingPlayingMove(currentNote, targetTick);
                return;
            }
            geometryApplied = editManager.moveNoteToPosition(track, currentNote, targetTick);
        }

        if (geometryApplied) {
            finishGeometryDriverSideEffects(track, now, MidiMapping::FaderType::FADER_COARSE);
        }
        // NOTE: Fader 2 (COARSE) now uses 500ms grace period to update fader 1
        // This prevents erratic movement and allows proper settling time
    }
}
NOTE_EDIT_MEM void ControlSurfaceManager::handleFineFaderInput(uint8_t ccValue, Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine editing disabled (grace period active)");
        return;
    }

    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for fine editing");
        return;
    }

    const uint32_t now = millis();
    const int movementDelta =
        abs(static_cast<int>(ccValue) - static_cast<int>(lastUserFineCc));
    const uint32_t timeSinceLastMovement =
        (lastFineFaderTime > 0) ? (now - lastFineFaderTime) : FINE_STABILITY_TIME;
    if (movementDelta < static_cast<int>(FINE_MOVEMENT_THRESHOLD) &&
        timeSinceLastMovement < FINE_STABILITY_TIME) {
        return;
    }

    lastUserFineCc = ccValue;
    lastFineFaderTime = now;
    lastMotorSyncDriverInputMs_ = now;
    clearPendingSelectDependentMotorSync();
    releaseEditedNoteAudition();
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;

    NoteUtils::DisplayNote currentNote = editManager.liveEditDisplayNoteAtSelect(track);
    const uint32_t loopStartTick = editManager.noteEditLoopStartTick(track);
    const uint32_t loopStartPhase = loopStartTick % loopLength;
    const bool focusActive = editManager.getEditSession().focus.active;
    
    if (focusActive) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader using focus.last: pitch=%d, start=%lu",
                   currentNote.note,
                   static_cast<unsigned long>(currentNote.startTick));
    }
        
    bool geometryApplied = false;
    if (editManager.isLengthEditingMode()) {
        const uint32_t ticksPerStep = Config::TICKS_PER_16TH_STEP;
        const uint32_t minNoteDuration = ticksPerStep;
        const uint32_t anchorTick = editManager.lengthFineAnchorEndTick();
        const int32_t fineOffset = lengthEditFineOffsetFromCc(ccValue);

        int32_t relativeTargetEndSigned = static_cast<int32_t>(anchorTick) + fineOffset;
        uint32_t relativeTargetEndTick = 0;
        clampLengthEditFineTargetTick(relativeTargetEndSigned, loopLength, relativeTargetEndTick);
        uint32_t targetEndTick =
            focusActive
                ? SelectNavigation::noteStorageTick(relativeTargetEndTick, loopStartPhase,
                                                    loopLength)
                : relativeTargetEndTick;
        applyLengthEndTargetRules(currentNote.startTick, currentNote.endTick, loopLength,
                                  minNoteDuration, targetEndTick);
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "LENGTH EDIT (fine): anchor %lu offset %ld -> tick %lu",
                   anchorTick, fineOffset, targetEndTick);
        if (clockManager.isTransportRunning()) {
            queuePendingPlayingLength(currentNote, targetEndTick);
            return;
        }
        geometryApplied =
            editManager.changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
    } else {
        const uint32_t currentNoteStartTick = currentNote.startTick;
        const uint32_t relativeStartTick =
            focusActive
                ? SelectNavigation::noteRelativeTick(currentNoteStartTick, loopStartPhase,
                                                     loopLength)
                : SelectNavigation::displayPhaseTick(currentNoteStartTick, loopLength);
        
        uint32_t sixteenthStepStartTick = editManager.getReferenceStep() * Config::TICKS_PER_16TH_STEP;
        int32_t offset = static_cast<int32_t>(ccValue) - 64;
        int32_t relativeTargetStartTickSigned =
            static_cast<int32_t>(sixteenthStepStartTick) + offset;
        
        uint32_t relativeTargetStartTick;
        if (relativeTargetStartTickSigned < 0) {
            relativeTargetStartTick = loopLength + static_cast<uint32_t>(relativeTargetStartTickSigned);
        } else {
            relativeTargetStartTick = static_cast<uint32_t>(relativeTargetStartTickSigned);
        }
        
        if (relativeTargetStartTick >= loopLength) {
            relativeTargetStartTick = relativeTargetStartTick % loopLength;
        }
        
        const uint32_t targetStartTick =
            SelectNavigation::noteStorageTick(relativeTargetStartTick, loopStartPhase, loopLength);
        
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "POSITION EDIT: Fine adjustment from relative tick %lu to %lu (absolute %lu -> %lu)", 
                   relativeStartTick, relativeTargetStartTick, currentNoteStartTick,
                   targetStartTick);
        if (clockManager.isTransportRunning()) {
            queuePendingPlayingMove(currentNote, targetStartTick);
            return;
        }
        geometryApplied = editManager.moveNoteToPosition(track, currentNote, targetStartTick);
    }
        
    logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader: CC=%d - %s mode", 
               ccValue, editManager.isLengthEditingMode() ? "LENGTH EDIT" : "POSITION EDIT");
        
    if (geometryApplied) {
        finishGeometryDriverSideEffects(track, now, MidiMapping::FaderType::FADER_FINE);
        lastMotorSyncDriverInputMs_ = now;
        clearPendingSelectDependentMotorSync();
    }
}
NOTE_EDIT_MEM void ControlSurfaceManager::handleNoteValueFaderInput(uint8_t ccValue, Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value editing disabled (grace period active)");
        return;
    }
    
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for note value editing");
        return;
    }

    uint32_t now = millis();
    if constexpr (kNoteEditFaderFeedbackEnabled) {
        if ((now - noteSelectionTime) < static_cast<uint32_t>(FEEDBACK_IGNORE_PERIOD)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Note value fader: ignoring input during post-select routing settle");
            return;
        }
    }

    const int movementDelta =
        abs(static_cast<int>(ccValue) - static_cast<int>(lastUserNoteValueCc));
    const uint32_t timeSinceLastMovement =
        (lastNoteValueFaderTime > 0) ? (now - lastNoteValueFaderTime) : 0;
    if (movementDelta < static_cast<int>(NOTE_VALUE_MOVEMENT_THRESHOLD) &&
        timeSinceLastMovement < NOTE_VALUE_STABILITY_TIME) {
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;

    const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
    const uint8_t currentNoteValue = liveNote.note;
    uint32_t noteStart = liveNote.startTick;
    uint32_t noteEnd = liveNote.endTick;
    if (editManager.getEditSession().focus.active) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Pitch edit using focus.last: pitch=%d, start=%lu, end=%lu",
                   currentNoteValue,
                   static_cast<unsigned long>(noteStart),
                   static_cast<unsigned long>(noteEnd));
    }
    const uint8_t newNoteValue = constrain(ccValue, static_cast<uint8_t>(0), static_cast<uint8_t>(127));
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader: currentNote=%d newNote=%d (cc=%d)", 
               currentNoteValue, newNoteValue, ccValue);
    
    if (currentNoteValue == newNoteValue) {
        return;
    }

    lastUserNoteValueCc = newNoteValue;
    lastNoteValueFaderTime = now;
    lastMotorSyncDriverInputMs_ = now;
    clearPendingSelectDependentMotorSync();
    releaseEditedNoteAudition();

    if (clockManager.isTransportRunning()) {
        queuePendingPlayingPitch(liveNote, currentNoteValue, newNoteValue);
        return;
    }
    const bool refreshPlaybackPreview = !clockManager.isTransportRunning();
    if (!applyPlayingPitchGeometry(track, liveNote, currentNoteValue, newNoteValue,
                                   refreshPlaybackPreview)) {
        return;
    }

    finishGeometryDriverSideEffects(track, now, MidiMapping::FaderType::FADER_NOTE_VALUE);
    sendEditedNoteAuditionWhenTransportStopped(track, static_cast<int16_t>(newNoteValue));
}
NOTE_EDIT_MEM void ControlSurfaceManager::handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    Track& track = trackManager.getSelectedTrack();
    if constexpr (kNoteEditFaderFeedbackEnabled) {
        if (faderType != MidiMapping::FaderType::FADER_SELECT && isFaderOutboundActive()) {
            recordFaderInputForValidation(faderType, pitchbendValue, ccValue);
            return;
        }
    }
    const bool ignoreInput =
        faderType == MidiMapping::FaderType::FADER_SELECT
            ? shouldIgnoreFaderInput(faderType, pitchbendValue, ccValue)
            : shouldIgnoreDependentFaderInput(faderType, pitchbendValue, ccValue, track);
    if (ignoreInput) {
        if (faderType == MidiMapping::FaderType::FADER_SELECT) {
            logSelectSlot(-1, pitchbendValue, true, "echo");
        }
        return;
    }

    const uint32_t now = millis();
    if constexpr (kNoteEditFaderFeedbackEnabled) {
        if (faderType != MidiMapping::FaderType::FADER_SELECT && selectDependentSettleUntilMs_ != 0 &&
            now < selectDependentSettleUntilMs_) {
            recordFaderInputForValidation(faderType, pitchbendValue, ccValue);
#if defined(SESSION_CAPTURE)
            if (!selectDependentSettleBlockLogged_) {
                const uint32_t remainMs = selectDependentSettleUntilMs_ - now;
                const char* faderLabel = "unknown";
                switch (faderType) {
                    case MidiMapping::FaderType::FADER_COARSE:
                        faderLabel = "coarse";
                        break;
                    case MidiMapping::FaderType::FADER_FINE:
                        faderLabel = "fine";
                        break;
                    case MidiMapping::FaderType::FADER_NOTE_VALUE:
                        faderLabel = "note_value";
                        break;
                    default:
                        break;
                }
                logger.info("#DBG select_dependent_settle_block fader=%s remain_ms=%lu", faderLabel,
                            remainMs);
                selectDependentSettleBlockLogged_ = true;
            }
#endif
            return;
        }
    }

    if (NoteEditFaderOutbound::isChannel15OutboundStep(outboundStep_) &&
        (faderType == MidiMapping::FaderType::FADER_COARSE ||
         faderType == MidiMapping::FaderType::FADER_FINE ||
         faderType == MidiMapping::FaderType::FADER_NOTE_VALUE)) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d input (outbound ch15 active)", faderType);
        return;
    }
    
    // Get the current track
    (void)track;
    
    // Route to the appropriate fader handler
    switch (faderType) {
        case MidiMapping::FaderType::FADER_SELECT:
            handleSelectFaderInput(pitchbendValue, track);
            break;
        case MidiMapping::FaderType::FADER_COARSE:
            handleCoarseFaderInput(pitchbendValue, track);
            break;
        case MidiMapping::FaderType::FADER_FINE:
            handleFineFaderInput(ccValue, track);
            break;
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            handleNoteValueFaderInput(ccValue, track);
            break;
        default:
            logger.log(CAT_MIDI, LOG_DEBUG, "Unknown fader type: %d", (int)faderType);
            break;
    }
}
