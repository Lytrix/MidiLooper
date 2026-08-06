//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"
#include "Logger.h"
#include "MidiConfig.h"
#include "NoteEditFocus.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/NoteEditDependentFaderSnapshot.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteEditFaderMotorTiming.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditMem.h"
#include "Utils/SelectNavigation.h"

NOTE_EDIT_MEM void ControlSurfaceManager::scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader) {
    if (editManager.isNoteEditActive() && clockManager.isTransportRunning()) {
        return;
    }
    Track& track = trackManager.getSelectedTrack();

    switch (driverFader) {
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            publishDependentFaderLatch(track, driverFader);
            break;
        case MidiMapping::FaderType::FADER_COARSE:
        case MidiMapping::FaderType::FADER_FINE:
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
NOTE_EDIT_MEM void ControlSurfaceManager::resetSelectNavSlotApplyState() {
    lastSelectMotorSyncMs_ = 0;
    lastMotorSyncF1Pitch_ = MidiConfig::Pitchbend::CENTER;
    selectDependentSettleUntilMs_ = 0;
    selectDependentSettleBlockLogged_ = false;
    clearPendingSelectDependentMotorSync();
    clearPendingGeometryDriverMotorSync();
    clearSelectionRelatchAfterGeometry();
    clearGeometryRelatchCycleEligibility();
}

NOTE_EDIT_MEM void ControlSurfaceManager::clearSelectionRelatchAfterGeometry() {
    selectionRelatchAfterGeometryActive_ = false;
    geometrySelectBlockedDuringGeometryHold_ = false;
    selectionRelatchMotorSentAtMs_ = 0;
    selectionRelatchSuspendOnly_ = false;
}

NOTE_EDIT_MEM void ControlSurfaceManager::clearGeometryRelatchCycleEligibility() {
    geometryRelatchConsumed_ = false;
}

NOTE_EDIT_MEM uint32_t ControlSurfaceManager::liveMovingNoteDisplayBracketForF1Sync(
    const Track& track) const {
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
        return UINT32_MAX;
    }
    const NoteEditKind kind = editManager.getNoteEditSessionState().kind;
    if (!isGeometryEditKind(kind) || kind == NoteEditKind::Select) {
        return UINT32_MAX;
    }
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return UINT32_MAX;
    }
    const uint32_t loopStartTick = editManager.noteEditLoopStartTick(track);
    const uint32_t storageBracket =
        editManager.isLengthEditingMode() ? focus.last.endTick : focus.last.startTick;
    return NoteEditDisplaySnapshot::displayStartTickFromStorage(storageBracket, loopStartTick,
                                                                loopLength);
}

NOTE_EDIT_MEM void ControlSurfaceManager::finishSelectApplyFromFader1Teardown() {
    clearSelectionRelatchAfterGeometry();
    clearPendingGeometryDriverMotorSync();
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    noteSelectionTime = millis();
#if defined(SESSION_CAPTURE)
    logger.info("#DBG selection_relatch cycle_end=1");
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::clearSelectFaderNavigationGates() {
    selectDependentSettleUntilMs_ = 0;
    selectDependentSettleBlockLogged_ = false;
    selectFaderFeedbackIgnoreUntilMs_ = 0;
#if defined(SESSION_CAPTURE)
    logger.info("#DBG select_fader_navigation_gates cleared=1");
#endif
}

NOTE_EDIT_MEM bool ControlSurfaceManager::fader1SelectTargetChangesSelection(
    const Fader1SelectTarget& target) const {
    if (!target.valid) {
        return false;
    }
    const EditorSelection& priorSelection = editManager.getNoteEditSessionState().selection;
    NoteId nextPrimaryNote = kInvalidNoteId;
    if (target.noteIdx >= 0) {
        const std::vector<NoteUtils::DisplayNote> notes =
            editManager.selectableDisplayNotesForEditUi(
                trackManager.getSelectedTrack());
        if (target.noteIdx < static_cast<int>(notes.size())) {
            nextPrimaryNote = noteIdFromFilteredDisplayNote(notes, target.noteIdx);
        }
    }
    return NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        priorSelection.primaryNote, nextPrimaryNote, priorSelection.selectedTick,
        target.absoluteTargetTick);
}

