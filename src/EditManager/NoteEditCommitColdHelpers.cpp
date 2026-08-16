//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditManagerInternal.h"

#include <algorithm>

#include "Logger.h"
#include "Utils/NoteUtils.h"

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#endif

using DisplayNote = NoteUtils::DisplayNote;

namespace {

const char* editActionTypeLabel(EditActionType actionType) {
    switch (actionType) {
        case EditActionType::Create:
            return "Create";
        case EditActionType::Update:
            return "Update";
        case EditActionType::Delete:
            return "Delete";
    }
    return "Unknown";
}

const char* editPropertyTypeLabel(EditPropertyType propertyType) {
    switch (propertyType) {
        case EditPropertyType::None:
            return "None";
        case EditPropertyType::Pitch:
            return "Pitch";
        case EditPropertyType::Length:
            return "Length";
        case EditPropertyType::NoteRange:
            return "NoteRange";
        case EditPropertyType::Velocity:
            return "Velocity";
        case EditPropertyType::Tick:
            return "Tick";
        case EditPropertyType::Value:
            return "Value";
    }
    return "Unknown";
}

#if defined(SESSION_CAPTURE)
bool addedEventsEqual(const MidiEventVec& lhs, const MidiEventVec& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        const MidiEvent& a = lhs[i];
        const MidiEvent& b = rhs[i];
        if (a.type != b.type || a.tick != b.tick || a.channel != b.channel ||
            a.noteId != b.noteId || a.data.noteData.note != b.data.noteData.note ||
            a.data.noteData.velocity != b.data.noteData.velocity) {
            return false;
        }
    }
    return true;
}

bool editPassRowsEqualForParity(const EditPass& lhs, const EditPass& rhs) {
    return lhs.passType == rhs.passType && lhs.actionType == rhs.actionType &&
           lhs.propertyType == rhs.propertyType && lhs.targetNoteId == rhs.targetNoteId &&
           lhs.startTick == rhs.startTick && lhs.endTick == rhs.endTick &&
           lhs.pitch == rhs.pitch && lhs.velocity == rhs.velocity &&
           addedEventsEqual(lhs.addedEvents, rhs.addedEvents);
}
#endif

}  // namespace

#if defined(SESSION_CAPTURE)
EDIT_MANAGER_IMPL_MEM void logGeomApplyUndo(bool ok, uint32_t elapsedUs, NoteEditKind kind) {
    logger.info("#CAP,%lu,GEOM_APPLY,undo,%s,%lu,%u,0", static_cast<unsigned long>(micros()),
                ok ? "ok" : "fail", static_cast<unsigned long>(elapsedUs),
                static_cast<unsigned>(kind));
}

