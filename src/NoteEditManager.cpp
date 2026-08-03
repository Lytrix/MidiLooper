//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstdint>
#include <algorithm>
#include <unordered_set>
#include "Globals.h"
#include "Utils/SelectNavigation.h"
#include "NoteEditManager.h"

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

NoteEditManager noteEditManager;

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

NOTE_EDIT_MEM void applyLengthEndTargetRules(uint32_t noteStart, uint32_t currentEnd, uint32_t loopLength,
                               uint32_t minNoteDuration, uint32_t& targetEndTick) {
    if (loopLength == 0) {
        return;
    }
    noteStart %= loopLength;
    currentEnd %= loopLength;
    targetEndTick %= loopLength;

    const bool nonWrap = currentEnd > noteStart;
    if (nonWrap) {
        const uint32_t minEndTick = noteStart + minNoteDuration;
        if (targetEndTick < minEndTick) {
            targetEndTick = minEndTick;
        }
        return;
    }

    const uint32_t newNoteDuration =
        NoteMovementUtils::calculateNoteLength(noteStart, targetEndTick, loopLength);
    if (newNoteDuration < minNoteDuration) {
        targetEndTick = (noteStart + minNoteDuration) % loopLength;
    }
}

NOTE_EDIT_MEM int16_t lengthEditLoopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength) {
    if (loopLength <= 1) {
        return MidiConfig::Pitchbend::CENTER;
    }
    tick %= loopLength;
    const float normalizedPos =
        static_cast<float>(tick) / static_cast<float>(loopLength - 1);
    const int16_t pitchbend = static_cast<int16_t>(
        MidiConfig::Pitchbend::MIN +
        normalizedPos * static_cast<float>(MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
    return constrain(pitchbend, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX);
}

NOTE_EDIT_MEM uint32_t lengthEditCoarsePitchbendToLoopTick(int16_t pitchValue, uint32_t loopLength) {
    if (loopLength <= 1) {
        return 0;
    }
    const float normalizedPos =
        static_cast<float>(pitchValue - MidiConfig::Pitchbend::MIN) /
        static_cast<float>(MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN);
    const float tickFloat = normalizedPos * static_cast<float>(loopLength - 1);
    const uint32_t tick = static_cast<uint32_t>(tickFloat + 0.5f);
    return tick >= loopLength ? loopLength - 1 : tick;
}

NOTE_EDIT_MEM void clampLengthEditFineTargetTick(int32_t signedTick, uint32_t loopLength,
                                   uint32_t& outRelativeTick) {
    if (loopLength == 0) {
        outRelativeTick = 0;
        return;
    }
    if (signedTick <= 0) {
        outRelativeTick = 0;
        return;
    }
    if (static_cast<uint32_t>(signedTick) >= loopLength) {
        outRelativeTick = loopLength - 1;
        return;
    }
    outRelativeTick = static_cast<uint32_t>(signedTick);
}

NOTE_EDIT_MEM int32_t lengthEditFineOffsetFromCc(uint8_t ccValue) {
    const int32_t halfRange = static_cast<int32_t>(Config::TICKS_PER_16TH_STEP);
    const int32_t rawOffset = static_cast<int32_t>(ccValue) - 64;
    return constrain(rawOffset, -halfRange, halfRange);
}

NOTE_EDIT_MEM uint8_t lengthEditFineCcFromOffset(int32_t offsetFromAnchor) {
    const int32_t halfRange = static_cast<int32_t>(Config::TICKS_PER_16TH_STEP);
    const int32_t clampedOffset = constrain(offsetFromAnchor, -halfRange, halfRange);
    return static_cast<uint8_t>(constrain(64 + clampedOffset, 0, 127));
}

}  // namespace

NOTE_EDIT_MEM NoteEditManager::NoteEditManager() 
    : loopEditManager(midiHandler) {
}

// Delegate MIDI note handling to V2 system
NOTE_EDIT_MEM void NoteEditManager::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
    buttonHandler.handleMidiNote(channel, note, velocity, isNoteOn);
}

NOTE_EDIT_MEM void NoteEditManager::update() {
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
    processPendingSelectDependentMotorSync(trackManager.getSelectedTrack());
    processPendingGeometryDriverMotorSync(trackManager.getSelectedTrack());
    editManager.processDeferredNoteEditDisplayRefresh(trackManager.getSelectedTrack());

    loopEditManager.update();
    faderHandler.update();
    buttonHandler.update();
}

NOTE_EDIT_MEM void NoteEditManager::handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
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
        handleFaderInput(MidiMapping::FaderType::FADER_COARSE, pitchValue, 0);
        return;
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Pitchbend ignored: not on monitored channels (%d or %d)", 
               PITCHBEND_SELECT_CHANNEL, PITCHBEND_START_CHANNEL);
}

