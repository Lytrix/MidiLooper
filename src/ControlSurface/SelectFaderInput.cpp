//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"

#include <Arduino.h>

#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "MidiConfig.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Track.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteEditFaderMotorTiming.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditFaderSelectSync.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"
#include "Utils/SelectNavigation.h"

NOTE_EDIT_MEM void ControlSurfaceManager::logSelectSlot(int slotIndex, int16_t pitchValue, bool ignored,
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
NOTE_EDIT_MEM void ControlSurfaceManager::logSelectApplyDecision(uint32_t targetBracketTick, int targetNoteIdx,
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
NOTE_EDIT_MEM void ControlSurfaceManager::logSelectMotorSyncDecision(int noteIdx, int priorNoteIdx, bool sent,
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
NOTE_EDIT_MEM int ControlSurfaceManager::selectNavSlotIndexForPitchbend(Track& track, int16_t pitchValue) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return -1;
    }
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        editManager.buildSelectNavigationSlots(track, editManager.getSelectedTick(), true);
    if (slots.empty()) {
        return -1;
    }
    return map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0,
               static_cast<int>(slots.size()) - 1);
}
NOTE_EDIT_MEM ControlSurfaceManager::Fader1SelectTarget ControlSurfaceManager::resolveFader1SelectTarget(Track& track,
                                                                                int16_t pitchValue) {
    Fader1SelectTarget target;
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return target;
    }
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        editManager.buildSelectNavigationSlots(track, editManager.getSelectedTick(), true);
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
NOTE_EDIT_MEM void ControlSurfaceManager::handleSelectFaderInput(int16_t pitchValue, Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }

    const uint32_t now = millis();
    const Fader1SelectTarget navigationTarget = resolveFader1SelectTarget(track, pitchValue);
    if (navigationTarget.valid &&
        NoteEditFaderSelectSync::shouldClearSelectFaderNavigationGatesOnTargetChange(
            fader1SelectTargetChangesSelection(navigationTarget), now,
            selectDependentSettleUntilMs_)) {
        clearSelectFaderNavigationGates();
    }

    const int slotIndex = selectNavSlotIndexForPitchbend(track, pitchValue);

    clearPendingGeometryDriverMotorSync();
    lastUserSelectFaderValue = pitchValue;
    lastSelectFaderTime = now;
    lastMotorSyncDriverInputMs_ = now;

    logSelectSlot(slotIndex, pitchValue, false);

    const Fader1SelectTarget& target = navigationTarget;
    if (!target.valid) {
        return;
    }

    const EditorSelection& priorSelection = editManager.getNoteEditSessionState().selection;
    NoteId nextPrimaryNote = kInvalidNoteId;
    if (target.noteIdx >= 0) {
        const std::vector<NoteUtils::DisplayNote> notes = editManager.selectableDisplayNotesForEditUi(track);
        if (target.noteIdx < static_cast<int>(notes.size())) {
            nextPrimaryNote = noteIdFromFilteredDisplayNote(notes, target.noteIdx);
        }
    }

    const bool selectionChanged = NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        priorSelection.primaryNote, nextPrimaryNote, priorSelection.selectedTick,
        target.absoluteTargetTick);

    const NoteEditFocus& focus = editManager.getEditSession().focus;
    const NoteEditKind kind = editManager.getNoteEditSessionState().kind;
    const bool geometryEditContext =
        focus.active && focus.movingNoteId != kInvalidNoteId &&
        isGeometryEditKind(kind) && kind != NoteEditKind::Select;
    const bool geometryDriverHoldActive = geometryEditContext && isGeometryDriverActive(now);
    const bool physicalTargetDivergent =
        NoteEditFaderSelectSync::physicalSelectTargetDivergesFromLogical(
            priorSelection.primaryNote, priorSelection.selectedTick, nextPrimaryNote,
            target.absoluteTargetTick);

    if (NoteEditFaderSelectSync::shouldBlockEmptyStepSelectDuringGeometryHold(
            geometryDriverHoldActive, geometryEditContext, target.noteIdx < 0)) {
        geometrySelectBlockedDuringGeometryHold_ = true;
        logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                               false, "geometry_hold_empty_blocked");
        return;
    }

    if (selectionChanged && target.noteIdx >= 0 && geometryEditContext &&
        (geometryDriverHoldActive || selectionRelatchAfterGeometryActive_)) {
        preemptGeometryHoldForSelectNavigation(now);
    }

    if (NoteEditFaderSelectSync::shouldIgnoreGeometryDriverHoldForSelectNavigation(
            geometryDriverHoldActive, geometryEditContext, selectionChanged)) {
        geometrySelectBlockedDuringGeometryHold_ = true;
        logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                               false, "geometry_driver_ignored");
        return;
    }

    if (!geometryDriverHoldActive && geometrySelectBlockedDuringGeometryHold_ &&
        !physicalTargetDivergent) {
        geometrySelectBlockedDuringGeometryHold_ = false;
    }

    if (selectionRelatchAfterGeometryActive_) {
        if (NoteEditFaderSelectSync::shouldSuspendSelectDuringRelatch(true, physicalTargetDivergent)) {
            if (selectionChanged && target.noteIdx >= 0) {
                preemptGeometryHoldForSelectNavigation(now);
            } else {
                const uint32_t intentionalNavPeriodMs =
                    selectionRelatchSuspendOnly_ ? SELECT_DEPENDENT_SETTLE_MS : FEEDBACK_IGNORE_PERIOD;
                const bool intentionalNavigation =
                    NoteEditFaderSelectSync::isIntentionalSelectNavigationDuringRelatch(
                        true, physicalTargetDivergent, selectionChanged, now,
                        selectionRelatchMotorSentAtMs_, intentionalNavPeriodMs);
                if (!intentionalNavigation) {
                    logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                                           false, "select_relatch_suspend");
                    return;
                }
                clearSelectionRelatchAfterGeometry();
#if defined(SESSION_CAPTURE)
                logger.info("#DBG selection_relatch intentional_navigation=1");
#endif
            }
        } else if (NoteEditFaderSelectSync::shouldDismissRelatchAsSynchronized(
                       true, physicalTargetDivergent, now, lastSelectFaderTime,
                       NoteEditFaderMotorTiming::kSelectFaderMotorIdleMs)) {
            clearSelectionRelatchAfterGeometry();
#if defined(SESSION_CAPTURE)
            logger.info("#DBG selection_relatch synchronized=1");
#endif
        }
    }

    if (!selectionChanged &&
        NoteEditFaderSelectSync::shouldEnterSelectionRelatchAfterGeometry(
            geometryDriverHoldActive, geometrySelectBlockedDuringGeometryHold_,
            physicalTargetDivergent, geometryRelatchConsumed_)) {
        selectionRelatchAfterGeometryActive_ = true;
        geometrySelectBlockedDuringGeometryHold_ = false;
        geometryRelatchConsumed_ = true;
        selectionRelatchMotorSentAtMs_ = now;
        if (target.noteIdx >= 0) {
            selectionRelatchSuspendOnly_ = false;
            sendFader1MotorTimedBurst(track);
#if defined(SESSION_CAPTURE)
            logger.info("#DBG selection_relatch armed=1 mode=motor_to_note");
#endif
        } else {
            selectionRelatchSuspendOnly_ = true;
#if defined(SESSION_CAPTURE)
            logger.info("#DBG selection_relatch armed=1 mode=suspend_only");
#endif
        }
        logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                               false, "select_relatch_after_geometry");
        return;
    }

    if (target.noteIdx < 0) {
        if (selectionChanged) {
            geometrySelectBlockedDuringGeometryHold_ = false;
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
        geometrySelectBlockedDuringGeometryHold_ = false;
        logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                               true, "note_changed");
        applyNoteSelectFromFader1Pitchbend(track, pitchValue, target.slotIndex);
    } else {
        logSelectApplyDecision(target.absoluteTargetTick, target.noteIdx, target.slotIndex, -1,
                               false, "unchanged_note");
    }
}
NOTE_EDIT_MEM bool ControlSurfaceManager::applyNoteSelectFromFader1Pitchbend(Track& track, int16_t pitchValue,
                                                         int posIndex) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const std::vector<SelectNavigation::SelectNavSlot> slots =
        editManager.buildSelectNavigationSlots(track, editManager.getSelectedTick(), true);

    if (slots.empty() || posIndex < 0 || posIndex >= static_cast<int>(slots.size())) {
        return false;
    }

    const SelectNavigation::SelectNavSlot& slot = slots[static_cast<size_t>(posIndex)];
    const uint32_t absoluteTargetTick = slot.relativeTick;
    const std::vector<NoteUtils::DisplayNote> notes = editManager.selectableDisplayNotesForEditUi(track);
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

        const NoteId selectNoteId = noteIdFromFilteredDisplayNote(notes, noteIdx);
        editManager.cancelPendingDeleteForSelectNote(selectNoteId);
        const uint32_t preservedF1Bracket = liveMovingNoteDisplayBracketForF1Sync(track);
        editManager.syncNoteEditFocusLastFromSessionStore(track);
        const bool macroCommitDeferred = isNoteEditMacroCommitDeferred(millis());
        const bool driverValid = editManager.isLiveEditDriverValidForTrack(track);
        const bool selectBracketAligned =
            editManager.isMacroCommitAlignedWithSelectTargetForTrack(track, selectNoteId,
                                                                     absoluteTargetTick);
        if (!macroCommitDeferred && driverValid && selectBracketAligned) {
            editManager.commitAllPendingNoteEditActions(track);
        }