EDIT_MANAGER_IMPL_MEM void logGeomApplyFocus(uint32_t elapsedUs, NoteEditKind kind) {
    logger.info("#CAP,%lu,GEOM_APPLY,focus,%lu,%u,0,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(elapsedUs), static_cast<unsigned>(kind));
}

EDIT_MANAGER_IMPL_MEM void logGeomApplyResolve(uint32_t elapsedUs, bool applied, NoteEditKind kind) {
    logger.info("#CAP,%lu,GEOM_APPLY,resolve,%lu,%u,%u,0", static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(elapsedUs), applied ? 1u : 0u,
                static_cast<unsigned>(kind));
}

EDIT_MANAGER_IMPL_MEM void logGeomApplyPhase(const char* phase, uint32_t elapsedUs, uint32_t extra0,
                                             uint32_t extra1) {
    logger.info("#CAP,%lu,GEOM_APPLY,phase,%s,%lu,%u,%u", static_cast<unsigned long>(micros()),
                phase, static_cast<unsigned long>(elapsedUs), extra0, extra1);
}
#endif

EDIT_MANAGER_IMPL_MEM EditPassIdList collectEditPassIdsPendingDurableCheckpoint(
    const EditPassIdList& currentIds, const EditPassIdList& checkpointedIds) {
    EditPassIdList pending;
    for (const EditPassId id : currentIds) {
        bool alreadyCheckpointed = false;
        for (const EditPassId checkpointed : checkpointedIds) {
            if (checkpointed == id) {
                alreadyCheckpointed = true;
                break;
            }
        }
        if (!alreadyCheckpointed) {
            pending.push_back(id);
        }
    }
    return pending;
}

EDIT_MANAGER_IMPL_MEM void markOverlapDeleteRowsEmitted(NoteEditFocus& focus, const EditPassVec& rows) {
    for (const EditPass& row : rows) {
        if (row.actionType != EditActionType::Delete) {
            continue;
        }
        if (OverlapNote* entry = findOverlapNoteEntry(focus, row.targetNoteId)) {
            entry->preCommitEmitted = true;
        }
    }
}

EDIT_MANAGER_IMPL_MEM void clearCommittedOverlapScratchExceptHidden(NoteEditFocus& focus) {
    for (auto it = focus.overlapNotes.begin(); it != focus.overlapNotes.end();) {
        if (it->second.state == OverlapNoteStoreState::Hidden) {
            ++it;
        } else {
            it = focus.overlapNotes.erase(it);
        }
    }
}

EDIT_MANAGER_IMPL_MEM NoteIdList collectCommittedOverlapDeleteIds(const NoteEditFocus& focus,
                                                                  const EditPassVec& rows) {
    NoteIdList noteIds;
    for (const EditPass& row : rows) {
        if (row.actionType != EditActionType::Delete || row.targetNoteId == kInvalidNoteId ||
            row.targetNoteId == focus.movingNoteId) {
            continue;
        }
        if (std::find(noteIds.begin(), noteIds.end(), row.targetNoteId) == noteIds.end()) {
            noteIds.push_back(row.targetNoteId);
        }
    }
    return noteIds;
}

EDIT_MANAGER_IMPL_MEM std::vector<std::pair<NoteId, NoteBaseline>> collectCommittedOverlapUpdateBaselines(
    const NoteEditFocus& focus, const EditPassVec& rows) {
    std::vector<std::pair<NoteId, NoteBaseline>> updates;
    for (const EditPass& row : rows) {
        if (row.actionType != EditActionType::Update || row.targetNoteId == kInvalidNoteId ||
            row.targetNoteId == focus.movingNoteId) {
            continue;
        }
        if (row.propertyType != EditPropertyType::Length &&
            row.propertyType != EditPropertyType::NoteRange) {
            continue;
        }
        const auto baselineIt = focus.baselineMap.find(row.targetNoteId);
        if (baselineIt == focus.baselineMap.end()) {
            continue;
        }
        NoteBaseline committedBaseline = baselineIt->second;
        committedBaseline.startTick = row.startTick;
        committedBaseline.endTick = row.endTick;
        updates.push_back({row.targetNoteId, committedBaseline});
    }
    return updates;
}

EDIT_MANAGER_IMPL_MEM void logChangeLengthCommitTrace(const char* stage, const SessionMidiEventVec& flat,
                                                      uint32_t loopLength, uint8_t homePitch,
                                                      uint32_t homeStart) {
    const std::vector<DisplayNote> notes = NoteUtils::reconstructNotes(flat, loopLength, false);
    for (const DisplayNote& n : notes) {
        if (n.note == homePitch && n.startTick == homeStart) {
            logger.log(CAT_TRACK, LOG_INFO,
                       "commitEditAction %s: M%d start=%lu end=%lu flatEvents=%u", stage,
                       static_cast<unsigned>(homePitch), static_cast<unsigned long>(n.startTick),
                       static_cast<unsigned long>(n.endTick), static_cast<unsigned>(flat.size()));
            return;
        }
    }
    logger.log(CAT_TRACK, LOG_INFO, "commitEditAction %s: M%d@%lu missing in recon flatEvents=%u", stage,
               static_cast<unsigned>(homePitch), static_cast<unsigned long>(homeStart),
               static_cast<unsigned>(flat.size()));
}

EDIT_MANAGER_IMPL_MEM void logChangeLengthCommitTrace(const char* stage, const MidiEventVec& flat,
                                                      uint32_t loopLength, uint8_t homePitch,
                                                      uint32_t homeStart) {
    const std::vector<DisplayNote> notes = NoteUtils::reconstructNotes(flat, loopLength, false);
    for (const DisplayNote& n : notes) {
        if (n.note == homePitch && n.startTick == homeStart) {
            logger.log(CAT_TRACK, LOG_INFO,
                       "commitEditAction %s: M%d start=%lu end=%lu flatEvents=%u", stage,
                       static_cast<unsigned>(homePitch), static_cast<unsigned long>(n.startTick),
                       static_cast<unsigned long>(n.endTick), static_cast<unsigned>(flat.size()));
            return;
        }
    }
    logger.log(CAT_TRACK, LOG_INFO, "commitEditAction %s: M%d@%lu missing in recon flatEvents=%u", stage,
               static_cast<unsigned>(homePitch), static_cast<unsigned long>(homeStart),
               static_cast<unsigned>(flat.size()));
}

EDIT_MANAGER_IMPL_MEM void logPreCommitEditPassRows(const NoteEditFocus& focus, const EditPassVec& rows,
                                                    bool fromApplyOwned) {
#if defined(SESSION_CAPTURE)
    for (size_t index = 0; index < rows.size(); ++index) {
        const EditPass& row = rows[index];
        const char* source = fromApplyOwned ? "apply_owned"
                            : (row.targetNoteId != kInvalidNoteId &&
                               row.targetNoteId == focus.movingNoteId)
                                  ? "mover_focus"
                                  : "overlap_baseline_diff";
        logger.log(CAT_TRACK, LOG_INFO,
                   "NOTE_EDIT pre-commit row: idx=%u source=%s targetNoteId=%lu "
                   "action=%s property=%s start=%lu end=%lu pitch=%u moverNoteId=%lu",
                   static_cast<unsigned>(index), source,
                   static_cast<unsigned long>(row.targetNoteId),
                   editActionTypeLabel(row.actionType), editPropertyTypeLabel(row.propertyType),
                   static_cast<unsigned long>(row.startTick),
                   static_cast<unsigned long>(row.endTick), static_cast<unsigned>(row.pitch),
                   static_cast<unsigned long>(focus.movingNoteId));
    }
#endif
}

#if defined(SESSION_CAPTURE)
EDIT_MANAGER_IMPL_MEM void logApplyOwnedCommitParity(const EditPassVec& canonicalRows,
                                                     const EditPassVec& comparedRows,
                                                     const char* comparedSource) {
    int firstMismatch = -1;
    const size_t sharedCount = std::min(canonicalRows.size(), comparedRows.size());
    for (size_t i = 0; i < sharedCount; ++i) {
        if (!editPassRowsEqualForParity(canonicalRows[i], comparedRows[i])) {
            firstMismatch = static_cast<int>(i);
            break;
        }
    }
    if (firstMismatch < 0 && canonicalRows.size() != comparedRows.size()) {
        firstMismatch = static_cast<int>(sharedCount);
    }
    if (firstMismatch < 0) {
        logger.log(CAT_TRACK, LOG_INFO, "NOTE_EDIT commit parity ok canonical=%u %s=%u",
                   static_cast<unsigned>(canonicalRows.size()), comparedSource,
                   static_cast<unsigned>(comparedRows.size()));
        return;
    }
    logger.log(CAT_TRACK, LOG_WARNING,
               "NOTE_EDIT commit parity mismatch canonical=%u %s=%u first=%d",
               static_cast<unsigned>(canonicalRows.size()), comparedSource,
               static_cast<unsigned>(comparedRows.size()), firstMismatch);
}
#endif

EDIT_MANAGER_IMPL_MEM void materializePassesExcludingEditPasses(const Loop& loop,
                                                                const EditPassIdList& editPassIds,
                                                                MidiEventVec& out) {
    loop.materializeExcludingEditPassIds(editPassIds, out);
}
