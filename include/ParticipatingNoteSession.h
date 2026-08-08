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

/// Session read model for a participating note (§12 R1–R3). Visibility, lifecycle, and geometry
/// predicates are explicit; row storage still uses `NoteEditPresenceType` on `NoteEditCurrentNoteState`.
struct ParticipatingNoteState {
  NoteId noteId = kInvalidNoteId;
  NoteBaseline currentSpan{};
  NoteBaseline committedSpan{};
  /// Sticky end-of-participation from `NoteEditCurrentNoteState` (§11 step 5.3).
  NoteEditOverlapParticipationType overlapParticipation =
      NoteEditOverlapParticipationType::Active;
  /// Prior macro sealed a visible overlap shorten into `committedSpan`.
  bool visibleOverlapShortenSealed = false;
  /// Explicit projection eligibility (§12 R1). When false, display projection emits no row.
  bool visible = true;
  /// Mutation/commit lifecycle (§12 R2). Independent from `visible`.
  NoteEditLifecycleType lifecycle = NoteEditLifecycleType::Existing;
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

ParticipatingNoteSession buildParticipatingNoteSession(const EditorSelection& selection,
                                                       const NoteEditCurrentState& currentState);
/// Hidden and Deleted do not. Implementation maps from `NoteEditPresenceType` until R5.
bool currentStateRowIsVisible(const NoteEditCurrentNoteState& row);

/// Participant-boundary visibility query (§12 R1).
bool participatingNoteIsVisible(const ParticipatingNoteState& state);

/// Lifecycle authority for current-state rows (§12 R2). Maps from `NoteEditPresenceType` until R5.
NoteEditLifecycleType currentStateRowLifecycle(const NoteEditCurrentNoteState& row);

bool currentStateRowLifecycleIsDeleted(const NoteEditCurrentNoteState& row);

bool currentStateRowLifecycleIsAdded(const NoteEditCurrentNoteState& row);

/// Participant-boundary lifecycle query (§12 R2).
NoteEditLifecycleType participatingNoteLifecycle(const ParticipatingNoteState& state);

/// Visible + Existing lifecycle — legacy `NoteEditPresenceType::Visible` semantics (§12 R4).
bool currentStateRowIsExistingAndVisible(const NoteEditCurrentNoteState& row);

bool participatingNoteIsExistingAndVisible(const ParticipatingNoteState& state);

/// Span geometry predicates (§12 R3) — derived from authoritative spans only.
uint32_t participatingSpanDurationTicks(const NoteBaseline& span);

bool participatingSpanIsRightTailShortened(const NoteBaseline& current,
                                           const NoteBaseline& committed);

bool participatingSpanIsMoved(const NoteBaseline& current, const NoteBaseline& committed);

bool participatingSpanHasShorterDuration(const NoteBaseline& current,
                                         const NoteBaseline& committed);

bool participatingSpanHasOriginalLength(const NoteBaseline& current,
                                        const NoteBaseline& committed);

bool currentStateRowIsRightTailShortened(const NoteEditCurrentNoteState& row);

bool currentStateRowIsMoved(const NoteEditCurrentNoteState& row);

bool currentStateRowHasShorterDuration(const NoteEditCurrentNoteState& row);

bool currentStateRowHasOriginalLength(const NoteEditCurrentNoteState& row);

bool participatingNoteIsRightTailShortened(const ParticipatingNoteState& state);

bool participatingNoteIsMoved(const ParticipatingNoteState& state);

bool participatingNoteHasShorterDuration(const ParticipatingNoteState& state);

bool participatingNoteHasOriginalLength(const ParticipatingNoteState& state);

/// Same as `participatingSpanIsRightTailShortened` — retained for call-site compat until R4.
bool participatingNoteShortenedVsCommitted(const NoteBaseline& current,
                                           const NoteBaseline& committed);

ParticipatingNoteState buildParticipatingNoteState(const NoteEditCurrentNoteState& row);

ParticipatingNoteSession buildParticipatingNoteSession(const EditorSelection& selection,
                                                       const NoteEditCurrentState& currentState);

ParticipatingNoteInvariantResult verifyParticipatingNoteInvariants(
    const ParticipatingNoteState& state);

ParticipatingSessionInvariantResult verifyParticipatingSessionInvariants(
    const ParticipatingNoteSession& session);

/// True when currentSpan differs from committedSpan (pitch/start/end), ignoring presence.
bool currentStateRowGeometryDiffersFromCommitted(const NoteEditCurrentNoteState& row);

/// Overlap participation membership from current state: Active + (Hidden/Deleted or geometry
/// differs). `Ended` is never a participant — sticky clear authority (§11 step 5.3).
bool currentStateRowIsOverlapParticipant(const NoteEditCurrentNoteState& row);

NoteIdList collectOverlapParticipantNoteIdsFromCurrentState(
    const NoteEditCurrentState& currentState, NoteId movingNoteId);

/// Geometry/pre-commit participation (§11 step 5.5): current-state membership only.
bool noteIsOverlapParticipant(NoteId noteId, const NoteEditFocus& focus,
                              const NoteEditCurrentState* currentState);

/// True when any non-mover overlap participant exists in current state.
bool hasOverlapParticipants(const NoteEditFocus& focus, const NoteEditCurrentState* currentState);

/// True when row is Ended with geometry still differing (sticky clear without span rewrite).
bool overlapParticipationEndedWhileGeometryDiffers(const NoteEditCurrentState& currentState,
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
