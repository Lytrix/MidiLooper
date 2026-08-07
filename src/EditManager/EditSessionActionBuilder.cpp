//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionActionBuilder.h"

#include <algorithm>

#include "EditSessionLiveStoreSpan.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "ParticipatingNoteSession.h"
#include "Utils/NoteEditMem.h"

#if defined(SESSION_CAPTURE)
#include "Logger.h"
#endif

namespace {

NOTE_EDIT_MEM bool baselineSpansEqual(const NoteBaseline& left, const NoteBaseline& right) {
  return left.pitch == right.pitch && left.velocity == right.velocity &&
         left.startTick == right.startTick && left.endTick == right.endTick;
}

NOTE_EDIT_MEM bool constrainedMatchesBaseline(const ConstrainedNoteGeometry& constrained,
                                const NoteBaseline& baseline) {
  return constrained.visible && constrained.startTick == baseline.startTick &&
         constrained.endTick == baseline.endTick && constrained.pitch == baseline.pitch;
}

NOTE_EDIT_MEM EditSessionAction makeAction(EditSessionActionType type, NoteId noteId, const NoteBaseline& span) {
  EditSessionAction action{};
  action.type = type;
  action.targetNoteId = noteId;
  action.startTick = span.startTick;
  action.endTick = span.endTick;
  action.pitch = span.pitch;
  action.velocity = span.velocity;
  return action;
}

NOTE_EDIT_MEM int actionTypeSortRank(EditSessionActionType type) {
  switch (type) {
    case EditSessionActionType::RestoreNote:
      return 0;
    case EditSessionActionType::ShortenNote:
      return 1;
    case EditSessionActionType::HideNote:
      return 2;
    case EditSessionActionType::MoveNote:
      return 3;
    case EditSessionActionType::ChangeLength:
      return 4;
    case EditSessionActionType::ChangePitch:
      return 5;
  }
  return 6;
}

NOTE_EDIT_MEM bool editSessionActionBefore(const EditSessionAction& left,
                                           const EditSessionAction& right) {
  const int leftRank = actionTypeSortRank(left.type);
  const int rightRank = actionTypeSortRank(right.type);
  if (leftRank != rightRank) {
    return leftRank < rightRank;
  }
  return left.targetNoteId < right.targetNoteId;
}

NOTE_EDIT_MEM void sortEditSessionActions(EditSessionActions& actions) {
  for (size_t i = 1; i < actions.size(); ++i) {
    const EditSessionAction key = actions[i];
    size_t j = i;
    while (j > 0 && editSessionActionBefore(key, actions[j - 1])) {
      actions[j] = actions[j - 1];
      --j;
    }
    actions[j] = key;
  }
}

NOTE_EDIT_MEM bool causingSpanCompletelyCoversBaseline(const EditedGeometry& editedGeometry,
                                                       const NoteBaseline& baseline) {
  for (const EditedNoteSpan& causing : editedGeometry.causingSpans) {
    if (causing.span.pitch != baseline.pitch) {
      continue;
    }
    if (baseline.startTick >= causing.span.startTick &&
        baseline.endTick <= causing.span.endTick) {
      return true;
    }
  }
  return false;
}

NOTE_EDIT_MEM bool editableRowProjectsToStore(NoteId noteId, const MidiEventVec& liveStore,
                                              uint8_t channel,
                                              const NoteEditCurrentState* currentState) {
  if (currentState != nullptr && currentState->hasRow(noteId)) {
    return currentState->rowProjectsToStore(noteId);
  }
  return liveStoreHasNotePair(liveStore, noteId, channel);
}

NOTE_EDIT_MEM bool readEditableCurrentSpan(NoteId noteId, const MidiEventVec& liveStore,
                                           uint8_t channel, const NoteEditCurrentState* currentState,
                                           NoteBaseline& out) {
  if (currentState != nullptr && currentState->readCurrentSpan(noteId, out)) {
    return true;
  }
  return readLiveLinearSpan(liveStore, noteId, channel, out);
}

NOTE_EDIT_MEM bool overlapClosureActiveForTarget(NoteId targetNoteId, const EditedGeometry& editedGeometry,
                                                 const NoteEditFocus& focus,
                                                 const NoteEditCurrentState* currentState) {
  if (focus.movingNoteId == kInvalidNoteId) {
    return false;
  }
  const NoteBaseline* causingSpan = findCausingSpanForMover(focus.movingNoteId, editedGeometry);
  if (causingSpan == nullptr) {
    return false;
  }
  if (currentState != nullptr) {
    const NoteEditCurrentNoteState* row = currentState->find(targetNoteId);
    if (row != nullptr) {
      const ParticipatingNoteState participant = buildParticipatingNoteState(*row);
      return participatingNoteOverlapClosureActive(participant, *causingSpan);
    }
  }
  const auto baselineIt = focus.baselineMap.find(targetNoteId);
  if (baselineIt == focus.baselineMap.end()) {
    return false;
  }
  ParticipatingNoteState fromBaseline{};
  fromBaseline.noteId = targetNoteId;
  fromBaseline.phase = ParticipatingNotePhase::Visible;
  fromBaseline.committedSpan = baselineIt->second;
  fromBaseline.currentSpan = baselineIt->second;
  fromBaseline.projectsToStore = true;
  return participatingNoteOverlapClosureActive(fromBaseline, *causingSpan);
}

NOTE_EDIT_MEM void appendOverlapTargetActions(
    const std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>&
        constrainedGeometry,
    const EditedGeometry& editedGeometry, const BaselineMap& projectedTransactionBaseline,
    const BaselineMap& storageTransactionBaseline, const NoteIdList& leaveRestoreTargetNoteIds,
    const MidiEventVec& liveStore, uint8_t channel, const NoteEditFocus& focus,
    EditSessionActions& actions, const NoteEditCurrentState* currentState) {
  for (const ConstrainedNoteGeometry& constrained : constrainedGeometry) {
    const bool leaveRestore =
        std::find(leaveRestoreTargetNoteIds.begin(), leaveRestoreTargetNoteIds.end(),
                  constrained.noteId) != leaveRestoreTargetNoteIds.end();
    const BaselineMap& transactionBaseline =
        leaveRestore ? storageTransactionBaseline : projectedTransactionBaseline;
    const auto baselineIt = transactionBaseline.find(constrained.noteId);
    if (baselineIt == transactionBaseline.end()) {
      continue;
    }
    const NoteBaseline& baseline = baselineIt->second;
    NoteId liveNoteId = constrained.noteId;
    if (!editableRowProjectsToStore(liveNoteId, liveStore, channel, currentState)) {
      const NoteId resolvedId =
          findLiveNoteIdForPitchStart(liveStore, channel, baseline.pitch, baseline.startTick);
      if (resolvedId != kInvalidNoteId && resolvedId != focus.movingNoteId &&
          editableRowProjectsToStore(resolvedId, liveStore, channel, currentState)) {
        liveNoteId = resolvedId;
      }
    }
    const bool livePresent = editableRowProjectsToStore(liveNoteId, liveStore, channel, currentState);
    NoteBaseline live{};
    const bool liveReadable =
        livePresent && readEditableCurrentSpan(liveNoteId, liveStore, channel, currentState, live);

    const bool overlapClosureActive =
        overlapClosureActiveForTarget(constrained.noteId, editedGeometry, focus, currentState);
    const NoteBaseline* causingSpan = findCausingSpanForMover(focus.movingNoteId, editedGeometry);

    if (!constrained.visible) {
      if (livePresent) {
        const bool closureActive =
            overlapClosureActiveForTarget(constrained.noteId, editedGeometry, focus, currentState);
        const bool constrainedShortensTail =
            constrained.endTick >= baseline.startTick && constrained.endTick < baseline.endTick;
        if (closureActive && constrainedShortensTail) {
          const NoteBaseline shortened{baseline.pitch, baseline.velocity, baseline.startTick,
                                       constrained.endTick};
          actions.push_back(makeAction(EditSessionActionType::ShortenNote, liveNoteId, shortened));
        } else if (closureActive && liveReadable &&
                   participatingNoteShortenedVsCommitted(live, baseline)) {
          if (constrainedShortensTail && live.endTick != constrained.endTick) {
            const NoteBaseline shortened{baseline.pitch, baseline.velocity, baseline.startTick,
                                         constrained.endTick};
            actions.push_back(makeAction(EditSessionActionType::ShortenNote, liveNoteId, shortened));
          }
        } else {
          actions.push_back(makeAction(EditSessionActionType::HideNote, liveNoteId, baseline));
        }
      }
      continue;
    }

    // After CompleteCover Hide, L→R that only OverlapNoteOff-shortens must reinsert the
    // stub (≥ noteMinLengthTicks). ShortenNote no-ops when the pair is absent
    // (session_20260804_220842). Never reinsert while a causing span still 100% covers the
    // baseline (session_20260804_223208).
    if (!livePresent) {
      if (causingSpanCompletelyCoversBaseline(editedGeometry, baseline)) {
        continue;
      }
      if (overlapClosureActive) {
        const NoteBaseline shortened{constrained.pitch, baseline.velocity, constrained.startTick,
                                     constrained.endTick};
        actions.push_back(makeAction(EditSessionActionType::ShortenNote, liveNoteId, shortened));
        continue;
      }
      if (currentState != nullptr && causingSpan != nullptr) {
        const NoteEditCurrentNoteState* row = currentState->find(liveNoteId);
        if (row != nullptr) {
          const ParticipatingNoteState participant = buildParticipatingNoteState(*row);
          if (participatingNoteVisibleOverlapTailInProgress(participant, *causingSpan)) {
            continue;
          }
        }
      }
      const NoteBaseline reinsert{constrained.pitch, baseline.velocity, constrained.startTick,
                                  constrained.endTick};
      actions.push_back(makeAction(EditSessionActionType::RestoreNote, liveNoteId, reinsert));
      continue;
    }

    if (constrainedMatchesBaseline(constrained, baseline)) {
      if (!liveReadable || !baselineSpansEqual(live, baseline)) {
        if (causingSpanCompletelyCoversBaseline(editedGeometry, baseline) || overlapClosureActive) {
          continue;
        }
        if (currentState != nullptr && causingSpan != nullptr) {
          const NoteEditCurrentNoteState* row = currentState->find(constrained.noteId);
          if (row != nullptr) {
            const ParticipatingNoteState participant = buildParticipatingNoteState(*row);
            if (participatingNoteVisibleOverlapTailInProgress(participant, *causingSpan)) {
              continue;
            }
          }
        }
        const NoteBaseline restoreSpan{constrained.pitch, baseline.velocity, constrained.startTick,
                                     constrained.endTick};
        actions.push_back(makeAction(EditSessionActionType::RestoreNote, liveNoteId, restoreSpan));
      }
      continue;
    }

    if (constrained.endTick < baseline.endTick) {
      const NoteBaseline shortened{constrained.pitch, baseline.velocity, constrained.startTick,
                                   constrained.endTick};
      if (!liveReadable || live.endTick != constrained.endTick) {
        actions.push_back(makeAction(EditSessionActionType::ShortenNote, liveNoteId, shortened));
      }
    }
  }
}

NOTE_EDIT_MEM bool causingNoteHasOrphanOnForAction(const NoteEditFocus& focus,
                                                   MidiEventVec& liveStore, NoteId noteId,
                                                   uint8_t channel, uint32_t loopLength) {
  if (!focus.active || focus.movingNoteId != noteId) {
    return false;
  }
  return findNoteOnForMovingNoteEdit(liveStore, focus, channel, focus.last.pitch,
                                     focus.last.startTick, loopLength) != nullptr;
}

NOTE_EDIT_MEM void appendCausingNoteActions(const EditedGeometry& editedGeometry,
                                            MidiEventVec& liveStore, const NoteEditFocus& focus,
                                            uint8_t channel, uint32_t loopLength,
                                            EditSessionActions& actions,
                                            const NoteEditCurrentState* currentState) {
  for (const EditedNoteSpan& causing : editedGeometry.causingSpans) {
    NoteBaseline editableSpan{};
    const bool hasEditableSpan =
        readEditableCurrentSpan(causing.noteId, liveStore, channel, currentState, editableSpan);
    const bool projectsToStore =
        editableRowProjectsToStore(causing.noteId, liveStore, channel, currentState);

    if (hasEditableSpan && projectsToStore &&
        baselineSpansEqual(causing.span, editableSpan)) {
      continue;
    }

    if (!hasEditableSpan || !projectsToStore) {
      if (!causingNoteHasOrphanOnForAction(focus, liveStore, causing.noteId, channel,
                                           loopLength)) {
        continue;
      }
      if (baselineSpansEqual(causing.span, focus.last)) {
        continue;
      }
    }

    const NoteBaseline& live =
        (hasEditableSpan && projectsToStore) ? editableSpan : focus.last;

    if (causing.span.pitch != live.pitch) {
      actions.push_back(makeAction(EditSessionActionType::ChangePitch, causing.noteId,
                                   causing.span));
      continue;
    }

    if (causing.span.startTick != live.startTick) {
      actions.push_back(makeAction(EditSessionActionType::MoveNote, causing.noteId, causing.span));
      continue;
    }

    if (causing.span.endTick != live.endTick) {
      actions.push_back(makeAction(EditSessionActionType::ChangeLength, causing.noteId,
                                   causing.span));
    }
  }
}

}  // namespace

