//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "NoteEditFocus.h"
#include "NoteEditCurrentState.h"
#include "EditSessionAction.h"
#include "NoteEditSessionState.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

/// Session phase for a participating note — evidence-backed mapping from
/// `NoteEditPresenceType` and span relationship. Not a parallel FSM yet; derived read model
/// for migration (contracts plan Stage 3).
enum class ParticipatingNotePhase : uint8_t {
  Visible,
  Hidden,
  Deleted,
  Added,
};

struct ParticipatingNoteState {
  NoteId noteId = kInvalidNoteId;
  ParticipatingNotePhase phase = ParticipatingNotePhase::Visible;
  NoteBaseline currentSpan{};
  NoteBaseline committedSpan{};
  /// `currentSpan` is a same-start shortened tail vs `committedSpan` (overlap inventory mask).
  bool shortenedVsCommitted = false;
  /// Prior macro sealed a visible overlap shorten into `committedSpan`.
  bool visibleOverlapShortenSealed = false;
  /// Whether this participant may appear in projecting session store / selectable inventory.
  bool projectsToStore = false;
};

template <typename T>
using ParticipatingNoteMapAllocator =
    ExternalMemoryFirstAllocator<std::pair<const NoteId, T>>;

using ParticipatingNoteMap =
    std::unordered_map<NoteId, ParticipatingNoteState, NoteIdHash, std::equal_to<NoteId>,
                       ParticipatingNoteMapAllocator<ParticipatingNoteState>>;

/// Explicit edit-session participant view — conceptual `NoteEditSession` in the contracts plan.
/// Built from `NoteEditCurrentState` + `EditorSelection`; does not own mutable session state yet.
struct ParticipatingNoteSession {
  NoteId primaryNoteId = kInvalidNoteId;
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> selectedNoteIds;
  ParticipatingNoteMap participatingNotes;
};

struct ParticipatingNoteInvariantResult {
  bool passed = true;
  bool duplicateNoteIds = false;
  bool missingCommittedSpan = false;
  bool driverNotProjecting = false;
  bool selectedNotInParticipants = false;
};

struct ParticipatingSessionInvariantResult {
  bool passed = true;
  bool primaryMissingFromParticipants = false;
  bool selectedOutsideParticipants = false;
  bool actionTargetWouldDependOnProjection = false;
};

ParticipatingNotePhase participatingPhaseFromPresence(NoteEditPresenceType presence);

bool participatingNoteShortenedVsCommitted(const NoteBaseline& current,
                                           const NoteBaseline& committed);

ParticipatingNoteState buildParticipatingNoteState(const NoteEditCurrentNoteState& row);

ParticipatingNoteSession buildParticipatingNoteSession(const EditorSelection& selection,
                                                       const NoteEditCurrentState& currentState);

ParticipatingNoteInvariantResult verifyParticipatingNoteInvariants(
    const ParticipatingNoteState& state);

ParticipatingSessionInvariantResult verifyParticipatingSessionInvariants(
    const ParticipatingNoteSession& session);

/// Geometry-derived overlap participation: Hidden/Deleted or currentSpan ≠ committedSpan.
/// After `clearChangedOverlapParticipationWhenInteractionCleared`, this can remain true while
/// the transitional latch is false — latch remains pipeline authority until §11 step 5 encodes
/// sticky end-of-participation in current/participating state (design session).
bool currentStateRowIsOverlapParticipant(const NoteEditCurrentNoteState& row);

NoteIdList collectOverlapParticipantNoteIdsFromCurrentState(
    const NoteEditCurrentState& currentState, NoteId movingNoteId);

/// Transitional latch membership (`NoteEditFocus::changedOverlapNoteIds`).
bool overlapParticipationLatchActive(const NoteEditFocus& focus, NoteId noteId);

/// True when geometry still differs from committed but the latch was cleared (sticky forget).
bool overlapParticipationLatchClearedWhileGeometryDiffers(const NoteEditFocus& focus,
                                                          const NoteEditCurrentState& currentState,
                                                          NoteId noteId);

/// Same-start shortened tail or head-trimmed span that may leave-restore to committed baseline.
bool participatingSpanQualifiesForOverlapLeaveRestore(const NoteBaseline& committed,
                                                      const NoteBaseline& current);

/// Session-unsealed Hidden overlap participant (not mover) that may leave-restore when cleared.
/// Sealed Deleted after deselect does not qualify (022849 E2).
bool participatingNoteQualifiesForLeaveRestoreTarget(const ParticipatingNoteState& participant,
                                                     NoteId movingNoteId);

/// Visible shortened overlap after macro commit — restore currentSpan to sealed committedSpan
/// when mover clears closure (second+ overlap pass). Never restore to pre-shorten storage baseline.
bool participatingNoteCommittedSpanSealedBelowStorageBaseline(
    const ParticipatingNoteState& participant, const NoteBaseline& storageBaseline);

bool participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(
    const ParticipatingNoteState& participant, const NoteBaseline& storageBaseline,
    NoteId movingNoteId);

/// Interaction overlay uses committed geometry while overlap closure is active (includes visible
/// shortened tails). Leave-restore RestoreNote remains session-unsealed Hidden only.
bool participatingNoteUsesCommittedBaselineDuringOverlapClosure(
    const ParticipatingNoteState& participant, NoteId movingNoteId);

/// Leave-restore must emit the session committed span — not a live-store shortened stub.
bool participatingNoteNeedsFullCommittedLeaveRestore(const ParticipatingNoteState& participant);

NoteBaseline participatingLeaveRestoreCommittedSpan(const ParticipatingNoteState& participant);

/// Inclusive linear overlap on the same pitch lane (edit-session analyze convention).
bool participatingSpansOverlapInclusive(const NoteBaseline& left, const NoteBaseline& right);

/// Mover span still intersects the participant's committed overlap closure.
bool participatingNoteOverlapClosureActive(const ParticipatingNoteState& participant,
                                             const NoteBaseline& causingSpan);

/// Leave-restore may use committed/session baseline — only when closure is cleared.
bool participatingNoteOverlapInteractionCleared(const ParticipatingNoteState& participant,
                                                const NoteBaseline& causingSpan);

/// Visible same-start shorten tail while mover still intersects committed overlap closure.
/// Defer leave-restore and macro-commit of transient bridge geometry (Stage 7.5).
bool participatingNoteVisibleOverlapTailInProgress(const ParticipatingNoteState& participant,
                                                   const NoteBaseline& causingSpan);

/// Selectable inventory mask for visible shortened overlap tails — active only while the
/// overlap mover is selected and the tail session is still in progress.
bool visibleShortenedOverlapTailInventoryMasked(const NoteEditCurrentNoteState& row,
                                                const NoteEditFocus& focus,
                                                int selectedNoteIdx);

const NoteBaseline* findCausingSpanForMover(NoteId movingNoteId,
                                            const EditedGeometry& editedGeometry);
