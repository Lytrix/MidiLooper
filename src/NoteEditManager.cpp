//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstdint>
#include <algorithm>
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

    processFaderSelectQuiet();
    processFaderOutbound();

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
            if (currentDriverFader == MidiMapping::FaderType::FADER_NOTE_VALUE &&
                lastDriverFaderTime > 0 &&
                (millis() - lastDriverFaderTime) < SELECTNOTE_UPDATE_DELAY) {
                auto& selectState =
                    midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT);
                const int16_t echoDiff = abs(pitchValue - selectState.lastSentPitchbend);
                static constexpr int16_t kSelectEchoTolerance = 100;
                if (echoDiff <= kSelectEchoTolerance) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                               "Pitchbend ch=%d ignored (ch16 echo after note-value edit, diff=%d)",
                               channel, echoDiff);
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
    if (editManager.getSelectedNoteIdx() < 0 && !editManager.hasLastFader1SelectRef()) {
        logger.info("MIDI Encoder: No note selected for deletion");
        return;
    }

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) return;

    const std::vector<NoteUtils::DisplayNote> filteredNotes =
        selectableDisplayNotesForEditUi(track);
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    const uint8_t channel = track.getMidiChannel();

    int selectedIdx = editManager.getSelectedNoteIdx();
    NoteRef deleteTargetRef{};
    if (editManager.hasLastFader1SelectRef()) {
        deleteTargetRef = editManager.getLastFader1SelectRef();
        selectedIdx = filteredDisplayNoteIndexForNoteRef(channel, focus, filteredNotes, deleteTargetRef);
    }
    if (selectedIdx < 0 || selectedIdx >= static_cast<int>(filteredNotes.size())) {
        logger.info("MIDI Encoder: Selected note index out of range");
        return;
    }
    if (deleteTargetRef.channel == 0) {
        deleteTargetRef = noteRefFromFilteredDisplayNote(channel, focus, filteredNotes, selectedIdx);
        if (deleteTargetRef.channel == 0) {
            deleteTargetRef = noteRefFromDisplay(channel, filteredNotes[static_cast<size_t>(selectedIdx)]);
        }
    }

    const NoteUtils::DisplayNote selectedNote = filteredNotes[static_cast<size_t>(selectedIdx)];

    const bool noteEditActive = editManager.isNoteEditActive();
    editManager.beginGeometryMutation(track, NoteEditKind::Delete, false);
    const bool deleteTargetDiffersFromFocus =
        noteEditActive && focus.active && !noteRefEquals(focus.moving, deleteTargetRef);
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
    const int refreshedIdx = filteredDisplayNoteIndexForNoteRef(
        track.getMidiChannel(), editManager.getEditSession().focus, notesAfter, deleteTargetRef);
    if (refreshedIdx >= 0 && refreshedIdx < static_cast<int>(notesAfter.size())) {
        const NoteUtils::DisplayNote& refreshed = notesAfter[static_cast<size_t>(refreshedIdx)];
        notePitch = refreshed.note;
        noteStart = refreshed.startTick;
        noteEnd = refreshed.endTick;
    } else {
        for (const NoteUtils::DisplayNote& n : notesAfter) {
            if (n.note == deleteTargetRef.note && n.startTick == deleteTargetRef.startTick) {
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
    del.target = {track.getMidiChannel(), notePitch, noteStart, noteEnd};
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

void NoteEditManager::sendStartNotePitchbend(Track& track) {
    sendCoarseFaderPosition(track);
    sendFineFaderPosition(track);
    lastPitchbendSentTime = millis();
    logger.log(CAT_MIDI, LOG_DEBUG,
               "Fader 2/3 feedback via length-aware coarse/fine (%s)",
               lengthEditingMode ? "LENGTH EDIT" : "POSITION EDIT");
}

void NoteEditManager::logOutboundStep(const char* label) {
#if defined(SESSION_CAPTURE)
    logger.info("#DBG outbound_step=%s", label);
#else
    (void)label;
#endif
}

void NoteEditManager::logSelectSlot(int slotIndex, int16_t pitchValue, bool ignored) {
#if defined(SESSION_CAPTURE)
    logger.info("#DBG select_slot idx=%d pitch=%d ignored=%d", slotIndex, pitchValue, ignored ? 1 : 0);
#else
    (void)slotIndex;
    (void)pitchValue;
    (void)ignored;
#endif
}

bool NoteEditManager::isFaderOutboundActive() const {
    return outboundStep_ != NoteEditFaderOutbound::Step::Idle &&
           outboundStep_ != NoteEditFaderOutbound::Step::Done;
}

void NoteEditManager::cancelActiveFaderOutbound() {
    activeOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
    outboundStep_ = NoteEditFaderOutbound::Step::Idle;
    outboundPlan_ = {};
    outboundStepStartedMs_ = 0;
    midiHandler.setDroidMotorOutboundPriority(false);
}

void NoteEditManager::requestFaderOutbound(NoteEditFaderOutbound::Trigger trigger,
                                             const NoteEditFaderOutbound::PlanFlags* planOverride) {
    if (trigger == NoteEditFaderOutbound::Trigger::None) {
        return;
    }

    if (NoteEditFaderOutbound::shouldCoalesceDependentRefresh(trigger, outboundStep_)) {
        pendingOutboundTrigger_ = trigger;
        logOutboundStep("COALESCE");
        return;
    }

    if (isFaderOutboundActive()) {
        if (NoteEditFaderOutbound::shouldPreemptActivePipeline(trigger)) {
            cancelActiveFaderOutbound();
        } else if (trigger == NoteEditFaderOutbound::Trigger::Fader1BracketOnly) {
            Track& track = trackManager.getSelectedTrack();
            sendFader1BracketFeedback(track, false);
            return;
        } else {
            pendingOutboundTrigger_ = trigger;
            return;
        }
    }

    activeOutboundTrigger_ = trigger;
    pendingOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
    outboundPlan_ = planOverride != nullptr ? *planOverride
                                            : NoteEditFaderOutbound::planForTrigger(trigger);
    outboundStep_ = NoteEditFaderOutbound::nextEnabledStep(NoteEditFaderOutbound::Step::Idle,
                                                             outboundPlan_);
    outboundStepStartedMs_ = millis();
    midiHandler.setDroidMotorOutboundPriority(true);
    logOutboundStep("BEGIN");
}

void NoteEditManager::evaluateDependentFaderRefreshDirty(Track& track,
                                                         bool& needsPositionRefresh,
                                                         bool& needsPitchRefresh) {
    needsPositionRefresh = false;
    needsPitchRefresh = false;

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;

    if (editManager.getSelectedNoteIdx() < 0) {
        const uint32_t bracketRelTick =
            SelectNavigation::noteRelativeTick(editManager.getBracketTick(), loopStartTick,
                                               loopLength);
        needsPositionRefresh =
            lastFeedbackAnchorRelTick_ == kFeedbackAnchorRelTickUnset ||
            bracketRelTick != lastFeedbackAnchorRelTick_;
        return;
    }

    const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
    const uint32_t relStart =
        SelectNavigation::noteRelativeTick(liveNote.startTick, loopStartTick, loopLength);

    needsPositionRefresh =
        lastFeedbackAnchorRelTick_ == kFeedbackAnchorRelTickUnset ||
        relStart != lastFeedbackAnchorRelTick_;
    needsPitchRefresh = lastFeedbackNotePitch_ == kFeedbackNotePitchUnset ||
                        static_cast<int8_t>(liveNote.note) != lastFeedbackNotePitch_;
}

bool NoteEditManager::requestDependentFaderRefreshFromSelection(Track& track) {
    bool needsPositionRefresh = false;
    bool needsPitchRefresh = false;
    evaluateDependentFaderRefreshDirty(track, needsPositionRefresh, needsPitchRefresh);
    if (!needsPositionRefresh && !needsPitchRefresh) {
#if defined(SESSION_CAPTURE)
        logger.info("#DBG dependent_refresh_skip pos=0 pitch=0");
#endif
        return false;
    }

    const NoteEditFaderOutbound::PlanFlags plan =
        NoteEditFaderOutbound::planForSelectDependent(needsPositionRefresh, needsPitchRefresh);
#if defined(SESSION_CAPTURE)
    logger.info("#DBG dependent_refresh_schedule pos=%d pitch=%d coarse=%d fine=%d note=%d",
                needsPositionRefresh ? 1 : 0, needsPitchRefresh ? 1 : 0, plan.coarse ? 1 : 0,
                plan.fine ? 1 : 0, plan.noteValue ? 1 : 0);
#endif
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::NoteSelectDependent, &plan);
    return true;
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

void NoteEditManager::resetFeedbackGeometrySnapshot() {
    lastFeedbackAnchorRelTick_ = kFeedbackAnchorRelTickUnset;
    lastFeedbackNotePitch_ = kFeedbackNotePitchUnset;
}

void NoteEditManager::stampFeedbackPositionFromSelection(Track& track) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }
    const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;

    if (editManager.getSelectedNoteIdx() < 0) {
        lastFeedbackAnchorRelTick_ =
            SelectNavigation::noteRelativeTick(editManager.getBracketTick(), loopStartTick,
                                               loopLength);
#if defined(SESSION_CAPTURE)
        logger.info("#DBG feedback_geometry_stamp pos_tick=%lu pitch=%d",
                    lastFeedbackAnchorRelTick_,
                    static_cast<int>(lastFeedbackNotePitch_));
#endif
        return;
    }

    const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
    lastFeedbackAnchorRelTick_ =
        SelectNavigation::noteRelativeTick(liveNote.startTick, loopStartTick, loopLength);
#if defined(SESSION_CAPTURE)
    logger.info("#DBG feedback_geometry_stamp pos_tick=%lu pitch=%d",
                lastFeedbackAnchorRelTick_,
                static_cast<int>(lastFeedbackNotePitch_));
#endif
}

void NoteEditManager::stampFeedbackPitchFromSelection(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        return;
    }
    const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
    lastFeedbackNotePitch_ = static_cast<int8_t>(liveNote.note);
#if defined(SESSION_CAPTURE)
    logger.info("#DBG feedback_geometry_stamp pos_tick=%lu pitch=%d",
                lastFeedbackAnchorRelTick_,
                static_cast<int>(lastFeedbackNotePitch_));
#endif
}

