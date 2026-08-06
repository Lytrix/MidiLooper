//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstdint>
#include <algorithm>
#include <unordered_set>
#include "Globals.h"
#include "Utils/SelectNavigation.h"
#include "ControlSurfaceManager.h"
#include "ControlSurfaceManagerInternal.h"
#include "LoopEditManager.h"

#include "ClockManager.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "TrackUndo.h"
#include "EditManager.h"
#include "NoteEditFocus.h"
#include "EditStates/EditLengthNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "Utils/NoteUtils.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/ValidationUtils.h"
#include "Utils/MidiEventUtils.h"
#include "Utils/MidiMapping.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditFaderSelectSync.h"
#include "Utils/NoteEditFaderMotorTiming.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "DisplayManager.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteEditDependentFaderSnapshot.h"
#include "Utils/LoopTickNormalize.h"
#include "Utils/LoopEventValidation.h"
#include "MidiFaderManager.h"
#include "MidiFaderProcessor.h"
#include "Utils/NoteEditMem.h"

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#endif

ControlSurfaceManager controlSurfaceManager;

#if defined(SWAP_FADER1_FADER2_TEST)
namespace {
struct LogFaderChannelSwap {
    LogFaderChannelSwap() {
        logger.info("DIAG SWAP_FADER1_FADER2_TEST: select motor ch%u, coarse motor ch%u",
                    static_cast<unsigned>(MidiConfig::Fader::SELECT_MOTOR_CHANNEL),
                    static_cast<unsigned>(MidiConfig::Fader::COARSE_MOTOR_CHANNEL));
    }
};
static LogFaderChannelSwap s_logFaderChannelSwap;
}  // namespace
#endif


NOTE_EDIT_MEM ControlSurfaceManager::ControlSurfaceManager() = default;

// Delegate MIDI note handling to V2 system
NOTE_EDIT_MEM void ControlSurfaceManager::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
    buttonHandler.handleMidiNote(channel, note, velocity, isNoteOn);
}

