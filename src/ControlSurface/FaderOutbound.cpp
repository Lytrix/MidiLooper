//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"

#include <Arduino.h>

#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Globals.h"
#include "Logger.h"
#include "MidiConfig.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/NoteEditFaderMotorTiming.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditMem.h"

NOTE_EDIT_MEM void ControlSurfaceManager::logOutboundStep(const char* label) {
#if defined(SESSION_CAPTURE)
    logger.info("#DBG outbound_step=%s", label);
#else
    (void)label;
#endif
}
NOTE_EDIT_MEM bool ControlSurfaceManager::isFaderOutboundActive() const {
    return outboundStep_ != NoteEditFaderOutbound::Step::Idle &&
           outboundStep_ != NoteEditFaderOutbound::Step::Done;
}

NOTE_EDIT_MEM void ControlSurfaceManager::queuePendingOutbound(NoteEditFaderOutbound::Trigger trigger,
                                           const NoteEditFaderOutbound::PlanFlags* planOverride) {
    pendingOutboundTrigger_ = trigger;
    pendingOutboundPlan_ = planOverride != nullptr ? *planOverride
                                                   : NoteEditFaderOutbound::planForTrigger(trigger);
    pendingOutboundPlanValid_ = true;
}

NOTE_EDIT_MEM void ControlSurfaceManager::cancelActiveFaderOutbound() {
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

NOTE_EDIT_MEM void ControlSurfaceManager::requestFaderOutbound(NoteEditFaderOutbound::Trigger trigger,
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
    if (trigger == NoteEditFaderOutbound::Trigger::LengthModeEnter ||
        trigger == NoteEditFaderOutbound::Trigger::LengthModeExit) {
        startEditingEnabled = false;
        armSelectDependentSettle(millis(), FEEDBACK_IGNORE_PERIOD);
    }
    outboundStep_ = NoteEditFaderOutbound::nextEnabledStep(NoteEditFaderOutbound::Step::Idle,
                                                             outboundPlan_);
    outboundStepStartedMs_ = millis();
    midiHandler.setDroidMotorOutboundPriority(true);
    logOutboundStep("BEGIN");
}
NOTE_EDIT_MEM void ControlSurfaceManager::sendFader1BracketFeedback(Track& track, bool updateNavStateFromOutbound) {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return;
    }
    int16_t targetPitchbend = 0;
    if (!EditSelectNoteState::resolveTargetPitchbend(editManager, track, targetPitchbend)) {
        return;
    }

    const bool pipelineActive = isFaderOutboundActive();
    const bool sessionOpenOutbound =
        pipelineActive &&
        activeOutboundTrigger_ == NoteEditFaderOutbound::Trigger::SessionOpen;
    if (!pipelineActive) {
        midiHandler.setDroidMotorOutboundPriority(true);
    }
    const uint32_t sentAt = millis();
    armSelectFaderFeedbackIgnore(sentAt, FEEDBACK_IGNORE_PERIOD);
    if (sessionOpenOutbound) {
        midiHandler.sendPitchBend(PITCHBEND_SELECT_CHANNEL, targetPitchbend);
    }
    NoteEditFaderMotorTiming::runMotorFaderBurst(
        [&]() { midiHandler.sendPitchBend(PITCHBEND_SELECT_CHANNEL, targetPitchbend); },
        [&]() { midiHandler.sendNoteOn(PITCHBEND_SELECT_CHANNEL, 0, 127); },
        [&]() { midiHandler.sendNoteOff(PITCHBEND_SELECT_CHANNEL, 0, 0); });
    lastSelectnoteSentTime = sentAt;
    auto& selectState = midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_SELECT);
    selectState.lastSentPitchbend = targetPitchbend;
    selectState.lastSentTime = sentAt;
    outboundSentFader1Pitchbend_ = targetPitchbend;
    if (updateNavStateFromOutbound || sessionOpenOutbound) {
        lastUserSelectFaderValue = targetPitchbend;
        lastSelectFaderTime = sentAt;
        const uint32_t loopLength = track.getLoopLength();
        if (loopLength > 0) {
            lastGeometryF1SyncedBracketTick_ = editManager.getSelectedTick() % loopLength;
        }
    }
    logOutboundStep("SEND_F1");
    if (!pipelineActive) {
        midiHandler.setDroidMotorOutboundPriority(false);
    }
    (void)track;
}

NOTE_EDIT_MEM bool ControlSurfaceManager::sendFader1MotorTimedBurst(Track& track) {
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

NOTE_EDIT_MEM void ControlSurfaceManager::completeOutboundPipelineAtDone(Track& track, uint32_t now) {
    (void)now;
    const NoteEditFaderOutbound::Trigger completedTrigger = activeOutboundTrigger_;
    if (completedTrigger == NoteEditFaderOutbound::Trigger::SessionOpen) {
        suppressSelectDependentMotorSync_ = false;
    }
    if (completedTrigger == NoteEditFaderOutbound::Trigger::LengthModeEnter ||
        completedTrigger == NoteEditFaderOutbound::Trigger::LengthModeExit) {
        syncDependentFaderTrackingFromOutboundLatch();
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

NOTE_EDIT_MEM void ControlSurfaceManager::processFaderOutbound() {
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
NOTE_EDIT_MEM void ControlSurfaceManager::drainFaderOutboundUntilIdle() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    uint8_t guard = 0;
    while (isFaderOutboundActive() && guard < 32) {
        processFaderOutbound();
        ++guard;
    }
}
