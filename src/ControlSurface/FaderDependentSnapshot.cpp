//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"

#include <unordered_set>

#include <Arduino.h>

#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "MidiConfig.h"
#include "NoteEditFocus.h"
#include "Track.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/LoopTickNormalize.h"
#include "Utils/NoteEditDependentFaderSnapshot.h"
#include "Utils/NoteEditFaderMotorTiming.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

NoteEditDependentFaderBuildInput ControlSurfaceManager::makeDependentFaderBuildInput(
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
            const std::vector<NoteUtils::DisplayNote> notes = editManager.selectableDisplayNotesForEditUi(track);
            if (selectTarget->noteIdx < static_cast<int>(notes.size())) {
                const NoteUtils::DisplayNote& note =
                    notes[static_cast<size_t>(selectTarget->noteIdx)];
                NoteUtils::DisplayNote spanNote = note;
                const NoteUtils::DisplayNoteVec& paint =
                    editManager.projectedNoteEditDisplayNotes(track);
                for (const NoteUtils::DisplayNote& dn : paint) {
                    if (dn.noteId == note.noteId) {
                        spanNote = dn;
                        break;
                    }
                }
                input.selectNoteStartTick = spanNote.startTick;
                input.selectNotePitch = spanNote.note;
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

NoteEditDependentFaderSnapshot ControlSurfaceManager::buildDependentFaderSnapshotForTrack(
    const Track& track, const Fader1SelectTarget* selectTarget) const {
    return buildDependentFaderSnapshot(makeDependentFaderBuildInput(track, selectTarget));
}

NOTE_EDIT_MEM bool ControlSurfaceManager::sendDependentFaderSnapshot(
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
    const bool outboundForcesDependentMotorPosition =
        activeOutboundTrigger_ == NoteEditFaderOutbound::Trigger::LengthModeEnter ||
        activeOutboundTrigger_ == NoteEditFaderOutbound::Trigger::LengthModeExit ||
        activeOutboundTrigger_ == NoteEditFaderOutbound::Trigger::SessionOpen;
    const bool skipDependentMotorPositionForAudition =
        kEditedNoteAuditionEnabled && editedNoteAuditionHeld_ &&
        !outboundForcesDependentMotorPosition;

    if (plan.coarse && snapshot.coarseValid) {
        bool skipCoarsePitchbendForAudition = false;
        if constexpr (kEditedNoteAuditionEnabled) {
            skipCoarsePitchbendForAudition =
                editedNoteAuditionHeld_ && !outboundForcesDependentMotorPosition;
        }
        if (!skipCoarsePitchbendForAudition) {
            midiHandler.sendPitchBend(PITCHBEND_START_CHANNEL, snapshot.coarsePitchbend);
            auto& coarseState =
                midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_COARSE);
            coarseState.lastSentPitchbend = snapshot.coarsePitchbend;
            coarseState.lastSentTime = now;
            sentAny = true;
        }
    }
    if (plan.fine && snapshot.fineValid) {
        bool skipFineCcForAudition = false;
        if constexpr (kEditedNoteAuditionEnabled) {
            skipFineCcForAudition =
                editedNoteAuditionHeld_ && !outboundForcesDependentMotorPosition;
        }
        if (!skipFineCcForAudition) {
            midiHandler.sendControlChange(FINE_CC_CHANNEL, FINE_CC_NUMBER, snapshot.fineCc);
            auto& fineState =
                midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_FINE);
            fineState.lastSentCC = snapshot.fineCc;
            fineState.lastSentTime = now;
            sentAny = true;
        }
    }
    if (plan.noteValue && snapshot.valid) {
        bool skipNoteValueCcForAudition = false;
        if constexpr (kEditedNoteAuditionEnabled) {
            skipNoteValueCcForAudition = editedNoteAuditionHeld_;
        }
        if (!skipNoteValueCcForAudition) {
            midiHandler.sendControlChange(NOTE_VALUE_CC_CHANNEL, NOTE_VALUE_CC_NUMBER,
                                          snapshot.noteValueCc);
            auto& noteValueState =
                midiFaderManager.getFaderStateMutable(MidiMapping::FaderType::FADER_NOTE_VALUE);
            noteValueState.lastSentCC = snapshot.noteValueCc;
            noteValueState.lastSentTime = now;
            sentAny = true;
        }
    }

    if (!sentAny) {
        if (mode == DependentFaderSendMode::ValueOnly) {
            return false;
        }
        const bool hasMotorPlan = (plan.coarse && snapshot.coarseValid) ||
                                  (plan.fine && snapshot.fineValid) ||
                                  (plan.noteValue && snapshot.valid);
        if (!hasMotorPlan) {
            return false;
        }
    } else if (mode == DependentFaderSendMode::ValueOnly) {
        return true;
    }

    struct DependentMotorBurstSlot {
        ControlSurfaceManager* owner = nullptr;
        bool enabled = false;
        bool skipMotorPositionForAudition = false;
        int16_t coarsePitchbend = 0;
        uint8_t fineCcValue = 0;
        uint8_t noteValueCc = 0;
        bool isCoarse = false;
        bool isFine = false;
        bool isNoteValue = false;
        void sendPosition() {
            if (!enabled || skipMotorPositionForAudition) {
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
    coarseSlot.skipMotorPositionForAudition = skipDependentMotorPositionForAudition;
    coarseSlot.isCoarse = true;
    coarseSlot.coarsePitchbend = snapshot.coarsePitchbend;

    DependentMotorBurstSlot fineSlot;
    fineSlot.owner = this;
    fineSlot.enabled = plan.fine && snapshot.fineValid;
    fineSlot.skipMotorPositionForAudition = skipDependentMotorPositionForAudition;
    fineSlot.isFine = true;
    fineSlot.fineCcValue = snapshot.fineCc;

    DependentMotorBurstSlot noteValueSlot;
    noteValueSlot.owner = this;
    noteValueSlot.enabled = plan.noteValue && snapshot.valid;
    noteValueSlot.skipMotorPositionForAudition = skipDependentMotorPositionForAudition;
    noteValueSlot.isNoteValue = true;
    noteValueSlot.noteValueCc = snapshot.noteValueCc;

    NoteEditFaderMotorTiming::runParallelMotorFaderBursts(coarseSlot, fineSlot, noteValueSlot);
    return true;
}

NOTE_EDIT_MEM void ControlSurfaceManager::publishDependentFaderLatch(Track& track,
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
            // NOTE_EDIT_PROJECTED_STORE_COMPAT: closure normalize on projected store until tasks.md §5.5.
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
NOTE_EDIT_MEM int16_t ControlSurfaceManager::loopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength) {
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

NOTE_EDIT_MEM bool ControlSurfaceManager::sendCoarseFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.coarse = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

NOTE_EDIT_MEM bool ControlSurfaceManager::sendFineFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.fine = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

NOTE_EDIT_MEM bool ControlSurfaceManager::sendNoteValueFaderPosition(Track& track) {
    const NoteEditDependentFaderSnapshot snapshot = buildDependentFaderSnapshotForTrack(track, nullptr);
    NoteEditFaderOutbound::PlanFlags plan{};
    plan.noteValue = true;
    return sendDependentFaderSnapshot(track, plan, snapshot, DependentFaderSendMode::ValueOnly);
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendCoarseFaderMotorNoteOn() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOn(PITCHBEND_START_CHANNEL, MidiConfig::Fader::MOTOR_TRIGGER_NOTE, 127);
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendCoarseFaderMotorNoteOff() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOff(PITCHBEND_START_CHANNEL, MidiConfig::Fader::MOTOR_TRIGGER_NOTE, 0);
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendFineFaderMotorNoteOn() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOn(FINE_CC_CHANNEL, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE, 127);
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendFineFaderMotorNoteOff() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOff(FINE_CC_CHANNEL, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE, 0);
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendNoteValueFaderMotorNoteOn() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOn(NOTE_VALUE_CC_CHANNEL, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE,
                           127);
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendNoteValueFaderMotorNoteOff() {
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        return;
    }
    midiHandler.sendNoteOff(NOTE_VALUE_CC_CHANNEL, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE,
                            0);
}