NOTE_EDIT_MEM void ControlSurfaceManager::update() {
    if (!startEditingEnabled && noteSelectionTime > 0) {
        enableStartEditing();
    }

    if constexpr (kEditedNoteAuditionEnabled) {
        const bool transportRunning = clockManager.isTransportRunning();
        if (transportRunning && !editedNoteAuditionTransportWasRunning_) {
            releaseEditedNoteAudition();
        }
        editedNoteAuditionTransportWasRunning_ = transportRunning;
    }

    processFaderOutbound();
    Track& selectedTrack = trackManager.getSelectedTrack();
    processPendingPlayingGeometry(selectedTrack);
    editManager.processDeferredNoteEditDisplayRefresh(selectedTrack);
    editManager.processKindBoundaryUndoWarm(selectedTrack);

    loopEditManager.update();
    faderHandler.update();
    buttonHandler.update();
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
    // Log all pitchbend messages for debugging
    logger.log(CAT_MIDI, LOG_DEBUG, "Received pitchbend: ch=%d value=%d", channel, pitchValue);
    
    // Route channel 16 based on current edit mode
    if (channel == PITCHBEND_SELECT_CHANNEL) {  // Channel 16
        if (editManager.getEditSessionType() == EditSessionType::Loop) {
            // In loop edit mode: Route to loop start fader
            loopEditManager.handleLoopStartFaderInput(pitchValue, trackManager.getSelectedTrack());
            logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ch=%d routed to loop start fader (LOOP_EDIT mode)", channel);
            return;
        } else {
            if (currentDriverFader == MidiMapping::FaderType::FADER_NOTE_VALUE) {
                auto& selectState =
                    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT);
                if (NoteEditFaderSelectSync::shouldIgnoreSelectFaderEcho(
                        pitchValue, selectState.lastSentPitchbend, SELECT_MOVEMENT_THRESHOLD)) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                               "Pitchbend ch=%d ignored (ch16 echo after note-value edit, diff<=%d)",
                               channel, SELECT_MOVEMENT_THRESHOLD);
                    return;
                }
            }
            handleFaderInput(MidiMapping::FaderType::FADER_SELECT, pitchValue, 0);
            logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ch=%d routed to select fader (NOTE_EDIT mode)", channel);
            return;
        }
    } else if (channel == PITCHBEND_START_CHANNEL) {  // Fader 2 coarse (channel 14)
        if (editManager.getEditSessionType() == EditSessionType::Loop) {
            loopEditManager.handleLoopLengthPitchbend(pitchValue, trackManager.getSelectedTrack());
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Pitchbend ch=%d routed to loop length fader (LOOP_EDIT mode)", channel);
            return;
        }
        handleFaderInput(MidiMapping::FaderType::FADER_COARSE, pitchValue, 0);
        return;
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ignored: not on monitored channels (%d or %d)", 
               PITCHBEND_SELECT_CHANNEL, PITCHBEND_START_CHANNEL);
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    logger.log(CAT_MIDI, LOG_DEBUG, "Received CC: ch=%d cc=%d value=%d", channel, ccNumber, value);
    
    // Check for loop length control first
    if (channel == MidiConfig::LoopEdit::LENGTH_CC_CHANNEL && ccNumber == MidiConfig::LoopEdit::LENGTH_CC_NUMBER) {
        loopEditManager.handleLoopLengthInput(value, trackManager.getSelectedTrack());
        return;
    }

    if (editManager.getEditSessionType() == EditSessionType::Loop &&
        channel == FINE_CC_CHANNEL && ccNumber == FINE_CC_NUMBER) {
        loopEditManager.handleLoopLengthInput(value, trackManager.getSelectedTrack());
        return;
    }
    
    // Route to unified fader system
    if (channel == FINE_CC_CHANNEL && ccNumber == FINE_CC_NUMBER) {
        handleFaderInput(MidiMapping::FaderType::FADER_FINE, 0, value);
        return;
    } else if (channel == NOTE_VALUE_CC_CHANNEL && ccNumber == NOTE_VALUE_CC_NUMBER) {
        handleFaderInput(MidiMapping::FaderType::FADER_NOTE_VALUE, 0, value);
        return;
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "CC ignored: not on monitored channels/CC (%d/%d, %d/%d, or loop length)", 
               FINE_CC_CHANNEL, FINE_CC_NUMBER, NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER);
}

NOTE_EDIT_MEM void ControlSurfaceManager::cycleEditSession(Track& track) {
    const EditSessionType priorSession = editManager.getEditSessionType();
    editManager.cycleEditSession(track);
    if (priorSession == EditSessionType::Note) {
        releaseEditedNoteAudition();
    }
}








NOTE_EDIT_MEM void ControlSurfaceManager::recordFaderInputForValidation(MidiMapping::FaderType faderType,
                                                    int16_t pitchbendValue, uint8_t ccValue) {
    const uint32_t now = millis();
    switch (faderType) {
        case MidiMapping::FaderType::FADER_COARSE:
            lastUserCoarseFaderValue = pitchbendValue;
            lastCoarseFaderTime = now;
            break;
        case MidiMapping::FaderType::FADER_FINE:
            lastFineCCValue = ccValue;
            fineCCInitialized = true;
            break;
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            break;
        default:
            break;
    }
}


NOTE_EDIT_MEM bool ControlSurfaceManager::isGeometryDriverActive(uint32_t now) const {
    if (currentDriverFader == MidiMapping::FaderType::FADER_SELECT) {
        return false;
    }
    if (lastDriverFaderTime == 0) {
        return false;
    }
    return (now - lastDriverFaderTime) < COARSE_STABILITY_TIME;
}

NOTE_EDIT_MEM void ControlSurfaceManager::armSelectFaderFeedbackIgnore(uint32_t sentAt, uint32_t durationMs) {
    const uint32_t until = sentAt + durationMs;
    if (until > selectFaderFeedbackIgnoreUntilMs_) {
        selectFaderFeedbackIgnoreUntilMs_ = until;
    }
}