NOTE_EDIT_MEM void NoteEditManager::handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value) {
    logger.log(CAT_MIDI, LOG_DEBUG, "Received CC: ch=%d cc=%d value=%d", channel, ccNumber, value);
    
    // Check for loop length control first
    if (channel == MidiConfig::LoopEdit::LENGTH_CC_CHANNEL && ccNumber == MidiConfig::LoopEdit::LENGTH_CC_NUMBER) {
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

NOTE_EDIT_MEM bool NoteEditManager::moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote,
                                         std::uint32_t targetTick) {
    releaseEditedNoteAudition();
    const bool moved = editManager.moveNoteToPosition(track, currentNote, targetTick);
    sendEditedNoteAuditionWhenTransportStopped(track);
    return moved;
}

NOTE_EDIT_MEM bool NoteEditManager::changeNoteEndWithOverlapHandling(Track& track,
                                                       const NoteUtils::DisplayNote& currentNote,
                                                       std::uint32_t targetEndTick) {
    releaseEditedNoteAudition();
    const bool changed =
        editManager.changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
    sendEditedNoteAuditionWhenTransportStopped(track);
    return changed;
}

NOTE_EDIT_MEM void NoteEditManager::processEncoderMovement(int rawDelta) {
    if (rawDelta == 0) {
        return;
    }

    static uint32_t lastEncoderTime = 0;
    const uint32_t now = millis();
    const uint32_t interval = now - lastEncoderTime;
    lastEncoderTime = now;

    int accel = 1;
    switch (editManager.getNoteEditSessionState().kind) {
        case NoteEditKind::Move:
            if (interval < 25) {
                accel = 24;
            } else if (interval < 50) {
                accel = 8;
            } else if (interval < 100) {
                accel = 4;
            }
            break;
        case NoteEditKind::Length:
            if (interval < 25) {
                accel = 8;
            } else if (interval < 50) {
                accel = 4;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
        case NoteEditKind::Pitch:
            if (interval < 50) {
                accel = 4;
            } else if (interval < 75) {
                accel = 3;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
        default:
            if (interval < 50) {
                accel = 4;
            } else if (interval < 75) {
                accel = 3;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
    }

    const int finalDelta = rawDelta * accel;
    if (editManager.getCurrentState() != nullptr) {
        editManager.onEncoderTurn(trackManager.getSelectedTrack(), finalDelta);
    }
}

NOTE_EDIT_MEM void NoteEditManager::cycleEditMode(Track& track) {
    editManager.cycleEditSession(track);
}

NOTE_EDIT_MEM void NoteEditManager::cycleEditSession(Track& track) {
    const EditSessionType priorSession = editManager.getEditSessionType();
    editManager.cycleEditSession(track);
    if (priorSession == EditSessionType::Note) {
        releaseEditedNoteAudition();
    }
}

NOTE_EDIT_MEM void NoteEditManager::deleteSelectedNote(Track& track) {
    const std::vector<NoteUtils::DisplayNote> filteredStd = selectableDisplayNotesForEditUi(track);
    NoteUtils::DisplayNoteVec filteredNotes(filteredStd.begin(), filteredStd.end());
    if (!editManager.deleteSelectedNote(track, filteredNotes)) {
        return;
    }
    resetLengthEditingModeOnNoteSelect();
    releaseEditedNoteAudition();
}

NOTE_EDIT_MEM void NoteEditManager::scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader) {
    Track& track = trackManager.getSelectedTrack();

    switch (driverFader) {
        case MidiMapping::FaderType::FADER_COARSE:
        case MidiMapping::FaderType::FADER_FINE:
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            syncSelectionFromGeometryEdit(track);
            publishDependentFaderLatch(track, driverFader);
            break;
        case MidiMapping::FaderType::FADER_SELECT:
            sendFader1BracketFeedback(track, true);
            break;
        default:
            break;
    }
}

NOTE_EDIT_MEM void NoteEditManager::logOutboundStep(const char* label) {
#if defined(SESSION_CAPTURE)
    logger.info("#DBG outbound_step=%s", label);
#else
    (void)label;
#endif
}

NOTE_EDIT_MEM void NoteEditManager::logSelectSlot(int slotIndex, int16_t pitchValue, bool ignored,
                                    const char* reason) {
#if defined(SESSION_CAPTURE)
    if (ignored && reason != nullptr) {
        logger.info("#DBG select_slot idx=%d pitch=%d ignored=%d reason=%s", slotIndex, pitchValue,
                    1, reason);
    } else {
        logger.info("#DBG select_slot idx=%d pitch=%d ignored=%d", slotIndex, pitchValue,
                    ignored ? 1 : 0);
    }
#else
    (void)slotIndex;
    (void)pitchValue;
    (void)ignored;
    (void)reason;
#endif
}

NOTE_EDIT_MEM void NoteEditManager::logSelectApplyDecision(uint32_t targetBracketTick, int targetNoteIdx,
                                             int slotIndex, int priorSlotIndex, bool apply,
                                             const char* reason) {
#if defined(SESSION_CAPTURE)
    logger.info(
        "#DBG select_apply selected_tick=%lu note_idx=%d slot=%d prior_slot=%d apply=%d reason=%s",
        targetBracketTick, targetNoteIdx, slotIndex, priorSlotIndex, apply ? 1 : 0, reason);
#else
    (void)targetBracketTick;
    (void)targetNoteIdx;
    (void)slotIndex;
    (void)priorSlotIndex;
    (void)apply;
    (void)reason;
#endif
}

NOTE_EDIT_MEM void NoteEditManager::logSelectMotorSyncDecision(int noteIdx, int priorNoteIdx, bool sent,
                                               const char* reason, int16_t f1Pitch,
                                               int16_t priorMotorSyncF1Pitch,
                                               uint32_t sinceSyncMs, int16_t f2Pb, int f4Cc,
                                               int16_t priorF2Pb, int priorF4Cc,
                                               bool motorValueChanged) {
#if defined(SESSION_CAPTURE)
    const int f1PitchSpan = abs(f1Pitch - priorMotorSyncF1Pitch);
    logger.info(
        "#DBG select_motor_sync sent=%d note_idx=%d prior_note_idx=%d reason=%s f1_pitch_span=%d "
        "since_sync_ms=%lu f2_pb=%d f4_cc=%d prior_f2_pb=%d prior_f4_cc=%d motor_value_changed=%d",
        sent ? 1 : 0, noteIdx, priorNoteIdx, reason, f1PitchSpan, sinceSyncMs, f2Pb, f4Cc,
        priorF2Pb, priorF4Cc, motorValueChanged ? 1 : 0);
#else
    (void)noteIdx;
    (void)priorNoteIdx;
    (void)sent;
    (void)reason;
    (void)f1Pitch;
    (void)priorMotorSyncF1Pitch;
    (void)sinceSyncMs;
    (void)f2Pb;
    (void)f4Cc;
    (void)priorF2Pb;
    (void)priorF4Cc;
    (void)motorValueChanged;
#endif
}

NOTE_EDIT_MEM void NoteEditManager::resetSelectNavSlotApplyState() {
    lastSelectMotorSyncMs_ = 0;
    lastMotorSyncF1Pitch_ = MidiConfig::Pitchbend::CENTER;
    selectDependentSettleUntilMs_ = 0;
    selectDependentSettleBlockLogged_ = false;
    clearPendingSelectDependentMotorSync();
    clearPendingGeometryDriverMotorSync();
}

NOTE_EDIT_MEM void NoteEditManager::clearPendingSelectDependentMotorSync() {
    pendingSelectDriverMotorSyncValid_ = false;
    pendingSelectMotorTarget_ = {};
    pendingSelectMotorPlan_ = {};
    pendingSelectMotorPriorSelection_ = {};
}

NOTE_EDIT_MEM void NoteEditManager::clearPendingGeometryDriverMotorSync() {
    pendingGeometryDriverMotorSyncValid_ = false;
}

NOTE_EDIT_MEM void NoteEditManager::syncSelectionFromGeometryEdit(Track& track) {
    const EditorSelection priorSelection = editManager.getNoteEditSessionState().selection;
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    uint32_t selectedTick = editManager.getSelectedTick() % loopLength;
    NoteId primaryNote = kInvalidNoteId;

    const NoteEditFocus& focus = editManager.getEditSession().focus;
    const uint32_t loopStartTick = editManager.noteEditLoopStartTick(track);
    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        primaryNote = focus.movingNoteId;
        selectedTick = editManager.isLengthEditingMode()
                          ? NoteEditDisplaySnapshot::displayStartTickFromStorage(
                                focus.last.endTick, loopStartTick, loopLength)
                          : NoteEditDisplaySnapshot::displayStartTickFromStorage(
                                focus.last.startTick, loopStartTick, loopLength);
    } else {
        const int selectedIdx = editManager.getSelectedNoteIdx();
        if (selectedIdx >= 0) {
            const NoteUtils::DisplayNoteVec& notes =
                editManager.selectableDisplayNotesAtEditSelect(track);
            if (selectedIdx < static_cast<int>(notes.size())) {
                const NoteUtils::DisplayNote& selected = notes[static_cast<size_t>(selectedIdx)];
                primaryNote = selected.noteId;
                selectedTick = editManager.isLengthEditingMode()
                                  ? NoteEditDisplaySnapshot::displayStartTickFromStorage(
                                        selected.endTick, loopStartTick, loopLength)
                                  : NoteEditDisplaySnapshot::displayStartTickFromStorage(
                                        selected.startTick, loopStartTick, loopLength);
            }
        }
    }

    if (priorSelection.selectedTick == selectedTick) {
        if (editorSelectionTargetChanged(priorSelection, selectedTick, primaryNote)) {
            editManager.setSelectedTick(selectedTick);
            editManager.applySelectionFromGeometryEdit(track, selectedTick, primaryNote);
        }
        return;
    }

    editManager.setSelectedTick(selectedTick);
    editManager.applySelectionFromGeometryEdit(track, selectedTick, primaryNote);
    scheduleSelectDependentMotorSync(track, priorSelection,
                                     editManager.getNoteEditSessionState().selection, true);
}

NOTE_EDIT_MEM void NoteEditManager::scheduleSelectDependentMotorSync(Track& track,
                                                       const EditorSelection& priorSelection,
                                                       const EditorSelection& nextSelection,
                                                       bool geometryIsDriver) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)track;
        (void)priorSelection;
        (void)nextSelection;
        (void)geometryIsDriver;
        return;
    }
    if (suppressSelectDependentMotorSync_) {
        clearPendingSelectDependentMotorSync();
        clearPendingGeometryDriverMotorSync();
        return;
    }

    if (geometryIsDriver) {
        const NoteEditFaderOutbound::PlanFlags plan =
            NoteEditFaderOutbound::planForGeometryDriverMotorSync(priorSelection.selectedTick,
                                                                  nextSelection.selectedTick);
        if (!plan.fader1) {
            return;
        }
        pendingGeometryDriverMotorSyncValid_ = true;
        (void)track;
#if defined(SESSION_CAPTURE)
        logger.info("#DBG selection_motor_sync scheduled=1 driver=geometry f1=1 bracket_tick=%lu",
                    static_cast<unsigned long>(nextSelection.selectedTick));
#endif
        return;
    }

    const NoteEditFaderOutbound::PlanFlags newPlan =
        NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(
            priorSelection.primaryNote, nextSelection.primaryNote, priorSelection.selectedTick,
            nextSelection.selectedTick);
    NoteEditFaderOutbound::PlanFlags plan = newPlan;
    if (editManager.getEditSession().focus.active) {
        const NoteEditKind kind = editManager.getNoteEditSessionState().kind;
        if (isGeometryEditKind(kind) && kind != NoteEditKind::Select) {
            plan.coarse = false;
            plan.fine = false;
            plan.noteValue = false;
        }
    }
    if (!plan.coarse && !plan.fine && !plan.noteValue) {
        return;
    }

    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    const uint32_t loopStartTick = editManager.noteEditLoopStartTick(track);
    const uint32_t loopLength = editManager.noteEditLoopLengthTicks(track);
    int noteIdx = -1;
    if (editorSelectionHasNote(nextSelection)) {
        noteIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
            nextSelection, notes, loopStartTick, loopLength);
    }
    if (noteIdx < 0 && editorSelectionHasNote(nextSelection)) {
        return;
    }

    Fader1SelectTarget target;
    target.noteIdx = noteIdx;
    target.absoluteTargetTick = nextSelection.selectedTick;
    target.slotIndex = -1;
    target.valid = true;

    if (pendingSelectDriverMotorSyncValid_) {
        pendingSelectMotorPlan_.coarse |= plan.coarse;
        pendingSelectMotorPlan_.fine |= plan.fine;
        pendingSelectMotorPlan_.noteValue |= plan.noteValue;
    } else {
        pendingSelectMotorPriorSelection_ = priorSelection;
        pendingSelectMotorPlan_ = plan;
        pendingSelectDriverMotorSyncValid_ = true;
    }
    pendingSelectMotorTarget_ = target;
    (void)track;
