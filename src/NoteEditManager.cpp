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
#include "DisplayManager.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteEditDependentFaderSnapshot.h"
#include "Utils/LoopTickNormalize.h"
#include "MidiFaderManager.h"
#include "MidiFaderProcessor.h"

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

void applyLengthEndTargetRules(uint32_t noteStart, uint32_t currentEnd, uint32_t loopLength,
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

int16_t lengthEditLoopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength) {
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

uint32_t lengthEditCoarsePitchbendToLoopTick(int16_t pitchValue, uint32_t loopLength) {
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

void clampLengthEditFineTargetTick(int32_t signedTick, uint32_t loopLength,
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

int32_t lengthEditFineOffsetFromCc(uint8_t ccValue) {
    const int32_t halfRange = static_cast<int32_t>(Config::TICKS_PER_16TH_STEP);
    const int32_t rawOffset = static_cast<int32_t>(ccValue) - 64;
    return constrain(rawOffset, -halfRange, halfRange);
}

uint8_t lengthEditFineCcFromOffset(int32_t offsetFromAnchor) {
    const int32_t halfRange = static_cast<int32_t>(Config::TICKS_PER_16TH_STEP);
    const int32_t clampedOffset = constrain(offsetFromAnchor, -halfRange, halfRange);
    return static_cast<uint8_t>(constrain(64 + clampedOffset, 0, 127));
}

}  // namespace

NoteEditManager::NoteEditManager() 
    : loopEditManager(midiHandler) {
}

// Delegate MIDI note handling to V2 system
void NoteEditManager::handleMidiNote(uint8_t channel, uint8_t note, uint8_t velocity, bool isNoteOn) {
    buttonHandler.handleMidiNote(channel, note, velocity, isNoteOn);
}

void NoteEditManager::update() {
    if (!startEditingEnabled && noteSelectionTime > 0) {
        enableStartEditing();
    }

    processFaderOutbound();
    processPendingSelectDependentMotorSync(trackManager.getSelectedTrack());
    processPendingGeometryDriverMotorSync(trackManager.getSelectedTrack());

    loopEditManager.update();
    faderHandler.update();
    buttonHandler.update();
}

void NoteEditManager::handleMidiPitchbend(uint8_t channel, int16_t pitchValue) {
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

void NoteEditManager::handleMidiCC(uint8_t channel, uint8_t ccNumber, uint8_t value) {
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

void NoteEditManager::moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote, std::uint32_t targetTick) {
    editManager.beginGeometryMutation(track, NoteEditKind::Move, true);
    uint32_t fromStart = currentNote.startTick;
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (focus.active && focus.last.pitch == currentNote.note &&
        focus.last.startTick == currentNote.startTick) {
        fromStart = focus.last.startTick;
    }
    const int32_t tickDifference =
        static_cast<int32_t>(targetTick) - static_cast<int32_t>(fromStart);

    logger.log(CAT_MIDI, LOG_DEBUG,
               "Note movement with overlap handling: from=%lu to=%lu difference=%ld overlapNotes=%zu",
               fromStart, targetTick, tickDifference,
               editManager.getEditSession().focus.overlapNotes.size());

    editManager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    if (focus.active) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Overlap move bridge: pitch=%d, start=%lu, end=%lu",
                   focus.last.pitch,
                   static_cast<unsigned long>(focus.last.startTick),
                   static_cast<unsigned long>(focus.last.endTick));
    }

    uint32_t dummyStart = currentNote.startTick;
    uint32_t dummyEnd = currentNote.endTick;
    NoteMovementUtils::applyNoteEditChange(track, editManager, NoteMovementUtils::NoteEditChangeKind::Move,
                                           currentNote, targetTick, static_cast<int>(tickDifference),
                                           0, 0, 0, dummyStart, dummyEnd);
}

void NoteEditManager::changeNoteEndWithOverlapHandling(Track& track,
                                                       const NoteUtils::DisplayNote& currentNote,
                                                       std::uint32_t targetEndTick) {
    editManager.beginGeometryMutation(track, NoteEditKind::Length, true);
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Note length change with overlap handling: pitch=%d, start=%lu, end %lu->%lu",
               currentNote.note, currentNote.startTick, currentNote.endTick, targetEndTick);
    NoteMovementUtils::changeLengthWithOverlapHandling(track, editManager, currentNote,
                                                       targetEndTick);
}