NOTE_EDIT_MEM void ControlSurfaceManager::preemptGeometryHoldForSelectNavigation(uint32_t now) {
    clearSelectionRelatchAfterGeometry();
    clearSelectFaderNavigationGates();
    clearPendingGeometryDriverMotorSync();
    geometrySelectBlockedDuringGeometryHold_ = false;
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    noteSelectionTime = now;
#if defined(SESSION_CAPTURE)
    logger.info("#DBG select_navigation preempt_geometry_hold=1");
#endif
}
NOTE_EDIT_MEM void ControlSurfaceManager::processDeferredFaderMotorSync() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    Track& track = trackManager.getSelectedTrack();
    processPendingSelectDependentMotorSync(track);
    const uint32_t now = millis();
    if (pendingGeometryDriverMotorSyncValid_ && !isGeometryDriverActive(now)) {
        processPendingGeometryDriverMotorSync(track, true);
    }
}

NOTE_EDIT_MEM void ControlSurfaceManager::clearPendingSelectDependentMotorSync() {
    pendingSelectDriverMotorSyncValid_ = false;
    pendingSelectDependentMotorRequiredPaintEpoch_ = 0;
    pendingSelectMotorTarget_ = {};
    pendingSelectMotorPlan_ = {};
    pendingSelectMotorPriorSelection_ = {};
}

NOTE_EDIT_MEM void ControlSurfaceManager::clearPendingGeometryDriverMotorSync() {
    pendingGeometryDriverMotorSyncValid_ = false;
    pendingGeometryMotorRequiredPaintEpoch_ = 0;
}

NOTE_EDIT_MEM void ControlSurfaceManager::syncSelectionFromGeometryEdit(Track& track) {
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

    if (priorSelection.selectedTick != selectedTick ||
        editorSelectionTargetChanged(priorSelection, selectedTick, primaryNote)) {
        editManager.setSelectedTick(selectedTick);
        editManager.applySelectionFromGeometryEdit(track, selectedTick, primaryNote);
    }

    const bool bracketChangedForF1 = selectedTick != lastGeometryF1SyncedBracketTick_;
    if (!bracketChangedForF1) {
        return;
    }

    EditorSelection motorPrior = priorSelection;
    if (lastGeometryF1SyncedBracketTick_ != UINT32_MAX) {
        motorPrior.selectedTick = lastGeometryF1SyncedBracketTick_;
    }
    const EditorSelection& nextSelection = editManager.getNoteEditSessionState().selection;
    scheduleSelectDependentMotorSync(track, motorPrior, nextSelection, true);
}

NOTE_EDIT_MEM void ControlSurfaceManager::scheduleSelectDependentMotorSync(Track& track,
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
        pendingGeometryMotorRequiredPaintEpoch_ = editManager.noteEditDisplayInvalidateEpoch();
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

    const std::vector<NoteUtils::DisplayNote> notes = editManager.selectableDisplayNotesForEditUi(track);
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
    pendingSelectDependentMotorRequiredPaintEpoch_ = editManager.noteEditDisplayInvalidateEpoch();
    (void)track;
#if defined(SESSION_CAPTURE)
    logger.info("#DBG selection_motor_sync scheduled=1 driver=select f1=0 f2=%d f4=%d note_idx=%d "
                "bracket_tick=%lu paint_epoch=%lu",
                plan.coarse ? 1 : 0, plan.noteValue ? 1 : 0, noteIdx,
                static_cast<unsigned long>(nextSelection.selectedTick),
                static_cast<unsigned long>(pendingSelectDependentMotorRequiredPaintEpoch_));
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::processPendingSelectDependentMotorSync(Track& track) {
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
    if (editManager.noteEditDisplayPaintedEpoch() < pendingSelectDependentMotorRequiredPaintEpoch_) {
        return;
    }
    if (!NoteEditFaderMotorTiming::selectDependentSettleExpired(now, selectDependentSettleUntilMs_)) {
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

    const bool sent = syncMotorsFromSelectTarget(track, target, plan);
    logSelectMotorSyncDecision(target.noteIdx, -1, sent, reason, lastUserSelectFaderValue,
                               priorMotorSyncF1Pitch, sinceSyncMs, planned.coarsePitchbend,
                               planned.valid ? static_cast<int>(planned.noteValueCc) : -1,
                               priorF2Pb, priorF4Cc, motorValueChanged);
    (void)priorSelection;
}

NOTE_EDIT_MEM void ControlSurfaceManager::processPendingGeometryDriverMotorSync(Track& track,
                                                                                bool forceFlush) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)track;
        (void)forceFlush;
        return;
    }
    if (!pendingGeometryDriverMotorSyncValid_ || suppressSelectDependentMotorSync_) {
        return;
    }
    if (isFaderOutboundActive()) {
        return;
    }
    const uint32_t now = millis();
    if (!forceFlush &&
        !NoteEditFaderMotorTiming::shouldFlushSelectDependentMotorSync(
            now, lastMotorSyncDriverInputMs_, pendingGeometryDriverMotorSyncValid_)) {
        return;
    }
    if (editManager.noteEditDisplayPaintedEpoch() < pendingGeometryMotorRequiredPaintEpoch_) {
        return;
    }

    clearPendingGeometryDriverMotorSync();
    syncSelectFaderTrackingFromLogicalBracket(track);
    if (sendFader1MotorTimedBurst(track)) {
        const uint32_t loopLength = track.getLoopLength();
        if (loopLength > 0) {
            lastGeometryF1SyncedBracketTick_ = editManager.getSelectedTick() % loopLength;
        }
    }
}

