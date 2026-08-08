//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ParticipatingNoteSession.h"

#include <algorithm>

#include "NoteEditFocus.h"

namespace {

void sortParticipatingNoteIdList(NoteIdList& noteIds) {
  std::sort(noteIds.begin(), noteIds.end());
  noteIds.erase(std::unique(noteIds.begin(), noteIds.end()), noteIds.end());
}

}  // namespace

ParticipatingNotePhase participatingPhaseFromPresence(NoteEditPresenceType presence) {
  switch (presence) {
    case NoteEditPresenceType::Visible:
      return ParticipatingNotePhase::Visible;
    case NoteEditPresenceType::Hidden:
      return ParticipatingNotePhase::Hidden;
    case NoteEditPresenceType::Deleted:
      return ParticipatingNotePhase::Deleted;
    case NoteEditPresenceType::Added:
      return ParticipatingNotePhase::Added;
  }
  return ParticipatingNotePhase::Visible;
}

bool participatingNoteShortenedVsCommitted(const NoteBaseline& current,
                                             const NoteBaseline& committed) {
  return current.pitch == committed.pitch && current.startTick == committed.startTick &&
         current.endTick < committed.endTick;
}

bool currentStateRowGeometryDiffersFromCommitted(const NoteEditCurrentNoteState& row) {
  return row.currentSpan.pitch != row.committedSpan.pitch ||
         row.currentSpan.startTick != row.committedSpan.startTick ||
         row.currentSpan.endTick != row.committedSpan.endTick;
}

bool currentStateRowIsOverlapParticipant(const NoteEditCurrentNoteState& row) {
  if (row.overlapParticipation == NoteEditOverlapParticipationType::Ended) {
    return false;
  }
  if (row.presence == NoteEditPresenceType::Hidden ||
      row.presence == NoteEditPresenceType::Deleted) {
    return true;
  }
  return currentStateRowGeometryDiffersFromCommitted(row);
}

NoteIdList collectOverlapParticipantNoteIdsFromCurrentState(
    const NoteEditCurrentState& currentState, NoteId movingNoteId) {
  NoteIdList out;
  for (const auto& [noteId, row] : currentState.rows()) {
    if (noteId == kInvalidNoteId || noteId == movingNoteId) {
      continue;
    }
    if (currentStateRowIsOverlapParticipant(row)) {
      out.push_back(noteId);
    }
  }
  sortParticipatingNoteIdList(out);
  return out;
}

bool noteIsOverlapParticipant(NoteId noteId, const NoteEditFocus& focus,
                              const NoteEditCurrentState* currentState) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  if (currentState != nullptr && !currentState->empty()) {
    const NoteEditCurrentNoteState* row = currentState->find(noteId);
    return row != nullptr && currentStateRowIsOverlapParticipant(*row);
  }
  return overlapParticipationLatchActive(focus, noteId);
}

bool hasOverlapParticipants(const NoteEditFocus& focus, const NoteEditCurrentState* currentState) {
  if (currentState != nullptr && !currentState->empty()) {
    return !collectOverlapParticipantNoteIdsFromCurrentState(*currentState, focus.movingNoteId)
                .empty();
  }
  return !focus.changedOverlapNoteIds.empty();
}

bool overlapParticipationLatchActive(const NoteEditFocus& focus, NoteId noteId) {
  // Inline latch membership so ParticipatingNoteSession does not hard-link NoteEditFocusOverlap
  // (native suites that include this TU without overlap.cpp).
  return std::find(focus.changedOverlapNoteIds.begin(), focus.changedOverlapNoteIds.end(),
                   noteId) != focus.changedOverlapNoteIds.end();
}

bool overlapParticipationEndedWhileGeometryDiffers(const NoteEditCurrentState& currentState,
                                                   NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  const NoteEditCurrentNoteState* row = currentState.find(noteId);
  if (row == nullptr ||
      row->overlapParticipation != NoteEditOverlapParticipationType::Ended) {
    return false;
  }
  return currentStateRowGeometryDiffersFromCommitted(*row);
}