void NoteEditManager::stampFeedbackGeometrySnapshotAtDone(
    Track& track, const NoteEditFaderOutbound::PlanFlags& plan) {
    if (plan.coarse || plan.fine) {
        stampFeedbackPositionFromSelection(track);
    }
    if (plan.noteValue) {
        stampFeedbackPitchFromSelection(track);
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

void NoteEditManager::processFaderOutbound() {
    if (outboundStep_ == NoteEditFaderOutbound::Step::Idle ||
        outboundStep_ == NoteEditFaderOutbound::Step::Done) {
        midiHandler.setDroidMotorOutboundPriority(false);
        if (pendingOutboundTrigger_ != NoteEditFaderOutbound::Trigger::None) {
            const NoteEditFaderOutbound::Trigger pending = pendingOutboundTrigger_;
            pendingOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
            if (pending == NoteEditFaderOutbound::Trigger::NoteSelectDependent) {
                Track& track = trackManager.getSelectedTrack();
                requestDependentFaderRefreshFromSelection(track);
            } else {
                requestFaderOutbound(pending);
            }
        }
        return;
    }

    midiHandler.setDroidMotorOutboundPriority(true);

    const uint32_t now = millis();
    if (outboundStepStartedMs_ > 0 &&
        (now - outboundStepStartedMs_) > NoteEditFaderOutbound::kOutboundWatchdogMs) {
        logger.info("NOTE_EDIT outbound watchdog — forcing Done");
        outboundStep_ = NoteEditFaderOutbound::Step::Done;
        midiHandler.setDroidMotorOutboundPriority(false);
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
        case NoteEditFaderOutbound::Step::ArmMotorBank:
            armNoteEditDroidMotorBank();
            logOutboundStep("ARM");
            outboundStep_ = NoteEditFaderOutbound::advanceOutboundStep(outboundStep_, outboundPlan_);
            outboundStepStartedMs_ = now;
            return;
        case NoteEditFaderOutbound::Step::SendCoarse:
            if (sendCoarseFaderPosition(track)) {
                stampFeedbackPositionFromSelection(track);
                armCoarseFaderFeedbackIgnore(now);
                armSelectFaderFeedbackIgnore(now, FEEDBACK_IGNORE_PERIOD);
                logOutboundStep("SEND_F2");
                outboundStep_ =
                    NoteEditFaderOutbound::advanceOutboundStep(outboundStep_, outboundPlan_);
            } else {
                outboundStep_ = NoteEditFaderOutbound::advanceOutboundStep(
                    NoteEditFaderOutbound::Step::TriggerCoarse, outboundPlan_);
            }
            outboundStepStartedMs_ = now;
            return;
        case NoteEditFaderOutbound::Step::TriggerCoarse:
            sendCoarseFaderMotorTrigger();
            armSelectFaderFeedbackIgnore(now, F2_OUTBOUND_SELECT_IGNORE_TAIL_MS);
            logOutboundStep("TRIGGER_F2");
            outboundStep_ = NoteEditFaderOutbound::advanceOutboundStep(outboundStep_, outboundPlan_);
            outboundStepStartedMs_ = now;
            return;
        case NoteEditFaderOutbound::Step::SendFine:
            if (sendFineFaderPosition(track)) {
                armChannel15CcFaderFeedbackIgnore(now);
                logOutboundStep("SEND_F3");
                outboundStep_ =
                    NoteEditFaderOutbound::advanceOutboundStep(outboundStep_, outboundPlan_);
            } else {
                outboundStep_ = NoteEditFaderOutbound::advanceOutboundStep(
                    NoteEditFaderOutbound::Step::TriggerFine, outboundPlan_);
            }
            outboundStepStartedMs_ = now;
            return;
        case NoteEditFaderOutbound::Step::TriggerFine:
            sendFineFaderMotorTrigger();
            logOutboundStep("TRIGGER_F3");
            outboundStep_ = NoteEditFaderOutbound::advanceOutboundStep(outboundStep_, outboundPlan_);
            outboundStepStartedMs_ = now;
            return;
        case NoteEditFaderOutbound::Step::SendNoteValue:
            if (sendNoteValueFaderPosition(track)) {
                stampFeedbackPitchFromSelection(track);
                logOutboundStep("SEND_F4");
                outboundStep_ =
                    NoteEditFaderOutbound::advanceOutboundStep(outboundStep_, outboundPlan_);
            } else {
                outboundStep_ = NoteEditFaderOutbound::advanceOutboundStep(
                    NoteEditFaderOutbound::Step::TriggerNoteValue, outboundPlan_);
            }
            outboundStepStartedMs_ = now;
            return;
        case NoteEditFaderOutbound::Step::TriggerNoteValue:
            sendNoteValueFaderMotorTrigger();
            logOutboundStep("TRIGGER_F4");
            armChannel15FaderFeedbackIgnore(now);
            outboundStep_ = NoteEditFaderOutbound::Step::Done;
            outboundStepStartedMs_ = now;
            startEditingEnabled = true;
            logOutboundStep("DONE");
            stampFeedbackGeometrySnapshotAtDone(track, outboundPlan_);
            armSelectFaderFeedbackIgnore(now, F2_OUTBOUND_SELECT_IGNORE_TAIL_MS);
            activeOutboundTrigger_ = NoteEditFaderOutbound::Trigger::None;
            outboundStep_ = NoteEditFaderOutbound::Step::Idle;
            midiHandler.setDroidMotorOutboundPriority(false);
            return;
        default:
            outboundStep_ = NoteEditFaderOutbound::Step::Idle;
            midiHandler.setDroidMotorOutboundPriority(false);
            return;
    }
}

void NoteEditManager::processFaderSelectQuiet() {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return;
    }
    if (faderSelectPhase_ != NoteEditFaderOutbound::SelectPhase::UserMovingFader1) {
        return;
    }
    const uint32_t now = millis();
    if (!NoteEditFaderOutbound::isUserQuiet(now, fader1LastUserInputMs_)) {
        return;
    }

    faderSelectPhase_ = NoteEditFaderOutbound::SelectPhase::PendingDependentRefresh;
    Track& track = trackManager.getSelectedTrack();
    const int slotIndex = selectNavSlotIndexForPitchbend(track, lastUserSelectFaderValue);
    const bool selectionApplied = applyNoteSelectFromFader1Pitchbend(
        track, lastUserSelectFaderValue, lastUserSelectFaderValue, now, false, false);
    if (selectionApplied) {
        lastAppliedSelectSlotIndex_ = slotIndex;
        if (requestDependentFaderRefreshFromSelection(track)) {
            logOutboundStep("QUIET_REFRESH");
        }
    } else if (slotIndex >= 0 && slotIndex != lastAppliedSelectSlotIndex_) {
        lastAppliedSelectSlotIndex_ = slotIndex;
    }
    faderSelectPhase_ = NoteEditFaderOutbound::SelectPhase::Idle;
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

void NoteEditManager::armNoteEditDroidMotorBank() {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return;
    }
    midiHandler.sendProgramChange(MidiConfig::PROGRAM_CHANGE_CHANNEL,
                                  MidiConfig::SessionProgram::NOTE_EDIT);
    logger.info("NOTE_EDIT re-arm DROID motor bank (PC 1 ch16)");
}

void NoteEditManager::sendSelectnoteFaderUpdate(Track& track) {
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::NoteSelectWithFader1);
    (void)track;
}

