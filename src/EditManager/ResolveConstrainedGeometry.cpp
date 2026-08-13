//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionInteraction.h"
#include "EditSessionLiveStoreSpan.h"
#include "NoteEditCurrentState.h"
#include "ResolveConstrainedGeometry.h"

#include <algorithm>

#include "NoteEditFocus.h"
#include "ParticipatingNoteSession.h"
#include "Utils/NoteEditMem.h"

namespace {

NOTE_EDIT_MEM bool liveStoreLinearSpanDiffersFromBaseline(NoteId noteId, const NoteBaseline& baseline,
                                              const MidiEventVec& liveStore, uint8_t channel,
                                              uint32_t loopLength) {
  (void)loopLength;
  NoteBaseline live{};
  if (!readLiveLinearSpan(liveStore, noteId, channel, live)) {
    return true;
  }
  return live.pitch != baseline.pitch || live.startTick != baseline.startTick ||
         live.endTick != baseline.endTick;
}

NOTE_EDIT_MEM bool spanQualifiesForOverlapLeaveRestore(const NoteBaseline& baseline,
                                                       const NoteBaseline& span) {
  return participatingSpanQualifiesForOverlapLeaveRestore(baseline, span);
}

NOTE_EDIT_MEM bool isOverlapLeaveRestoreBaselineDiff(const NoteBaseline& baseline,
                                                     const MidiEventVec& liveStore, NoteId noteId,
                                                     uint8_t channel, uint32_t loopLength) {
  (void)loopLength;
  NoteBaseline live{};
  if (!readLiveLinearSpan(liveStore, noteId, channel, live)) {
    return true;
  }
  return spanQualifiesForOverlapLeaveRestore(baseline, live);
}

NOTE_EDIT_MEM bool currentSpanDiffersFromBaseline(NoteId noteId, const NoteBaseline& baseline,
                                                  const NoteEditCurrentState& currentState) {
  NoteBaseline current{};
  if (!currentState.readCurrentSpan(noteId, current)) {
    return true;
  }
  return current.pitch != baseline.pitch || current.startTick != baseline.startTick ||
         current.endTick != baseline.endTick;
}

NOTE_EDIT_MEM bool hasIncomingInteraction(NoteId targetNoteId,
                            const EditSessionInteractionsByTarget& grouped) {
  for (const TargetNoteInteractionGroup& group : grouped.groups) {
    if (group.targetNoteId == targetNoteId) {
      return !group.incoming.empty();
    }
  }
  return false;
}

NOTE_EDIT_MEM bool isCausingNoteInEditedGeometry(NoteId noteId, const EditedGeometry& editedGeometry) {
  for (const EditedNoteSpan& causing : editedGeometry.causingSpans) {
    if (causing.noteId == noteId) {
      return true;
    }
  }
  return false;
}

NOTE_EDIT_MEM bool isResolveTargetExcluded(NoteId noteId, const EditorSelection& selection,
                                           const EditedGeometry& editedGeometry) {
  (void)selection;
  // Causing notes are resolved via edited geometry. Selected stationary siblings remain overlap
  // targets when only focus.last moves (session_20260807_111955).
  return isCausingNoteInEditedGeometry(noteId, editedGeometry);
}

NOTE_EDIT_MEM const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>*
incomingForTarget(NoteId targetNoteId, const EditSessionInteractionsByTarget& grouped) {
  for (const TargetNoteInteractionGroup& group : grouped.groups) {
    if (group.targetNoteId == targetNoteId) {
      return &group.incoming;
    }
  }
  return nullptr;
}

NOTE_EDIT_MEM bool interactionIsOverlapNoteOff(const EditSessionInteraction& interaction) {
  return interaction.type == InteractionType::OverlapNoteOff;
}

NOTE_EDIT_MEM bool interactionsAreBoundaryTouchOnly(
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>&
        incoming) {
  if (incoming.empty()) {
    return false;
  }
  for (const EditSessionInteraction& interaction : incoming) {
    if (interaction.type != InteractionType::BoundaryTouch) {
      return false;
    }
  }
  return true;
}

NOTE_EDIT_MEM ConstrainedNoteGeometry constrainedGeometryFromRestoreCandidate(
    NoteId targetNoteId, const NoteBaseline& transactionBaseline, const MidiEventVec& liveStore,
    uint8_t channel, const NoteEditFocus& focus, const NoteEditCurrentState* currentState,
    const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  ConstrainedNoteGeometry geometry{};
  geometry.noteId = targetNoteId;
  geometry.pitch = transactionBaseline.pitch;
  geometry.visible = true;

  NoteBaseline painted{};
  const bool hasPainted = committedDisplayNotes != nullptr &&
                          displaySpanForNoteId(*committedDisplayNotes, targetNoteId, painted);

  if (currentState != nullptr) {
    const NoteEditCurrentNoteState* row = currentState->find(targetNoteId);
    if (row != nullptr) {
      const ParticipatingNoteState participant = buildParticipatingNoteState(*row);
      if (participatingNoteNeedsFullCommittedLeaveRestore(participant)) {
        const NoteBaseline committed =
            hasPainted ? painted : participatingLeaveRestoreCommittedSpan(participant);
        geometry.startTick = committed.startTick;
        geometry.endTick = committed.endTick;
        geometry.pitch = committed.pitch;
        return geometry;
      }
      // Visible shortened leave-restore paints committedSpan (021407 pitch vacate after ShortenNote)
      // unless Stage 8 painted DisplayNote is shorter rematerialize pairing (200154 note 5).
      if (participatingNoteIsExistingAndVisible(participant) &&
          participatingNoteIsRightTailShortened(participant)) {
        const NoteBaseline committed = hasPainted ? painted : participant.committedSpan;
        geometry.startTick = committed.startTick;
        geometry.endTick = committed.endTick;
        geometry.pitch = committed.pitch;
        return geometry;
      }
    }
  }

  NoteBaseline live{};
  if (readLiveLinearSpan(liveStore, targetNoteId, channel, live)) {
    if (currentState != nullptr) {
      const NoteEditCurrentNoteState* row = currentState->find(targetNoteId);
      if (row != nullptr && live.startTick != transactionBaseline.startTick) {
        const bool headTrimmedLive =
            live.startTick > transactionBaseline.startTick &&
            live.endTick == transactionBaseline.endTick;
        if (!headTrimmedLive) {
          geometry.startTick = live.startTick;
          if (row->committedSpan.startTick == live.startTick) {
            geometry.endTick = row->committedSpan.endTick;
          } else {
            const uint32_t committedLength =
                row->committedSpan.endTick - row->committedSpan.startTick;
            geometry.endTick = live.startTick + committedLength;
          }
          geometry.pitch = live.pitch;
          return geometry;
        }
      }
    }
    if (live.endTick < transactionBaseline.endTick ||
        live.startTick > transactionBaseline.startTick ||
        live.pitch != transactionBaseline.pitch) {
      geometry.startTick = transactionBaseline.startTick;
      geometry.endTick = transactionBaseline.endTick;
      geometry.pitch = transactionBaseline.pitch;
      return geometry;
    }
    geometry.startTick = live.startTick;
    geometry.endTick = live.endTick;
    geometry.pitch = live.pitch;
    return geometry;
  }

  const auto scratchIt = focus.overlapNotes.find(targetNoteId);
  if (scratchIt != focus.overlapNotes.end() &&
      scratchIt->second.state != OverlapNoteStoreState::Visible) {
    geometry.startTick = scratchIt->second.baseline.startTick;
    geometry.endTick = scratchIt->second.baseline.endTick;
    geometry.pitch = scratchIt->second.baseline.pitch;
    return geometry;
  }

  geometry.startTick = transactionBaseline.startTick;
  geometry.endTick = transactionBaseline.endTick;
  return geometry;
}

NOTE_EDIT_MEM uint8_t resolveOverlapLanePitchForTargets(const EditedGeometry& editedGeometry,
                                                      const NoteEditFocus& focus) {
  for (const EditedNoteSpan& causing : editedGeometry.causingSpans) {
    if (causing.noteId == focus.movingNoteId) {
      return causing.span.pitch;
    }
  }
  return focus.last.pitch;
}

}  // namespace

