//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ParticipatingNoteSession.h"

#include <algorithm>

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

bool currentStateRowIsOverlapParticipant(const NoteEditCurrentNoteState& row) {
  if (row.presence == NoteEditPresenceType::Hidden ||
      row.presence == NoteEditPresenceType::Deleted) {
    return true;
  }
  return row.currentSpan.pitch != row.committedSpan.pitch ||
         row.currentSpan.startTick != row.committedSpan.startTick ||
         row.currentSpan.endTick != row.committedSpan.endTick;
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

ParticipatingNoteState buildParticipatingNoteState(const NoteEditCurrentNoteState& row) {
  ParticipatingNoteState out{};
  out.noteId = row.noteId;
  out.phase = participatingPhaseFromPresence(row.presence);
  out.currentSpan = row.currentSpan;
  out.committedSpan = row.committedSpan;
  out.shortenedVsCommitted =
      participatingNoteShortenedVsCommitted(row.currentSpan, row.committedSpan);
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