void NoteEditManager::performSelectnoteFaderUpdate(Track& track) {
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::NoteSelectWithFader1);
    (void)track;
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

void NoteEditManager::scheduleNoteSelectFaderSync(Track& track) {
    noteSelectionTime = millis();
    lastAppliedSelectSlotIndex_ = selectNavSlotIndexForPitchbend(track, lastUserSelectFaderValue);
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::NoteSelectWithFader1);
}

void NoteEditManager::sendNoteEditSessionFaderFeedback(Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return;
    }

    faderSelectPhase_ = NoteEditFaderOutbound::SelectPhase::Idle;
    lastAppliedSelectSlotIndex_ = -1;
    resetFeedbackGeometrySnapshot();
    noteSelectionTime = millis();
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

bool NoteEditManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType) {
    return shouldIgnoreFaderInput(faderType, -1, -1); // Use overloaded version with unknown values
}

bool NoteEditManager::shouldIgnoreFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    MidiFaderProcessor::FaderState& state = midiFaderManager.getFaderStateMutable(faderType);
    uint32_t now = millis();

    if (faderType == MidiMapping::FaderType::FADER_SELECT &&
        selectFaderFeedbackIgnoreUntilMs_ != 0 && now < selectFaderFeedbackIgnoreUntilMs_) {
        if (pitchbendValue != -1) {
            const int16_t userDelta = abs(pitchbendValue - state.lastSentPitchbend);
            if (userDelta >= SELECT_MOVEMENT_THRESHOLD) {
                selectFaderFeedbackIgnoreUntilMs_ = 0;
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Fader 1 user select overrides pending bracket sync (delta=%d)",
                           userDelta);
                return false;
            }
        }
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Ignoring fader 1 input (pending bracket sync): %lu ms remaining",
                   selectFaderFeedbackIgnoreUntilMs_ - now);
        return true;
    }
    
    // No feedback prevention if we haven't sent anything recently
    if (state.lastSentTime == 0 || (now - state.lastSentTime) >= FEEDBACK_IGNORE_PERIOD) {
        return false;
    }
    
    // If we don't have the incoming values, use the old blanket ignore logic as fallback
    if (pitchbendValue == -1 && ccValue == (uint8_t)-1) {
        uint32_t remaining = FEEDBACK_IGNORE_PERIOD - (now - state.lastSentTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d input (feedback prevention): %lu ms remaining", 
                   faderType, remaining);
        return true;
    }
    
    // EDGE CASE FIX: Immediate post-update grace period
    // For the first 200ms after sending an update, ignore ALL input to prevent
    // user reactions to automatic fader movements from triggering new updates
    const uint32_t POST_UPDATE_GRACE_PERIOD = 200; // 200ms strict ignore period
    if ((now - state.lastSentTime) < POST_UPDATE_GRACE_PERIOD) {
        uint32_t remaining = POST_UPDATE_GRACE_PERIOD - (now - state.lastSentTime);
        logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d input (post-update grace period): %lu ms remaining", 
                   faderType, remaining);
        return true;
    }
    
    // Smart feedback detection: only ignore if the incoming value matches what we just sent
    const int16_t FEEDBACK_TOLERANCE_PITCHBEND = 100;  // Allow 100 units tolerance for pitchbend
    const uint8_t FEEDBACK_TOLERANCE_CC = 3;           // Allow 3 units tolerance for CC
    
    bool isProbablyFeedback = false;
    
    if (faderType == MidiMapping::FaderType::FADER_SELECT || faderType == MidiMapping::FaderType::FADER_COARSE) {
        // For pitchbend faders, check if incoming value is close to what we last sent
        int16_t diff = abs(pitchbendValue - state.lastSentPitchbend);
        if (diff <= FEEDBACK_TOLERANCE_PITCHBEND) {
            isProbablyFeedback = true;
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d pitchbend %d (feedback: sent %d, diff=%d)", 
                       faderType, pitchbendValue, state.lastSentPitchbend, diff);
        }
    } else if (faderType == MidiMapping::FaderType::FADER_FINE || faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        // For CC faders, check if incoming value is close to what we last sent
        uint8_t diff = abs((int)ccValue - (int)state.lastSentCC);
        if (diff <= FEEDBACK_TOLERANCE_CC) {
            isProbablyFeedback = true;
            logger.log(CAT_MIDI, LOG_DEBUG, "Ignoring fader %d CC %d (feedback: sent %d, diff=%d)", 
                       faderType, ccValue, state.lastSentCC, diff);
        }
    }
    
    if (!isProbablyFeedback) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Accepting fader %d input (significant user movement, not feedback)", 
                   faderType);
    }
    
    return isProbablyFeedback;
}