void NoteEditManager::processEncoderMovement(int rawDelta) {
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

void NoteEditManager::cycleEditMode(Track& track) {
    editManager.cycleEditSession(track);
}

void NoteEditManager::cycleEditSession(Track& track) {
    editManager.cycleEditSession(track);
}

void NoteEditManager::deleteSelectedNote(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0 &&
        editManager.getLastFader1SelectNoteId() == kInvalidNoteId) {
        logger.info("MIDI Encoder: No note selected for deletion");
        return;
    }

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;

    const std::vector<NoteUtils::DisplayNote> filteredNotes =
        selectableDisplayNotesForEditUi(track);
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    const EditorSelection& selection = editManager.getNoteEditSessionState().selection;

    int selectedIdx = editManager.getSelectedNoteIdx();
    NoteId deleteTargetNoteId = kInvalidNoteId;
    if (editorSelectionHasNote(selection)) {
        deleteTargetNoteId = selection.primaryNote;
    } else if (editManager.getLastFader1SelectNoteId() != kInvalidNoteId) {
        deleteTargetNoteId = editManager.getLastFader1SelectNoteId();
    }
    if (deleteTargetNoteId != kInvalidNoteId) {
        selectedIdx = filteredDisplayNoteIndexForNoteId(filteredNotes, deleteTargetNoteId);
    }
    if (selectedIdx < 0 || selectedIdx >= static_cast<int>(filteredNotes.size())) {
        logger.info("MIDI Encoder: Selected note index out of range");
        return;
    }
    if (deleteTargetNoteId == kInvalidNoteId) {
        deleteTargetNoteId = noteIdFromFilteredDisplayNote(filteredNotes, selectedIdx);
    }

    const NoteUtils::DisplayNote selectedNote = filteredNotes[static_cast<size_t>(selectedIdx)];

    const bool noteEditActive = editManager.isNoteEditActive();
    editManager.beginGeometryMutation(track, NoteEditKind::Delete, false);
    const bool deleteTargetDiffersFromFocus =
        noteEditActive && focus.active && focus.movingNoteId != deleteTargetNoteId;
    if (deleteTargetDiffersFromFocus) {
        editManager.commitPendingOverlapNoteEdits(track);
        editManager.rebuildNoteEditFocusForDisplayNote(track, selectedNote);
    } else if (noteEditActive && !focus.active) {
        editManager.rebuildNoteEditFocusForDisplayNote(track, selectedNote);
    }
    editManager.commitAllPendingNoteEditActions(track);

    uint8_t notePitch = selectedNote.note;
    uint32_t noteStart = selectedNote.startTick;
    uint32_t noteEnd = selectedNote.endTick;
    const std::vector<NoteUtils::DisplayNote> notesAfter =
        selectableDisplayNotesForEditUi(track);
    const int refreshedIdx =
        filteredDisplayNoteIndexForNoteId(notesAfter, deleteTargetNoteId);
    if (refreshedIdx >= 0 && refreshedIdx < static_cast<int>(notesAfter.size())) {
        const NoteUtils::DisplayNote& refreshed = notesAfter[static_cast<size_t>(refreshedIdx)];
        notePitch = refreshed.note;
        noteStart = refreshed.startTick;
        noteEnd = refreshed.endTick;
    } else {
        for (const NoteUtils::DisplayNote& n : notesAfter) {
            if (n.noteId == deleteTargetNoteId) {
                notePitch = n.note;
                noteStart = n.startTick;
                noteEnd = n.endTick;
                break;
            }
        }
    }

    logger.info("MIDI Encoder: Deleting note pitch=%d, start=%lu, end=%lu",
                notePitch, noteStart, noteEnd);

    auto& midiEvents = track.editAwareMidiEvents();
    MidiEvent* noteOnEvent = nullptr;
    for (MidiEvent& e : midiEvents) {
        if (e.type == midi::NoteOn && e.data.noteData.velocity > 0 &&
            e.data.noteData.note == notePitch && e.tick == noteStart) {
            noteOnEvent = &e;
            break;
        }
    }

    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent != nullptr) {
        noteOffEvent = NoteMovementUtils::findCorrespondingNoteOff(
            midiEvents, noteOnEvent, notePitch, noteStart, noteEnd);
    }

    int deletedCount = 0;
    auto eraseByPointer = [&](MidiEvent* needle) {
        if (needle == nullptr) {
            return;
        }
        for (auto it = midiEvents.begin(); it != midiEvents.end(); ++it) {
            if (&(*it) == needle) {
                midiEvents.erase(it);
                ++deletedCount;
                return;
            }
        }
    };
    if (noteOnEvent != nullptr || noteOffEvent != nullptr) {
        eraseByPointer(noteOffEvent);
        eraseByPointer(noteOnEvent);
    } else {
        auto it = midiEvents.begin();
        while (it != midiEvents.end()) {
            const bool matchOn =
                (it->type == midi::NoteOn && it->data.noteData.velocity > 0 &&
                 it->data.noteData.note == notePitch && it->tick == noteStart);
            const bool matchOff =
                ((it->type == midi::NoteOff ||
                  (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) &&
                 it->data.noteData.note == notePitch && it->tick == noteEnd);
            if (matchOn || matchOff) {
                it = midiEvents.erase(it);
                ++deletedCount;
            } else {
                ++it;
            }
        }
    }

    logger.info("MIDI Encoder: Deleted %d MIDI events for note", deletedCount);

    EditPass del{};
    del.passType = EditPassType::Note;
    del.actionType = EditActionType::Delete;
    del.propertyType = EditPropertyType::None;
    del.targetNoteId = deleteTargetNoteId;
    editManager.commitEditAction(track, EditPassVec{del});
    track.invalidateCaches();

    editManager.setSelectedNoteIdx(-1);
    editManager.rebuildNoteEditFocusAtSelect(track, -1);
    resetLengthEditingModeOnNoteSelect();

    // Since we're using dedicated faders now, we don't need to manage complex edit modes
    // Just send the current main edit mode to keep the system synchronized
    editManager.sendEditSessionChange(editManager.getEditSessionType());
    
    logger.info("MIDI Encoder: Note deleted, maintaining current edit mode");
}

void NoteEditManager::scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader) {
    Track& track = trackManager.getSelectedTrack();

    switch (driverFader) {
        case MidiMapping::FaderType::FADER_COARSE:
        case MidiMapping::FaderType::FADER_FINE:
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            syncSelectionFromGeometryEdit(track);
            publishDependentFaderLatch(track);
            break;
        case MidiMapping::FaderType::FADER_SELECT:
            sendFader1BracketFeedback(track, true);
            break;
        default:
            break;
    }
}