NOTE_EDIT_MEM void ControlSurfaceManager::enableStartEditing() {
    const uint32_t now = millis();
    if constexpr (kNoteEditFaderFeedbackEnabled) {
        if (selectDependentSettleUntilMs_ != 0 && now < selectDependentSettleUntilMs_) {
            return;
        }
    }
    if (now - noteSelectionTime >= NOTE_SELECTION_GRACE_PERIOD) {
        if (!startEditingEnabled) {
            startEditingEnabled = true;
            logger.info("Start editing enabled - grace period elapsed (%lu ms since selection)",
                        now - noteSelectionTime);
        }
    }
}

NOTE_EDIT_MEM void ControlSurfaceManager::prepareNoteEditSessionOpen() {
    suppressSelectDependentMotorSync_ = true;
    clearPendingSelectDependentMotorSync();
    clearPendingGeometryDriverMotorSync();
    lastGeometryF1SyncedBracketTick_ = UINT32_MAX;
    startEditingEnabled = false;
}

NOTE_EDIT_MEM void ControlSurfaceManager::scheduleNoteSelectFaderSync(Track& track) {
    noteSelectionTime = millis();
    startEditingEnabled = false;
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::NoteSelectWithFader1);
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendNoteEditSessionFaderFeedback(Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return;
    }

    resetSelectNavSlotApplyState();
    noteSelectionTime = millis();
    startEditingEnabled = false;
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        suppressSelectDependentMotorSync_ = false;
        logger.info("NOTE_EDIT session fader sync: feedback disabled");
        (void)track;
        return;
    }
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::SessionOpen);
    logger.info("NOTE_EDIT session fader sync: outbound coordinator");
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::armChannel15FaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = sentAt;
    lastPitchbendSentTime = sentAt;
}

NOTE_EDIT_MEM void ControlSurfaceManager::armCoarseFaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = sentAt;
    lastPitchbendSentTime = sentAt;
}

NOTE_EDIT_MEM void ControlSurfaceManager::armChannel15CcFaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = sentAt;
}

NOTE_EDIT_MEM void ControlSurfaceManager::refreshEditingActivity() {
    lastEditingActivityTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG, "Editing activity refreshed - note selection disabled for %dms", NOTE_SELECTION_GRACE_PERIOD);
}

// Unified Fader State Machine Implementation - now delegated to MidiFaderProcessor

// MidiFaderProcessor::FaderState& ControlSurfaceManager::getFaderState(MidiMapping::FaderType faderType) {
//     for (auto& state : faderStates) {
//         if (state.type == faderType) {
//             return state;
//         }
//     }
//     // Should never happen, but return first as fallback
//     return faderStates[0];
// }


NOTE_EDIT_MEM void ControlSurfaceManager::releaseEditedNoteAudition() {
    if constexpr (!kEditedNoteAuditionEnabled) {
        return;
    }
    if (!editedNoteAuditionHeld_) {
        return;
    }
    midiHandler.sendNoteOff(editedNoteAuditionChannel_, editedNoteAuditionPitch_, 0);
    midiHandler.sendPitchBend(editedNoteAuditionChannel_, MidiConfig::Pitchbend::CENTER);
    editedNoteAuditionHeld_ = false;
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendEditedNoteAuditionWhenTransportStopped(Track& track,
                                                                 int16_t pitchOverride) {
    if constexpr (!kEditedNoteAuditionEnabled) {
        return;
    }
    if (clockManager.isTransportRunning()) {
        releaseEditedNoteAudition();
        return;
    }
    if (editManager.getEditSessionType() != EditSessionType::Note ||
        !editManager.isNoteEditActive()) {
        releaseEditedNoteAudition();
        return;
    }

    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
        releaseEditedNoteAudition();
        return;
    }

    const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
    uint8_t pitch =
        pitchOverride >= 0 ? static_cast<uint8_t>(pitchOverride) : liveNote.note;
    uint8_t velocity = liveNote.velocity > 0 ? liveNote.velocity : 100;

    const uint8_t outChannel = track.getMidiChannel();
    if (editedNoteAuditionHeld_ && editedNoteAuditionChannel_ == outChannel &&
        editedNoteAuditionPitch_ == pitch) {
        return;
    }
    if (editedNoteAuditionHeld_) {
        midiHandler.sendNoteOff(editedNoteAuditionChannel_, editedNoteAuditionPitch_, 0);
    }
    midiHandler.sendPitchBend(outChannel, MidiConfig::Pitchbend::CENTER);
    midiHandler.sendNoteOn(outChannel, pitch, velocity);
    editedNoteAuditionChannel_ = outChannel;
    editedNoteAuditionPitch_ = pitch;
    editedNoteAuditionHeld_ = true;
}