void NoteEditManager::scheduleOtherFaderUpdates(MidiMapping::FaderType driverFader) {
    Track& track = trackManager.getSelectedTrack();
    const uint32_t now = millis();

    switch (driverFader) {
        case MidiMapping::FaderType::FADER_COARSE:
        case MidiMapping::FaderType::FADER_FINE:
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            if (now - lastGeometryFader1BracketSentMs_ < GEOMETRY_F1_BRACKET_MIN_GAP_MS) {
                return;
            }
            lastGeometryFader1BracketSentMs_ = now;
            sendFader1BracketFeedback(track, false);
            break;
        case MidiMapping::FaderType::FADER_SELECT:
            sendFader1BracketFeedback(track, true);
            break;
        default:
            break;
    }
}

void NoteEditManager::sendFaderUpdate(MidiMapping::FaderType faderType, Track& track) {
    // IMPORTANT: Don't update CC faders (faders 3 and 4) when they were recently the driver
    // These faders represent user input and should maintain their position for a reasonable time
    // The MIDI events are the single source of truth - don't send calculated positions back to these faders
    if ((faderType == MidiMapping::FaderType::FADER_FINE && currentDriverFader == MidiMapping::FaderType::FADER_FINE) ||
        (faderType == MidiMapping::FaderType::FADER_NOTE_VALUE && currentDriverFader == MidiMapping::FaderType::FADER_NOTE_VALUE)) {
        uint32_t now = millis();
        uint32_t timeSinceDriverSet = now - lastDriverFaderTime;
        if (timeSinceDriverSet < 1000) { // 1 second protection period
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping fader %d update - fader %d was recently the driver (%lu ms ago)", 
                       faderType, faderType, timeSinceDriverSet);
            return;
        }
    }
    
    // Send position update (session PC 0=loop / 1=note is sendEditSessionChange only)
    sendFaderPosition(faderType, track);
    
    // Record when we sent this update and set ignore periods
    uint32_t now = millis();
    midiFaderManager.getFaderStateMutable(faderType).lastSentTime = now;
    
    if (faderType == MidiMapping::FaderType::FADER_COARSE) {
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE).lastSentTime = now;
    } else if (faderType == MidiMapping::FaderType::FADER_FINE ||
               faderType == MidiMapping::FaderType::FADER_NOTE_VALUE) {
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentTime = now;
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentTime = now;
        logger.log(CAT_MIDI, LOG_DEBUG, "Set ignore periods for channel 15 CC faders (shared channel)");
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Fader %d updated - ignoring incoming for %dms", 
               faderType, FEEDBACK_IGNORE_PERIOD);
}