void NoteEditManager::logOutboundStep(const char* label) {
#if defined(SESSION_CAPTURE)
    logger.info("#DBG outbound_step=%s", label);
#else
    (void)label;
#endif
}

void NoteEditManager::logSelectSlot(int slotIndex, int16_t pitchValue, bool ignored,
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

void NoteEditManager::logSelectApplyDecision(uint32_t targetBracketTick, int targetNoteIdx,
                                             int slotIndex, int priorSlotIndex, bool apply,
                                             const char* reason) {
#if defined(SESSION_CAPTURE)
    logger.info(
        "#DBG select_apply bracket_tick=%lu note_idx=%d slot=%d prior_slot=%d apply=%d reason=%s",
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

void NoteEditManager::logSelectMotorSyncDecision(int noteIdx, int priorNoteIdx, bool sent,
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

void NoteEditManager::resetSelectNavSlotApplyState() {
    lastSelectMotorSyncMs_ = 0;
    lastMotorSyncF1Pitch_ = MidiConfig::Pitchbend::CENTER;
    selectDependentSettleUntilMs_ = 0;
    selectDependentSettleBlockLogged_ = false;
    clearPendingSelectDependentMotorSync();
    clearPendingGeometryDriverMotorSync();
}

void NoteEditManager::clearPendingSelectDependentMotorSync() {
    pendingSelectDriverMotorSyncValid_ = false;
    pendingSelectMotorTarget_ = {};
    pendingSelectMotorPlan_ = {};
    pendingSelectMotorPriorSelection_ = {};
}

void NoteEditManager::clearPendingGeometryDriverMotorSync() {
    pendingGeometryDriverMotorSyncValid_ = false;
}

void NoteEditManager::syncSelectionFromGeometryEdit(Track& track) {
    const EditorSelection priorSelection = editManager.getNoteEditSessionState().selection;
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    uint32_t bracketTick = editManager.getBracketTick() % loopLength;
    NoteId primaryNote = kInvalidNoteId;

    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        primaryNote = focus.movingNoteId;
        bracketTick = lengthEditingMode ? focus.last.endTick % loopLength
                                        : focus.last.startTick % loopLength;
    } else {
        const int selectedIdx = editManager.getSelectedNoteIdx();
        if (selectedIdx >= 0) {
            const std::vector<NoteUtils::DisplayNote> notes =
                editManager.selectableDisplayNotesAtEditSelect(track);
            if (selectedIdx < static_cast<int>(notes.size())) {
                const NoteUtils::DisplayNote& selected = notes[static_cast<size_t>(selectedIdx)];
                primaryNote = selected.noteId;
                bracketTick = lengthEditingMode ? selected.endTick % loopLength
                                                : selected.startTick % loopLength;
            }
        }
    }

    if (priorSelection.bracketTick == bracketTick) {
        if (editorSelectionTargetChanged(priorSelection, bracketTick, primaryNote)) {
            editManager.setBracketTick(bracketTick);
            editManager.applySelectionFromGeometryEdit(track, bracketTick, primaryNote);
        }
        return;
    }

    editManager.setBracketTick(bracketTick);
    editManager.applySelectionFromGeometryEdit(track, bracketTick, primaryNote);
    scheduleSelectDependentMotorSync(track, priorSelection,
                                     editManager.getNoteEditSessionState().selection, true);
}

void NoteEditManager::scheduleSelectDependentMotorSync(Track& track,
                                                       const EditorSelection& priorSelection,
                                                       const EditorSelection& nextSelection,
                                                       bool geometryIsDriver) {
    if (suppressSelectDependentMotorSync_) {
        clearPendingSelectDependentMotorSync();
        clearPendingGeometryDriverMotorSync();
        return;
    }

    if (geometryIsDriver) {
        const NoteEditFaderOutbound::PlanFlags plan =
            NoteEditFaderOutbound::planForGeometryDriverMotorSync(priorSelection.bracketTick,
                                                                  nextSelection.bracketTick);
        if (!plan.fader1) {
            return;
        }
        pendingGeometryDriverMotorSyncValid_ = true;
        (void)track;
#if defined(SESSION_CAPTURE)
        logger.info("#DBG selection_motor_sync scheduled=1 driver=geometry f1=1 bracket_tick=%lu",
                    static_cast<unsigned long>(nextSelection.bracketTick));
#endif
        return;
    }

    const NoteEditFaderOutbound::PlanFlags newPlan =
        NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(
            priorSelection.primaryNote, nextSelection.primaryNote, priorSelection.bracketTick,
            nextSelection.bracketTick);
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
    int noteIdx = -1;
    if (editorSelectionHasNote(nextSelection)) {
        noteIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(nextSelection,
                                                                                notes);
    }
    if (noteIdx < 0 && editorSelectionHasNote(nextSelection)) {
        return;
    }

    Fader1SelectTarget target;
    target.noteIdx = noteIdx;
    target.absoluteTargetTick = nextSelection.bracketTick;
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
                static_cast<unsigned long>(nextSelection.bracketTick));
#endif
}

void NoteEditManager::processPendingSelectDependentMotorSync(Track& track) {
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

void NoteEditManager::processPendingGeometryDriverMotorSync(Track& track) {
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

void NoteEditManager::armSelectDependentSettle(uint32_t sentAt) {
    selectDependentSettleUntilMs_ = sentAt + SELECT_DEPENDENT_SETTLE_MS;
    noteSelectionTime = sentAt;
    selectDependentSettleBlockLogged_ = false;
#if defined(SESSION_CAPTURE)
    logger.info("#DBG select_dependent_settle until_ms=%lu", selectDependentSettleUntilMs_);
#endif
}

void NoteEditManager::recordFaderInputForValidation(MidiMapping::FaderType faderType,
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

bool NoteEditManager::isFaderOutboundActive() const {
    return outboundStep_ != NoteEditFaderOutbound::Step::Idle &&
           outboundStep_ != NoteEditFaderOutbound::Step::Done;
}

void NoteEditManager::queuePendingOutbound(NoteEditFaderOutbound::Trigger trigger,
                                           const NoteEditFaderOutbound::PlanFlags* planOverride) {
    pendingOutboundTrigger_ = trigger;
    pendingOutboundPlan_ = planOverride != nullptr ? *planOverride
                                                   : NoteEditFaderOutbound::planForTrigger(trigger);
    pendingOutboundPlanValid_ = true;
}

void NoteEditManager::cancelActiveFaderOutbound() {
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

void NoteEditManager::requestFaderOutbound(NoteEditFaderOutbound::Trigger trigger,
                                             const NoteEditFaderOutbound::PlanFlags* planOverride) {
    if (trigger == NoteEditFaderOutbound::Trigger::None) {
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
        trigger == NoteEditFaderOutbound::Trigger::NoteSelectWithFader1 ||
        trigger == NoteEditFaderOutbound::Trigger::LengthModeEnter ||
        trigger == NoteEditFaderOutbound::Trigger::LengthModeExit) {
        startEditingEnabled = false;
    }
    outboundStep_ = NoteEditFaderOutbound::nextEnabledStep(NoteEditFaderOutbound::Step::Idle,
                                                             outboundPlan_);
    outboundStepStartedMs_ = millis();
    midiHandler.setDroidMotorOutboundPriority(true);
    logOutboundStep("BEGIN");
}

bool NoteEditManager::isGeometryDriverActive(uint32_t now) const {
    if (currentDriverFader == MidiMapping::FaderType::FADER_SELECT) {
        return false;
    }
    if (lastDriverFaderTime == 0) {
        return false;
    }
    return (now - lastDriverFaderTime) < COARSE_STABILITY_TIME;
}

void NoteEditManager::armSelectFaderFeedbackIgnore(uint32_t sentAt, uint32_t durationMs) {
    const uint32_t until = sentAt + durationMs;
    if (until > selectFaderFeedbackIgnoreUntilMs_) {
        selectFaderFeedbackIgnoreUntilMs_ = until;
    }
}

void NoteEditManager::sendFader1BracketFeedback(Track& track, bool updateNavStateFromOutbound) {
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

bool NoteEditManager::sendFader1MotorTimedBurst(Track& track) {
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
                static_cast<unsigned long>(editManager.getBracketTick()), targetPitchbend);
#endif
    midiHandler.setDroidMotorOutboundPriority(false);
    (void)track;
    return true;
}

void NoteEditManager::completeOutboundPipelineAtDone(Track& track, uint32_t now) {
    (void)now;
    const NoteEditFaderOutbound::Trigger completedTrigger = activeOutboundTrigger_;
    if (completedTrigger == NoteEditFaderOutbound::Trigger::SessionOpen) {
        suppressSelectDependentMotorSync_ = false;
    }
    logOutboundStep("DONE");
    activeOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
    midiHandler.setDroidMotorOutboundPriority(false);
    if (completedTrigger == NoteEditFaderOutbound::Trigger::NoteSelectWithFader1 ||
        completedTrigger == NoteEditFaderOutbound::Trigger::SessionOpen) {
        (void)track;
    }
}

void NoteEditManager::processFaderOutbound() {
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
                armSelectDependentSettle(sentAt);
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

int NoteEditManager::selectNavSlotIndexForPitchbend(Track& track, int16_t pitchValue) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return -1;
    }
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, editManager.getBracketTick(), true);
    if (slots.empty()) {
        return -1;
    }
    return map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0,
               static_cast<int>(slots.size()) - 1);
}


void NoteEditManager::enableStartEditing() {
    const uint32_t now = millis();
    if (now - noteSelectionTime >= NOTE_SELECTION_GRACE_PERIOD) {
        if (!startEditingEnabled) {
            startEditingEnabled = true;
            logger.info("Start editing enabled - grace period elapsed (%lu ms since selection)",
                        now - noteSelectionTime);
        }
    }
}

void NoteEditManager::prepareNoteEditSessionOpen() {
    suppressSelectDependentMotorSync_ = true;
    clearPendingSelectDependentMotorSync();
    clearPendingGeometryDriverMotorSync();
    startEditingEnabled = false;
}

void NoteEditManager::syncReferenceStepFromBracketTick(uint32_t bracketTick) {
    referenceStep = bracketTick / Config::TICKS_PER_16TH_STEP;
}

void NoteEditManager::scheduleNoteSelectFaderSync(Track& track) {
    noteSelectionTime = millis();
    startEditingEnabled = false;
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::NoteSelectWithFader1);
    (void)track;
}

void NoteEditManager::sendNoteEditSessionFaderFeedback(Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return;
    }

    resetSelectNavSlotApplyState();
    noteSelectionTime = millis();
    startEditingEnabled = false;
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::SessionOpen);
    logger.info("NOTE_EDIT session fader sync: outbound coordinator");
    (void)track;
}

