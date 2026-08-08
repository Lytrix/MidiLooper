//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "EditSessionAction.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "Utils/IntervalProjection.h"

class NoteEditCurrentState;

// --- D17 orchestrator (selection + latch; not inside analyze) ---

bool isSelectedNote(NoteId noteId, const EditorSelection& selection);

bool isIntraSelectionPair(NoteId causingNoteId, NoteId targetNoteId,
                          const EditorSelection& selection);

bool geometryChangedThisTick(NoteId causingNoteId, const NoteBaseline& priorLatch,
                             const NoteBaseline& currentSpan);

std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineChangedCausingNotes(
    const EditorSelection& selection, const EditedGeometry& editedGeometry,
    const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId);

/// Single source of truth for which notes one geometry tick may evaluate: the mover's current
/// pitch lane, plus every note the pipeline already hid or shortened under this edit driver.
/// The sticky part matters after a pitch change — a note hidden on the source lane must stay in
/// scope so it can still be restored, which is why the transaction baseline stays full-loop.
/// Candidate pairing and baseline projection both consume this one sorted list, so the two
/// cannot disagree about scope. Without a lane the scope is every note (pre-lane behaviour).
/// Membership is NoteId + pitch lane; the track's output channel is not an identity key.
NoteIdList collectEvaluationScopeNoteIds(const BaselineMap& transactionBaseline,
                                         const MidiEventVec& liveStore, NoteId movingNoteId,
                                         std::optional<uint8_t> overlapPitchLane,
                                         const NoteEditCurrentState* currentState = nullptr);

/// Projects only the notes in `evaluationScope`, plus the mover, which resolve and build need.
BaselineMap projectTransactionBaselineForEvaluationScope(const EditorSelection& selection,
                                                        const BaselineMap& transactionBaseline,
                                                        const NoteIdList& evaluationScope,
                                                        NoteId movingNoteId, uint32_t loopLength,
                                                        int32_t originTick);

/// Insert missing evaluation-scope spans into focus.baselineMap from the live store (D19).
void ensureBaselineMapEntriesForEvaluationScope(NoteEditFocus& focus,
                                                const NoteIdList& evaluationScope,
                                                const MidiEventVec& liveStore, uint8_t channel,
                                                const NoteEditCurrentState* currentState = nullptr);

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

/// Overlap analyze uses storage baselineMap ticks. Session-moved notes keep committed start
/// in baselineMap while live store holds the moved span — overlay live for analyze only.
BaselineMap overlayAnalysisBaselineForSessionMovedOverlaps(const BaselineMap& storageBaseline,
                                                           NoteId movingNoteId,
                                                           const MidiEventVec& liveStore,
                                                           uint8_t channel, uint32_t loopLength,
                                                           const NoteEditCurrentState* currentState =
                                                               nullptr,
                                                           const NoteBaseline* causingSpan =
                                                               nullptr);

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
/// `originTick` is the shared edit projection origin for the resolve pass (projected causing-note
/// storage start) so wrapped spans pick aligned k-shift copies.
NoteBaseline projectNoteBaselineForEditAnalysis(const EditorSelection& selection,
                                                const NoteBaseline& baseline, NoteId noteId,
                                                uint32_t loopLength, int32_t originTick);