NOTE_EDIT_MEM void ControlSurfaceManager::syncSelectFaderTrackingFromLogicalBracket(Track& track) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)track;
        return;
    }
    int16_t targetPitchbend = 0;
    if (!EditSelectNoteState::resolveTargetPitchbend(editManager, track, targetPitchbend)) {
        return;
    }
    const uint32_t sentAt = millis();
    lastUserSelectFaderValue = targetPitchbend;
    lastSelectFaderTime = sentAt;
    armSelectFaderFeedbackIgnore(sentAt, FEEDBACK_IGNORE_PERIOD);
}

NOTE_EDIT_MEM void ControlSurfaceManager::armSelectDependentSettle(uint32_t sentAt,
                                                                 uint32_t durationMs) {
    selectDependentSettleUntilMs_ = sentAt + durationMs;
    selectDependentSettleBlockLogged_ = false;
#if defined(SESSION_CAPTURE)
    logger.info("#DBG select_dependent_settle until_ms=%lu", selectDependentSettleUntilMs_);
#endif
}

NOTE_EDIT_MEM void ControlSurfaceManager::syncDependentFaderTrackingFromOutboundLatch() {
    const uint32_t now = millis();
    lastUserCoarseFaderValue =
        midiFaderManager.getFaderState(MidiMapping::FaderType::FADER_COARSE).lastSentPitchbend;
    lastCoarseFaderTime = now;
    lastUserFineCc = midiFaderManager.getFaderState(MidiMapping::FaderType::FADER_FINE).lastSentCC;
    lastFineFaderTime = now;
    lastUserNoteValueCc =
        midiFaderManager.getFaderState(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentCC;
    lastNoteValueFaderTime = now;
}
NOTE_EDIT_MEM bool ControlSurfaceManager::syncMotorsFromSelectTarget(
    Track& track, const Fader1SelectTarget& target,
    const NoteEditFaderOutbound::PlanFlags& plan) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        (void)track;
        (void)target;
        (void)plan;
        return false;
    }
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0 || !target.valid) {
        return false;
    }
    if (!plan.coarse && !plan.fine && !plan.noteValue) {
        return false;
    }

    if constexpr (kEditedNoteAuditionEnabled) {
        releaseEditedNoteAudition();
    }

    midiHandler.setDroidMotorOutboundPriority(true);
#if defined(SESSION_CAPTURE)
    logger.info("#DBG select_motor_sync mode=SELECT_SYNC sequence=parallel_timed_burst");
#endif

    const bool sent = sendDependentFadersParallelTimedBurst(track, plan, &target);
    if (sent) {
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
    return sent;
}

NOTE_EDIT_MEM bool ControlSurfaceManager::sendDependentFadersParallelTimedBurst(
    Track& track, const NoteEditFaderOutbound::PlanFlags& plan,
    const Fader1SelectTarget* selectTarget) {
    const NoteEditDependentFaderSnapshot snapshot =
        buildDependentFaderSnapshotForTrack(track, selectTarget);
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueAndMotor);
}