#if defined(SESSION_CAPTURE)
    logger.info("#DBG selection_motor_sync scheduled=1 driver=select f1=0 f2=%d f4=%d note_idx=%d "
                "bracket_tick=%lu",
                plan.coarse ? 1 : 0, plan.noteValue ? 1 : 0, noteIdx,
                static_cast<unsigned long>(nextSelection.selectedTick));
#endif
}

NOTE_EDIT_MEM void NoteEditManager::processPendingSelectDependentMotorSync(Track& track) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    if (!pendingSelectDriverMotorSyncValid_ || suppressSelectDependentMotorSync_) {
        return;
    }
    if (isFaderOutboundActive()) {
        return;
    }
    const uint32_t now = millis();
    if (!NoteEditFaderMotorTiming::shouldFlushSelectDependentMotorSync(
            now, lastMotorSyncDriverInputMs_, pendingSelectDriverMotorSyncValid_)) {
        return;
    }

    const Fader1SelectTarget target = pendingSelectMotorTarget_;
    const NoteEditFaderOutbound::PlanFlags plan = pendingSelectMotorPlan_;
    const EditorSelection priorSelection = pendingSelectMotorPriorSelection_;
    clearPendingSelectDependentMotorSync();

    if (!plan.coarse && !plan.fine && !plan.noteValue) {
        return;
    }

    const uint32_t sinceSyncMs =
        lastSelectMotorSyncMs_ > 0 ? now - lastSelectMotorSyncMs_ : 0U;
    const int16_t priorMotorSyncF1Pitch = lastMotorSyncF1Pitch_;
    const int16_t priorF2Pb =
        midiFaderManager.getFaderState(MidiMapping::FaderType::FADER_COARSE).lastSentPitchbend;
    const uint8_t priorFineCc =
        midiFaderManager.getFaderState(MidiMapping::FaderType::FADER_FINE).lastSentCC;
    const int priorF4Cc = static_cast<int>(
        midiFaderManager.getFaderState(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentCC);
    const NoteEditDependentFaderSnapshot planned =
        buildDependentFaderSnapshotForTrack(track, &target);
    const bool motorValueChanged = NoteEditDependentFaderFeedback::motorValueChanged(
        planned, priorF2Pb, priorFineCc, priorF4Cc, plan.coarse, plan.fine, plan.noteValue);
    const char* reason = (!plan.coarse && plan.noteValue) ? "display_pitch_changed_same_bracket"
                                                          : "display_note_changed";

    logSelectMotorSyncDecision(target.noteIdx, -1, true, reason, lastUserSelectFaderValue,
                               priorMotorSyncF1Pitch, sinceSyncMs, planned.coarsePitchbend,
                               planned.valid ? static_cast<int>(planned.noteValueCc) : -1,
                               priorF2Pb, priorF4Cc, motorValueChanged);
    (void)priorSelection;
    syncMotorsFromSelectTarget(track, target, plan);
}

NOTE_EDIT_MEM void NoteEditManager::processPendingGeometryDriverMotorSync(Track& track) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    if (!pendingGeometryDriverMotorSyncValid_ || suppressSelectDependentMotorSync_) {
        return;
    }
    if (isFaderOutboundActive()) {
        return;
    }
    const uint32_t now = millis();
    if (!NoteEditFaderMotorTiming::shouldFlushSelectDependentMotorSync(
            now, lastMotorSyncDriverInputMs_, pendingGeometryDriverMotorSyncValid_)) {
        return;
    }

    clearPendingGeometryDriverMotorSync();
    sendFader1MotorTimedBurst(track);
}

NOTE_EDIT_MEM void NoteEditManager::armSelectDependentSettle(uint32_t sentAt) {
    selectDependentSettleUntilMs_ = sentAt + SELECT_DEPENDENT_SETTLE_MS;
    selectDependentSettleBlockLogged_ = false;
#if defined(SESSION_CAPTURE)
    logger.info("#DBG select_dependent_settle until_ms=%lu", selectDependentSettleUntilMs_);
#endif
}