NOTE_EDIT_MEM bool ControlSurfaceManager::shouldIgnoreDependentFaderInput(MidiMapping::FaderType faderType,
                                                      int16_t pitchbendValue, uint8_t ccValue,
                                                      Track& track) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)faderType;
        (void)pitchbendValue;
        (void)ccValue;
        (void)track;
        return false;
    }
    if (faderType != MidiMapping::FaderType::FADER_COARSE &&
        faderType != MidiMapping::FaderType::FADER_FINE &&
        faderType != MidiMapping::FaderType::FADER_NOTE_VALUE) {
        return false;
    }

    MidiFaderProcessor::FaderState& state = midiFaderManager.getFaderStateMutable(faderType);
    const uint32_t now = millis();

    if (faderType == currentDriverFader && lastDriverFaderTime != 0 &&
        (now - lastDriverFaderTime) < DRIVER_FADER_ACTIVE_MS) {
        return false;
    }

    const NoteEditDependentFaderSnapshot liveSnapshot =
        buildDependentFaderSnapshotForTrack(track, nullptr);
    const bool focusActive =
        editManager.isNoteEditActive() && editManager.getEditSession().focus.active;

    static constexpr int16_t kFeedbackTolerancePitchbend = 100;
    static constexpr uint8_t kFineFeedbackToleranceCc = 1;
    static constexpr uint8_t kNoteValueFeedbackToleranceCc = 1;

    if (faderType == MidiMapping::FaderType::FADER_COARSE && pitchbendValue != -1) {
        if (NoteEditDependentFaderFeedback::shouldIgnoreStaleLatch(
                pitchbendValue, state.lastSentPitchbend, liveSnapshot.coarsePitchbend,
                kFeedbackTolerancePitchbend, focusActive, liveSnapshot.coarseValid)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Ignoring fader 2 pitchbend %d (stale latch: sent %d live %d)",
                       pitchbendValue, state.lastSentPitchbend, liveSnapshot.coarsePitchbend);
            return true;
        }
    } else if ((faderType == MidiMapping::FaderType::FADER_FINE ||
                faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) &&
               ccValue != static_cast<uint8_t>(-1)) {
        const int liveCc = faderType == MidiMapping::FaderType::FADER_FINE
                               ? static_cast<int>(liveSnapshot.fineCc)
                               : static_cast<int>(liveSnapshot.noteValueCc);
        const bool snapshotValid = faderType == MidiMapping::FaderType::FADER_FINE
                                       ? liveSnapshot.fineValid
                                       : liveSnapshot.valid;
        const uint8_t staleTolerance = faderType == MidiMapping::FaderType::FADER_FINE
                                           ? kFineFeedbackToleranceCc
                                           : kNoteValueFeedbackToleranceCc;
        if (NoteEditDependentFaderFeedback::shouldIgnoreStaleLatch(
                static_cast<int>(ccValue), static_cast<int>(state.lastSentCC), liveCc,
                static_cast<int>(staleTolerance), focusActive, snapshotValid)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Ignoring fader %d CC %d (stale latch: sent %d live %d)", faderType, ccValue,
                       state.lastSentCC, liveCc);
            return true;
        }
    }

    if (state.lastSentTime > 0 && (now - state.lastSentTime) < FEEDBACK_IGNORE_PERIOD) {
        if (pitchbendValue == -1 && ccValue == static_cast<uint8_t>(-1)) {
            return true;
        }
        if (faderType == MidiMapping::FaderType::FADER_COARSE && pitchbendValue != -1) {
            if (pitchbendValue == state.lastSentPitchbend) {
                return true;
            }
            const int16_t diff = abs(pitchbendValue - state.lastSentPitchbend);
            if (diff <= kFeedbackTolerancePitchbend) {
                return true;
            }
        } else if (ccValue != static_cast<uint8_t>(-1)) {
            if (ccValue == state.lastSentCC) {
                return true;
            }
            const uint8_t tolerance = faderType == MidiMapping::FaderType::FADER_FINE
                                          ? kFineFeedbackToleranceCc
                                          : kNoteValueFeedbackToleranceCc;
            const uint8_t diff = static_cast<uint8_t>(
                abs(static_cast<int>(ccValue) - static_cast<int>(state.lastSentCC)));
            if (diff <= tolerance) {
                return true;
            }
        }
    }

    return false;
}

