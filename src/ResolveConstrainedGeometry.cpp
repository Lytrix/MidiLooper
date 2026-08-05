//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionInteraction.h"
#include "EditSessionLiveStoreSpan.h"
#include "ResolveConstrainedGeometry.h"

#include <algorithm>

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
  return isSelectedNote(noteId, selection) || isCausingNoteInEditedGeometry(noteId, editedGeometry);
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

NOTE_EDIT_MEM bool interactionHasCompleteHidePrecedence(const EditSessionInteraction& interaction) {
  return interaction.type == InteractionType::OverlapNoteOn ||
         interaction.type == InteractionType::CompleteCover;
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

}  // namespace

NOTE_EDIT_MEM std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineConstrainedGeometryTargetNoteIds(
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength,
    const EditorSelection& selection, const EditedGeometry& editedGeometry) {
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
    if (liveStoreLinearSpanDiffersFromBaseline(noteId, baseline, liveStore, channel, loopLength)) {
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
  bool hasOverlapNoteOff = false;
  uint32_t shortestEndTick = baseline.endTick;

  for (const EditSessionInteraction& interaction : incomingInteractionsForTarget) {
    if (interactionHasCompleteHidePrecedence(interaction)) {
      completeHide = true;
      break;
    }
    if (interactionIsOverlapNoteOff(interaction)) {
      hasOverlapNoteOff = true;
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

  if (hasOverlapNoteOff) {
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
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength,
    uint32_t noteMinLengthTicks, bool noteMinLengthRemoveEnabled,
    const EditorSelection& selection, const EditedGeometry& editedGeometry) {
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targetIds =
      determineConstrainedGeometryTargetNoteIds(grouped, transactionBaseline, liveStore, channel,
                                                loopLength, selection, editedGeometry);

  std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>> out;
  for (NoteId targetNoteId : targetIds) {
    const auto baselineIt = transactionBaseline.find(targetNoteId);
    if (baselineIt == transactionBaseline.end()) {
      continue;
    }
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
        emptyIncoming;
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>*
        incoming = incomingForTarget(targetNoteId, grouped);
    const auto& incomingInteractions = incoming != nullptr ? *incoming : emptyIncoming;
    out.push_back(resolveConstrainedGeometry(targetNoteId, baselineIt->second, incomingInteractions,
                                             loopLength, noteMinLengthTicks,
                                             noteMinLengthRemoveEnabled));
  }
  return out;
}