NOTE_EDIT_MEM void NoteEditManager::recordFaderInputForValidation(MidiMapping::FaderType faderType,
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

NOTE_EDIT_MEM bool NoteEditManager::isFaderOutboundActive() const {
    return outboundStep_ != NoteEditFaderOutbound::Step::Idle &&
           outboundStep_ != NoteEditFaderOutbound::Step::Done;
}

NOTE_EDIT_MEM void NoteEditManager::queuePendingOutbound(NoteEditFaderOutbound::Trigger trigger,
                                           const NoteEditFaderOutbound::PlanFlags* planOverride) {
    pendingOutboundTrigger_ = trigger;
    pendingOutboundPlan_ = planOverride != nullptr ? *planOverride
                                                   : NoteEditFaderOutbound::planForTrigger(trigger);
    pendingOutboundPlanValid_ = true;
}

NOTE_EDIT_MEM void NoteEditManager::cancelActiveFaderOutbound() {
    if (outboundStep_ != NoteEditFaderOutbound::Step::Idle &&
        outboundStep_ != NoteEditFaderOutbound::Step::Done) {
        Track& track = trackManager.getSelectedTrack();
        completeOutboundPipelineAtDone(track, millis());
    }
    activeOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
    outboundStep_ = NoteEditFaderOutbound::Step::Idle;
    outboundPlan_ = {};
    outboundStepStartedMs_ = 0;
    clearPendingSelectDependentMotorSync();
    clearPendingGeometryDriverMotorSync();
    midiHandler.setDroidMotorOutboundPriority(false);
}

namespace {

NOTE_EDIT_MEM void applyFaderOutboundDisabledSideEffects(NoteEditFaderOutbound::Trigger trigger,
                                            bool& suppressSelectDependentMotorSync,
                                            bool& startEditingEnabled) {
    if (trigger == NoteEditFaderOutbound::Trigger::SessionOpen) {
        suppressSelectDependentMotorSync = false;
    }
    if (trigger == NoteEditFaderOutbound::Trigger::LengthModeEnter ||
        trigger == NoteEditFaderOutbound::Trigger::LengthModeExit) {
        startEditingEnabled = true;
    }
}

}  // namespace

NOTE_EDIT_MEM void NoteEditManager::requestFaderOutbound(NoteEditFaderOutbound::Trigger trigger,
                                             const NoteEditFaderOutbound::PlanFlags* planOverride) {
    if (trigger == NoteEditFaderOutbound::Trigger::None) {
        return;
    }
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        applyFaderOutboundDisabledSideEffects(trigger, suppressSelectDependentMotorSync_,
                                            startEditingEnabled);
        return;
    }

    if (NoteEditFaderOutbound::shouldRestartDependentPipelineOnSelectionChange(
            trigger, activeOutboundTrigger_, outboundStep_)) {
        logOutboundStep("RESTART");
        cancelActiveFaderOutbound();
    } else if (NoteEditFaderOutbound::shouldCoalesceDependentRefresh(trigger, outboundStep_)) {
        queuePendingOutbound(trigger, planOverride);
        logOutboundStep("COALESCE");
        return;
    }

    if (outboundStep_ == NoteEditFaderOutbound::Step::Done) {
        Track& track = trackManager.getSelectedTrack();
        completeOutboundPipelineAtDone(track, millis());
        outboundStep_ = NoteEditFaderOutbound::Step::Idle;
    }

    if (isFaderOutboundActive()) {
        if (NoteEditFaderOutbound::shouldPreemptActivePipeline(trigger)) {
            clearPendingSelectDependentMotorSync();
            clearPendingGeometryDriverMotorSync();
            cancelActiveFaderOutbound();
        } else if (trigger == NoteEditFaderOutbound::Trigger::Fader1BracketOnly) {
            Track& track = trackManager.getSelectedTrack();
            sendFader1BracketFeedback(track, false);
            return;
        } else {
            queuePendingOutbound(trigger, planOverride);
            return;
        }
    }

    activeOutboundTrigger_ = trigger;
    pendingOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
    pendingOutboundPlanValid_ = false;
    outboundPlan_ = planOverride != nullptr ? *planOverride
                                            : NoteEditFaderOutbound::planForTrigger(trigger);
    if (trigger == NoteEditFaderOutbound::Trigger::SessionOpen ||
        trigger == NoteEditFaderOutbound::Trigger::NoteSelectWithFader1) {
        startEditingEnabled = false;
    }
    outboundStep_ = NoteEditFaderOutbound::nextEnabledStep(NoteEditFaderOutbound::Step::Idle,
                                                             outboundPlan_);
    outboundStepStartedMs_ = millis();
    midiHandler.setDroidMotorOutboundPriority(true);
    logOutboundStep("BEGIN");
}

NOTE_EDIT_MEM bool NoteEditManager::isGeometryDriverActive(uint32_t now) const {
    if (currentDriverFader == MidiMapping::FaderType::FADER_SELECT) {
        return false;
    }
    if (lastDriverFaderTime == 0) {
        return false;
    }
    return (now - lastDriverFaderTime) < COARSE_STABILITY_TIME;
}

NOTE_EDIT_MEM void NoteEditManager::armSelectFaderFeedbackIgnore(uint32_t sentAt, uint32_t durationMs) {
    const uint32_t until = sentAt + durationMs;
    if (until > selectFaderFeedbackIgnoreUntilMs_) {
        selectFaderFeedbackIgnoreUntilMs_ = until;
    }
}

NOTE_EDIT_MEM void NoteEditManager::sendFader1BracketFeedback(Track& track, bool updateNavStateFromOutbound) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return;
    }
    const bool pipelineActive = isFaderOutboundActive();
    if (!pipelineActive) {
        midiHandler.setDroidMotorOutboundPriority(true);
    }
    EditSelectNoteState::sendTargetPitchbend(editManager, track);
    const uint32_t sentAt = millis();
    lastSelectnoteSentTime = sentAt;
    auto& selectState = midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT);
    selectState.lastSentTime = sentAt;
    outboundSentFader1Pitchbend_ = selectState.lastSentPitchbend;
    if (updateNavStateFromOutbound) {
        lastUserSelectFaderValue = selectState.lastSentPitchbend;
        lastSelectFaderTime = sentAt;
    }
    armSelectFaderFeedbackIgnore(sentAt, FEEDBACK_IGNORE_PERIOD);
    logOutboundStep("SEND_F1");
    if (!pipelineActive) {
        midiHandler.setDroidMotorOutboundPriority(false);
    }
}

NOTE_EDIT_MEM bool NoteEditManager::sendFader1MotorTimedBurst(Track& track) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return false;
    }
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return false;
    }

    int16_t targetPitchbend = 0;
    if (!EditSelectNoteState::resolveTargetPitchbend(editManager, track, targetPitchbend)) {
        return false;
    }

    midiHandler.setDroidMotorOutboundPriority(true);
    NoteEditFaderMotorTiming::runMotorFaderBurst(
        [&]() { midiHandler.sendPitchBend(PITCHBEND_SELECT_CHANNEL, targetPitchbend); },
        [&]() { midiHandler.sendNoteOn(PITCHBEND_SELECT_CHANNEL, 0, 127); },
        [&]() { midiHandler.sendNoteOff(PITCHBEND_SELECT_CHANNEL, 0, 0); });

    const uint32_t sentAt = millis();
    lastSelectnoteSentTime = sentAt;
    auto& selectState = midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT);
    selectState.lastSentPitchbend = targetPitchbend;
    selectState.lastSentTime = sentAt;
    armSelectFaderFeedbackIgnore(sentAt, FEEDBACK_IGNORE_PERIOD);
    logOutboundStep("SEND_F1");
#if defined(SESSION_CAPTURE)
    logger.info("#DBG geometry_motor_sync sent=1 bracket_tick=%lu pb=%d mode=GEOMETRY_SYNC sequence=timed_burst",
                static_cast<unsigned long>(editManager.getSelectedTick()), targetPitchbend);
#endif
    midiHandler.setDroidMotorOutboundPriority(false);
    (void)track;
    return true;
}

NOTE_EDIT_MEM void NoteEditManager::completeOutboundPipelineAtDone(Track& track, uint32_t now) {
    (void)now;
    const NoteEditFaderOutbound::Trigger completedTrigger = activeOutboundTrigger_;
    if (completedTrigger == NoteEditFaderOutbound::Trigger::SessionOpen) {
        suppressSelectDependentMotorSync_ = false;
    }
    if (completedTrigger == NoteEditFaderOutbound::Trigger::LengthModeEnter ||
        completedTrigger == NoteEditFaderOutbound::Trigger::LengthModeExit) {
        startEditingEnabled = true;
    }
    logOutboundStep("DONE");
    activeOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
    midiHandler.setDroidMotorOutboundPriority(false);
    if (completedTrigger == NoteEditFaderOutbound::Trigger::NoteSelectWithFader1 ||
        completedTrigger == NoteEditFaderOutbound::Trigger::SessionOpen) {
        (void)track;
    }
}

