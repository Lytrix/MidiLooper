//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RunEditSessionGeometryPipeline.h"

#include <algorithm>
#include <unordered_set>

#include "ApplyEditSessionActions.h"
#include "EditManager.h"
#include "EditSessionActionBuilder.h"
#include "EditSessionInteraction.h"
#include "EditSessionLiveStoreSpan.h"
#include "Globals.h"
#include "ResolveConstrainedGeometry.h"
#include "TrackManager.h"
#include "Utils/NoteEditMem.h"

#if defined(SESSION_CAPTURE)
#include "Logger.h"
#endif

namespace {

NOTE_EDIT_MEM BaselineMap projectTransactionBaselineForAnalysis(const EditorSelection& selection,
                                                   const BaselineMap& transactionBaseline,
                                                   uint32_t loopLength) {
  BaselineMap projected;
  for (const auto& [noteId, baseline] : transactionBaseline) {
    projected[noteId] =
        projectNoteBaselineForEditAnalysis(selection, baseline, noteId, loopLength);
  }
  return projected;
}

NOTE_EDIT_MEM EditedGeometry projectEditedGeometryForAnalysis(const EditedGeometry& editedGeometry,
                                                uint32_t loopLength) {
  EditedGeometry projected = editedGeometry;
  for (EditedNoteSpan& causing : projected.causingSpans) {
    causing.span =
        projectNoteBaselineForEditAnalysis(editedGeometry.selection, causing.span, causing.noteId,
                                           loopLength);
  }
  return projected;
}

NOTE_EDIT_MEM std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> collectCandidateTargetNoteIds(
    const BaselineMap& transactionBaseline, const MidiEventVec& liveStore, NoteId movingNoteId,
    uint8_t channel) {
  std::unordered_set<NoteId> idSet;
  for (const auto& [noteId, baseline] : transactionBaseline) {
    (void)baseline;
    if (noteId == kInvalidNoteId || noteId == movingNoteId) {
      continue;
    }
    idSet.insert(noteId);
  }
  for (const MidiEvent& evt : liveStore) {
    if (evt.channel != channel || !evt.isNoteOn() || evt.data.noteData.velocity == 0) {
      continue;
    }
    if (evt.noteId == kInvalidNoteId || evt.noteId == movingNoteId) {
      continue;
    }
    idSet.insert(evt.noteId);
  }
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> candidates(idSet.begin(), idSet.end());
  sortNoteIdVector(candidates);
  return candidates;
}

}  // namespace

NOTE_EDIT_MEM bool runEditSessionGeometryPipeline(
    Track& track, EditManager& manager, const EditedGeometry& editedGeometry,
    const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId,
    std::optional<uint8_t> overlapPitchLane, bool refreshPlaybackPreview) {
  if (!manager.isNoteSessionStoreOpen()) {
    return false;
  }

  MidiEventVec& liveStore = track.editAwareMidiEvents();
  NoteEditFocus& focus = manager.getEditSession().focus;
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return false;
  }

  const uint8_t channel = track.getMidiChannel();
  const uint32_t loopLength = manager.noteEditLoopLengthTicks(track);
  if (loopLength == 0) {
    return false;
  }

  // D21: full-loop baseline — discover every live note missing from baselineMap so
  // cross-pitch overlap targets participate in analyze/restore (not same-pitch lane only).
  // Transaction baseline comes from committed materialize, not the mutating session store.
  Loop& loop = trackManager.getSelectedLoop(track);
  loop.assignMissingNoteIds(liveStore);
  stampNoteIdsOntoPairedNoteOffs(liveStore, channel);

  const MidiEventVec& committedEvents = manager.materializedLoopEventsForNoteEditFocus(track);
  enrichBaselineMapFromCommittedAndLive(focus.baselineMap, committedEvents, liveStore,
                                        focus.movingNoteId, channel, loopLength);
  (void)overlapPitchLane;

  const BaselineMap& transactionBaseline = focus.baselineMap;
  const EditorSelection& selection = editedGeometry.selection;

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changedCausingNotes =
      determineChangedCausingNotes(selection, editedGeometry, priorLatchByNoteId);
  if (changedCausingNotes.empty()) {
    return false;
  }

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> candidateTargetNoteIds =
      collectCandidateTargetNoteIds(transactionBaseline, liveStore, focus.movingNoteId, channel);

  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> eligiblePairs =
      determineEligiblePairs(selection, changedCausingNotes, candidateTargetNoteIds);

  const BaselineMap projectedBaseline =
      projectTransactionBaselineForAnalysis(selection, transactionBaseline, loopLength);
  const EditedGeometry projectedEdited =
      projectEditedGeometryForAnalysis(editedGeometry, loopLength);

  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      interactions =
          analyzeEditSessionInteractions(eligiblePairs, projectedEdited, projectedBaseline);

  const EditSessionInteractionsByTarget grouped =
      groupEditSessionInteractionsByTarget(interactions);

  const std::vector<ConstrainedNoteGeometry,
                     InternalHeapFirstAllocator<ConstrainedNoteGeometry>> constrained =
      resolveAllConstrainedGeometry(grouped, transactionBaseline, liveStore, channel, loopLength,
                                    noteMinLengthTicks, noteMinLengthRemoveEnabled, selection,
                                    editedGeometry);

  const EditSessionActions actions =
      buildEditSessionActions(constrained, editedGeometry, transactionBaseline, liveStore,
                              channel, focus, loopLength);

  if (actions.empty()) {
    return false;
  }