void NoteEditManager::sendFaderPosition(MidiMapping::FaderType faderType, Track& track) {
    switch (faderType) {
        case MidiMapping::FaderType::FADER_SELECT:
            EditSelectNoteState::sendTargetPitchbend(editManager, track);
            break;
        case MidiMapping::FaderType::FADER_COARSE:
            sendCoarseFaderPosition(track);
            break;
        case MidiMapping::FaderType::FADER_FINE:
            sendFineFaderPosition(track);
            break;
        case MidiMapping::FaderType::FADER_NOTE_VALUE:
            sendNoteValueFaderPosition(track);
            break;
    }
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
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;

    if (editManager.getSelectedNoteIdx() < 0) {
        if (lengthEditingMode) {
            logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for coarse position");
            return false;
        }

        const uint32_t anchorTick =
            SelectNavigation::noteRelativeTick(editManager.getBracketTick(), loopStartTick,
                                               loopLength);
        const uint32_t currentSixteenthStep = anchorTick / Config::TICKS_PER_16TH_STEP;
        const int16_t coarseMidiPitchbend = loopTickToCoarsePitchbend(anchorTick, loopLength);

        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Coarse fader position (EMPTY_STEP): step %lu tick %lu -> pitchbend",
                   currentSixteenthStep, anchorTick);

#if defined(SESSION_CAPTURE)
        {
            const int16_t expectedPbRel = loopTickToCoarsePitchbend(anchorTick, loopLength);
            const int slotIndex = selectNavSlotIndexForPitchbend(track, lastUserSelectFaderValue);
            logger.info(
                "#DBG outbound_ctx f2 anchor_tick=%lu rel_tick=%lu loop_start=%lu loop_len=%lu "
                "pb=%d expected_pb_rel=%d step=%lu f1_pb=%d slot=%d mode=EMPTY_STEP",
                anchorTick, anchorTick, loopStartTick, loopLength, coarseMidiPitchbend,
                expectedPbRel, currentSixteenthStep, lastUserSelectFaderValue, slotIndex);
        }
#endif

        midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, coarseMidiPitchbend);

        auto& coarseState = midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE);
        coarseState.lastSentPitchbend = coarseMidiPitchbend;
        coarseState.lastSentTime = millis();

        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Sent coarse pitchbend=%d on ch%d (step %lu)",
                   coarseMidiPitchbend, PITCHBEND_START_CHANNEL, currentSixteenthStep);
        return true;
    }

    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
        const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;
        uint32_t anchorTick = 0;
        const char* modeLabel = "POSITION EDIT";

        if (lengthEditingMode) {
            const uint32_t storageTick = liveNote.endTick;
            anchorTick =
                SelectNavigation::noteRelativeTick(storageTick, loopStartTick, loopLength);
            modeLabel = "LENGTH EDIT";
        } else {
            const uint32_t storageTick = liveNote.startTick;
            anchorTick =
                SelectNavigation::noteRelativeTick(storageTick, loopStartTick, loopLength);
        }

        const uint32_t currentSixteenthStep = anchorTick / Config::TICKS_PER_16TH_STEP;
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Coarse fader position (%s): step %lu tick %lu -> pitchbend",
                   modeLabel, currentSixteenthStep, anchorTick);

        const int16_t coarseMidiPitchbend =
            lengthEditingMode ? lengthEditLoopTickToCoarsePitchbend(anchorTick, loopLength)
                              : loopTickToCoarsePitchbend(anchorTick, loopLength);