void NoteEditManager::armChannel15FaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = sentAt;
    lastPitchbendSentTime = sentAt;
}

void NoteEditManager::armCoarseFaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = sentAt;
    lastPitchbendSentTime = sentAt;
}

void NoteEditManager::armChannel15CcFaderFeedbackIgnore(uint32_t sentAt) {
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = sentAt;
    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = sentAt;
}

void NoteEditManager::refreshEditingActivity() {
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
    input.loopStartTick = track.getLoopStartTick();
    input.selectedIdx = editManager.getSelectedNoteIdx();
    input.bracketTick = editManager.getBracketTick();
    input.lengthEditingMode = lengthEditingMode;
    input.lengthFineAnchorEndTick = lengthFineAnchorEndTick;
    input.referenceStep = referenceStep;

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

bool NoteEditManager::sendDependentFaderSnapshot(
    Track& track, const NoteEditFaderOutbound::PlanFlags& plan,
    const NoteEditDependentFaderSnapshot& snapshot, DependentFaderSendMode mode) {
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
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentCC =
            snapshot.fineCc;
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

void NoteEditManager::publishDependentFaderLatch(Track& track) {
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
            }
        }
    }
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.coarse = snapshot.coarseValid;
    plan.fine = snapshot.fineValid;
    plan.noteValue = snapshot.valid;
    sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