#if defined(SESSION_CAPTURE)
NOTE_EDIT_MEM void logEditSessionActions(const EditSessionActions& actions) {
  for (const EditSessionAction& action : actions) {
    logger.log(CAT_MIDI, LOG_DEBUG,
                 "EditSessionAction: type=%u noteId=%lu start=%lu end=%lu pitch=%u",
                 static_cast<unsigned>(action.type), static_cast<unsigned long>(action.targetNoteId),
                 static_cast<unsigned long>(action.startTick),
                 static_cast<unsigned long>(action.endTick), static_cast<unsigned>(action.pitch));
  }
}
#endif

NOTE_EDIT_MEM EditSessionActions buildEditSessionActions(
    const std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>&
        constrainedGeometry,
    const EditedGeometry& editedGeometry, const BaselineMap& projectedTransactionBaseline,
    const BaselineMap& storageTransactionBaseline, const NoteIdList& leaveRestoreTargetNoteIds,
    MidiEventVec& liveStore, uint8_t channel, const NoteEditFocus& focus, uint32_t loopLength,
    const NoteEditCurrentState* currentState) {
  EditSessionActions actions;
  appendOverlapTargetActions(constrainedGeometry, editedGeometry, projectedTransactionBaseline,
                             storageTransactionBaseline, leaveRestoreTargetNoteIds, liveStore,
                             channel, focus, actions, currentState);
  appendCausingNoteActions(editedGeometry, liveStore, focus, channel, loopLength, actions,
                           currentState);

  sortEditSessionActions(actions);
  return actions;
}
