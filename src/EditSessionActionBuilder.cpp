//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionActionBuilder.h"

#include <algorithm>

#include "EditSessionLiveStoreSpan.h"

#if defined(SESSION_CAPTURE)
#include "Logger.h"
#endif

namespace {

bool baselineSpansEqual(const NoteBaseline& left, const NoteBaseline& right) {
  return left.pitch == right.pitch && left.velocity == right.velocity &&
         left.startTick == right.startTick && left.endTick == right.endTick;
}

bool constrainedMatchesBaseline(const ConstrainedNoteGeometry& constrained,
                                const NoteBaseline& baseline) {
  return constrained.visible && constrained.startTick == baseline.startTick &&
         constrained.endTick == baseline.endTick && constrained.pitch == baseline.pitch;
}

EditSessionAction makeAction(EditSessionActionType type, NoteId noteId, const NoteBaseline& span) {
  EditSessionAction action{};
  action.type = type;
  action.targetNoteId = noteId;
  action.startTick = span.startTick;
  action.endTick = span.endTick;
  action.pitch = span.pitch;
  action.velocity = span.velocity;
  return action;
}

int actionTypeSortRank(EditSessionActionType type) {
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

void appendOverlapTargetActions(
    const std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>&
        constrainedGeometry,
    const BaselineMap& transactionBaseline, const MidiEventVec& liveStore, uint8_t channel,
    EditSessionActions& actions) {
  for (const ConstrainedNoteGeometry& constrained : constrainedGeometry) {
    const auto baselineIt = transactionBaseline.find(constrained.noteId);
    if (baselineIt == transactionBaseline.end()) {
      continue;
    }
    const NoteBaseline& baseline = baselineIt->second;
    const bool livePresent = liveStoreHasNotePair(liveStore, constrained.noteId, channel);
    NoteBaseline live{};
    const bool liveReadable =
        livePresent && readLiveLinearSpan(liveStore, constrained.noteId, channel, live);

    if (!constrained.visible) {
      if (livePresent) {
        actions.push_back(makeAction(EditSessionActionType::HideNote, constrained.noteId,
                                     baseline));
      }
      continue;
    }

    if (constrainedMatchesBaseline(constrained, baseline)) {
      if (!livePresent || !liveReadable || !baselineSpansEqual(live, baseline)) {
        actions.push_back(makeAction(EditSessionActionType::RestoreNote, constrained.noteId,
                                     baseline));
      }
      continue;
    }

    if (constrained.endTick < baseline.endTick) {
      const NoteBaseline shortened{constrained.pitch, baseline.velocity, constrained.startTick,
                                   constrained.endTick};
      if (!liveReadable || live.endTick != constrained.endTick) {
        actions.push_back(makeAction(EditSessionActionType::ShortenNote, constrained.noteId,
                                     shortened));
      }
    }
  }
}

void appendCausingNoteActions(const EditedGeometry& editedGeometry, const MidiEventVec& liveStore,
                              uint8_t channel, EditSessionActions& actions) {
  for (const EditedNoteSpan& causing : editedGeometry.causingSpans) {
    NoteBaseline live{};
    const bool liveReadable =
        readLiveLinearSpan(liveStore, causing.noteId, channel, live);
    if (!liveReadable) {
      continue;
    }
    if (baselineSpansEqual(causing.span, live)) {
      continue;
    }

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
void logEditSessionActions(const EditSessionActions& actions) {
  for (const EditSessionAction& action : actions) {
    logger.log(CAT_MIDI, LOG_DEBUG,
                 "EditSessionAction: type=%u noteId=%lu start=%lu end=%lu pitch=%u",
                 static_cast<unsigned>(action.type), static_cast<unsigned long>(action.targetNoteId),
                 static_cast<unsigned long>(action.startTick),
                 static_cast<unsigned long>(action.endTick), static_cast<unsigned>(action.pitch));
  }
}
#endif

EditSessionActions buildEditSessionActions(
    const std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>&
        constrainedGeometry,
    const EditedGeometry& editedGeometry, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel) {
  EditSessionActions actions;
  appendOverlapTargetActions(constrainedGeometry, transactionBaseline, liveStore, channel, actions);
  appendCausingNoteActions(editedGeometry, liveStore, channel, actions);

  std::sort(actions.begin(), actions.end(),
            [](const EditSessionAction& left, const EditSessionAction& right) {
              const int leftRank = actionTypeSortRank(left.type);
              const int rightRank = actionTypeSortRank(right.type);
              if (leftRank != rightRank) {
                return leftRank < rightRank;
              }
              return left.targetNoteId < right.targetNoteId;
            });
  return actions;
}