bool NoteEditManager::shouldIgnoreDependentFaderInput(MidiMapping::FaderType faderType,
                                                      int16_t pitchbendValue, uint8_t ccValue,
                                                      Track& track) {
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
    static constexpr uint8_t kNoteValueFeedbackToleranceCc = 0;

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

bool NoteEditManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType) {
    return shouldIgnoreFaderInput(faderType, -1, -1); // Use overloaded version with unknown values
}

bool NoteEditManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
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

int16_t NoteEditManager::loopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength) {
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

bool NoteEditManager::sendCoarseFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.coarse = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

bool NoteEditManager::sendFineFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.fine = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

bool NoteEditManager::sendNoteValueFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.noteValue = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

void NoteEditManager::sendCoarseFaderMotorNoteOn() {
    midiHandler.sendNoteOn(PITCHBEND_START_CHANNEL, MidiConfig::Fader::MOTOR_TRIGGER_NOTE, 127);
}

void NoteEditManager::sendCoarseFaderMotorNoteOff() {
    midiHandler.sendNoteOff(PITCHBEND_START_CHANNEL, MidiConfig::Fader::MOTOR_TRIGGER_NOTE, 0);
}

void NoteEditManager::sendFineFaderMotorNoteOn() {
    midiHandler.sendNoteOn(FINE_CC_CHANNEL, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE, 127);
}

void NoteEditManager::sendFineFaderMotorNoteOff() {
    midiHandler.sendNoteOff(FINE_CC_CHANNEL, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE, 0);
}

void NoteEditManager::sendNoteValueFaderMotorNoteOn() {
    midiHandler.sendNoteOn(NOTE_VALUE_CC_CHANNEL, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE,
                           127);
}

void NoteEditManager::sendNoteValueFaderMotorNoteOff() {
    midiHandler.sendNoteOff(NOTE_VALUE_CC_CHANNEL, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE,
                            0);
}

NoteEditManager::Fader1SelectTarget NoteEditManager::resolveFader1SelectTarget(Track& track,
                                                                                int16_t pitchValue) {
    Fader1SelectTarget target;
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return target;
    }
    const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, editManager.getBracketTick(), true);
    if (slots.empty()) {
        return target;
    }
    const int posIndex = map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0,
                             static_cast<int>(slots.size()) - 1);
    const SelectNavigation::SelectNavSlot& slot = slots[static_cast<size_t>(posIndex)];
    target.slotIndex = posIndex;
    target.absoluteTargetTick =
        SelectNavigation::noteStorageTick(slot.relativeTick, loopStartTick, loopLength);
    target.noteIdx = SelectNavigation::resolveNoteIdxAtSlot(slot);
    target.valid = true;
    return target;
}

std::vector<NoteUtils::DisplayNote> NoteEditManager::selectableDisplayNotesForEditUi(
    const Track& track) const {
    const uint32_t loopLength = track.getLoopLength();
    NoteUtils::DisplayNoteVec notes;
    if (!editManager.isNoteEditActive() || loopLength == 0) {
        const auto& cachedNotes = track.getCachedNotes();
        notes.assign(cachedNotes.begin(), cachedNotes.end());
    } else {
        const NoteEditFocus& focus = editManager.getEditSession().focus;
        const std::vector<NoteUtils::DisplayNote> filtered =
            filterSelectableDisplayNotes(track.editAwareMidiEvents(), focus,
                                         track.getMidiChannel(), loopLength);
        notes.assign(filtered.begin(), filtered.end());
    }

    if (displayManager_ != nullptr && loopLength > 0) {
        const uint8_t displaySlot = track.getActiveLoopIndex();
        const uint32_t currentTick = clockManager.getCurrentTick();
        const DetailedWindowContext window =
            displayManager_->resolveDetailedWindow(track, displaySlot, currentTick);
        if (window.active) {
            notes = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(
                notes, window.windowStartTick, window.windowLengthTicks, loopLength);
        }
    }
    return std::vector<NoteUtils::DisplayNote>(notes.begin(), notes.end());
}