NOTE_EDIT_MEM void NoteEditManager::processFaderOutbound() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    if (outboundStep_ == NoteEditFaderOutbound::Step::Done) {
        Track& track = trackManager.getSelectedTrack();
        completeOutboundPipelineAtDone(track, millis());
        outboundStep_ = NoteEditFaderOutbound::Step::Idle;
    }

    if (outboundStep_ == NoteEditFaderOutbound::Step::Idle) {
        midiHandler.setDroidMotorOutboundPriority(false);
        if (pendingOutboundTrigger_ != NoteEditFaderOutbound::Trigger::None) {
            const NoteEditFaderOutbound::Trigger pending = pendingOutboundTrigger_;
            pendingOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
            const NoteEditFaderOutbound::PlanFlags* planPtr =
                pendingOutboundPlanValid_ ? &pendingOutboundPlan_ : nullptr;
            pendingOutboundPlanValid_ = false;
            requestFaderOutbound(pending, planPtr);
        }
        return;
    }

    midiHandler.setDroidMotorOutboundPriority(true);

    const uint32_t now = millis();
    if (outboundStepStartedMs_ > 0 &&
        (now - outboundStepStartedMs_) > NoteEditFaderOutbound::kOutboundWatchdogMs) {
        logger.info("NOTE_EDIT outbound watchdog — forcing Done");
        outboundStep_ = NoteEditFaderOutbound::Step::Done;
        return;
    }

    Track& track = trackManager.getSelectedTrack();

    switch (outboundStep_) {
        case NoteEditFaderOutbound::Step::WaitFader1Echo: {
            const int16_t echoDiff = abs(lastUserSelectFaderValue - outboundSentFader1Pitchbend_);
            const bool echoMatched = echoDiff <= SELECT_MOVEMENT_THRESHOLD;
            const bool ignoreExpired =
                selectFaderFeedbackIgnoreUntilMs_ != 0 && now >= selectFaderFeedbackIgnoreUntilMs_;
            if (echoMatched || ignoreExpired) {
                outboundStep_ =
                    NoteEditFaderOutbound::advanceOutboundStep(outboundStep_, outboundPlan_);
                outboundStepStartedMs_ = now;
            }
            return;
        }
        case NoteEditFaderOutbound::Step::SendFader1Bracket:
            sendFader1BracketFeedback(track);
            outboundStep_ = NoteEditFaderOutbound::advanceOutboundStep(outboundStep_, outboundPlan_);
            outboundStepStartedMs_ = now;
            return;
        case NoteEditFaderOutbound::Step::SendCoarse: {
            const bool sent = sendDependentFadersParallelTimedBurst(track, outboundPlan_, nullptr);
            if (sent) {
                const uint32_t sentAt = millis();
                if (outboundPlan_.coarse) {
                    armCoarseFaderFeedbackIgnore(sentAt);
                }
                if (outboundPlan_.fine || outboundPlan_.noteValue) {
                    armChannel15CcFaderFeedbackIgnore(sentAt);
                }
                if (outboundPlan_.noteValue) {
                    armChannel15FaderFeedbackIgnore(sentAt);
                }
                if (activeOutboundTrigger_ != NoteEditFaderOutbound::Trigger::LengthModeEnter &&
                    activeOutboundTrigger_ != NoteEditFaderOutbound::Trigger::LengthModeExit) {
                    armSelectDependentSettle(sentAt);
                }
                logOutboundStep("SEND_F2_F3_F4");
            } else {
                logOutboundStep("SKIP_SEND");
            }
            outboundStep_ = NoteEditFaderOutbound::Step::Done;
            outboundStepStartedMs_ = now;
            return;
        }
        case NoteEditFaderOutbound::Step::SendFine:
            logOutboundStep("SKIP_SEND");
            outboundStep_ = NoteEditFaderOutbound::Step::Done;
            outboundStepStartedMs_ = now;
            return;
        case NoteEditFaderOutbound::Step::SendNoteValue:
            logOutboundStep("SKIP_SEND");
            outboundStep_ = NoteEditFaderOutbound::Step::Done;
            outboundStepStartedMs_ = now;
            return;
        default:
            logger.info("NOTE_EDIT outbound unexpected step — forcing Done");
            outboundStep_ = NoteEditFaderOutbound::Step::Done;
            return;
    }
}

NOTE_EDIT_MEM int NoteEditManager::selectNavSlotIndexForPitchbend(Track& track, int16_t pitchValue) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return -1;
    }
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, editManager.getSelectedTick(), true);
    if (slots.empty()) {
        return -1;
    }
    return map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0,
               static_cast<int>(slots.size()) - 1);
}


NOTE_EDIT_MEM void NoteEditManager::enableStartEditing() {
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

NOTE_EDIT_MEM void NoteEditManager::prepareNoteEditSessionOpen() {
    suppressSelectDependentMotorSync_ = true;
    clearPendingSelectDependentMotorSync();
    clearPendingGeometryDriverMotorSync();
    startEditingEnabled = false;
}

NOTE_EDIT_MEM void NoteEditManager::syncReferenceStepFromSelectedTick(uint32_t selectedTick) {
    editManager.syncReferenceStepFromSelectedTick(selectedTick);
}

NOTE_EDIT_MEM void NoteEditManager::scheduleNoteSelectFaderSync(Track& track) {
    noteSelectionTime = millis();
    startEditingEnabled = false;
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::NoteSelectWithFader1);
    (void)track;
}

NOTE_EDIT_MEM void NoteEditManager::sendNoteEditSessionFaderFeedback(Track& track) {
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

NOTE_EDIT_MEM void NoteEditManager::armChannel15FaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = sentAt;
    lastPitchbendSentTime = sentAt;
}

NOTE_EDIT_MEM void NoteEditManager::armCoarseFaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = sentAt;
    lastPitchbendSentTime = sentAt;
}

NOTE_EDIT_MEM void NoteEditManager::armChannel15CcFaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = sentAt;
}

NOTE_EDIT_MEM void NoteEditManager::refreshEditingActivity() {
    lastEditingActivityTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG, "Editing activity refreshed - note selection disabled for %dms", NOTE_SELECTION_GRACE_PERIOD);
}

// Unified Fader State Machine Implementation - now delegated to MidiFaderProcessor

// MidiFaderProcessor::FaderState& NoteEditManager::getFaderState(MidiMapping::FaderType faderType) {
//     for (auto& state : faderStates) {
//         if (state.type == faderType) {
//             return state;
//         }
//     }
//     // Should never happen, but return first as fallback
//     return faderStates[0];
// }

NoteEditDependentFaderBuildInput NoteEditManager::makeDependentFaderBuildInput(
    const Track& track, const Fader1SelectTarget* selectTarget) const {
    NoteEditDependentFaderBuildInput input;
    input.loopLength = track.getLoopLength();
    if (input.loopLength == 0) {
        return input;
    }
    input.loopStartTick = editManager.noteEditLoopStartTick(track);
    input.selectedIdx = editManager.getSelectedNoteIdx();
    input.selectedTick = editManager.getSelectedTick();
    input.lengthEditingMode = editManager.isLengthEditingMode();
    input.lengthFineAnchorEndTick = editManager.lengthFineAnchorEndTick();
    input.referenceStep = editManager.getReferenceStep();

    if (selectTarget != nullptr && selectTarget->valid) {
        input.selectTarget.active = true;
        input.selectTarget.absoluteTargetTick = selectTarget->absoluteTargetTick;
        input.selectTarget.noteIdx = selectTarget->noteIdx;
        if (selectTarget->noteIdx >= 0) {
            const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
            if (selectTarget->noteIdx < static_cast<int>(notes.size())) {
                const NoteUtils::DisplayNote& note =
                    notes[static_cast<size_t>(selectTarget->noteIdx)];
                input.selectNoteStartTick = note.startTick;
                if (editManager.isNoteEditActive() && editManager.getEditSession().focus.active) {
                    input.selectNotePitch =
                        editManager.liveEditDisplayNoteAtSelect(track).note;
                } else {
                    input.selectNotePitch = note.note;
                }
                input.hasSelectNote = true;
            }
        }
        return input;
    }

    if (input.selectedIdx >= 0) {
        const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
        input.hasLiveNote = true;
        input.liveStartTick = liveNote.startTick;
        input.liveEndTick = liveNote.endTick;
        input.livePitch = liveNote.note;
    }
    return input;
}

NoteEditDependentFaderSnapshot NoteEditManager::buildDependentFaderSnapshotForTrack(
    const Track& track, const Fader1SelectTarget* selectTarget) const {
    return buildDependentFaderSnapshot(makeDependentFaderBuildInput(track, selectTarget));
}