bool overlapParticipationLatchClearedWhileGeometryDiffers(const NoteEditFocus& focus,
                                                          const NoteEditCurrentState& currentState,
                                                          NoteId noteId) {
  if (noteId == kInvalidNoteId || overlapParticipationLatchActive(focus, noteId)) {
    return false;
  }
  return overlapParticipationEndedWhileGeometryDiffers(currentState, noteId);
}

bool participatingSpanQualifiesForOverlapLeaveRestore(const NoteBaseline& committed,
                                                      const NoteBaseline& current) {
  if (current.startTick == committed.startTick) {
    return true;
  }
  if (current.startTick > committed.startTick && current.endTick == committed.endTick) {
    return true;
  }
  return false;
}

bool participatingNoteQualifiesForLeaveRestoreTarget(const ParticipatingNoteState& participant,
                                                     NoteId movingNoteId) {
  if (participant.noteId == kInvalidNoteId || participant.noteId == movingNoteId) {
    return false;
  }
  // Session-unsealed Hidden only. Sealed Deleted after deselect must not leave-restore (022849 E2).
  return participant.phase == ParticipatingNotePhase::Hidden;
}

bool participatingNoteCommittedSpanSealedBelowStorageBaseline(
    const ParticipatingNoteState& participant, const NoteBaseline& storageBaseline) {
  if (participant.committedSpan.pitch != storageBaseline.pitch) {
    return false;
  }
  return participant.committedSpan.endTick < storageBaseline.endTick ||
         participant.committedSpan.startTick > storageBaseline.startTick;
}

bool participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(
    const ParticipatingNoteState& participant, const NoteBaseline& storageBaseline,
    NoteId movingNoteId) {
  if (participant.noteId == kInvalidNoteId || participant.noteId == movingNoteId) {
    return false;
  }
  if (participant.phase != ParticipatingNotePhase::Visible || !participant.shortenedVsCommitted) {
    return false;
  }
  if (participant.visibleOverlapShortenSealed) {
    return true;
  }
  return participatingNoteCommittedSpanSealedBelowStorageBaseline(participant, storageBaseline);
}

bool participatingNoteUsesCommittedBaselineDuringOverlapClosure(
    const ParticipatingNoteState& participant, NoteId movingNoteId) {
  if (participant.noteId == kInvalidNoteId || participant.noteId == movingNoteId) {
    return false;
  }
  return participant.phase == ParticipatingNotePhase::Hidden ||
         participant.phase == ParticipatingNotePhase::Deleted ||
         participant.shortenedVsCommitted;
}

bool participatingNoteNeedsFullCommittedLeaveRestore(const ParticipatingNoteState& participant) {
  return participant.phase == ParticipatingNotePhase::Hidden;
}

NoteBaseline participatingLeaveRestoreCommittedSpan(const ParticipatingNoteState& participant) {
  return participant.committedSpan;
}

bool participatingSpansOverlapInclusive(const NoteBaseline& left, const NoteBaseline& right) {
  if (left.pitch != right.pitch) {
    return false;
  }
  return left.startTick <= right.endTick && right.startTick <= left.endTick;
}

bool participatingNoteOverlapClosureActive(const ParticipatingNoteState& participant,
                                           const NoteBaseline& causingSpan) {
  return participatingSpansOverlapInclusive(causingSpan, participant.committedSpan);
}

bool participatingNoteOverlapInteractionCleared(const ParticipatingNoteState& participant,
                                                const NoteBaseline& causingSpan) {
  return !participatingNoteOverlapClosureActive(participant, causingSpan);
}

bool participatingNoteVisibleOverlapTailInProgress(const ParticipatingNoteState& participant,
                                                   const NoteBaseline& causingSpan) {
  return participant.phase == ParticipatingNotePhase::Visible &&
         participant.shortenedVsCommitted &&
         participatingNoteOverlapClosureActive(participant, causingSpan);
}