std::vector<SelectNavigation::SelectNavSlot> NoteEditManager::buildSelectNavigationSlots(
    const Track& track, uint32_t bracketTick, bool includeBracketIfMissing) const {
    const uint32_t loopLength = track.getLoopLength();
    const uint32_t loopStartTick = loopLength > 0 ? track.getLoopStartTick() % loopLength : 0;
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    return SelectNavigation::buildSelectNavigationSlots(
        loopLength,
        loopStartTick,
        notes,
        bracketTick,
        includeBracketIfMissing);
}

void NoteEditManager::syncMotorsFromSelectTarget(
    Track& track, const Fader1SelectTarget& target,
    const NoteEditFaderOutbound::PlanFlags& plan) {
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

bool NoteEditManager::sendDependentFadersParallelTimedBurst(
    Track& track, const NoteEditFaderOutbound::PlanFlags& plan,
    const Fader1SelectTarget* selectTarget) {
    const NoteEditDependentFaderSnapshot snapshot =
        buildDependentFaderSnapshotForTrack(track, selectTarget);
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueAndMotor);
}

void NoteEditManager::handleSelectFaderInput(int16_t pitchValue, Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }

    const int slotIndex = selectNavSlotIndexForPitchbend(track, pitchValue);

    const uint32_t now = millis();

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
        priorSelection.primaryNote, nextPrimaryNote, priorSelection.bracketTick,
        target.absoluteTargetTick);

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

bool NoteEditManager::applyNoteSelectFromFader1Pitchbend(Track& track, int16_t pitchValue,
                                                         int posIndex) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, editManager.getBracketTick(), true);

    if (slots.empty() || posIndex < 0 || posIndex >= static_cast<int>(slots.size())) {
        return false;
    }

    const SelectNavigation::SelectNavSlot& slot = slots[static_cast<size_t>(posIndex)];
    const uint32_t absoluteTargetTick =
        SelectNavigation::noteStorageTick(slot.relativeTick, loopStartTick, loopLength);
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
        referenceStep = absoluteTargetTick / Config::TICKS_PER_16TH_STEP;
        noteSelectionTime = millis();
        currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    } else {
        editManager.commitAllPendingNoteEditActions(track);
        editManager.rebuildNoteEditFocusAtSelect(track, -1);
        editManager.applySelectNav(track, absoluteTargetTick, kInvalidNoteId, false, false);
        referenceStep = absoluteTargetTick / Config::TICKS_PER_16TH_STEP;
        startEditingEnabled = true;
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader: selected empty step at tick %lu (no note)",
                   absoluteTargetTick);
    }
    return true;
}

void NoteEditManager::handleCoarseFaderInput(int16_t pitchValue, Track& track) {
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

    if (lengthEditingMode) {
        editManager.syncNoteEditFocusLastFromSessionStore(track);
    }
    
    // Movement filtering - prevent jitter from rescheduling updates
    uint32_t now = millis();
    if (!lengthEditingMode &&
        (now - noteSelectionTime) < static_cast<uint32_t>(FEEDBACK_IGNORE_PERIOD)) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Coarse fader: ignoring input during post-select routing settle");
        return;
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
                   movementDelta, timeSinceLastMovement, lengthEditingMode ? "LENGTH EDIT" : "POSITION EDIT");
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
        uint32_t loopStartTick = track.getLoopStartTick();
        
        if (editManager.getEditSession().focus.active) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Coarse fader using focus.last: pitch=%d, start=%lu",
                       currentNote.note,
                       static_cast<unsigned long>(currentNote.startTick));
        }
        
        if (lengthEditingMode) {
            const uint32_t ticksPerStep = Config::TICKS_PER_16TH_STEP;
            const uint32_t minNoteDuration = ticksPerStep;

            const uint32_t relativeEndTick = currentNote.endTick % loopLength;
            uint32_t targetEndTick =
                lengthEditCoarsePitchbendToLoopTick(pitchValue, loopLength);
            applyLengthEndTargetRules(currentNote.startTick, currentNote.endTick, loopLength,
                                      minNoteDuration, targetEndTick);
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "LENGTH EDIT: pitchbend %d -> tick %lu (was %lu)",
                       pitchValue, targetEndTick, relativeEndTick);
            changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
            const NoteUtils::DisplayNote liveAfterLength =
                editManager.liveEditDisplayNoteAtSelect(track);
            lengthFineAnchorEndTick = liveAfterLength.endTick % loopLength;
            referenceStep = lengthFineAnchorEndTick / ticksPerStep;
        } else {
            // POSITION EDIT MODE: Move the note START position in 16th step increments
            // Convert current start tick to relative position
            uint32_t relativeStartTick = (currentNoteStartTick >= loopStartTick) ? 
                (currentNoteStartTick - loopStartTick) : (currentNoteStartTick + loopLength - loopStartTick);
            relativeStartTick = relativeStartTick % loopLength;
            
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
            
            // Convert back to absolute position
            uint32_t targetTick = (relativeTargetTick + loopStartTick) % loopLength;
            
            logger.log(CAT_MIDI, LOG_DEBUG, "POSITION EDIT: Note moved from step %lu to %lu (tick %lu -> %lu, relative %lu -> %lu)", 
                       currentSixteenthStep, targetSixteenthStep, currentNoteStartTick, targetTick, relativeStartTick, relativeTargetTick);
            
            // Store the target step as reference for fine adjustments
            referenceStep = targetSixteenthStep;
            
            // Mark editing activity to prevent note selection changes
            refreshEditingActivity();
            // Set up driver tracking for coarse fader
            this->currentDriverFader = MidiMapping::FaderType::FADER_COARSE;
            this->lastDriverFaderTime = millis();
            moveNoteToPosition(track, currentNote, targetTick);
        }
        
        // Mark editing activity to prevent note selection changes
        refreshEditingActivity();
        // Set up driver tracking for coarse fader
        this->currentDriverFader = MidiMapping::FaderType::FADER_COARSE;
        this->lastDriverFaderTime = millis();
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_COARSE);
        // NOTE: Fader 2 (COARSE) now uses 500ms grace period to update fader 1
        // This prevents erratic movement and allows proper settling time
    }
}