NOTE_EDIT_MEM bool NoteEditManager::sendDependentFaderSnapshot(
    Track& track, const NoteEditFaderOutbound::PlanFlags& plan,
    const NoteEditDependentFaderSnapshot& snapshot, DependentFaderSendMode mode) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)track;
        (void)plan;
        (void)snapshot;
        (void)mode;
        return false;
    }
    (void)track;
    const uint32_t now = millis();
    bool sentAny = false;

    if (plan.coarse && snapshot.coarseValid) {
        midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, snapshot.coarsePitchbend);
        auto& coarseState =
            midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE);
        coarseState.lastSentPitchbend = snapshot.coarsePitchbend;
        coarseState.lastSentTime = now;
        sentAny = true;
    }
    if (plan.fine && snapshot.fineValid) {
        midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, snapshot.fineCc);
        auto& fineState = midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE);
        fineState.lastSentCC = snapshot.fineCc;
        fineState.lastSentTime = now;
        sentAny = true;
    }
    if (plan.noteValue && snapshot.valid) {
        midiHandler.sendControlChange(NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER,
                                      snapshot.noteValueCc);
        auto& noteValueState =
            midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE);
        noteValueState.lastSentCC = snapshot.noteValueCc;
        noteValueState.lastSentTime = now;
        sentAny = true;
    }

    if (!sentAny) {
        return false;
    }

    if (mode == DependentFaderSendMode::ValueOnly) {
        return true;
    }

    struct DependentMotorBurstSlot {
        NoteEditManager* owner = nullptr;
        bool enabled = false;
        int16_t coarsePitchbend = 0;
        uint8_t fineCcValue = 0;
        uint8_t noteValueCc = 0;
        bool isCoarse = false;
        bool isFine = false;
        bool isNoteValue = false;
        void sendPosition() {
            if (!enabled) {
                return;
            }
            if (isCoarse) {
                midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, coarsePitchbend);
            } else if (isFine) {
                midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, fineCcValue);
            } else if (isNoteValue) {
                midiHandler.sendControlChange(NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER,
                                              noteValueCc);
            }
        }
        void sendNoteOn() {
            if (!enabled || owner == nullptr) {
                return;
            }
            if (isCoarse) {
                owner->sendCoarseFaderMotorNoteOn();
            } else if (isFine) {
                owner->sendFineFaderMotorNoteOn();
            } else if (isNoteValue) {
                owner->sendNoteValueFaderMotorNoteOn();
            }
        }
        void sendNoteOff() {
            if (!enabled || owner == nullptr) {
                return;
            }
            if (isCoarse) {
                owner->sendCoarseFaderMotorNoteOff();
            } else if (isFine) {
                owner->sendFineFaderMotorNoteOff();
            } else if (isNoteValue) {
                owner->sendNoteValueFaderMotorNoteOff();
            }
        }
    };

    DependentMotorBurstSlot coarseSlot;
    coarseSlot.owner = this;
    coarseSlot.enabled = plan.coarse && snapshot.coarseValid;
    coarseSlot.isCoarse = true;
    coarseSlot.coarsePitchbend = snapshot.coarsePitchbend;

    DependentMotorBurstSlot fineSlot;
    fineSlot.owner = this;
    fineSlot.enabled = plan.fine && snapshot.fineValid;
    fineSlot.isFine = true;
    fineSlot.fineCcValue = snapshot.fineCc;

    DependentMotorBurstSlot noteValueSlot;
    noteValueSlot.owner = this;
    noteValueSlot.enabled = plan.noteValue && snapshot.valid;
    noteValueSlot.isNoteValue = true;
    noteValueSlot.noteValueCc = snapshot.noteValueCc;

    NoteEditFaderMotorTiming::runParallelMotorFaderBursts(coarseSlot, fineSlot, noteValueSlot);
    return true;
}

NOTE_EDIT_MEM void NoteEditManager::publishDependentFaderLatch(Track& track,
                                                 MidiMapping::FaderType driverFader) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)track;
        (void)driverFader;
        return;
    }
    const uint32_t loopLength = track.getLoopLength();
    if (editManager.isNoteEditActive() && loopLength > 0) {
        const NoteEditFocus& focus = editManager.getEditSession().focus;
        if (focus.active) {
            MidiEventVec& store = editManager.sessionMidiEvents();
            std::unordered_set<NoteId> closure =
                buildEditClosureNoteIds(focus, store, track.getMidiChannel(), loopLength);
            if (!closure.empty()) {
                LoopTickNormalize::NormalizeOptions microOptions;
                microOptions.closeOpenTails = false;
                const LoopTickNormalize::NormalizeResult normResult = LoopTickNormalize::normalize(
                    store, loopLength,
                    LoopTickNormalize::NormalizeScope::noteIds(std::move(closure)), microOptions);
                if (normResult.wrapPairsMerged > 0 || normResult.synthOffsPromoted > 0 ||
                    normResult.openTailsClosed > 0) {
                    editManager.bumpSessionPreviewRevision();
                }
                const MidiEventVec closureEvents = LoopEventValidation::extractEventsForNoteIds(
                    store, closure);
                const LoopEventValidation::LoopEventValidationResult microInvariantResult =
                    LoopEventValidation::validateLoopEvents(
                        closureEvents, loopLength,
                        LoopEventValidation::kClosureLinearGeometryMask);
                if (!microInvariantResult.passed) {
                    logger.log(CAT_TRACK, LOG_WARNING,
                               "NOTE_EDIT micro normalize: closure geometry failed (check=%u)",
                               static_cast<unsigned>(microInvariantResult.firstFailure));
                }
            }
        }
    }
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    switch (driverFader) {
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            plan.noteValue = snapshot.valid;
            break;
        case MidiMapping::FaderType::FADER_FINE:
            plan.fine = snapshot.fineValid;
            plan.noteValue = snapshot.valid;
            break;
        case MidiMapping::FaderType::FADER_COARSE:
        default:
            plan.coarse = snapshot.coarseValid;
            plan.fine = snapshot.fineValid;
            plan.noteValue = snapshot.valid;
            break;
    }
    if (sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly)) {
        const uint32_t sentAt = millis();
        if (plan.coarse) {
            armCoarseFaderFeedbackIgnore(sentAt);
        }
        if (plan.fine || plan.noteValue) {
            armChannel15CcFaderFeedbackIgnore(sentAt);
        }
    }
}

NOTE_EDIT_MEM void NoteEditManager::releaseEditedNoteAudition() {
    if constexpr (!kEditedNoteAuditionEnabled) {
        return;
    }
    if (!editedNoteAuditionHeld_) {
        return;
    }
    midiHandler.sendNoteOff(editedNoteAuditionChannel_, editedNoteAuditionPitch_, 0);
    editedNoteAuditionHeld_ = false;
}

NOTE_EDIT_MEM void NoteEditManager::sendEditedNoteAuditionWhenTransportStopped(Track& track) {
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

    uint8_t pitch = focus.last.pitch;
    uint8_t velocity = 100;
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength > 0) {
        MidiEventVec& store = editManager.sessionMidiEvents();
        NoteBaseline baseline{};
        const uint8_t storeChannel = track.getMidiChannel();
        if (findLinearNoteSpanForNoteId(store, focus.movingNoteId, storeChannel, baseline,
                                        focus.last.startTick, loopLength) ||
            findLinearNoteSpanForNoteId(store, focus.movingNoteId, storeChannel, baseline,
                                        UINT32_MAX, loopLength)) {
            pitch = baseline.pitch;
            if (baseline.velocity > 0) {
                velocity = baseline.velocity;
            }
        }
    }

    const uint8_t outChannel = track.getMidiChannel();
    if (editedNoteAuditionHeld_ && editedNoteAuditionChannel_ == outChannel &&
        editedNoteAuditionPitch_ == pitch) {
        return;
    }
    if (editedNoteAuditionHeld_) {
        midiHandler.sendNoteOff(editedNoteAuditionChannel_, editedNoteAuditionPitch_, 0);
    }
    midiHandler.sendNoteOn(outChannel, pitch, velocity);
    editedNoteAuditionChannel_ = outChannel;
    editedNoteAuditionPitch_ = pitch;
    editedNoteAuditionHeld_ = true;
}

NOTE_EDIT_MEM bool NoteEditManager::shouldIgnoreDependentFaderInput(MidiMapping::FaderType faderType,
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

NOTE_EDIT_MEM bool NoteEditManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType) {
    return shouldIgnoreFaderInput(faderType, -1, -1); // Use overloaded version with unknown values
}

NOTE_EDIT_MEM bool NoteEditManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
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