#if defined(SESSION_CAPTURE)
        {
            const uint32_t storageTick = lengthEditingMode ? liveNote.endTick : liveNote.startTick;
            const uint32_t relTick =
                SelectNavigation::noteRelativeTick(storageTick, loopStartTick, loopLength);
            const int16_t expectedPbRel = loopTickToCoarsePitchbend(relTick, loopLength);
            const int slotIndex = selectNavSlotIndexForPitchbend(track, lastUserSelectFaderValue);
            logger.info(
                "#DBG outbound_ctx f2 anchor_tick=%lu rel_tick=%lu loop_start=%lu loop_len=%lu "
                "pb=%d expected_pb_rel=%d step=%lu f1_pb=%d slot=%d mode=%s",
                anchorTick, relTick, loopStartTick, loopLength, coarseMidiPitchbend,
                expectedPbRel, currentSixteenthStep, lastUserSelectFaderValue, slotIndex,
                modeLabel);
        }
#endif

        midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, coarseMidiPitchbend);

        auto& coarseState = midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE);
        coarseState.lastSentPitchbend = coarseMidiPitchbend;
        coarseState.lastSentTime = millis();

        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Sent coarse pitchbend=%d on ch%d (step %lu)",
                   coarseMidiPitchbend, PITCHBEND_START_CHANNEL, currentSixteenthStep);
        return true;
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "Coarse position: Invalid selectedIdx=%d, notes.size()=%lu", 
               selectedIdx, notes.size());
    return false;
}

bool NoteEditManager::sendFineFaderPosition(Track& track) {
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;

    if (editManager.getSelectedNoteIdx() < 0) {
        if (lengthEditingMode) {
            logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for fine position");
            return false;
        }

        const uint32_t bracketRelTick =
            SelectNavigation::noteRelativeTick(editManager.getBracketTick(), loopStartTick,
                                               loopLength);
        const uint32_t stepStartTick =
            (bracketRelTick / Config::TICKS_PER_16TH_STEP) * Config::TICKS_PER_16TH_STEP;
        const int32_t offsetFromReferenceStep =
            static_cast<int32_t>(bracketRelTick) -
            static_cast<int32_t>(stepStartTick);
        const uint8_t fineCCValue =
            static_cast<uint8_t>(constrain(64 + offsetFromReferenceStep, 0, 127));

        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Fine fader position (EMPTY_STEP): offset %ld -> CC=%d",
                   offsetFromReferenceStep, fineCCValue);

        midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, fineCCValue);
        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentCC =
            fineCCValue;
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Sent fine CC=%d (empty step offset %ld)",
                   fineCCValue, offsetFromReferenceStep);
        return true;
    }

    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        const NoteUtils::DisplayNote liveNote = editManager.liveEditDisplayNoteAtSelect(track);
        uint32_t targetTick = 0;

        if (lengthEditingMode) {
            targetTick = liveNote.endTick % loopLength;
            const int32_t offsetFromAnchor =
                static_cast<int32_t>(targetTick) -
                static_cast<int32_t>(lengthFineAnchorEndTick % loopLength);
            const uint8_t fineCCValue = lengthEditFineCcFromOffset(offsetFromAnchor);

            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Fine fader position (LENGTH EDIT): anchor %lu end %lu offset %ld -> CC=%d",
                       lengthFineAnchorEndTick % loopLength, targetTick, offsetFromAnchor,
                       fineCCValue);

            midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, fineCCValue);
            midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentCC =
                fineCCValue;
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Sent fine CC=%d (length anchor offset %ld)",
                       fineCCValue, offsetFromAnchor);
            return true;
        }

        targetTick = liveNote.startTick % loopLength;

        const uint32_t referenceStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
        const int32_t offsetFromReferenceStep =
            static_cast<int32_t>(targetTick) -
            static_cast<int32_t>(referenceStepStartTick);

        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Fine fader position (POSITION EDIT): offset %ld -> CC=%d",
                   offsetFromReferenceStep, targetTick);

        // CC64 = 0 tick offset from reference step start, CC0 = -64 ticks, CC127 = +63 ticks
        uint8_t fineCCValue =
            (uint8_t)constrain(64 + offsetFromReferenceStep, 0, 127);
        midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, fineCCValue);

        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE).lastSentCC =
            fineCCValue;

        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Sent fine CC=%d (note offset %ld from reference step %lu)",
                   fineCCValue, offsetFromReferenceStep, referenceStep);
        return true;
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "Fine position: Invalid selectedIdx=%d, notes.size()=%lu", 
               selectedIdx, notes.size());
    return false;
}

bool NoteEditManager::sendNoteValueFaderPosition(Track& track) {
    if (editManager.getSelectedNoteIdx() < 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No note selected for note value position");
        return false;
    }
    
    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }
    
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    int selectedIdx = editManager.getSelectedNoteIdx();
    
    if (selectedIdx >= 0 && selectedIdx < (int)notes.size()) {
        // Note value doesn't need relative positioning - it's just the MIDI note number
        uint8_t noteValue = notes[static_cast<size_t>(selectedIdx)].note;
        
        logger.log(CAT_MIDI, LOG_DEBUG, "Note value fader position: note %d -> CC=%d",
                   noteValue, noteValue);
        
        midiHandler.sendControlChange(NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER, noteValue);

        midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE).lastSentCC =
            noteValue;

        logger.log(CAT_MIDI, LOG_DEBUG, "Sent note value CC=%d (note value %d)", noteValue, noteValue);
        return true;
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "Note value: Invalid selectedIdx=%d, notes.size()=%lu", 
               selectedIdx, notes.size());
    return false;
}

void NoteEditManager::sendCoarseFaderMotorTrigger() {
    midiHandler.sendNoteOn(PITCHBEND_START_CHANNEL, MidiConfig::Fader::MOTOR_TRIGGER_NOTE, 127);
    midiHandler.sendNoteOff(PITCHBEND_START_CHANNEL, MidiConfig::Fader::MOTOR_TRIGGER_NOTE, 0);
}