NOTE_EDIT_MEM bool ControlSurfaceManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType) {
    return shouldIgnoreFaderInput(faderType, -1, -1); // Use overloaded version with unknown values
}

NOTE_EDIT_MEM bool ControlSurfaceManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)faderType;
        (void)pitchbendValue;
        (void)ccValue;
        return false;
    }
    if (faderType != MidiMapping::FaderType::FADER_SELECT) {
        (void)ccValue;
        (void)pitchbendValue;
        return false;
    }

    MidiFaderProcessor::FaderState& state = midiFaderManager.getFaderStateMutable(faderType);
    uint32_t now = millis();

    if (pitchbendValue == -1) {
        return false;
    }
    if (state.lastSentTime > 0 &&
        (now - state.lastSentTime) < FEEDBACK_IGNORE_PERIOD &&
        NoteEditFaderSelectSync::shouldIgnoreSelectFaderEcho(pitchbendValue, state.lastSentPitchbend,
                                                             SELECT_MOVEMENT_THRESHOLD)) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader 1 pitchbend %d (motor echo: sent %d)",
                   pitchbendValue, state.lastSentPitchbend);
        return true;
    }
    if (selectFaderFeedbackIgnoreUntilMs_ != 0 && now < selectFaderFeedbackIgnoreUntilMs_) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Ignoring fader 1 pitchbend %d (feedback ignore window until %lu)",
                   pitchbendValue, selectFaderFeedbackIgnoreUntilMs_);
        return true;
    }
    return false;
}










NOTE_EDIT_MEM void ControlSurfaceManager::resetLengthEditingModeOnSessionBoundary() {
    resetSelectNavSlotApplyState();
    suppressSelectDependentMotorSync_ = false;
    const bool wasLength = editManager.isLengthEditingMode();
    editManager.clearLengthEditingMode(false);
    if (!wasLength) {
        return;
    }
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    logger.info("[MIDI] Length editing mode DISABLED (edit session boundary)");
}

NOTE_EDIT_MEM void ControlSurfaceManager::resetLengthEditingModeOnNoteSelect() {
    if (editManager.isLengthEditingMode()) {
        editManager.clearLengthEditingModeOnNoteSelect();
        logger.info("[MIDI] Length editing mode DISABLED (note select)");
    }
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    lastUserCoarseFaderValue = 0;
    lastCoarseFaderTime = 0;
    lastUserNoteValueCc = 64;
    lastNoteValueFaderTime = 0;
    lastUserFineCc = 64;
    lastFineFaderTime = 0;
}

NOTE_EDIT_MEM void ControlSurfaceManager::toggleLengthEditingMode() {
    uint32_t now = millis();

    if (now - lastLengthModeToggleTime < LENGTH_MODE_DEBOUNCE_TIME) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Length mode toggle ignored (debounce protection)");
        return;
    }
    lastLengthModeToggleTime = now;

    Track& track = trackManager.getSelectedTrack();
    const bool wasLength = editManager.isLengthEditingMode();
    editManager.toggleLengthEditMode(track);
    if (wasLength && !editManager.isLengthEditingMode()) {
        currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
        lastDriverFaderTime = now;
        lastUserCoarseFaderValue = 0;
        lastCoarseFaderTime = 0;
    }
}

