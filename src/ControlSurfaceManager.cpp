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

namespace {

#if defined(SESSION_CAPTURE)
void logGeomApplyQueue(uint8_t kind, uint32_t targetField, bool transportRunning) {
    logger.info("#CAP,%lu,GEOM_APPLY,queue,%u,%lu,%u,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned>(kind), static_cast<unsigned long>(targetField),
                transportRunning ? 1u : 0u);
}

void logGeomApplyDequeue(uint32_t queueAgeMs, uint8_t kind) {
    logger.info("#CAP,%lu,GEOM_APPLY,dequeue,%lu,%u,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(queueAgeMs), static_cast<unsigned>(kind));
}

void logGeomApplyDone(bool applied, uint32_t displayRevision) {
    logger.info("#CAP,%lu,GEOM_APPLY,done,%u,%lu,0,0", static_cast<unsigned long>(micros()),
                applied ? 1u : 0u, static_cast<unsigned long>(displayRevision));
}

void logGeomApplySkip(uint8_t reasonCode) {
    logger.info("#CAP,%lu,GEOM_APPLY,skip,%u,0,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned>(reasonCode));
}
#endif

}  // namespace

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





NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingMove(const NoteUtils::DisplayNote& note,
                                                                  uint32_t targetTick) {
    pendingPlayingGeometryType_ = PendingPlayingGeometryType::Move;
    pendingPlayingGeometryNote_ = note;
    pendingPlayingGeometryTargetTick_ = targetTick;
    pendingPlayingGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingGeometryType::Move), targetTick,
                      clockManager.isTransportRunning());
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingLength(const NoteUtils::DisplayNote& note,
                                                                    uint32_t targetEndTick) {
    pendingPlayingGeometryType_ = PendingPlayingGeometryType::Length;
    pendingPlayingGeometryNote_ = note;
    pendingPlayingGeometryTargetTick_ = targetEndTick;
    pendingPlayingGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingGeometryType::Length), targetEndTick,
                      clockManager.isTransportRunning());
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingPlayingPitch(const NoteUtils::DisplayNote& note,
                                                                   uint8_t currentPitch,
                                                                   uint8_t newPitch) {
    pendingPlayingGeometryType_ = PendingPlayingGeometryType::Pitch;
    pendingPlayingGeometryNote_ = note;
    pendingPlayingGeometryPitchCurrent_ = currentPitch;
    pendingPlayingGeometryPitchNew_ = newPitch;
    pendingPlayingGeometryQueuedAtMs_ = millis();
#if defined(SESSION_CAPTURE)
    logGeomApplyQueue(static_cast<uint8_t>(PendingPlayingGeometryType::Pitch), newPitch,
                      clockManager.isTransportRunning());
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::finishGeometryDriverSideEffects(
    Track& track, uint32_t now, MidiMapping::FaderType driverFader) {
    clearSelectionRelatchAfterGeometry();
    clearGeometryRelatchCycleEligibility();
    refreshEditingActivity();
    currentDriverFader = driverFader;
    lastDriverFaderTime = now;

    switch (driverFader) {
        case MidiMapping::FaderType::FADER_COARSE:
        case MidiMapping::FaderType::FADER_FINE:
            syncSelectionFromGeometryEdit(track);
            if (!clockManager.isTransportRunning()) {
                publishDependentFaderLatch(track, driverFader);
            }
            break;
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            if (!clockManager.isTransportRunning()) {
                publishDependentFaderLatch(track, driverFader);
            }
            break;
        default:
            break;
    }

    if (driverFader == MidiMapping::FaderType::FADER_COARSE &&
        !clockManager.isTransportRunning()) {
        const bool geometryDriverWasIdle = !isGeometryDriverActive(now);
        if (geometryDriverWasIdle && pendingGeometryDriverMotorSyncValid_) {
            processPendingGeometryDriverMotorSync(track, true);
        }
    }
}

NOTE_EDIT_MEM bool ControlSurfaceManager::applyPlayingPitchGeometry(Track& track,
                                                                    const NoteUtils::DisplayNote& liveNote,
                                                                    uint8_t currentPitch,
                                                                    uint8_t newPitch,
                                                                    bool refreshPlaybackPreview) {
#if defined(SESSION_CAPTURE)
    const uint32_t focusStartUs = micros();
#endif
    editManager.ensureNoteEditFocusForLiveEdit(track, liveNote);
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,%lu,GEOM_APPLY,focus,%lu,%u,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(micros() - focusStartUs),
                static_cast<unsigned>(NoteEditKind::Pitch));
    const uint32_t undoStartUs = micros();
#endif
    if (!editManager.beginGeometryMutation(track, NoteEditKind::Pitch, true)) {
#if defined(SESSION_CAPTURE)
        logger.info("#CAP,%lu,GEOM_APPLY,undo,fail,%lu,%u,0", static_cast<unsigned long>(micros()),
                    static_cast<unsigned long>(micros() - undoStartUs),
                    static_cast<unsigned>(NoteEditKind::Pitch));
#endif
        logger.log(CAT_MIDI, LOG_WARNING,
                   "Note pitch change aborted: session undo snapshot unavailable (heap reserve)");
        return false;
    }
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,%lu,GEOM_APPLY,undo,ok,%lu,%u,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(micros() - undoStartUs),
                static_cast<unsigned>(NoteEditKind::Pitch));
    const uint32_t pipelineStartUs = micros();