NOTE_EDIT_MEM int16_t NoteEditManager::loopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength) {
    if (loopLength <= 1) {
        return MidiConfig::Pitchbend::CENTER;
    }
    tick %= loopLength;
    const uint32_t ticksPerStep = Config::TICKS_PER_16TH_STEP;
    const uint32_t numSteps = loopLength / ticksPerStep;
    if (numSteps <= 1) {
        return MidiConfig::Pitchbend::CENTER;
    }
    const uint32_t step = tick / ticksPerStep;
    const uint32_t offsetInStep = tick % ticksPerStep;
    const float stepFraction =
        static_cast<float>(step) +
        static_cast<float>(offsetInStep) / static_cast<float>(ticksPerStep);
    const float normalizedPos = stepFraction / static_cast<float>(numSteps - 1);
    int16_t pitchbend = static_cast<int16_t>(
        MidiConfig::Pitchbend::MIN +
        normalizedPos * static_cast<float>(MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
    return constrain(pitchbend, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX);
}

NOTE_EDIT_MEM bool NoteEditManager::sendCoarseFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.coarse = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

NOTE_EDIT_MEM bool NoteEditManager::sendFineFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.fine = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

NOTE_EDIT_MEM bool NoteEditManager::sendNoteValueFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.noteValue = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

NOTE_EDIT_MEM void NoteEditManager::sendCoarseFaderMotorNoteOn() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOn(PITCHBEND_START_CHANNEL, MidiConfig::Fader::MOTOR_TRIGGER_NOTE, 127);
}

NOTE_EDIT_MEM void NoteEditManager::sendCoarseFaderMotorNoteOff() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOff(PITCHBEND_START_CHANNEL, MidiConfig::Fader::MOTOR_TRIGGER_NOTE, 0);
}

NOTE_EDIT_MEM void NoteEditManager::sendFineFaderMotorNoteOn() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOn(FINE_CC_CHANNEL, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE, 127);
}

NOTE_EDIT_MEM void NoteEditManager::sendFineFaderMotorNoteOff() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOff(FINE_CC_CHANNEL, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE, 0);
}

NOTE_EDIT_MEM void NoteEditManager::sendNoteValueFaderMotorNoteOn() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOn(NOTE_VALUE_CC_CHANNEL, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE,
                           127);
}

NOTE_EDIT_MEM void NoteEditManager::sendNoteValueFaderMotorNoteOff() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOff(NOTE_VALUE_CC_CHANNEL, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE,
                            0);
}

NOTE_EDIT_MEM NoteEditManager::Fader1SelectTarget NoteEditManager::resolveFader1SelectTarget(Track& track,
                                                                                int16_t pitchValue) {
    Fader1SelectTarget target;
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return target;
    }
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, editManager.getSelectedTick(), true);
    if (slots.empty()) {
        return target;
    }
    const int posIndex = map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0,
                             static_cast<int>(slots.size()) - 1);
    const SelectNavigation::SelectNavSlot& slot = slots[static_cast<size_t>(posIndex)];
    target.slotIndex = posIndex;
    target.absoluteTargetTick = slot.relativeTick;
    target.noteIdx = SelectNavigation::resolveNoteIdxAtSlot(slot);
    target.valid = true;
    return target;
}

std::vector<NoteUtils::DisplayNote> NoteEditManager::selectableDisplayNotesForEditUi(
    const Track& track) const {
    return editManager.selectableDisplayNotesForEditUi(track);
}

std::vector<SelectNavigation::SelectNavSlot> NoteEditManager::buildSelectNavigationSlots(
    const Track& track, uint32_t selectedTick, bool includeSelectedTickIfMissing) const {
    return editManager.buildSelectNavigationSlots(track, selectedTick, includeSelectedTickIfMissing);
}

NOTE_EDIT_MEM void NoteEditManager::syncMotorsFromSelectTarget(
    Track& track, const Fader1SelectTarget& target,
    const NoteEditFaderOutbound::PlanFlags& plan) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)track;
        (void)target;
        (void)plan;
        return;
    }
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0 || !target.valid) {
        return;
    }
    if (!plan.coarse && !plan.fine && !plan.noteValue) {
        return;
    }

    midiHandler.setDroidMotorOutboundPriority(true);
#if defined(SESSION_CAPTURE)
    logger.info("#DBG select_motor_sync mode=SELECT_SYNC sequence=parallel_timed_burst");
#endif

    if (sendDependentFadersParallelTimedBurst(track, plan, &target)) {
        const uint32_t sentAt = millis();
        lastSelectMotorSyncMs_ = sentAt;
        lastMotorSyncF1Pitch_ = lastUserSelectFaderValue;
        if (plan.coarse) {
            armCoarseFaderFeedbackIgnore(sentAt);
        }
        if (plan.fine || plan.noteValue) {
            armChannel15CcFaderFeedbackIgnore(sentAt);
        }
        armSelectDependentSettle(sentAt);
    }

    midiHandler.setDroidMotorOutboundPriority(false);
}

NOTE_EDIT_MEM bool NoteEditManager::sendDependentFadersParallelTimedBurst(
    Track& track, const NoteEditFaderOutbound::PlanFlags& plan,
    const Fader1SelectTarget* selectTarget) {
    const NoteEditDependentFaderSnapshot snapshot =
        buildDependentFaderSnapshotForTrack(track, selectTarget);
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueAndMotor);
}

NOTE_EDIT_MEM void NoteEditManager::handleSelectFaderInput(int16_t pitchValue, Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }

    const uint32_t now = millis();
    if constexpr (kNoteEditFaderFeedbackEnabled) {
        if (selectDependentSettleUntilMs_ != 0 && now < selectDependentSettleUntilMs_) {
            logSelectSlot(-1, pitchValue, true, "settle");
            return;
        }
    }

    const int slotIndex = selectNavSlotIndexForPitchbend(track, pitchValue);

    clearPendingGeometryDriverMotorSync();
    lastUserSelectFaderValue = pitchValue;
    lastSelectFaderTime = now;
    lastMotorSyncDriverInputMs_ = now;

    logSelectSlot(slotIndex, pitchValue, false);

    const Fader1SelectTarget target = resolveFader1SelectTarget(track, pitchValue);
    if (!target.valid) {
        return;
    }

    const EditorSelection& priorSelection = editManager.getNoteEditSessionState().selection;
    NoteId nextPrimaryNote = kInvalidNoteId;
    if (target.noteIdx >= 0) {
        const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
        if (target.noteIdx < static_cast<int>(notes.size())) {
            nextPrimaryNote = noteIdFromFilteredDisplayNote(notes, target.noteIdx);
        }
    }

    const bool selectionChanged = NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        priorSelection.primaryNote, nextPrimaryNote, priorSelection.selectedTick,
        target.absoluteTargetTick);

    const NoteEditFocus& focus = editManager.getEditSession().focus;
    const NoteEditKind kind = editManager.getNoteEditSessionState().kind;
    const bool geometryDriverActive =
        currentDriverFader != MidiMapping::FaderType::FADER_SELECT &&
        lastDriverFaderTime != 0 &&
        (now - lastDriverFaderTime) < DRIVER_FADER_ACTIVE_MS;
    if (geometryDriverActive && focus.active && focus.movingNoteId != kInvalidNoteId &&
        isGeometryEditKind(kind) && kind != NoteEditKind::Select) {
        logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                               false, "geometry_driver_ignored");
        return;
    }

    if (target.noteIdx < 0) {
        if (selectionChanged) {
            logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                                   true, "empty_step");
            applyNoteSelectFromFader1Pitchbend(track, pitchValue, target.slotIndex);
        } else {
            logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                                   false, "empty_step");
        }
        return;
    }

    if (selectionChanged) {
        logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                               true, "note_changed");
        applyNoteSelectFromFader1Pitchbend(track, pitchValue, target.slotIndex);
    } else {
        logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                               false, "unchanged_note");
    }
}

