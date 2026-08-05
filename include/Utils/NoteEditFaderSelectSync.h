//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef NOTE_EDIT_FADER_SELECT_SYNC_H
#define NOTE_EDIT_FADER_SELECT_SYNC_H

#include <cstdint>
#include <cstdlib>

#include "Utils/MidiMapping.h"
#include "Utils/NoteEditFaderOutboundPlan.h"

namespace NoteEditFaderSelectSync {

inline bool isGeometryEditDriverFader(MidiMapping::FaderType driver) {
    return driver == MidiMapping::FaderType::FADER_COARSE ||
           driver == MidiMapping::FaderType::FADER_FINE ||
           driver == MidiMapping::FaderType::FADER_NOTE_VALUE;
}

/** Block coarse only when F1 physical nav diverges and user is not geometry-driving. */
inline bool shouldBlockCoarseForPendingSelectNavigation(bool physicalTargetDivergent,
                                                        MidiMapping::FaderType currentDriverFader,
                                                        bool geometryDriverActive) {
    if (!physicalTargetDivergent) {
        return false;
    }
    if (geometryDriverActive || isGeometryEditDriverFader(currentDriverFader)) {
        return false;
    }
    return true;
}

inline bool shouldIgnoreSelectFaderEcho(int16_t incomingPitchbend, int16_t lastSentPitchbend,
                                        int16_t tolerance) {
    const int16_t diff = abs(incomingPitchbend - lastSentPitchbend);
    return diff <= tolerance;
}

/** Physical select nav target differs from logical EditorSelection (note or bracket). */
inline bool physicalSelectTargetDivergesFromLogical(NoteId logicalPrimary, uint32_t logicalBracket,
                                                    NoteId physicalPrimary,
                                                    uint32_t physicalBracket) {
    return NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        logicalPrimary, physicalPrimary, logicalBracket, physicalBracket);
}

/** After geometry-driver hold expires, arm relatch only when select was blocked during hold. */
inline bool shouldEnterSelectionRelatchAfterGeometry(bool geometryDriverHoldActive,
                                                     bool selectBlockedDuringGeometryHold,
                                                     bool physicalTargetDivergent,
                                                     bool geometryRelatchConsumed) {
    return selectBlockedDuringGeometryHold && !geometryDriverHoldActive &&
           physicalTargetDivergent && !geometryRelatchConsumed;
}

/** During Selection Relatch, suspend applies while physical target still diverges. */
inline bool shouldSuspendSelectDuringRelatch(bool relatchActive, bool physicalTargetDivergent) {
    return relatchActive && physicalTargetDivergent;
}

/** After relatch motor feedback completes, divergent select is intentional navigation. */
inline bool isIntentionalSelectNavigationDuringRelatch(bool relatchActive,
                                                       bool physicalTargetDivergent,
                                                       bool selectionChanged, uint32_t now,
                                                       uint32_t relatchMotorSentAtMs,
                                                       uint32_t motorFeedbackPeriodMs) {
    if (!relatchActive || !physicalTargetDivergent || !selectionChanged) {
        return false;
    }
    if (relatchMotorSentAtMs == 0) {
        return false;
    }
    return (now - relatchMotorSentAtMs) >= motorFeedbackPeriodMs;
}

/** Geometry hold blocks F1 only when selection did not change (drift wobble). */
inline bool shouldIgnoreGeometryDriverHoldForSelectNavigation(bool geometryDriverHoldActive,
                                                             bool geometryEditContext,
                                                             bool selectionChanged) {
    return geometryDriverHoldActive && geometryEditContext && !selectionChanged;
}

/** Dismiss relatch sync only when F1 has been idle (not scrubbing through logical bracket). */
inline bool shouldDismissRelatchAsSynchronized(bool relatchActive, bool physicalTargetDivergent,
                                               uint32_t now, uint32_t lastSelectFaderInputMs,
                                               uint32_t selectFaderIdleMs) {
    if (!relatchActive || physicalTargetDivergent) {
        return false;
    }
    if (lastSelectFaderInputMs == 0) {
        return true;
    }
    return (now - lastSelectFaderInputMs) >= selectFaderIdleMs;
}

}  // namespace NoteEditFaderSelectSync

#endif