NOTE_EDIT_MEM std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineConstrainedGeometryTargetNoteIds(
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength,
    const EditorSelection& selection, const EditedGeometry& editedGeometry,
    const NoteEditFocus& focus, const NoteEditCurrentState* currentState) {
  const uint8_t overlapLanePitch = resolveOverlapLanePitchForTargets(editedGeometry, focus);
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targets;
  for (const TargetNoteInteractionGroup& group : grouped.groups) {
    // Causing/selected notes are edited via edited geometry, never resolve targets.
    if (isResolveTargetExcluded(group.targetNoteId, selection, editedGeometry)) {
      continue;
    }
    targets.push_back(group.targetNoteId);
  }

  for (const auto& [noteId, baseline] : transactionBaseline) {
    if (noteId == kInvalidNoteId) {
      continue;
    }
    // After move/length, the mover differs from select-time baselineMap; that is causing-note
    // geometry, not an overlap restore candidate (session_20260804_215203).
    if (isResolveTargetExcluded(noteId, selection, editedGeometry)) {
      continue;
    }
    if (hasIncomingInteraction(noteId, grouped)) {
      continue;
    }
    // §11 step 5.5: leave-restore / sticky targets from current-state participation only.
    if (currentState == nullptr || currentState->empty()) {
      continue;
    }
    const NoteEditCurrentNoteState* participantRow = currentState->find(noteId);
    if (participantRow == nullptr || !currentStateRowIsOverlapParticipant(*participantRow)) {
      continue;
    }
    const NoteBaseline* causingSpan = findCausingSpanForMover(focus.movingNoteId, editedGeometry);
    const bool onCurrentLane = baseline.pitch == overlapLanePitch;

    // Stage 7.5.D1/D2 (020050 / 021407): after ChangePitch away from a lane, leave-restore
    // Hidden/Deleted or Visible shortened overlap participants on the vacated pitch.
    // Without currentState, keep RC9i / 011115 (no vacated restore from live-store alone).
    if (!onCurrentLane) {
      if (currentState == nullptr) {
        continue;
      }
      const NoteEditCurrentNoteState* vacatedRow = currentState->find(noteId);
      if (vacatedRow == nullptr) {
        continue;
      }
      const ParticipatingNoteState vacatedParticipant = buildParticipatingNoteState(*vacatedRow);
      const bool hiddenLeave = participatingNoteQualifiesForLeaveRestoreTarget(
          vacatedParticipant, focus.movingNoteId);
      const bool shortenedLeave = participatingNoteIsExistingAndVisible(vacatedParticipant) &&
                                  participatingNoteIsRightTailShortened(vacatedParticipant);
      if (!hiddenLeave && !shortenedLeave) {
        continue;
      }
      if (causingSpan != nullptr &&
          !participatingNoteOverlapInteractionCleared(vacatedParticipant, *causingSpan)) {
        continue;
      }
      targets.push_back(noteId);
      continue;
    }

    if (currentState != nullptr) {
      const NoteEditCurrentNoteState* row = currentState->find(noteId);
      if (row == nullptr) {
        continue;
      }
      const ParticipatingNoteState participant = buildParticipatingNoteState(*row);
      if (participatingNoteQualifiesForLeaveRestoreTarget(participant, focus.movingNoteId)) {
        if (causingSpan == nullptr ||
            !participatingNoteOverlapInteractionCleared(participant, *causingSpan)) {
          continue;
        }
        if (causingSpan != nullptr &&
            participatingNoteVisibleOverlapTailInProgress(participant, *causingSpan)) {
          continue;
        }
        targets.push_back(noteId);
        continue;
      }
      if (participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(participant, baseline,
                                                                         focus.movingNoteId)) {
        if (causingSpan == nullptr ||
            !participatingNoteOverlapInteractionCleared(participant, *causingSpan)) {
          continue;
        }
        if (participatingNoteVisibleOverlapTailInProgress(participant, *causingSpan)) {
          continue;
        }
        targets.push_back(noteId);
        continue;
      }
      if (currentSpanDiffersFromBaseline(noteId, baseline, *currentState) &&
          participatingSpanQualifiesForOverlapLeaveRestore(baseline, row->currentSpan)) {
        // Slice A: defer while mover still inside committed overlap closure (224633 tail).
        // Once cleared, Visible shortened leave-restores to committedSpan (022151 LTR).
        if (causingSpan != nullptr &&
            participatingNoteVisibleOverlapTailInProgress(participant, *causingSpan)) {
          continue;
        }
        if (causingSpan != nullptr &&
            !participatingNoteOverlapInteractionCleared(participant, *causingSpan)) {
          continue;
        }
        targets.push_back(noteId);
      }
      continue;
    }
    if (liveStoreLinearSpanDiffersFromBaseline(noteId, baseline, liveStore, channel, loopLength) &&
        isOverlapLeaveRestoreBaselineDiff(baseline, liveStore, noteId, channel, loopLength)) {
      targets.push_back(noteId);
    }
  }

  sortNoteIdVector(targets);
  targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  return targets;
}