void NoteEditManager::handleFineFaderInput(uint8_t ccValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine editing disabled (grace period active)");
        return;
    }

    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for fine editing");
        return;
    }

    if (lengthEditingMode) {
        editManager.syncNoteEditFocusLastFromSessionStore(track);
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        NoteUtils::DisplayNote currentNote = editManager.liveEditDisplayNoteAtSelect(track);
        uint32_t loopStartTick = track.getLoopStartTick();
        
        if (editManager.getEditSession().focus.active) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader using focus.last: pitch=%d, start=%lu",
                       currentNote.note,
                       static_cast<unsigned long>(currentNote.startTick));
        }
        
        if (lengthEditingMode) {
            const uint32_t ticksPerStep = Config::TICKS_PER_16TH_STEP;
            const uint32_t minNoteDuration = ticksPerStep;
            const uint32_t anchorTick = lengthFineAnchorEndTick % loopLength;
            const int32_t fineOffset = lengthEditFineOffsetFromCc(ccValue);

            int32_t relativeTargetEndSigned =
                static_cast<int32_t>(anchorTick) + fineOffset;
            uint32_t relativeTargetEndTick = 0;
            clampLengthEditFineTargetTick(relativeTargetEndSigned, loopLength,
                                            relativeTargetEndTick);
            uint32_t targetEndTick = relativeTargetEndTick;
            applyLengthEndTargetRules(currentNote.startTick, currentNote.endTick, loopLength,
                                      minNoteDuration, targetEndTick);
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "LENGTH EDIT (fine): anchor %lu offset %ld -> tick %lu",
                       anchorTick, fineOffset, targetEndTick);
            changeNoteEndWithOverlapHandling(track, currentNote, targetEndTick);
        } else {
            // POSITION EDIT MODE: Adjust note START position with fine control
            uint32_t currentNoteStartTick = currentNote.startTick;
            
            // Convert to relative position for calculation
            uint32_t relativeStartTick = (currentNoteStartTick >= loopStartTick) ? 
                (currentNoteStartTick - loopStartTick) : (currentNoteStartTick + loopLength - loopStartTick);
            relativeStartTick = relativeStartTick % loopLength;
            
            // Use the reference step established by coarse fader as the base
            uint32_t sixteenthStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
            
            // CC2 gives us 127 steps for precise control: CC=64 is center (no offset)
            int32_t offset = (int32_t)ccValue - 64;  // -64 to +63
            
            // Calculate target start tick: 16th step boundary + CC offset (relative)
            int32_t relativeTargetStartTickSigned = (int32_t)sixteenthStepStartTick + offset;
            
            // Handle negative values by wrapping to end of loop
            uint32_t relativeTargetStartTick;
            if (relativeTargetStartTickSigned < 0) {
                relativeTargetStartTick = loopLength + relativeTargetStartTickSigned;
            } else {
                relativeTargetStartTick = (uint32_t)relativeTargetStartTickSigned;
            }
            
            // Constrain to valid range within the loop
            if (relativeTargetStartTick >= loopLength) {
                relativeTargetStartTick = relativeTargetStartTick % loopLength;
            }
            
            // Convert back to absolute position
            uint32_t targetStartTick = (relativeTargetStartTick + loopStartTick) % loopLength;
            
            logger.log(CAT_MIDI, LOG_DEBUG, "POSITION EDIT: Fine adjustment from relative tick %lu to %lu (absolute %lu -> %lu)", 
                       relativeStartTick, relativeTargetStartTick, currentNoteStartTick, targetStartTick);
            
            // Mark editing activity to prevent note selection changes
            refreshEditingActivity();
            // Set up driver tracking for fine fader
            this->currentDriverFader = MidiMapping::FaderType::FADER_FINE;
            this->lastDriverFaderTime = millis();
            moveNoteToPosition(track, currentNote, targetStartTick);
        }
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Fine fader: CC=%d - %s mode", 
                   ccValue, lengthEditingMode ? "LENGTH EDIT" : "POSITION EDIT");
        
        // Mark editing activity to prevent note selection changes (for both modes)
        refreshEditingActivity();
        // Set up driver tracking for fine fader
        this->currentDriverFader = MidiMapping::FaderType::FADER_FINE;
        this->lastDriverFaderTime = millis();
        lastMotorSyncDriverInputMs_ = millis();
        clearPendingSelectDependentMotorSync();

        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_FINE);
    }
}