void NoteEditManager::sendFineFaderMotorTrigger() {
    midiHandler.sendNoteOn(FINE_CC_CHANNEL, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE, 127);
    midiHandler.sendNoteOff(FINE_CC_CHANNEL, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE, 0);
}

void NoteEditManager::sendNoteValueFaderMotorTrigger() {
    midiHandler.sendNoteOn(NOTE_VALUE_CC_CHANNEL, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE,
                           127);
    midiHandler.sendNoteOff(NOTE_VALUE_CC_CHANNEL, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE,
                            0);
}

namespace {

/// When several notes share a nav slot tick, keep the active moving note selected (**focus.last**).
int resolveNoteIdxAtSlot(const std::vector<SelectNavigation::SelectNavSlot>& slots,
                         const SelectNavigation::SelectNavSlot& slot,
                         const EditManager& editManager,
                         const std::vector<NoteUtils::DisplayNote>& notes,
                         bool selectingNewTick) {
    if (slot.noteIdx < 0) {
        return slot.noteIdx;
    }
    std::vector<int> candidates;
    for (const SelectNavigation::SelectNavSlot& candidate : slots) {
        if (candidate.noteIdx >= 0 && candidate.relativeTick == slot.relativeTick &&
            candidate.noteIdx < static_cast<int>(notes.size())) {
            candidates.push_back(candidate.noteIdx);
        }
    }
    if (candidates.empty()) {
        return slot.noteIdx;
    }

    if (selectingNewTick) {
        // When jumping to a new step, keep selection deterministic: first slot at that step.
        return candidates.front();
    }

    const NoteEditFocus& focus = editManager.getEditSession().focus;
    if (editManager.isNoteEditActive() && focus.active) {
        const uint8_t movingPitch = focus.last.pitch;
        const uint32_t movingStart = focus.last.startTick;
        for (int candidateIdx : candidates) {
            const NoteUtils::DisplayNote& n = notes[static_cast<size_t>(candidateIdx)];
            if (n.note == movingPitch && n.startTick == movingStart) {
                return candidateIdx;
            }
        }
        const int selectedIdx = editManager.getSelectedNoteIdx();
        if (selectedIdx >= 0 && selectedIdx < static_cast<int>(notes.size())) {
            const NoteUtils::DisplayNote& sel = notes[static_cast<size_t>(selectedIdx)];
            if (sel.note == movingPitch && sel.startTick == movingStart) {
                return selectedIdx;
            }
        }
        return slot.noteIdx;
    }
    return candidates.front();
}

}  // namespace

std::vector<NoteUtils::DisplayNote> NoteEditManager::selectableDisplayNotesForEditUi(
    const Track& track) {
    const uint32_t loopLength = track.getLoopLength();
    if (!editManager.isNoteEditActive() || loopLength == 0) {
        const auto& cachedNotes = track.getCachedNotes();
        return std::vector<NoteUtils::DisplayNote>(cachedNotes.begin(), cachedNotes.end());
    }
    const NoteEditFocus& focus = editManager.getEditSession().focus;
    return filterSelectableDisplayNotes(track.editAwareMidiEvents(), focus,
                                        track.getMidiChannel(), loopLength);
}

std::vector<SelectNavigation::SelectNavSlot> NoteEditManager::buildSelectNavigationSlots(
    const Track& track, uint32_t bracketTick, bool includeBracketIfMissing) {
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

void NoteEditManager::handleSelectFaderInput(int16_t pitchValue, Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Select fader input ignored: not in NOTE_EDIT mode (current mode: %s)", 
                   (editManager.getEditSessionType() == EditSessionType::Loop) ? "LOOP_EDIT" : "UNKNOWN");
        return;
    }

    const uint32_t now = millis();
    const int16_t priorSelectFaderValue = lastUserSelectFaderValue;
    const int priorSlotIndex =
        (lastSelectFaderTime == 0) ? -1 : selectNavSlotIndexForPitchbend(track, priorSelectFaderValue);

    const int slotIndex = selectNavSlotIndexForPitchbend(track, pitchValue);
    if (isGeometryDriverActive(now)) {
        logSelectSlot(slotIndex, pitchValue, true);
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Select fader input blocked during geometry driver (fader %d)",
                   static_cast<int>(currentDriverFader));
        return;
    }

    lastUserSelectFaderValue = pitchValue;
    lastSelectFaderTime = now;
    fader1LastUserInputMs_ = now;
    faderSelectPhase_ = NoteEditFaderOutbound::SelectPhase::UserMovingFader1;

    logSelectSlot(slotIndex, pitchValue, false);

    if (NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(priorSlotIndex, slotIndex)) {
        const bool selectionApplied =
            applyNoteSelectFromFader1Pitchbend(track, pitchValue, priorSelectFaderValue, now, true,
                                               false);
        lastAppliedSelectSlotIndex_ = slotIndex;
        if (selectionApplied) {
            requestDependentFaderRefreshFromSelection(track);
        }
    }
}