#endif

    NoteUtils::DisplayNote pitchTarget = liveNote;
    pitchTarget.startTick = liveNote.startTick;
    pitchTarget.endTick = liveNote.endTick;
    const bool pitchUpdated = NoteMovementUtils::applyNoteEditChange(
        track, editManager, NoteMovementUtils::NoteEditChangeKind::Pitch, pitchTarget, 0, 0, 0,
        currentPitch, newPitch, pitchTarget.startTick, pitchTarget.endTick, refreshPlaybackPreview);
#if defined(SESSION_CAPTURE)
    logger.info("#CAP,%lu,GEOM_APPLY,pipeline,%lu,%u,%u,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(micros() - pipelineStartUs), pitchUpdated ? 1u : 0u,
                static_cast<unsigned>(NoteEditKind::Pitch));
#endif
    if (!pitchUpdated) {
        return false;
    }

    NoteEditFocus& focus = editManager.getEditSession().focus;
    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        focus.baselineMap[focus.movingNoteId] = focus.last;
    }
    return true;
}

NOTE_EDIT_MEM void ControlSurfaceManager::processPendingPlayingGeometry(Track& track) {
    if (pendingPlayingGeometryType_ == PendingPlayingGeometryType::None) {
        return;
    }
    if (!clockManager.isTransportRunning()) {
#if defined(SESSION_CAPTURE)
        logGeomApplySkip(1);
#endif
        pendingPlayingGeometryType_ = PendingPlayingGeometryType::None;
        return;
    }
    const uint32_t queueAgeMs =
        pendingPlayingGeometryQueuedAtMs_ > 0 ? millis() - pendingPlayingGeometryQueuedAtMs_ : 0;
#if defined(SESSION_CAPTURE)
    logGeomApplyDequeue(queueAgeMs,
                        static_cast<uint8_t>(pendingPlayingGeometryType_));
#endif
    editManager.processKindBoundaryUndoWarm(track);
    const uint32_t now = millis();

    const PendingPlayingGeometryType kind = pendingPlayingGeometryType_;
    pendingPlayingGeometryType_ = PendingPlayingGeometryType::None;
    pendingPlayingGeometryQueuedAtMs_ = 0;

    bool applied = false;
    MidiMapping::FaderType driverFader = MidiMapping::FaderType::FADER_COARSE;
    switch (kind) {
        case PendingPlayingGeometryType::Move:
            applied = editManager.moveNoteToPosition(track, pendingPlayingGeometryNote_,
                                                     pendingPlayingGeometryTargetTick_);
            driverFader = MidiMapping::FaderType::FADER_COARSE;
            break;
        case PendingPlayingGeometryType::Length:
            applied = editManager.changeNoteEndWithOverlapHandling(
                track, pendingPlayingGeometryNote_, pendingPlayingGeometryTargetTick_);
            driverFader = MidiMapping::FaderType::FADER_COARSE;
            break;
        case PendingPlayingGeometryType::Pitch:
            applied = applyPlayingPitchGeometry(track, pendingPlayingGeometryNote_,
                                                pendingPlayingGeometryPitchCurrent_,
                                                pendingPlayingGeometryPitchNew_, true);
            driverFader = MidiMapping::FaderType::FADER_NOTE_VALUE;
            break;
        case PendingPlayingGeometryType::None:
            break;
    }
    if (applied) {
        finishGeometryDriverSideEffects(track, now, driverFader);
    }
#if defined(SESSION_CAPTURE)
    logGeomApplyDone(applied, editManager.sessionPreviewRevision());
#endif
}

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
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
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