bool visibleShortenedOverlapTailInventoryMasked(const NoteEditCurrentNoteState& row,
                                              const NoteEditFocus& focus,
                                              int selectedNoteIdx) {
  if (!participatingNoteShortenedVsCommitted(row.currentSpan, row.committedSpan)) {
    return false;
  }
  if (selectedNoteIdx < 0 || !focus.active || focus.movingNoteId == kInvalidNoteId) {
    return false;
  }
  // Inventory mask follows current-state participation (Ended on sticky clear).
  if (!currentStateRowIsOverlapParticipant(row)) {
    return false;
  }
  const ParticipatingNoteState participant = buildParticipatingNoteState(row);
  const NoteBaseline causingSpan{focus.last.pitch, focus.last.velocity, focus.last.startTick,
                                 focus.last.endTick};
  return participatingNoteVisibleOverlapTailInProgress(participant, causingSpan);
}

const NoteBaseline* findCausingSpanForMover(NoteId movingNoteId,
                                            const EditedGeometry& editedGeometry) {
  for (const EditedNoteSpan& causing : editedGeometry.causingSpans) {
    if (causing.noteId == movingNoteId) {
      return &causing.span;
    }
  }
  return nullptr;
}

ParticipatingNoteState buildParticipatingNoteState(const NoteEditCurrentNoteState& row) {
  ParticipatingNoteState out{};
  out.noteId = row.noteId;
  out.phase = participatingPhaseFromPresence(row.presence);
  out.currentSpan = row.currentSpan;
  out.committedSpan = row.committedSpan;
  out.overlapParticipation = row.overlapParticipation;
  out.shortenedVsCommitted =
      participatingNoteShortenedVsCommitted(row.currentSpan, row.committedSpan);
  out.visibleOverlapShortenSealed = row.visibleOverlapShortenSealed;
  switch (row.presence) {
    case NoteEditPresenceType::Visible:
    case NoteEditPresenceType::Added:
      out.projectsToStore = true;
      break;
    case NoteEditPresenceType::Hidden:
    case NoteEditPresenceType::Deleted:
      out.projectsToStore = false;
      break;
  }
  return out;
}

ParticipatingNoteSession buildParticipatingNoteSession(const EditorSelection& selection,
                                                       const NoteEditCurrentState& currentState) {
  ParticipatingNoteSession session{};
  session.primaryNoteId = selection.primaryNote;
  session.selectedNoteIds = selection.selectedNotes;
  for (const auto& [noteId, row] : currentState.rows()) {
    if (noteId == kInvalidNoteId) {
      continue;
    }
    session.participatingNotes.emplace(noteId, buildParticipatingNoteState(row));
  }
  return session;
}

ParticipatingNoteInvariantResult verifyParticipatingNoteInvariants(
    const ParticipatingNoteState& state) {
  ParticipatingNoteInvariantResult result{};
  if (state.noteId == kInvalidNoteId) {
    result.passed = false;
    result.missingCommittedSpan = true;
    return result;
  }
  if (state.committedSpan.endTick <= state.committedSpan.startTick) {
    result.passed = false;
    result.missingCommittedSpan = true;
  }
  if (state.phase == ParticipatingNotePhase::Hidden ||
      state.phase == ParticipatingNotePhase::Deleted) {
    if (state.projectsToStore) {
      result.passed = false;
      result.driverNotProjecting = true;
    }
  }
  if (state.phase == ParticipatingNotePhase::Visible ||
      state.phase == ParticipatingNotePhase::Added) {
    if (!state.projectsToStore) {
      result.passed = false;
      result.driverNotProjecting = true;
    }
  }
  return result;
}

ParticipatingSessionInvariantResult verifyParticipatingSessionInvariants(
    const ParticipatingNoteSession& session) {
  ParticipatingSessionInvariantResult result{};
  if (session.primaryNoteId != kInvalidNoteId) {
    const auto primaryIt = session.participatingNotes.find(session.primaryNoteId);
    if (primaryIt == session.participatingNotes.end()) {
      result.passed = false;
      result.primaryMissingFromParticipants = true;
    } else if (!primaryIt->second.projectsToStore) {
      result.passed = false;
      result.actionTargetWouldDependOnProjection = true;
    }
  }
  for (NoteId selectedId : session.selectedNoteIds) {
    if (selectedId == kInvalidNoteId) {
      continue;
    }
    if (session.participatingNotes.find(selectedId) == session.participatingNotes.end()) {
      result.passed = false;
      result.selectedOutsideParticipants = true;
      break;
    }
  }
  return result;
}