bool NoteEditManager::applyNoteSelectFromFader1Pitchbend(Track& track, int16_t pitchValue,
                                                         int16_t priorPitchValue, uint32_t now,
                                                         bool enforceStaleEchoLockout,
                                                         bool sendDependentFeedback) {
    // Check if we're in grace period after recent editing activity (live nav only).
    if (enforceStaleEchoLockout && lastEditingActivityTime > 0 &&
        (now - lastEditingActivityTime) < NOTE_SELECTION_GRACE_PERIOD) {
        const int16_t graceOverrideDelta = abs(pitchValue - priorPitchValue);
        if (graceOverrideDelta < SELECT_MOVEMENT_THRESHOLD) {
            uint32_t remaining = NOTE_SELECTION_GRACE_PERIOD - (now - lastEditingActivityTime);
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Note selection disabled - editing grace period: %lu ms remaining", remaining);
            return false;
        }
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Fader1 select overrides editing grace (delta=%d)", graceOverrideDelta);
    }

    uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const uint32_t loopStartTick = track.getLoopStartTick() % loopLength;
    const std::vector<SelectNavigation::SelectNavSlot> slots =
        buildSelectNavigationSlots(track, editManager.getBracketTick(), true);

    if (slots.empty()) {
        return false;
    }

    int posIndex = map(pitchValue, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX, 0,
                       (int)slots.size() - 1);
    const SelectNavigation::SelectNavSlot& slot = slots[posIndex];
    const uint32_t absoluteTargetTick =
        SelectNavigation::noteStorageTick(slot.relativeTick, loopStartTick, loopLength);
    const std::vector<NoteUtils::DisplayNote> notes = selectableDisplayNotesForEditUi(track);
    const bool selectingNewTick = absoluteTargetTick != editManager.getBracketTick();
    int noteIdx = resolveNoteIdxAtSlot(slots, slot, editManager, notes, selectingNewTick);

    if (enforceStaleEchoLockout && isGeometryDriverActive(now)) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Select fader: ignoring apply during geometry driver");
        return false;
    }

    if (enforceStaleEchoLockout) {
        const bool positionEditLockout =
            currentDriverFader != MidiMapping::FaderType::FADER_SELECT &&
            lastDriverFaderTime > 0 &&
            (now - lastDriverFaderTime) < (SELECTNOTE_UPDATE_DELAY + 200);
        if (positionEditLockout && absoluteTargetTick != editManager.getBracketTick()) {
            const int16_t deltaFromPrior = abs(pitchValue - priorPitchValue);
            if (deltaFromPrior < SELECT_MOVEMENT_THRESHOLD) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Select fader: ignoring stale echo during edit sync (fader1 tick %lu, bracket %lu, delta=%d)",
                           absoluteTargetTick, editManager.getBracketTick(), deltaFromPrior);
                return false;
            }
        }
    }

    if (absoluteTargetTick != editManager.getBracketTick() || noteIdx != editManager.getSelectedNoteIdx()) {
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
            const NoteRef selectRef = noteRefFromFilteredDisplayNote(
                track.getMidiChannel(), editManager.getEditSession().focus, notes, noteIdx);
            editManager.applySelectNav(track, noteIdx, absoluteTargetTick, selectRef, true, false);
            resetLengthEditingModeOnNoteSelect();
            referenceStep = absoluteTargetTick / Config::TICKS_PER_16TH_STEP;
            noteSelectionTime = millis();
            if (!isGeometryDriverActive(now)) {
                currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
            }
            if (sendDependentFeedback) {
                requestDependentFaderRefreshFromSelection(track);
            }
        } else {
            editManager.commitAllPendingNoteEditActions(track);
            editManager.rebuildNoteEditFocusAtSelect(track, -1);
            editManager.applySelectNav(track, -1, absoluteTargetTick, {}, false, false);
            referenceStep = absoluteTargetTick / Config::TICKS_PER_16TH_STEP;
            startEditingEnabled = true;
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Select fader: selected empty step at tick %lu (no note)", absoluteTargetTick);
        }
        return true;
    }

    return false;
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

        NoteUtils::DisplayNote pitchTarget{currentNoteValue, notes[static_cast<size_t>(selectedIdx)].velocity,
                                           noteStart, noteEnd};
        const bool pitchUpdated = NoteMovementUtils::applyNoteEditChange(
            track, editManager, NoteMovementUtils::NoteEditChangeKind::Pitch, pitchTarget,
            0, 0, 0, currentNoteValue, newNoteValue, noteStart, noteEnd);
        if (!pitchUpdated) {
            return;
        }

        const uint32_t displayEnd =
            (noteEnd >= loopLength) ? (noteEnd % loopLength) : noteEnd;
        const std::vector<NoteUtils::DisplayNote> updatedNotes = selectableDisplayNotesForEditUi(track);
        int newSelectedIdx = -1;

        // Find the updated note in the new notes list
        for (int i = 0; i < (int)updatedNotes.size(); i++) {
            if (updatedNotes[i].note == newNoteValue &&
                updatedNotes[i].startTick == noteStart &&
                updatedNotes[i].endTick == displayEnd) {
                newSelectedIdx = i;
                break;
            }
        }

        if (newSelectedIdx >= 0) {
            editManager.setSelectedNoteIdx(newSelectedIdx);
            editManager.setBracketTick(noteStart);
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Updated selectedNoteIdx: %d -> %d (note with new value)",
                       selectedIdx, newSelectedIdx);
        } else {
            editManager.selectClosestNote(track, editManager.getBracketTick());
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Resynced selection after pitch (merged note idx not found)");
        }

        // Mark editing activity to prevent note selection changes
        refreshEditingActivity();
        // Set up driver tracking for note value fader
        this->currentDriverFader = MidiMapping::FaderType::FADER_NOTE_VALUE;
        this->lastDriverFaderTime = millis();
        scheduleOtherFaderUpdates(MidiMapping::FaderType::FADER_NOTE_VALUE);
    }
}

void NoteEditManager::handleFaderInput(MidiMapping::FaderType faderType, int16_t pitchbendValue, uint8_t ccValue) {
    if (shouldIgnoreFaderInput(faderType, pitchbendValue, ccValue)) {
        if (faderType == MidiMapping::FaderType::FADER_SELECT) {
            logSelectSlot(-1, pitchbendValue, true);
        }
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
    Track& track = trackManager.getSelectedTrack();
    
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