//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/NoteEditDependentFaderSnapshot.h"

#include <algorithm>
#include <cmath>

#include "Globals.h"
#include "MidiConfig.h"
#include "Utils/NoteEditLengthFaderMapping.h"
#include "Utils/SelectNavigation.h"

namespace {

template <typename T>
T clampValue(T value, T low, T high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

int16_t positionModeLoopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength) {
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
    const int16_t pitchbend = static_cast<int16_t>(
        MidiConfig::Pitchbend::MIN +
        normalizedPos * static_cast<float>(MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
    return static_cast<int16_t>(clampValue(pitchbend, static_cast<int16_t>(MidiConfig::Pitchbend::MIN),
                                           static_cast<int16_t>(MidiConfig::Pitchbend::MAX)));
}

uint8_t lengthEditFineCcFromOffset(int32_t offsetFromAnchor) {
    const int32_t halfRange = static_cast<int32_t>(Config::TICKS_PER_16TH_STEP);
    const int32_t clampedOffset =
        clampValue(offsetFromAnchor, -halfRange, halfRange);
    return static_cast<uint8_t>(
        clampValue(static_cast<int32_t>(64 + clampedOffset), static_cast<int32_t>(0),
                   static_cast<int32_t>(127)));
}

uint8_t fineCcFromRelativeTick(uint32_t relTick, uint32_t referenceStep) {
    const uint32_t referenceStepStartTick = referenceStep * Config::TICKS_PER_16TH_STEP;
    const int32_t offsetFromReferenceStep =
        static_cast<int32_t>(relTick) - static_cast<int32_t>(referenceStepStartTick);
    return static_cast<uint8_t>(
        clampValue(static_cast<int32_t>(64 + offsetFromReferenceStep), static_cast<int32_t>(0),
                   static_cast<int32_t>(127)));
}

uint8_t emptyStepFineCc(uint32_t bracketRelTick) {
    const uint32_t stepStartTick =
        (bracketRelTick / Config::TICKS_PER_16TH_STEP) * Config::TICKS_PER_16TH_STEP;
    const int32_t offsetFromReferenceStep =
        static_cast<int32_t>(bracketRelTick) - static_cast<int32_t>(stepStartTick);
    return static_cast<uint8_t>(
        clampValue(static_cast<int32_t>(64 + offsetFromReferenceStep), static_cast<int32_t>(0),
                   static_cast<int32_t>(127)));
}

}  // namespace

NoteEditDependentFaderSnapshot buildDependentFaderSnapshot(
    const NoteEditDependentFaderBuildInput& input) {
    NoteEditDependentFaderSnapshot snapshot;
    if (input.loopLength == 0) {
        return snapshot;
    }

    const uint32_t loopStartTick = input.loopStartTick % input.loopLength;

    if (input.selectedIdx < 0 && !input.selectTarget.active) {
        if (input.lengthEditingMode) {
            return snapshot;
        }

        const uint32_t anchorTick =
            SelectNavigation::displayPhaseTick(input.selectedTick, input.loopLength);
        snapshot.coarsePitchbend = positionModeLoopTickToCoarsePitchbend(anchorTick, input.loopLength);
        snapshot.coarseValid = true;
        snapshot.fineCc = emptyStepFineCc(anchorTick);
        snapshot.fineValid = true;
        return snapshot;
    }

    uint32_t coarseAnchorTick = 0;
    uint32_t fineRelTick = 0;
    bool haveFineTick = false;

    if (input.selectTarget.active) {
        // absoluteTargetTick is projected-interval phase (slot.relativeTick / selectedTick),
        // not storage — do not pass through noteRelativeTick.
        coarseAnchorTick = SelectNavigation::displayPhaseTick(input.selectTarget.absoluteTargetTick,
                                                              input.loopLength);
        if (input.hasSelectNote) {
            fineRelTick = SelectNavigation::noteRelativeTick(input.selectNoteStartTick,
                                                             loopStartTick, input.loopLength);
            haveFineTick = true;
        } else {
            fineRelTick = coarseAnchorTick;
            haveFineTick = true;
        }
    } else if (input.hasLiveNote) {
        if (input.lengthEditingMode) {
            coarseAnchorTick = SelectNavigation::noteRelativeTick(input.liveEndTick, loopStartTick,
                                                                  input.loopLength);
            const int32_t offsetFromAnchor =
                static_cast<int32_t>(coarseAnchorTick) -
                static_cast<int32_t>(input.lengthFineAnchorEndTick % input.loopLength);
            snapshot.fineCc = lengthEditFineCcFromOffset(offsetFromAnchor);
            snapshot.fineValid = true;
        } else {
            coarseAnchorTick = SelectNavigation::noteRelativeTick(input.liveStartTick,
                                                                    loopStartTick, input.loopLength);
            fineRelTick = coarseAnchorTick;
            haveFineTick = true;
        }
    }

    if (input.lengthEditingMode) {
        snapshot.coarsePitchbend =
            NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(coarseAnchorTick, input.loopLength);
    } else {
        snapshot.coarsePitchbend =
            positionModeLoopTickToCoarsePitchbend(coarseAnchorTick, input.loopLength);
    }
    snapshot.coarseValid = true;

    if (!snapshot.fineValid && haveFineTick) {
        if (input.lengthEditingMode) {
            const int32_t offsetFromAnchor =
                static_cast<int32_t>(fineRelTick) -
                static_cast<int32_t>(input.lengthFineAnchorEndTick % input.loopLength);
            snapshot.fineCc = lengthEditFineCcFromOffset(offsetFromAnchor);
        } else if (input.selectTarget.active && input.hasSelectNote) {
            const uint32_t sixteenthStep = coarseAnchorTick / Config::TICKS_PER_16TH_STEP;
            const uint32_t stepStartTick = sixteenthStep * Config::TICKS_PER_16TH_STEP;
            const int32_t offsetFromReferenceStep =
                static_cast<int32_t>(fineRelTick) - static_cast<int32_t>(stepStartTick);
            snapshot.fineCc = static_cast<uint8_t>(
                clampValue(static_cast<int32_t>(64 + offsetFromReferenceStep), static_cast<int32_t>(0),
                   static_cast<int32_t>(127)));
        } else {
            snapshot.fineCc = fineCcFromRelativeTick(fineRelTick, input.referenceStep);
        }
        snapshot.fineValid = true;
    }

    if (input.selectTarget.active) {
        if (input.selectTarget.noteIdx >= 0 && input.hasSelectNote) {
            snapshot.noteValueCc = input.selectNotePitch;
            snapshot.valid = true;
        }
    } else if (input.hasLiveNote && input.selectedIdx >= 0) {
        snapshot.noteValueCc = input.livePitch;
        snapshot.valid = true;
    }

    return snapshot;
}

namespace NoteEditDependentFaderFeedback {

bool shouldIgnoreStaleLatch(int inboundValue, int lastSentValue, int liveSnapshotValue,
                            int tolerance, bool focusActive, bool snapshotValid) {
    if (!focusActive || !snapshotValid || lastSentValue < 0) {
        return false;
    }
    const int latchDiff = std::abs(inboundValue - lastSentValue);
    const int liveDiff = std::abs(inboundValue - liveSnapshotValue);
    return latchDiff <= tolerance && liveDiff > tolerance;
}

bool motorValueChanged(const NoteEditDependentFaderSnapshot& planned, int16_t priorF2Pitchbend,
                       uint8_t priorFineCc, int priorF4Cc, bool planCoarse, bool planFine,
                       bool planNoteValue) {
    if (planCoarse && planned.coarseValid && planned.coarsePitchbend != priorF2Pitchbend) {
        return true;
    }
    if (planFine && planned.fineValid && planned.fineCc != priorFineCc) {
        return true;
    }
    if (planNoteValue && planned.valid && priorF4Cc >= 0 &&
        static_cast<int>(planned.noteValueCc) != priorF4Cc) {
        return true;
    }
    return false;
}

}  // namespace NoteEditDependentFaderFeedback
