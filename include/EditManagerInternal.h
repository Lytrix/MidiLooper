//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <utility>
#include <vector>

#include "EditPass.h"
#include "Loop.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteEditMem.h"

#if defined(__IMXRT1062__)
#define EDIT_MANAGER_IMPL_MEM NOTE_EDIT_MEM
#else
#define EDIT_MANAGER_IMPL_MEM
#endif

EDIT_MANAGER_IMPL_MEM EditPassIdList collectEditPassIdsPendingDurableCheckpoint(
    const EditPassIdList& currentIds, const EditPassIdList& checkpointedIds);

EDIT_MANAGER_IMPL_MEM void markOverlapDeleteRowsEmitted(NoteEditFocus& focus, const EditPassVec& rows);

EDIT_MANAGER_IMPL_MEM void clearCommittedOverlapScratchExceptHidden(NoteEditFocus& focus);

EDIT_MANAGER_IMPL_MEM NoteIdList collectCommittedOverlapDeleteIds(const NoteEditFocus& focus,
                                                                  const EditPassVec& rows);

EDIT_MANAGER_IMPL_MEM std::vector<std::pair<NoteId, NoteBaseline>> collectCommittedOverlapUpdateBaselines(
    const NoteEditFocus& focus, const EditPassVec& rows);

EDIT_MANAGER_IMPL_MEM void logChangeLengthCommitTrace(const char* stage, const SessionMidiEventVec& flat,
                                                      uint32_t loopLength, uint8_t homePitch,
                                                      uint32_t homeStart);

EDIT_MANAGER_IMPL_MEM void logChangeLengthCommitTrace(const char* stage, const MidiEventVec& flat,
                                                      uint32_t loopLength, uint8_t homePitch,
                                                      uint32_t homeStart);

EDIT_MANAGER_IMPL_MEM void logPreCommitEditPassRows(const NoteEditFocus& focus, const EditPassVec& rows,
                                                    bool fromApplyOwned);

#if defined(SESSION_CAPTURE)
EDIT_MANAGER_IMPL_MEM void logGeomApplyUndo(bool ok, uint32_t elapsedUs, NoteEditKind kind);

EDIT_MANAGER_IMPL_MEM void logGeomApplyFocus(uint32_t elapsedUs, NoteEditKind kind);

EDIT_MANAGER_IMPL_MEM void logGeomApplyResolve(uint32_t elapsedUs, bool applied, NoteEditKind kind);

EDIT_MANAGER_IMPL_MEM void logGeomApplyPhase(const char* phase, uint32_t elapsedUs, uint32_t extra0,
                                             uint32_t extra1);

EDIT_MANAGER_IMPL_MEM void logApplyOwnedCommitParity(const EditPassVec& canonicalRows,
                                                     const EditPassVec& comparedRows,
                                                     const char* comparedSource);
#endif

EDIT_MANAGER_IMPL_MEM void materializePassesExcludingEditPasses(const Loop& loop,
                                                                const EditPassIdList& editPassIds,
                                                                MidiEventVec& out);
