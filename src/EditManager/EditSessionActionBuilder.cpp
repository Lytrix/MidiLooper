//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionActionBuilder.h"

#include <algorithm>

#include "EditSessionLiveStoreSpan.h"
#include "NoteEditFocus.h"
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

NOTE_EDIT_MEM void appendOverlapTargetActions(
    const std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>&
        constrainedGeometry,
    const EditedGeometry& editedGeometry, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, const NoteEditFocus& focus,
    EditSessionActions& actions) {
  for (const ConstrainedNoteGeometry& constrained : constrainedGeometry) {
    const auto baselineIt = transactionBaseline.find(constrained.noteId);
    if (baselineIt == transactionBaseline.end()) {
      continue;
    }
    const NoteBaseline& baseline = baselineIt->second;
    NoteId liveNoteId = constrained.noteId;
    if (!liveStoreHasNotePair(liveStore, liveNoteId, channel)) {
      const NoteId resolvedId =
          findLiveNoteIdForPitchStart(liveStore, channel, baseline.pitch, baseline.startTick);
      if (resolvedId != kInvalidNoteId && resolvedId != focus.movingNoteId &&
          liveStoreHasNotePair(liveStore, resolvedId, channel)) {
        liveNoteId = resolvedId;
      }
    }
    const bool livePresent = liveStoreHasNotePair(liveStore, liveNoteId, channel);
    NoteBaseline live{};
    const bool liveReadable =
        livePresent && readLiveLinearSpan(liveStore, liveNoteId, channel, live);

    if (!constrained.visible) {
      if (livePresent) {
        actions.push_back(makeAction(EditSessionActionType::HideNote, liveNoteId, baseline));
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
      const NoteBaseline reinsert{constrained.pitch, baseline.velocity, constrained.startTick,
                                  constrained.endTick};
      actions.push_back(makeAction(EditSessionActionType::RestoreNote, liveNoteId, reinsert));
      continue;
    }

    if (constrainedMatchesBaseline(constrained, baseline)) {
      if (!liveReadable || !baselineSpansEqual(live, baseline)) {
        if (causingSpanCompletelyCoversBaseline(editedGeometry, baseline)) {
          continue;
        }
        const NoteBaseline restoreSpan{constrained.pitch, baseline.velocity, constrained.startTick,
                                     constrained.endTick};
        actions.push_back(makeAction(EditSessionActionType::RestoreNote, liveNoteId, restoreSpan));
      }
      continue;
    }

    if (constrained.startTick > baseline.startTick &&
        constrained.endTick >= baseline.endTick) {
      const NoteBaseline headTrimmed{constrained.pitch, baseline.velocity, constrained.startTick,
                                     constrained.endTick};
      if (!liveReadable || live.startTick != constrained.startTick) {
        actions.push_back(makeAction(EditSessionActionType::MoveNote, liveNoteId, headTrimmed));
      }
    } else if (constrained.endTick < baseline.endTick) {
      const NoteBaseline shortened{constrained.pitch, baseline.velocity, constrained.startTick,
                                   constrained.endTick};
      if (!liveReadable || live.endTick != constrained.endTick) {
        actions.push_back(makeAction(EditSessionActionType::ShortenNote, liveNoteId, shortened));
      }
    }
  }
}

NOTE_EDIT_MEM bool readStoreLinearBaseline(MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                                           uint32_t preferredStartTick, uint32_t loopLength,
                                           NoteBaseline& out) {
  if (findLinearNoteSpanForNoteId(liveStore, noteId, channel, out, preferredStartTick,
                                  loopLength)) {
    return true;
  }
  if (preferredStartTick != UINT32_MAX &&
      findLinearNoteSpanForNoteId(liveStore, noteId, channel, out, UINT32_MAX, loopLength)) {
    return true;
  }
  return false;
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
                                            EditSessionActions& actions) {
  for (const EditedNoteSpan& causing : editedGeometry.causingSpans) {
    NoteBaseline storeSpan{};
    const bool hasStoreSpan = readStoreLinearBaseline(liveStore, causing.noteId, channel,
                                                      causing.span.startTick, loopLength, storeSpan);

    // Store is authoritative for skip. focus.last fallback is only for orphan-on emit
    // (session_20260804_231426) — never skip when the live store span still differs.
    if (hasStoreSpan && baselineSpansEqual(causing.span, storeSpan)) {
      continue;
    }

    if (!hasStoreSpan) {
      if (!causingNoteHasOrphanOnForAction(focus, liveStore, causing.noteId, channel,
                                           loopLength)) {
        continue;
      }
      if (baselineSpansEqual(causing.span, focus.last)) {
        continue;
      }
    }

    const NoteBaseline& live = hasStoreSpan ? storeSpan : focus.last;

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
    const EditedGeometry& editedGeometry, const BaselineMap& transactionBaseline,
    MidiEventVec& liveStore, uint8_t channel, const NoteEditFocus& focus,
    uint32_t loopLength) {
  EditSessionActions actions;
  appendOverlapTargetActions(constrainedGeometry, editedGeometry, transactionBaseline, liveStore,
                             channel, focus, actions);
  appendCausingNoteActions(editedGeometry, liveStore, focus, channel, loopLength, actions);

  sortEditSessionActions(actions);
  return actions;
}
