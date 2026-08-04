//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionInteraction.h"
#include "ResolveConstrainedGeometry.h"

#include <algorithm>

namespace {

bool readLiveLinearSpan(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                        NoteBaseline& out) {
  const MidiEvent* noteOn = nullptr;
  for (const MidiEvent& event : liveStore) {
    if (event.type == midi::NoteOn && event.channel == channel && event.noteId == noteId) {
      noteOn = &event;
      break;
    }
  }
  if (noteOn == nullptr) {
    return false;
  }
  const MidiEvent* noteOff = nullptr;
  for (const MidiEvent& event : liveStore) {
    if (event.type == midi::NoteOff && event.channel == channel &&
        event.data.noteData.note == noteOn->data.noteData.note && event.tick >= noteOn->tick) {
      if (noteOff == nullptr || event.tick < noteOff->tick) {
        noteOff = &event;
      }
    }
  }
  if (noteOff == nullptr) {
    return false;
  }
  out.pitch = noteOn->data.noteData.note;
  out.velocity = noteOn->data.noteData.velocity;
  out.startTick = noteOn->tick;
  out.endTick = noteOff->tick;
  return true;
}

bool liveStoreLinearSpanDiffersFromBaseline(NoteId noteId, const NoteBaseline& baseline,
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

bool hasIncomingInteraction(NoteId targetNoteId,
                            const EditSessionInteractionsByTarget& grouped) {
  for (const TargetNoteInteractionGroup& group : grouped.groups) {
    if (group.targetNoteId == targetNoteId) {
      return !group.incoming.empty();
    }
  }
  return false;
}

const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>*
incomingForTarget(NoteId targetNoteId, const EditSessionInteractionsByTarget& grouped) {
  for (const TargetNoteInteractionGroup& group : grouped.groups) {
    if (group.targetNoteId == targetNoteId) {
      return &group.incoming;
    }
  }
  return nullptr;
}

bool interactionHasCompleteHidePrecedence(const EditSessionInteraction& interaction) {
  return interaction.type == InteractionType::OverlapNoteOn ||
         interaction.type == InteractionType::CompleteCover;
}

bool interactionIsOverlapNoteOff(const EditSessionInteraction& interaction) {
  return interaction.type == InteractionType::OverlapNoteOff;
}

bool interactionsAreBoundaryTouchOnly(
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

std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineConstrainedGeometryTargetNoteIds(
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength) {
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targets;
  for (const TargetNoteInteractionGroup& group : grouped.groups) {
    targets.push_back(group.targetNoteId);
  }

  for (const auto& [noteId, baseline] : transactionBaseline) {
    if (hasIncomingInteraction(noteId, grouped)) {
      continue;
    }
    if (liveStoreLinearSpanDiffersFromBaseline(noteId, baseline, liveStore, channel, loopLength)) {
      targets.push_back(noteId);
    }
  }

  std::sort(targets.begin(), targets.end());
  targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  return targets;
}

ConstrainedNoteGeometry resolveConstrainedGeometry(
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

  if (noteMinLengthRemoveEnabled && geometry.visible &&
      geometry.endTick > geometry.startTick &&
      (geometry.endTick - geometry.startTick) < noteMinLengthTicks) {
    geometry.visible = false;
  }

  return geometry;
}

std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>
resolveAllConstrainedGeometry(
    const EditSessionInteractionsByTarget& grouped, const BaselineMap& transactionBaseline,
    const MidiEventVec& liveStore, uint8_t channel, uint32_t loopLength,
    uint32_t noteMinLengthTicks, bool noteMinLengthRemoveEnabled) {
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targetIds =
      determineConstrainedGeometryTargetNoteIds(grouped, transactionBaseline, liveStore, channel,
                                                loopLength);

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
