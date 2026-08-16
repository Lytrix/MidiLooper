//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteGeometryResolver.h"

#include <algorithm>
#include <unordered_set>

#include "ApplyEditSessionActions.h"
#include "EditManager.h"
#include "EditSessionActionBuilder.h"
#include "EditSessionInteraction.h"
#include "EditSessionLiveStoreSpan.h"
#include "Globals.h"
#include "NoteEditCurrentState.h"
#include "ParticipatingNoteSession.h"
#include "ResolveConstrainedGeometry.h"
#include "Utils/NoteEditMem.h"

#if defined(SESSION_CAPTURE)
#include "Arduino.h"
#include "EditManagerInternal.h"
#include "Logger.h"
#endif

NOTE_EDIT_MEM bool NoteGeometryResolver::resolve(
    Track& track, EditManager& manager, const EditedGeometry& editedGeometry,
    const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId,
    std::optional<uint8_t> overlapPitchLane, bool refreshPlaybackPreview) {
    if (!manager.isNoteSessionStoreOpen()) {
        return false;
    }

    // Projection flat for audition/read helpers; geometry mutations go through currentState
    // (applyEditSessionActions → projectToSessionStore). Always pass non-const currentState so
    // an empty map builds from the store instead of taking the live-store-only apply path.
    MidiEventVec& liveStore = track.editAwareMidiEvents();
    NoteEditFocus& focus = manager.getEditSession().focus;
    NoteEditCurrentState& currentState = manager.getEditSession().noteEditCurrentState;
    const NoteEditCurrentState* currentStateReader =
        currentState.empty() ? nullptr : &currentState;
    if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
        return false;
    }

    const uint8_t channel = track.getMidiChannel();
    const uint32_t loopLength = manager.noteEditLoopLengthTicks(track);
    if (loopLength == 0) {
        return false;
    }

#if defined(SESSION_CAPTURE)
    const uint32_t setupStartUs = micros();
#endif
    // noteIds are assigned once in openNoteEditSession — never mint mid-edit (Delete rows
    // must resolve against capture-pass materialize). Stamp offs only.
    stampNoteIdsOntoPairedNoteOffs(liveStore);
#if defined(SESSION_CAPTURE)
    size_t liveNoteOnCount = 0;
    for (const MidiEvent& evt : liveStore) {
        if (!evt.isNoteOn() || evt.data.noteData.velocity == 0) {
            continue;
        }
        ++liveNoteOnCount;
        if (evt.noteId == kInvalidNoteId) {
            logger.log(CAT_MIDI, LOG_WARNING,
                       "NoteGeometryResolver: note-on missing noteId tick=%lu pitch=%u (session open "
                       "should have assigned)",
                       static_cast<unsigned long>(evt.tick),
                       static_cast<unsigned>(evt.data.noteData.note));
        }
    }
#endif

    const BaselineMap& transactionBaseline = focus.baselineMap;
    const EditorSelection& selection = editedGeometry.selection;

    const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changedCausingNotes =
        determineChangedCausingNotes(selection, editedGeometry, priorLatchByNoteId);
    const bool overlapRestoreOnly =
        changedCausingNotes.empty() && hasOverlapParticipants(focus, currentStateReader);
    if (changedCausingNotes.empty() && !overlapRestoreOnly) {
        return false;
    }

    manager.ensureCurrentStateVisibleRowsFromVisualCache(track);
    const NoteUtils::DisplayNoteVec& committedDisplayNotes =
        manager.visualCacheNotesForSelectedSlot(track);
    overlayUneditedBaselineMapFromDisplayNotes(focus, currentStateReader, committedDisplayNotes);

    const NoteIdList evaluationScope =
        collectEvaluationScopeNoteIds(transactionBaseline, liveStore, focus.movingNoteId,
                                      overlapPitchLane, currentStateReader);

    ensureBaselineMapEntriesForEvaluationScope(focus, evaluationScope, liveStore, channel,
                                               currentStateReader, &committedDisplayNotes);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("setup", micros() - setupStartUs,
                      static_cast<uint32_t>(evaluationScope.size()),
                      static_cast<uint32_t>(focus.baselineMap.size()));
    const uint32_t analyzeStartUs = micros();
#endif
    const BaselineMap& transactionBaselineAfterEnsure = focus.baselineMap;

    const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> eligiblePairs =
        determineEligiblePairs(selection, changedCausingNotes, evaluationScope);

    std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> overlapPairs;
    overlapPairs.reserve(eligiblePairs.size());
    for (const CausingTargetPair& pair : eligiblePairs) {
      overlapPairs.push_back(pair);
    }

    const NoteBaseline* causingSpan = nullptr;
    for (const EditedNoteSpan& causing : editedGeometry.causingSpans) {
      if (causing.noteId == focus.movingNoteId) {
        causingSpan = &causing.span;
        break;
      }
    }
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("pairs", micros() - analyzeStartUs,
                      static_cast<uint32_t>(eligiblePairs.size()),
                      static_cast<uint32_t>(evaluationScope.size()));
    const uint32_t overlayStartUs = micros();
