//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionInteraction.h"

#include <algorithm>

namespace {

const NoteBaseline* findCausingSpan(NoteId noteId, const EditedGeometry& editedGeometry) {
  for (const EditedNoteSpan& entry : editedGeometry.causingSpans) {
    if (entry.noteId == noteId) {
      return &entry.span;
    }
  }
  return nullptr;
}

const NoteBaseline* findBaselineSpan(NoteId noteId, const BaselineMap& transactionBaseline) {
  const auto it = transactionBaseline.find(noteId);
  return it == transactionBaseline.end() ? nullptr : &it->second;
}

bool baselineSpansEqual(const NoteBaseline& left, const NoteBaseline& right) {
  return left.pitch == right.pitch && left.velocity == right.velocity &&
         left.startTick == right.startTick && left.endTick == right.endTick;
}

}  // namespace

bool isSelectedNote(NoteId noteId, const EditorSelection& selection) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (NoteId selectedId : selection.selectedNotes) {
    if (selectedId == noteId) {
      return true;
    }
  }
  return false;
}

bool isIntraSelectionPair(NoteId causingNoteId, NoteId targetNoteId,
                            const EditorSelection& selection) {
  return isSelectedNote(causingNoteId, selection) && isSelectedNote(targetNoteId, selection);
}

bool geometryChangedThisTick(NoteId causingNoteId, const NoteBaseline& priorLatch,
                             const NoteBaseline& currentSpan) {
  (void)causingNoteId;
  return !baselineSpansEqual(priorLatch, currentSpan);
}

std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineChangedCausingNotes(
    const EditorSelection& selection, const EditedGeometry& editedGeometry,
    const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId) {
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed;
  for (const EditedNoteSpan& entry : editedGeometry.causingSpans) {
    if (!isSelectedNote(entry.noteId, selection)) {
      continue;
    }
    const auto priorIt = priorLatchByNoteId.find(entry.noteId);
    if (priorIt == priorLatchByNoteId.end() ||
        geometryChangedThisTick(entry.noteId, priorIt->second, entry.span)) {
      changed.push_back(entry.noteId);
    }
  }
  std::sort(changed.begin(), changed.end());
  return changed;
}

std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>>
determineEligiblePairs(const EditorSelection& selection,
                       const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>&
                           changedCausingNoteIds,
                       const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>&
                           candidateTargetNoteIds) {
  std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs;
  for (NoteId causingNoteId : changedCausingNoteIds) {
    if (!isSelectedNote(causingNoteId, selection)) {
      continue;
    }
    for (NoteId targetNoteId : candidateTargetNoteIds) {
      if (targetNoteId == kInvalidNoteId || causingNoteId == targetNoteId) {
        continue;
      }
      if (isIntraSelectionPair(causingNoteId, targetNoteId, selection)) {
        continue;
      }
      pairs.push_back(CausingTargetPair{causingNoteId, targetNoteId});
    }
  }
  std::sort(pairs.begin(), pairs.end(),
            [](const CausingTargetPair& left, const CausingTargetPair& right) {
              if (left.causingNoteId != right.causingNoteId) {
                return left.causingNoteId < right.causingNoteId;
              }
              return left.targetNoteId < right.targetNoteId;
            });
  return pairs;
}

bool linearSpansOverlapForAnalysis(uint32_t causingStart, uint32_t causingEnd,
                                   uint32_t targetStart, uint32_t targetEnd) {
  return causingStart < targetEnd && targetStart < causingEnd;
}

bool isBoundaryTouchForAnalysis(uint32_t causingStart, uint32_t causingEnd, uint32_t targetStart,
                                uint32_t targetEnd) {
  if (linearSpansOverlapForAnalysis(causingStart, causingEnd, targetStart, targetEnd)) {
    return false;
  }
  return causingStart == targetEnd || causingEnd == targetStart;
}

InteractionType classifyEditSessionInteraction(uint32_t causingStart, uint32_t causingEnd,
                                               uint32_t targetStart, uint32_t targetEnd) {
  if (!linearSpansOverlapForAnalysis(causingStart, causingEnd, targetStart, targetEnd)) {
    return InteractionType::BoundaryTouch;
  }

  if (targetStart >= causingStart && targetEnd <= causingEnd) {
    return InteractionType::CompleteCover;
  }

  if (targetStart >= causingStart) {
    return InteractionType::OverlapNoteOn;
  }

  if (targetStart < causingStart && targetEnd > causingStart) {
    return InteractionType::OverlapNoteOff;
  }

  return InteractionType::OverlapNoteOff;
}