NOTE_EDIT_MEM ConstrainedNoteGeometry resolveConstrainedGeometry(
    NoteId targetNoteId, const NoteBaseline& baseline,
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>&
        incomingInteractionsForTarget,
    uint32_t loopLength, uint32_t noteMinLengthTicks, bool noteMinLengthRemoveEnabled) {
  ConstrainedNoteGeometry geometry{};
  geometry.noteId = targetNoteId;
  geometry.pitch = baseline.pitch;
  geometry.startTick = baseline.startTick;
  geometry.endTick = baseline.endTick;
  geometry.visible = true;

  bool completeHide = false;
  bool hasTailShorten = false;
  uint32_t shortestEndTick = baseline.endTick;

  for (const EditSessionInteraction& interaction : incomingInteractionsForTarget) {
    if (interaction.type == InteractionType::CompleteCover ||
        interaction.type == InteractionType::OverlapNoteOn) {
      completeHide = true;
      break;
    }

    if (interactionIsOverlapNoteOff(interaction)) {
      hasTailShorten = true;
      const uint32_t candidateEnd = computeShortenedEndTick(interaction, loopLength);
      if (candidateEnd < shortestEndTick) {
        shortestEndTick = candidateEnd;
      }
    }
  }

  if (completeHide) {
    geometry.visible = false;
    return geometry;
  }

  if (hasTailShorten) {
    geometry.visible = true;
    geometry.startTick = baseline.startTick;
    geometry.endTick = shortestEndTick;
  } else if (!incomingInteractionsForTarget.empty() &&
             interactionsAreBoundaryTouchOnly(incomingInteractionsForTarget)) {
    geometry.visible = true;
    geometry.startTick = baseline.startTick;
    geometry.endTick = baseline.endTick;
  } else if (incomingInteractionsForTarget.empty()) {
    geometry.visible = true;
    geometry.startTick = baseline.startTick;
    geometry.endTick = baseline.endTick;
  }

  // Inverted / empty span (projected shorten past start) must Hide — never emit end < start
  // into the live store (LinearNoteOff / check=2).
  if (geometry.visible && geometry.endTick <= geometry.startTick) {
    geometry.visible = false;
    return geometry;
  }

  if (noteMinLengthRemoveEnabled && geometry.visible &&
      (geometry.endTick - geometry.startTick) < noteMinLengthTicks) {
    geometry.visible = false;
  }

  return geometry;
}

