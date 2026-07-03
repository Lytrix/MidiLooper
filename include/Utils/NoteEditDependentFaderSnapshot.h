//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef NOTE_EDIT_DEPENDENT_FADER_SNAPSHOT_H
#define NOTE_EDIT_DEPENDENT_FADER_SNAPSHOT_H

#include <cstdint>

struct NoteEditDependentFaderSnapshot {
    int16_t coarsePitchbend = 0;
    uint8_t fineCc = 64;
    uint8_t noteValueCc = 0;
    bool coarseValid = false;
    bool fineValid = false;
    bool valid = false;
};

struct NoteEditDependentFaderSelectTarget {
    bool active = false;
    uint32_t absoluteTargetTick = 0;
    int noteIdx = -1;
};

struct NoteEditDependentFaderBuildInput {
    uint32_t loopLength = 0;
    uint32_t loopStartTick = 0;
    int selectedIdx = -1;
    uint32_t bracketTick = 0;
    bool lengthEditingMode = false;
    uint32_t lengthFineAnchorEndTick = 0;
    uint32_t referenceStep = 0;
    bool hasLiveNote = false;
    uint32_t liveStartTick = 0;
    uint32_t liveEndTick = 0;
    uint8_t livePitch = 0;
    NoteEditDependentFaderSelectTarget selectTarget{};
    uint32_t selectNoteStartTick = 0;
    uint8_t selectNotePitch = 0;
    bool hasSelectNote = false;
};

enum class DependentFaderSendMode { ValueOnly, ValueAndMotor };

NoteEditDependentFaderSnapshot buildDependentFaderSnapshot(
    const NoteEditDependentFaderBuildInput& input);

namespace NoteEditDependentFaderFeedback {

bool shouldIgnoreStaleLatch(int inboundValue, int lastSentValue, int liveSnapshotValue,
                            int tolerance, bool focusActive, bool snapshotValid);

bool motorValueChanged(const NoteEditDependentFaderSnapshot& planned, int16_t priorF2Pitchbend,
                       uint8_t priorFineCc, int priorF4Cc, bool planCoarse, bool planFine,
                       bool planNoteValue);

}  // namespace NoteEditDependentFaderFeedback

#endif  // NOTE_EDIT_DEPENDENT_FADER_SNAPSHOT_H