#endif

    const BaselineMap analysisBaseline =
        overlayAnalysisBaselineForSessionMovedOverlaps(transactionBaselineAfterEnsure,
                                                       focus.movingNoteId, liveStore, channel,
                                                       loopLength, currentStateReader,
                                                       causingSpan, &committedDisplayNotes);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("overlay", micros() - overlayStartUs,
                      static_cast<uint32_t>(analysisBaseline.size()),
                      static_cast<uint32_t>(transactionBaselineAfterEnsure.size()));
    const uint32_t interactStartUs = micros();
#endif
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
        interactions =
            analyzeEditSessionInteractions(overlapPairs, editedGeometry, analysisBaseline);

    const EditSessionInteractionsByTarget grouped =
        groupEditSessionInteractionsByTarget(interactions);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("interact", micros() - interactStartUs,
                      static_cast<uint32_t>(interactions.size()),
                      static_cast<uint32_t>(overlapPairs.size()));
    const uint32_t constrainStartUs = micros();
#endif

    NoteIdList leaveRestoreTargetNoteIds;
    const std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>
        constrained = resolveAllConstrainedGeometry(
            grouped, analysisBaseline, transactionBaselineAfterEnsure, liveStore,
            channel, loopLength, noteMinLengthTicks, noteMinLengthRemoveEnabled, selection,
            editedGeometry, focus, leaveRestoreTargetNoteIds, currentStateReader,
            &committedDisplayNotes);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("constrain", micros() - constrainStartUs,
                      static_cast<uint32_t>(constrained.size()),
                      static_cast<uint32_t>(leaveRestoreTargetNoteIds.size()));
    const uint32_t buildStartUs = micros();
#endif

    const EditSessionActions actions =
        buildEditSessionActions(constrained, editedGeometry, analysisBaseline,
                                transactionBaselineAfterEnsure, leaveRestoreTargetNoteIds, liveStore,
                                channel, focus, loopLength, currentStateReader);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("build", micros() - buildStartUs,
                      static_cast<uint32_t>(actions.size()), 0);
    logGeomApplyPhase("analyze", micros() - analyzeStartUs,
                      static_cast<uint32_t>(actions.size()),
                      static_cast<uint32_t>(interactions.size()));
#endif

    if (actions.empty()) {
        return false;
    }

#if defined(SESSION_CAPTURE)
    const unsigned participantCount =
        currentStateReader != nullptr && !currentStateReader->empty()
            ? static_cast<unsigned>(
                  collectOverlapParticipantNoteIdsFromCurrentState(*currentStateReader,
                                                                   focus.movingNoteId)
                      .size())
            : 0u;
    logger.log(CAT_MIDI, LOG_DEBUG,
               "NoteGeometryResolver: storeNoteOns=%u baselineMap=%u lane=%d changed=%u candidates=%u "
               "pairs=%u interactions=%u constrained=%u actions=%u",
               static_cast<unsigned>(liveNoteOnCount),
               static_cast<unsigned>(transactionBaselineAfterEnsure.size()),
               overlapPitchLane.has_value() ? static_cast<int>(overlapPitchLane.value()) : -1,
               participantCount,
               static_cast<unsigned>(evaluationScope.size()),
               static_cast<unsigned>(eligiblePairs.size()),
               static_cast<unsigned>(interactions.size()),
               static_cast<unsigned>(constrained.size()),
               static_cast<unsigned>(actions.size()));
    logEditSessionActions(actions);
#endif

#if defined(SESSION_CAPTURE)
    const uint32_t applyStartUs = micros();
#endif
    applyEditSessionActions(actions, liveStore, focus, channel, loopLength,
                            &manager.getEditSession().applyOwnedEditPassRows,
                            &currentState);
#if defined(SESSION_CAPTURE)
    logGeomApplyPhase("apply", micros() - applyStartUs,
                      static_cast<uint32_t>(actions.size()), 0);
#endif
    manager.bumpSessionPreviewRevision();
    track.invalidateCaches(refreshPlaybackPreview);
    return true;
}

NOTE_EDIT_MEM bool NoteGeometryResolver::resolveForCausingNote(
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
    editedGeometry.selection.primaryNote = causingNoteId;
    editedGeometry.selection.selectedNotes.clear();
    editedGeometry.selection.selectedNotes.push_back(causingNoteId);

    EditedNoteSpan causingSpan{};
    causingSpan.noteId = causingNoteId;
    causingSpan.span = editedSpan;
    editedGeometry.causingSpans.push_back(causingSpan);

    return resolve(track, manager, editedGeometry, priorLatchByNoteId, overlapPitchLane,
                   refreshPlaybackPreview);
}