NOTE_EDIT_MEM std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>
resolveAllConstrainedGeometry(
    const EditSessionInteractionsByTarget& grouped,
    const BaselineMap& projectedTransactionBaseline, const BaselineMap& storageTransactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength,
    uint32_t noteMinLengthTicks, bool noteMinLengthRemoveEnabled,
    const EditorSelection& selection, const EditedGeometry& editedGeometry,
    const NoteEditFocus& focus, NoteIdList& leaveRestoreTargetNoteIds,
    const NoteEditCurrentState* currentState,
    const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  leaveRestoreTargetNoteIds.clear();
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targetIds =
      determineConstrainedGeometryTargetNoteIds(grouped, storageTransactionBaseline, liveStore,
                                                channel, loopLength, selection, editedGeometry,
                                                focus, currentState);

  std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>> out;
  for (NoteId targetNoteId : targetIds) {
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
        emptyIncoming;
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>*
        incoming = incomingForTarget(targetNoteId, grouped);
    const auto& incomingInteractions = incoming != nullptr ? *incoming : emptyIncoming;
    if (incomingInteractions.empty()) {
      const auto storageIt = storageTransactionBaseline.find(targetNoteId);
      if (storageIt == storageTransactionBaseline.end()) {
        continue;
      }
      ConstrainedNoteGeometry restoreCandidate = constrainedGeometryFromRestoreCandidate(
          targetNoteId, storageIt->second, liveStore, channel, focus, currentState,
          committedDisplayNotes);
      if (restoreCandidate.endTick <= restoreCandidate.startTick) {
        continue;
      }
      leaveRestoreTargetNoteIds.push_back(targetNoteId);
      out.push_back(restoreCandidate);
      continue;
    }
    const auto projectedIt = projectedTransactionBaseline.find(targetNoteId);
    if (projectedIt == projectedTransactionBaseline.end()) {
      continue;
    }
    out.push_back(resolveConstrainedGeometry(targetNoteId, projectedIt->second, incomingInteractions,
                                             loopLength, noteMinLengthTicks,
                                             noteMinLengthRemoveEnabled));
  }
  sortNoteIdVector(leaveRestoreTargetNoteIds);
  return out;
}
