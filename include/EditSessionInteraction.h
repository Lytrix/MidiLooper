//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "EditSessionAction.h"
#include "NoteEditFocus.h"
#include "Utils/IntervalProjection.h"

// --- D17 orchestrator (selection + latch; not inside analyze) ---

bool isSelectedNote(NoteId noteId, const EditorSelection& selection);

bool isIntraSelectionPair(NoteId causingNoteId, NoteId targetNoteId,
                          const EditorSelection& selection);

bool geometryChangedThisTick(NoteId causingNoteId, const NoteBaseline& priorLatch,
                             const NoteBaseline& currentSpan);

std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineChangedCausingNotes(
    const EditorSelection& selection, const EditedGeometry& editedGeometry,
    const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId);

std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>>
determineEligiblePairs(const EditorSelection& selection,
                       const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>&
                           changedCausingNoteIds,
                       const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>&
                           candidateTargetNoteIds);

// --- Pure geometry (no store mutation) ---

bool linearSpansOverlapForAnalysis(uint32_t causingStart, uint32_t causingEnd,
                                   uint32_t targetStart, uint32_t targetEnd);

InteractionType classifyEditSessionInteraction(uint32_t causingStart, uint32_t causingEnd,
                                               uint32_t targetStart, uint32_t targetEnd);

std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
analyzeEditSessionInteractions(
    const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>>&
        eligiblePairs,
    const EditedGeometry& editedGeometry, const BaselineMap& transactionBaseline);

EditSessionInteractionsByTarget groupEditSessionInteractionsByTarget(
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>&
        interactions);

uint32_t computeShortenedEndTick(const EditSessionInteraction& interaction, uint32_t loopLength);

/// D20 — linearize one note span via Edit projection before analyze.
NoteBaseline projectNoteBaselineForEditAnalysis(const EditorSelection& selection,
                                                const NoteBaseline& baseline, NoteId noteId,
                                                uint32_t loopLength);