NOTE_EDIT_MEM void ControlSurfaceManager::onEditEvent(EditEvent event) {
    Track& track = trackManager.getSelectedTrack();
    switch (event) {
        case EditEvent::SessionOpened:
            handleSessionOpenedEvent(track);
            break;
        case EditEvent::SessionClosed:
            handleSessionClosedEvent(track);
            break;
        case EditEvent::LengthModeChanged:
            handleLengthModeChangedEvent(track);
            break;
        case EditEvent::SelectionChanged:
            handleSelectionChangedEvent(track);
            break;
        case EditEvent::GeometryChanged:
            handleGeometryChangedEvent(track);
            break;
        default:
            break;
    }
}


NOTE_EDIT_MEM void ControlSurfaceManager::sendEditSessionMidi(EditSessionType sessionType) {
    uint8_t program = MidiConfig::SessionProgram::LOOP_EDIT;
    uint8_t triggerNote = 0;
    const char* modeName = "LOOP_EDIT";

    switch (sessionType) {
        case EditSessionType::Loop:
            program = MidiConfig::SessionProgram::LOOP_EDIT;
            triggerNote = 100;
            modeName = "LOOP_EDIT";
            break;
        case EditSessionType::Note:
            program = MidiConfig::SessionProgram::NOTE_EDIT;
            triggerNote = 0;
            modeName = "NOTE_EDIT";
            break;
        case EditSessionType::ControlChange:
            return;
    }

    midiHandler.sendProgramChange(MidiConfig::PROGRAM_CHANGE_CHANNEL, program);
    midiHandler.sendLedFeedbackNoteOn(triggerNote, 64);
    delay(10);
    midiHandler.sendLedFeedbackNoteOff(triggerNote);
    logger.log(CAT_MIDI, LOG_INFO, "Edit session: %s (Program %d, Note %d trigger)",
               modeName, program, triggerNote);
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleSessionOpenedEvent(Track& track) {
    if (editManager.sessionOpenedIncludesMidi()) {
        sendEditSessionMidi(editManager.getEditSessionType());
    }
    if (editManager.sessionOpenedIncludesFaderFeedback() && editManager.isNoteEditActive()) {
        prepareNoteEditSessionOpen();
        sendNoteEditSessionFaderFeedback(track);
        drainFaderOutboundUntilIdle();
    }
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleSessionClosedEvent(Track& track) {
    releaseEditedNoteAudition();
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleGeometryChangedEvent(Track& track) {
    resetLengthEditingModeOnNoteSelect();
    releaseEditedNoteAudition();
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleLengthModeChangedEvent(Track& track) {
    const std::vector<NoteUtils::DisplayNote> notes = editManager.selectableDisplayNotesForEditUi(track);
    const int selectedIdx = editManager.getSelectedNoteIdx();
    if (notes.empty() || selectedIdx < 0 || selectedIdx >= static_cast<int>(notes.size())) {
        return;
    }
    releaseEditedNoteAudition();
    if (editManager.isLengthEditingMode()) {
        requestFaderOutbound(NoteEditFaderOutbound::Trigger::LengthModeEnter);
    } else {
        requestFaderOutbound(NoteEditFaderOutbound::Trigger::LengthModeExit);
    }
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleSelectionChangedEvent(Track& track) {
    const EditorSelection& prior = editManager.selectionChangePrior();
    const EditorSelection& next = editManager.getNoteEditSessionState().selection;
    if (editManager.selectionChangeRequestFaderSync()) {
        scheduleNoteSelectFaderSync(track);
        return;
    }
    scheduleSelectDependentMotorSync(track, prior, next);
}

NOTE_EDIT_MEM void ControlSurfaceManager::onTrackChanged(Track& newTrack) {
    releaseEditedNoteAudition();
    // If we're in loop edit mode, send the new track's loop length as CC feedback
    if (editManager.getEditSessionType() == EditSessionType::Loop) {
        loopEditManager.onTrackChanged(newTrack);
    }
}