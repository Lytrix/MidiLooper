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

/// Overlap closure participant: session-mutated overlap row (not the mover).
bool currentStateRowIsOverlapParticipant(const NoteEditCurrentNoteState& row);

NoteIdList collectOverlapParticipantNoteIdsFromCurrentState(
    const NoteEditCurrentState& currentState, NoteId movingNoteId);

/// Same-start shortened tail or head-trimmed span that may leave-restore to committed baseline.
bool participatingSpanQualifiesForOverlapLeaveRestore(const NoteBaseline& committed,
                                                      const NoteBaseline& current);

/// Overlap participant (not mover) that may receive leave-restore when interactions clear.
bool participatingNoteQualifiesForLeaveRestoreTarget(const ParticipatingNoteState& participant,
                                                       NoteId movingNoteId);

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

const NoteBaseline* findCausingSpanForMover(NoteId movingNoteId,
                                            const EditedGeometry& editedGeometry);