void NoteEditManager::handleNoteValueFaderInput(uint8_t ccValue, Track& track) {
    // Only process fader input when in NOTE_EDIT mode
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }
    
    // Only process if start editing is enabled
    if (!startEditingEnabled) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value editing disabled (grace period active)");
        return;
    }
    
    // Only process if we have a selected note
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for note value editing");
        return;
    }

    uint32_t now = millis();
    if ((now - noteSelectionTime) < static_cast<uint32_t>(FEEDBACK_IGNORE_PERIOD)) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Note value fader: ignoring input during post-select routing settle");
        return;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;
    
    std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
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
        uint8_t newNoteValue = ccValue;  // Direct 1:1 mapping from CC to MIDI note value
        
        // Constrain to valid MIDI note range (0-127)
        newNoteValue = constrain(newNoteValue, 0, 127);
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader: currentNote=%d newNote=%d (cc=%d)", 
                   currentNoteValue, newNoteValue, ccValue);
        
        // If the pitch isn't changing, no need to do anything
        if (currentNoteValue == newNoteValue) {
            return;
        }

        editManager.beginGeometryMutation(track, NoteEditKind::Pitch, true);

        NoteUtils::DisplayNote pitchTarget = liveNote;
        pitchTarget.startTick = noteStart;
        pitchTarget.endTick = noteEnd;
        const bool pitchUpdated = NoteMovementUtils::applyNoteEditChange(
            track, editManager, NoteMovementUtils::NoteEditChangeKind::Pitch, pitchTarget,
            0, 0, 0, currentNoteValue, newNoteValue, noteStart, noteEnd);
        if (!pitchUpdated) {
            return;
        }

        editManager.syncSelectedNoteIdxToFilteredInventory(track);

        refreshEditingActivity();
        this->currentDriverFader = MidiMapping::FaderType::FADER_NOTE_VALUE;
        this->lastDriverFaderTime = millis();
        lastMotorSyncDriverInputMs_ = millis();
        clearPendingSelectDependentMotorSync();
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_NOTE_VALUE);
    }
}

void NoteEditManager::handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
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

void NoteEditManager::resetLengthEditingModeOnSessionBoundary() {
    resetSelectNavSlotApplyState();
    suppressSelectDependentMotorSync_ = false;
    if (!lengthEditingMode) {
        return;
    }
    lengthEditingMode = false;
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    logger.info("[MIDI] Length editing mode DISABLED (edit session boundary)");
}

void NoteEditManager::resetLengthEditingModeOnNoteSelect() {
    if (lengthEditingMode) {
        lengthEditingMode = false;
        logger.info("[MIDI] Length editing mode DISABLED (note select)");
    }
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    lastUserCoarseFaderValue = 0;
    lastCoarseFaderTime = 0;
}

void NoteEditManager::toggleLengthEditingMode() {
    uint32_t now = millis();
    
    // Debounce protection
    if (now - lastLengthModeToggleTime < LENGTH_MODE_DEBOUNCE_TIME) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Length mode toggle ignored (debounce protection)");
        return;
    }
    lastLengthModeToggleTime = now;
    
    const bool enabling = !lengthEditingMode;
    lengthEditingMode = enabling;
    
    Track& track = trackManager.getSelectedTrack();
    
    if (lengthEditingMode) {
        logger.info("[MIDI] Length editing mode ENABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE END position (length editing)");
        editManager.syncNoteEditFocusLastFromSessionStore(track);
        const uint32_t loopLength = track.getLoopLength();
        if (editManager.getSelectedNoteIdx() >= 0 && loopLength > 0) {
            const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
            const uint32_t relEnd = liveNote.endTick % loopLength;
            editManager.setBracketTick(relEnd);
            lengthFineAnchorEndTick = relEnd;
            referenceStep = relEnd / Config::TICKS_PER_16TH_STEP;
            editManager.beginGeometryMutation(track, NoteEditKind::Length, false);
        }
    } else {
        currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
        lastDriverFaderTime = now;
        lastUserCoarseFaderValue = 0;
        lastCoarseFaderTime = 0;
        logger.info("[MIDI] Length editing mode DISABLED");
        logger.info("[MIDI] Faders 1, 2 & 3 now control NOTE START position (position editing)");
        editManager.commitAllPendingNoteEditActions(track);
        editManager.syncNoteEditFocusLastFromSessionStore(track);
        const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
        const uint32_t loopLength = track.getLoopLength();
        if (loopLength > 0) {
            const uint32_t relStart = liveNote.startTick % loopLength;
            editManager.setBracketTick(relStart);
            referenceStep = relStart / Config::TICKS_PER_16TH_STEP;
        }
        if (editManager.getSelectedNoteIdx() >= 0) {
            editManager.beginGeometryMutation(track, NoteEditKind::Move, false);
        }
    }

    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    const int selectedIdx = editManager.getSelectedNoteIdx();
    if (!notes.empty() && selectedIdx >= 0 && selectedIdx < static_cast<int>(notes.size())) {
        noteSelectionTime = millis();
        if (lengthEditingMode) {
            requestFaderOutbound(NoteEditFaderOutbound::Trigger::LengthModeEnter);
        } else {
            requestFaderOutbound(NoteEditFaderOutbound::Trigger::LengthModeExit);
        }
        lastUserCoarseFaderValue =
            midiFaderManager.getFaderState(MidiMapping::FaderType::FADER_COARSE).lastSentPitchbend;
        lastCoarseFaderTime = millis();
    }
}

void NoteEditManager::onTrackChanged(Track& newTrack) {
    // If we're in loop edit mode, send the new track's loop length as CC feedback
    if (editManager.getEditSessionType() == EditSessionType::Loop) {
        loopEditManager.onTrackChanged(newTrack);
    }
}