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

bool currentStateRowIsVisible(const NoteEditCurrentNoteState& row) {
  return row.presence == NoteEditPresenceType::Visible ||
         row.presence == NoteEditPresenceType::Added;
}

bool participatingNoteIsVisible(const ParticipatingNoteState& state) {
  return state.visible;
}

NoteEditLifecycleType currentStateRowLifecycle(const NoteEditCurrentNoteState& row) {
  switch (row.presence) {
    case NoteEditPresenceType::Added:
      return NoteEditLifecycleType::Added;
    case NoteEditPresenceType::Deleted:
      return NoteEditLifecycleType::Deleted;
    case NoteEditPresenceType::Visible:
    case NoteEditPresenceType::Hidden:
      return NoteEditLifecycleType::Existing;
  }
  return NoteEditLifecycleType::Existing;
}

bool currentStateRowLifecycleIsDeleted(const NoteEditCurrentNoteState& row) {
  return currentStateRowLifecycle(row) == NoteEditLifecycleType::Deleted;
}

bool currentStateRowLifecycleIsAdded(const NoteEditCurrentNoteState& row) {
  return currentStateRowLifecycle(row) == NoteEditLifecycleType::Added;
}

NoteEditLifecycleType participatingNoteLifecycle(const ParticipatingNoteState& state) {
  return state.lifecycle;
}

bool currentStateRowIsExistingAndVisible(const NoteEditCurrentNoteState& row) {
  return currentStateRowIsVisible(row) &&
         currentStateRowLifecycle(row) == NoteEditLifecycleType::Existing;
}

bool participatingNoteIsExistingAndVisible(const ParticipatingNoteState& state) {
  return participatingNoteIsVisible(state) &&
         participatingNoteLifecycle(state) == NoteEditLifecycleType::Existing;
}

uint32_t participatingSpanDurationTicks(const NoteBaseline& span) {
  if (span.endTick <= span.startTick) {
    return 0;
  }
  return span.endTick - span.startTick;
}

bool participatingSpanIsRightTailShortened(const NoteBaseline& current,
                                             const NoteBaseline& committed) {
  return current.pitch == committed.pitch && current.startTick == committed.startTick &&
         current.endTick < committed.endTick;
}

bool participatingSpanIsMoved(const NoteBaseline& current, const NoteBaseline& committed) {
  return current.startTick != committed.startTick;
}

bool participatingSpanHasShorterDuration(const NoteBaseline& current,
                                           const NoteBaseline& committed) {
  return participatingSpanDurationTicks(current) < participatingSpanDurationTicks(committed);
}

bool participatingSpanHasOriginalLength(const NoteBaseline& current,
                                            const NoteBaseline& committed) {
  return participatingSpanDurationTicks(current) == participatingSpanDurationTicks(committed);
}

bool currentStateRowIsRightTailShortened(const NoteEditCurrentNoteState& row) {
  return participatingSpanIsRightTailShortened(row.currentSpan, row.committedSpan);
}

bool currentStateRowIsMoved(const NoteEditCurrentNoteState& row) {
  return participatingSpanIsMoved(row.currentSpan, row.committedSpan);
}

bool currentStateRowHasShorterDuration(const NoteEditCurrentNoteState& row) {
  return participatingSpanHasShorterDuration(row.currentSpan, row.committedSpan);
}

bool currentStateRowHasOriginalLength(const NoteEditCurrentNoteState& row) {
  return participatingSpanHasOriginalLength(row.currentSpan, row.committedSpan);
}

bool participatingNoteIsRightTailShortened(const ParticipatingNoteState& state) {
  return participatingSpanIsRightTailShortened(state.currentSpan, state.committedSpan);
}

bool participatingNoteIsMoved(const ParticipatingNoteState& state) {
  return participatingSpanIsMoved(state.currentSpan, state.committedSpan);
}

bool participatingNoteHasShorterDuration(const ParticipatingNoteState& state) {
  return participatingSpanHasShorterDuration(state.currentSpan, state.committedSpan);
}

bool participatingNoteHasOriginalLength(const ParticipatingNoteState& state) {
  return participatingSpanHasOriginalLength(state.currentSpan, state.committedSpan);
}

bool participatingNoteShortenedVsCommitted(const NoteBaseline& current,
                                             const NoteBaseline& committed) {
  return participatingSpanIsRightTailShortened(current, committed);
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
  if (currentStateRowLifecycleIsDeleted(row) || !currentStateRowIsVisible(row)) {
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
  (void)focus;
  if (noteId == kInvalidNoteId || currentState == nullptr || currentState->empty()) {
    return false;
  }
  const NoteEditCurrentNoteState* row = currentState->find(noteId);
  return row != nullptr && currentStateRowIsOverlapParticipant(*row);
}

bool hasOverlapParticipants(const NoteEditFocus& focus, const NoteEditCurrentState* currentState) {
  if (currentState == nullptr || currentState->empty()) {
    return false;
  }
  return !collectOverlapParticipantNoteIdsFromCurrentState(*currentState, focus.movingNoteId)
              .empty();
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
  return !participatingNoteIsVisible(participant) &&
         participatingNoteLifecycle(participant) == NoteEditLifecycleType::Existing;
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
  if (!participatingNoteIsExistingAndVisible(participant) ||
      !participatingNoteIsRightTailShortened(participant)) {
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
  return !participatingNoteIsVisible(participant) ||
         participatingNoteIsRightTailShortened(participant);
}

bool participatingNoteNeedsFullCommittedLeaveRestore(const ParticipatingNoteState& participant) {
  return !participatingNoteIsVisible(participant) &&
         participatingNoteLifecycle(participant) == NoteEditLifecycleType::Existing;
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
  return participatingNoteIsExistingAndVisible(participant) &&
         participatingNoteIsRightTailShortened(participant) &&
         participatingNoteOverlapClosureActive(participant, causingSpan);
}

bool visibleShortenedOverlapTailInventoryMasked(const NoteEditCurrentNoteState& row,
                                              const NoteEditFocus& focus,
                                              int selectedNoteIdx) {
  if (!currentStateRowIsRightTailShortened(row)) {
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
  out.currentSpan = row.currentSpan;
  out.committedSpan = row.committedSpan;
  out.overlapParticipation = row.overlapParticipation;
  out.visibleOverlapShortenSealed = row.visibleOverlapShortenSealed;
  out.visible = currentStateRowIsVisible(row);
  out.lifecycle = currentStateRowLifecycle(row);
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
  if (state.lifecycle == NoteEditLifecycleType::Deleted && state.visible) {
    result.passed = false;
    result.driverNotProjecting = true;
  }
  if (state.lifecycle == NoteEditLifecycleType::Added && !state.visible) {
    result.passed = false;
    result.driverNotProjecting = true;
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
    } else if (!primaryIt->second.visible) {
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
