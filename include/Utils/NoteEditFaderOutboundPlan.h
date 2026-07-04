//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "EditPass.h"

#include "MidiEvent.h"

namespace NoteEditFaderOutbound {

enum class Trigger : uint8_t {
    None = 0,
    SessionOpen,
    NoteSelectDependent,
    NoteSelectWithFader1,
    LengthModeEnter,
    LengthModeExit,
    Fader1BracketOnly,
};

enum class Step : uint8_t {
    Idle = 0,
    SendFader1Bracket,
    WaitFader1Echo,
    SendCoarse,
    SendFine,
    SendNoteValue,
    Done,
};

struct PlanFlags {
    bool fader1 = false;
    bool waitFader1Echo = false;
    bool coarse = false;
    bool fine = false;
    bool noteValue = false;
};

constexpr uint32_t kOutboundWatchdogMs = 5000;
constexpr uint32_t kFader1EchoWaitCapMs = 1600;
/** Capture/log only: pair ch13 motor acks this many ms after MO notegate (host recording alignment). */
constexpr uint32_t kCh13AckCorrelationWindowMs = 11;

inline PlanFlags planForTrigger(Trigger trigger) {
    PlanFlags plan;
    switch (trigger) {
        case Trigger::SessionOpen:
            plan.fader1 = true;
            plan.waitFader1Echo = true;
            plan.coarse = true;
            plan.fine = true;
            plan.noteValue = true;
            break;
        case Trigger::NoteSelectWithFader1:
            plan.fader1 = true;
            plan.coarse = true;
            plan.fine = true;
            plan.noteValue = true;
            break;
        case Trigger::NoteSelectDependent:
            plan.coarse = true;
            plan.fine = true;
            plan.noteValue = true;
            break;
        case Trigger::LengthModeEnter:
            plan.coarse = true;
            plan.fine = true;
            break;
        case Trigger::LengthModeExit:
            plan.fader1 = true;
            plan.coarse = true;
            plan.fine = true;
            plan.noteValue = true;
            break;
        case Trigger::Fader1BracketOnly:
            plan.fader1 = true;
            break;
        default:
            break;
    }
    return plan;
}

inline PlanFlags planForSelectDependent(bool needsPositionRefresh, bool needsPitchRefresh) {
    PlanFlags plan;
    if (needsPositionRefresh) {
        plan.coarse = true;
        plan.fine = true;
    }
    if (needsPitchRefresh) {
        plan.noteValue = true;
    }
    return plan;
}

inline PlanFlags planForSelectDependentFromDelta(uint32_t priorBracketTick, int priorNoteIdx,
                                               uint32_t newBracketTick, int newNoteIdx) {
    (void)priorNoteIdx;
    if (newNoteIdx < 0) {
        return {};
    }
    if (newBracketTick != priorBracketTick) {
        return planForSelectDependent(true, true);
    }
    if (newNoteIdx != priorNoteIdx) {
        return planForSelectDependent(false, true);
    }
    return {};
}

inline PlanFlags planForSelectDependentFromNoteIdChange(NoteId priorPrimary, NoteId newPrimary,
                                                      uint32_t priorBracketTick,
                                                      uint32_t newBracketTick) {
    if (newPrimary == kInvalidNoteId) {
        return {};
    }
    if (newBracketTick != priorBracketTick) {
        return planForSelectDependent(true, true);
    }
    if (priorPrimary != newPrimary) {
        return planForSelectDependent(true, true);
    }
    return {};
}

/** F2/F3/F4 geometry driver: refresh F1 bracket motor when selection bracket moves. */
inline PlanFlags planForGeometryDriverMotorSync(uint32_t priorBracketTick, uint32_t newBracketTick) {
    PlanFlags plan;
    if (newBracketTick != priorBracketTick) {
        plan.fader1 = true;
    }
    return plan;
}

/** Select-driver and geometry-driver motor plans must not overlap (symmetric queues). */
inline bool motorSyncPlansAreDirectionIsolated(const PlanFlags& selectDriverPlan,
                                               const PlanFlags& geometryDriverPlan) {
    if (geometryDriverPlan.coarse || geometryDriverPlan.fine || geometryDriverPlan.noteValue) {
        return false;
    }
    if (selectDriverPlan.fader1) {
        return false;
    }
    return true;
}

inline bool shouldApplySelectionOnNoteIdChange(NoteId priorPrimary, NoteId newPrimary,
                                               uint32_t priorBracketTick,
                                               uint32_t newBracketTick) {
    if (priorBracketTick != newBracketTick) {
        return true;
    }
    return priorPrimary != newPrimary;
}

inline bool shouldPreemptActivePipeline(Trigger incoming) {
    return incoming == Trigger::SessionOpen || incoming == Trigger::LengthModeEnter ||
           incoming == Trigger::LengthModeExit;
}

inline bool shouldCoalesceDependentRefresh(Trigger incoming, Step activeStep) {
    if (incoming != Trigger::NoteSelectWithFader1) {
        return false;
    }
    return activeStep != Step::Idle && activeStep != Step::Done;
}

inline bool isChannel15OutboundStep(Step step) {
    switch (step) {
        case Step::SendCoarse:
        case Step::SendFine:
        case Step::SendNoteValue:
            return true;
        default:
            return false;
    }
}

inline bool shouldRestartDependentPipelineOnSelectionChange(Trigger incoming, Trigger activeTrigger,
                                                            Step activeStep) {
    if (incoming != Trigger::NoteSelectDependent) {
        return false;
    }
    if (activeTrigger != Trigger::NoteSelectDependent) {
        return false;
    }
    return isChannel15OutboundStep(activeStep);
}

inline bool shouldApplySelectionOnSlotChange(int priorSlotIndex, int newSlotIndex) {
    return newSlotIndex >= 0 && priorSlotIndex != newSlotIndex;
}

inline bool shouldApplySelectionOnNoteChange(int priorNoteIdx, int newNoteIdx) {
    return newNoteIdx >= 0 && priorNoteIdx != newNoteIdx;
}

inline bool shouldApplySelectionOnNavChange(int priorSlotIndex, int priorNoteIdx, int newSlotIndex,
                                            int newNoteIdx) {
    if (newSlotIndex < 0) {
        return false;
    }
    return priorSlotIndex != newSlotIndex || priorNoteIdx != newNoteIdx;
}

inline bool shouldApplySelectionOnTargetChange(uint32_t targetBracketTick, int targetNoteIdx,
                                               uint32_t currentBracketTick, int currentNoteIdx) {
    return targetBracketTick != currentBracketTick || targetNoteIdx != currentNoteIdx;
}

inline Step nextEnabledStep(Step step, const PlanFlags& plan) {
    switch (step) {
        case Step::Idle:
            if (plan.fader1) {
                return Step::SendFader1Bracket;
            }
            if (plan.coarse || plan.fine || plan.noteValue) {
                return Step::SendCoarse;
            }
            return Step::Done;
        case Step::SendFader1Bracket:
            if (plan.waitFader1Echo) {
                return Step::WaitFader1Echo;
            }
            return nextEnabledStep(Step::WaitFader1Echo, plan);
        case Step::WaitFader1Echo:
            if (plan.coarse || plan.fine || plan.noteValue) {
                return Step::SendCoarse;
            }
            return Step::Done;
        case Step::SendCoarse:
            return Step::Done;
        case Step::SendFine:
            if (plan.noteValue) {
                return Step::SendNoteValue;
            }
            return Step::Done;
        case Step::SendNoteValue:
        case Step::Done:
            return Step::Done;
    }
    return Step::Done;
}

inline Step advanceOutboundStep(Step step, const PlanFlags& plan) {
    if (step == Step::Done || step == Step::Idle) {
        return Step::Done;
    }
    return nextEnabledStep(step, plan);
}

}  // namespace NoteEditFaderOutbound