#if defined(SESSION_CAPTURE)
  logger.log(CAT_MIDI, LOG_DEBUG,
             "GeometryPipeline: baselineMap=%u candidates=%u pairs=%u interactions=%u "
             "constrained=%u actions=%u",
             static_cast<unsigned>(transactionBaseline.size()),
             static_cast<unsigned>(candidateTargetNoteIds.size()),
             static_cast<unsigned>(eligiblePairs.size()),
             static_cast<unsigned>(interactions.size()),
             static_cast<unsigned>(constrained.size()), static_cast<unsigned>(actions.size()));
  logEditSessionActions(actions);
#endif

  applyEditSessionActions(actions, liveStore, focus, channel, loopLength);
  track.invalidateCaches(refreshPlaybackPreview);
  return true;
}

/// Single-causing-note entry — latch map built in FLASHMEM (keeps std::unordered_map out of
/// NoteMovementUtils translation unit / RAM1 ITCM). Declared in RunEditSessionGeometryPipelineDriver.h.
NOTE_EDIT_MEM bool runEditSessionGeometryPipelineForCausingNote(
    Track& track, EditManager& manager, NoteId causingNoteId, const NoteBaseline& editedSpan,
    const NoteBaseline& priorLatch, std::optional<uint32_t> selectionTick,
    std::optional<uint8_t> overlapPitchLane, bool refreshPlaybackPreview) {
  if (causingNoteId == kInvalidNoteId) {
    return false;
  }

  std::unordered_map<NoteId, NoteBaseline, NoteIdHash> priorLatchByNoteId;
  priorLatchByNoteId[causingNoteId] = priorLatch;

  EditedGeometry editedGeometry{};
  editedGeometry.selection = manager.getNoteEditSessionState().selection;
  if (selectionTick.has_value()) {
    editedGeometry.selection.selectedTick = selectionTick.value();
  }
  // Geometry drivers edit focus.movingNoteId; fader-1 browse may leave selection on another note.
  editedGeometry.selection.primaryNote = causingNoteId;
  editedGeometry.selection.selectedNotes.clear();
  editedGeometry.selection.selectedNotes.push_back(causingNoteId);

  EditedNoteSpan causingSpan{};
  causingSpan.noteId = causingNoteId;
  causingSpan.span = editedSpan;
  editedGeometry.causingSpans.push_back(causingSpan);

  return runEditSessionGeometryPipeline(track, manager, editedGeometry, priorLatchByNoteId,
                                        overlapPitchLane, refreshPlaybackPreview);
}