NOTE_EDIT_MEM bool NoteEditManager::applyNoteSelectFromFader1Pitchbend(Track& track, int16_t pitchValue,
                                                         int posIndex) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, editManager.getSelectedTick(), true);

    if (slots.empty() || posIndex < 0 || posIndex >= static_cast<int>(slots.size())) {
        return false;
    }

    const SelectNavigation::SelectNavSlot& slot = slots[static_cast<size_t>(posIndex)];
    const uint32_t absoluteTargetTick = slot.relativeTick;
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    const int noteIdx = SelectNavigation::resolveNoteIdxAtSlot(slot);

    (void)pitchValue;

    if (noteIdx >= 0) {
        int notesAtPosition = 0;
        int notePosition = 0;
        for (const SelectNavigation::SelectNavSlot& s : slots) {
            if (s.relativeTick == slot.relativeTick && s.noteIdx >= 0) {
                notesAtPosition++;
                if (s.noteIdx == noteIdx) {
                    notePosition = notesAtPosition;
                }
            }
        }

        if (notesAtPosition > 1) {
            logger.log(CAT_MIDI, LOG_INFO,
                       "Select fader: selected note %d at tick %lu (%d/%d notes at this position)",
                       noteIdx, absoluteTargetTick, notePosition, notesAtPosition);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected note %d at tick %lu", noteIdx,
                       absoluteTargetTick);
        }

        editManager.commitAllPendingNoteEditActions(track);
        editManager.rebuildNoteEditFocusForDisplayNote(track, notes[static_cast<size_t>(noteIdx)]);
        const NoteId selectNoteId = noteIdFromFilteredDisplayNote(notes, noteIdx);
        editManager.applySelectNav(track, absoluteTargetTick, selectNoteId, false, false);
        resetLengthEditingModeOnNoteSelect();
        lastUserNoteValueCc = notes[static_cast<size_t>(noteIdx)].note;
        lastNoteValueFaderTime = 0;
        editManager.setReferenceStep(absoluteTargetTick / Config::TICKS_PER_16TH_STEP);
        noteSelectionTime = millis();
        currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    } else {
        editManager.commitAllPendingNoteEditActions(track);
        editManager.rebuildNoteEditFocusAtSelect(track, -1);
        editManager.applySelectNav(track, absoluteTargetTick, kInvalidNoteId, false, false);
        editManager.setReferenceStep(absoluteTargetTick / Config::TICKS_PER_16TH_STEP);
        startEditingEnabled = true;
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected empty step at tick %lu (no note)",
                   absoluteTargetTick);
        releaseEditedNoteAudition();
    }
    if constexpr (kEditedNoteAuditionEnabled) {
        sendEditedNoteAuditionWhenTransportStopped(track);
    }
    return true;
}

NOTE_EDIT_MEM void NoteEditManager::handleCoarseFaderInput(int16_t pitchValue, Track& track) {
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
    
    // Movement filtering - prevent jitter from rescheduling updates
    uint32_t now = millis();
    if constexpr (kNoteEditFaderFeedbackEnabled) {
        if (!editManager.isLengthEditingMode() &&
            (now - noteSelectionTime) < static_cast<uint32_t>(FEEDBACK_IGNORE_PERIOD)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Coarse fader: ignoring input during post-select routing settle");
            return;
        }
    }
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
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
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
            geometryApplied =
                changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
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
            geometryApplied = moveNoteToPosition(track, currentNote, targetTick);
        }

        if (geometryApplied) {
            refreshEditingActivity();
            this->currentDriverFader = MidiMapping::FaderType::FADER_COARSE;
            this->lastDriverFaderTime = millis();
            scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_COARSE);
        }
        // NOTE: Fader 2 (COARSE) now uses 500ms grace period to update fader 1
        // This prevents erratic movement and allows proper settling time
    }
}

NOTE_EDIT_MEM void NoteEditManager::handleFineFaderInput(uint8_t ccValue, Track& track) {
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
        geometryApplied =
            changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
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
        geometryApplied = moveNoteToPosition(track, currentNote, targetStartTick);
    }
        
    logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader: CC=%d - %s mode", 
               ccValue, editManager.isLengthEditingMode() ? "LENGTH EDIT" : "POSITION EDIT");
        
    if (geometryApplied) {
        lastUserFineCc = ccValue;
        lastFineFaderTime = now;
        refreshEditingActivity();
        this->currentDriverFader = MidiMapping::FaderType::FADER_FINE;
        this->lastDriverFaderTime = millis();
        lastMotorSyncDriverInputMs_ = millis();
        clearPendingSelectDependentMotorSync();
        releaseEditedNoteAudition();
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_FINE);
    }
}

NOTE_EDIT_MEM void NoteEditManager::handleNoteValueFaderInput(uint8_t ccValue, Track& track) {
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

    editManager.ensureNoteEditFocusForLiveEdit(track, liveNote);
    if (!editManager.beginGeometryMutation(track, NoteEditKind::Pitch, true)) {
        logger.log(CAT_MIDI, LOG_WARNING,
                   "Note pitch change aborted: session undo snapshot unavailable (heap reserve)");
        return;
    }

    NoteUtils::DisplayNote pitchTarget = liveNote;
    pitchTarget.startTick = noteStart;
    pitchTarget.endTick = noteEnd;
    const bool pitchUpdated = NoteMovementUtils::applyNoteEditChange(
        track, editManager, NoteMovementUtils::NoteEditChangeKind::Pitch, pitchTarget, 0, 0, 0,
        currentNoteValue, newNoteValue, noteStart, noteEnd, false);
    if (!pitchUpdated) {
        return;
    }

    NoteEditFocus& focus = editManager.getEditSession().focus;
    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        focus.baselineMap[focus.movingNoteId] = focus.last;
    }

    refreshEditingActivity();
    this->currentDriverFader = MidiMapping::FaderType::FADER_NOTE_VALUE;
    this->lastDriverFaderTime = millis();
    scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_NOTE_VALUE);
}

NOTE_EDIT_MEM void NoteEditManager::handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    Track& track = trackManager.getSelectedTrack();
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

NOTE_EDIT_MEM void NoteEditManager::resetLengthEditingModeOnSessionBoundary() {
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

NOTE_EDIT_MEM void NoteEditManager::resetLengthEditingModeOnNoteSelect() {
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

NOTE_EDIT_MEM void NoteEditManager::toggleLengthEditingMode() {
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

NOTE_EDIT_MEM void NoteEditManager::onEditEvent(EditEvent event) {
    Track& track = trackManager.getSelectedTrack();
    switch (event) {
        case EditEvent::LengthModeChanged:
            handleLengthModeChangedEvent(track);
            break;
        case EditEvent::SelectionChanged:
            handleSelectionChangedEvent(track);
            break;
        default:
            break;
    }
}

NOTE_EDIT_MEM void NoteEditManager::handleLengthModeChangedEvent(Track& track) {
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    const int selectedIdx = editManager.getSelectedNoteIdx();
    if (notes.empty() || selectedIdx < 0 || selectedIdx >= static_cast<int>(notes.size())) {
        return;
    }
    if (editManager.isLengthEditingMode()) {
        requestFaderOutbound(NoteEditFaderOutbound::Trigger::LengthModeEnter);
    } else {
        requestFaderOutbound(NoteEditFaderOutbound::Trigger::LengthModeExit);
    }
    lastUserCoarseFaderValue =
        midiFaderManager.getFaderState(MidiMapping::FaderType::FADER_COARSE).lastSentPitchbend;
    lastCoarseFaderTime = millis();
    (void)track;
}

NOTE_EDIT_MEM void NoteEditManager::handleSelectionChangedEvent(Track& track) {
    const EditorSelection& prior = editManager.selectionChangePrior();
    const EditorSelection& next = editManager.getNoteEditSessionState().selection;
    if (editManager.selectionChangeRequestFaderSync()) {
        scheduleNoteSelectFaderSync(track);
        return;
    }
    scheduleSelectDependentMotorSync(track, prior, next);
}

NOTE_EDIT_MEM void NoteEditManager::onTrackChanged(Track& newTrack) {
    releaseEditedNoteAudition();
    // If we're in loop edit mode, send the new track's loop length as CC feedback
    if (editManager.getEditSessionType() == EditSessionType::Loop) {
        loopEditManager.onTrackChanged(newTrack);
    }
}