#if defined(SESSION_CAPTURE)
        else if (!macroCommitDeferred && driverValid && !selectBracketAligned) {
            logger.log(CAT_TRACK, LOG_WARNING,
                       "NOTE_EDIT macro commit skipped: select bracket mismatch "
                       "(moving=%lu bracket=%lu focus_last=%lu-%lu)",
                       static_cast<unsigned long>(editManager.getEditSession().focus.movingNoteId),
                       static_cast<unsigned long>(absoluteTargetTick),
                       static_cast<unsigned long>(editManager.getEditSession().focus.last.startTick),
                       static_cast<unsigned long>(editManager.getEditSession().focus.last.endTick));
        }
#endif
        const std::vector<NoteUtils::DisplayNote> notesAfterCommit =
            editManager.selectableDisplayNotesForEditUi(track);
        int postCommitNoteIdx = filteredDisplayNoteIndexForNoteIdAndStart(
            notesAfterCommit, selectNoteId, absoluteTargetTick);
        if (postCommitNoteIdx < 0) {
            postCommitNoteIdx = filteredDisplayNoteIndexForNoteId(notesAfterCommit, selectNoteId);
        }
        if (postCommitNoteIdx < 0) {
            const std::vector<SelectNavigation::SelectNavSlot> slotsAfter =
                editManager.buildSelectNavigationSlots(track, absoluteTargetTick, true);
            if (posIndex >= 0 && posIndex < static_cast<int>(slotsAfter.size())) {
                const SelectNavigation::SelectNavSlot& slotAfter = slotsAfter[static_cast<size_t>(posIndex)];
                if (slotAfter.relativeTick == absoluteTargetTick) {
                    postCommitNoteIdx = SelectNavigation::resolveNoteIdxAtSlot(slotAfter);
                }
            }
        }
        if (postCommitNoteIdx < 0 || postCommitNoteIdx >= static_cast<int>(notesAfterCommit.size())) {
            editManager.rebuildNoteEditFocusAtSelect(track, -1);
            editManager.applySelectNav(track, absoluteTargetTick, kInvalidNoteId, false, false);
            editManager.setReferenceStep(absoluteTargetTick / Config::TICKS_PER_16TH_STEP);
            if (preservedF1Bracket != UINT32_MAX) {
                lastGeometryF1SyncedBracketTick_ = preservedF1Bracket;
            }
            startEditingEnabled = true;
            releaseEditedNoteAudition();
            finishSelectApplyFromFader1Teardown();
            return true;
        }
        const uint32_t loopStartTick = editManager.noteEditLoopStartTick(track);
        const NoteUtils::DisplayNote& selectedNote =
            notesAfterCommit[static_cast<size_t>(postCommitNoteIdx)];
        editManager.rebuildNoteEditFocusForDisplayNote(track, selectedNote);
        const NoteEditFocus& focusAfterRebuild = editManager.getEditSession().focus;
        const NoteId navNoteId =
            focusAfterRebuild.movingNoteId != kInvalidNoteId ? focusAfterRebuild.movingNoteId
                                                             : selectedNote.noteId;
        uint32_t bracketTick =
            NoteEditFaderSelectSync::noteSelectBracketTickFromDisplayNote(
                selectedNote, loopStartTick, loopLength, editManager.isLengthEditingMode());
        if (editManager.isLiveEditDriverValidForTrack(track)) {
            const uint32_t storageBracket =
                editManager.isLengthEditingMode() ? focusAfterRebuild.last.endTick
                                                  : focusAfterRebuild.last.startTick;
            bracketTick = NoteEditDisplaySnapshot::displayStartTickFromStorage(
                storageBracket, loopStartTick, loopLength);
        }
        editManager.applySelectNav(track, bracketTick, navNoteId, false, false);
        if (!editManager.isLiveEditDriverValidForTrack(track)) {
            editManager.rebuildNoteEditFocusForDisplayNote(track, selectedNote);
        }
        resetLengthEditingModeOnNoteSelect();
        lastUserNoteValueCc = selectedNote.note;
        lastNoteValueFaderTime = 0;
        editManager.setReferenceStep(bracketTick / Config::TICKS_PER_16TH_STEP);
        lastGeometryF1SyncedBracketTick_ = bracketTick;
        armSelectDependentSettle(millis());
        finishSelectApplyFromFader1Teardown();
    } else {
        const uint32_t preservedF1Bracket = liveMovingNoteDisplayBracketForF1Sync(track);
        editManager.syncNoteEditFocusLastFromSessionStore(track);
        const NoteEditKind sessionKind = editManager.getNoteEditSessionState().kind;
        if (!isNoteEditMacroCommitDeferred(millis()) &&
            editManager.isLiveEditDriverValidForTrack(track) &&
            sessionKind != NoteEditKind::Select &&
            editManager.isMacroCommitAlignedWithSelectTargetForTrack(
                track, kInvalidNoteId, absoluteTargetTick)) {
            editManager.commitAllPendingNoteEditActions(track);
        }
        editManager.rebuildNoteEditFocusAtSelect(track, -1);
        editManager.applySelectNav(track, absoluteTargetTick, kInvalidNoteId, false, false);
        editManager.setReferenceStep(absoluteTargetTick / Config::TICKS_PER_16TH_STEP);
        if (preservedF1Bracket != UINT32_MAX) {
            lastGeometryF1SyncedBracketTick_ = preservedF1Bracket;
        }
        armSelectDependentSettle(millis());
        startEditingEnabled = true;
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected empty step at tick %lu (no note)",
                   absoluteTargetTick);
        releaseEditedNoteAudition();
        finishSelectApplyFromFader1Teardown();
    }
    return true;
}