std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
analyzeEditSessionInteractions(
    const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>>&
        eligiblePairs,
    const EditedGeometry& editedGeometry, const BaselineMap& transactionBaseline) {
  std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>> out;
  for (const CausingTargetPair& pair : eligiblePairs) {
    const NoteBaseline* causingSpan = findCausingSpan(pair.causingNoteId, editedGeometry);
    const NoteBaseline* targetBaseline = findBaselineSpan(pair.targetNoteId, transactionBaseline);
    if (causingSpan == nullptr || targetBaseline == nullptr) {
      continue;
    }

    const uint32_t causingStart = causingSpan->startTick;
    const uint32_t causingEnd = causingSpan->endTick;
    const uint32_t targetStart = targetBaseline->startTick;
    const uint32_t targetEnd = targetBaseline->endTick;

    if (!linearSpansOverlapForAnalysis(causingStart, causingEnd, targetStart, targetEnd) &&
        !isBoundaryTouchForAnalysis(causingStart, causingEnd, targetStart, targetEnd)) {
      continue;
    }

    EditSessionInteraction interaction{};
    interaction.type =
        classifyEditSessionInteraction(causingStart, causingEnd, targetStart, targetEnd);
    interaction.causingNoteId = pair.causingNoteId;
    interaction.targetNoteId = pair.targetNoteId;
    interaction.causingSpan = *causingSpan;
    interaction.baselineSpan = *targetBaseline;
    out.push_back(interaction);
  }
  return out;
}

EditSessionInteractionsByTarget groupEditSessionInteractionsByTarget(
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>&
        interactions) {
  EditSessionInteractionsByTarget grouped;
  for (const EditSessionInteraction& interaction : interactions) {
    auto groupIt = std::find_if(
        grouped.groups.begin(), grouped.groups.end(),
        [&](const TargetNoteInteractionGroup& group) {
          return group.targetNoteId == interaction.targetNoteId;
        });
    if (groupIt == grouped.groups.end()) {
      TargetNoteInteractionGroup group{};
      group.targetNoteId = interaction.targetNoteId;
      group.incoming.push_back(interaction);
      grouped.groups.push_back(group);
    } else {
      groupIt->incoming.push_back(interaction);
    }
  }

  for (TargetNoteInteractionGroup& group : grouped.groups) {
    std::sort(group.incoming.begin(), group.incoming.end(),
              [](const EditSessionInteraction& left, const EditSessionInteraction& right) {
                return left.causingNoteId < right.causingNoteId;
              });
  }

  std::sort(grouped.groups.begin(), grouped.groups.end(),
            [](const TargetNoteInteractionGroup& left, const TargetNoteInteractionGroup& right) {
              return left.targetNoteId < right.targetNoteId;
            });
  return grouped;
}

uint32_t computeShortenedEndTick(const EditSessionInteraction& interaction, uint32_t loopLength) {
  (void)interaction;
  const uint32_t causingStart = interaction.causingSpan.startTick;
  if (causingStart == 0) {
    return loopLength > 0 ? loopLength - 1 : 0;
  }
  return causingStart - 1;
}

NoteBaseline projectNoteBaselineForEditAnalysis(const EditorSelection& selection,
                                                const NoteBaseline& baseline, NoteId noteId,
                                                uint32_t loopLength) {
  const TickInterval window = IntervalProjection::makeFullLoopEditAnalysisWindow(loopLength);
  const int32_t primaryStart =
      selection.primaryNote == noteId
          ? static_cast<int32_t>(baseline.startTick)
          : static_cast<int32_t>(baseline.startTick);
  const ProjectionContext context = IntervalProjection::buildEditProjectionContext(
      selection, loopLength, window, primaryStart);
  CanonicalNoteSpan span{};
  span.noteId = noteId;
  span.interval = TickInterval{static_cast<int32_t>(baseline.startTick),
                               static_cast<int32_t>(baseline.endTick)};
  span.pitch = baseline.pitch;
  span.velocity = baseline.velocity;
  const ProjectedNoteInterval projected =
      IntervalProjection::projectEditLinearSpan(span, context);
  NoteBaseline linear = baseline;
  if (projected.noteId != kInvalidNoteId) {
    linear.startTick = static_cast<uint32_t>(projected.interval.start);
    linear.endTick = static_cast<uint32_t>(projected.interval.end);
  }
  return linear;
}
