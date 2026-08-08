//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditCurrentState.h"
#include "NoteEditSessionState.h"
#include "NoteEditFocusInternal.h"
#include "ParticipatingNoteSession.h"

#include <algorithm>

#include "Utils/NoteEditMem.h"

#if defined(SESSION_CAPTURE)
#include "Logger.h"
#endif

NOTE_EDIT_FOCUS_INTERNAL_MEM EditPass makeNoteEditRow(EditActionType actionType,
                                                      EditPropertyType propertyType) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = actionType;
  row.propertyType = propertyType;
  row.state = EditPassState::Active;
  return row;
}

NOTE_EDIT_MEM EditPassVec buildPreCommitBaselineLiveDiffOverlapPasses(
    const NoteEditFocus& focus, const MidiEventVec& sessionEvents, uint8_t channel,
    uint32_t loopLength, const NoteEditCurrentState* currentState) {
  EditPassVec rows;
  if (!focus.active) {
    return rows;
  }
  (void)loopLength;
  if (currentState == nullptr || currentState->empty()) {
    return rows;
  }

  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    const NoteEditCurrentNoteState* stateRow = currentState->find(noteId);
    if (stateRow == nullptr || !currentStateRowIsOverlapParticipant(*stateRow)) {
      continue;
    }
    NoteBaseline live{};
    const bool hasLive =
        readLiveBaselineForOverlapDiff(sessionEvents, noteId, baseline, channel, loopLength,
                                       focus.movingNoteId, live);
    if (!hasLive) {
      // Delete authority: only an Active overlap participant may be removed. An unresolved
      // baseline entry (pass-materialize noteId vs session-store noteId) is preserved and
      // reported — a lookup miss must never destroy a note.
      EditPass row = makeNoteEditRow(EditActionType::Delete, EditPropertyType::None);
      row.targetNoteId = noteId;
      rows.push_back(row);
      continue;
    }
    if (live.pitch != baseline.pitch) {
      continue;
    }
    if (loopLength > 0 &&
        !isPlausibleStorageSpan(live.startTick, live.endTick, loopLength)) {
#if defined(SESSION_CAPTURE)
      logger.log(CAT_TRACK, LOG_WARNING,
                 "NOTE_EDIT pre-commit: overlap noteId=%lu live span start=%lu end=%lu implausible; "
                 "skipped",
                 static_cast<unsigned long>(noteId),
                 static_cast<unsigned long>(live.startTick),
                 static_cast<unsigned long>(live.endTick));
#endif
      continue;
    }
    if (live.startTick != baseline.startTick) {
      EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
      row.targetNoteId = noteId;
      row.startTick = live.startTick;
      row.endTick = live.endTick;
      rows.push_back(row);
    } else if (live.endTick != baseline.endTick) {
      EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
      row.targetNoteId = noteId;
      row.startTick = baseline.startTick;
      row.endTick = live.endTick;
      rows.push_back(row);
    }
  }
  return rows;
}

namespace {

NOTE_EDIT_MEM bool isPlausibleMoverLinearSpan(uint32_t startTick, uint32_t endTick,
                                              uint32_t loopLength) {
  if (endTick <= startTick) {
    return false;
  }
  if (loopLength == 0) {
    return false;
  }
  return startTick < loopLength;
}

NOTE_EDIT_MEM bool shouldRejectMoverPreCommitRow(const EditPass& row, const NoteEditFocus& focus,
                                                 uint32_t loopLength) {
  if (!focus.active || row.targetNoteId != focus.movingNoteId) {
    return false;
  }
  if (row.actionType != EditActionType::Update) {
    return false;
  }
  if (row.propertyType == EditPropertyType::NoteRange) {
    if (row.startTick != focus.last.startTick || row.endTick != focus.last.endTick) {
      return true;
    }
    if (!isPlausibleMoverLinearSpan(row.startTick, row.endTick, loopLength)) {
      return true;
    }
    if (row.startTick == 0 && focus.commitBaseline.startTick != 0) {
      return true;
    }
    return false;
  }
  if (row.propertyType == EditPropertyType::Length) {
    if (row.startTick != focus.commitBaseline.startTick || row.endTick != focus.last.endTick) {
      return true;
    }
    if (focus.commitBaseline.endTick >= loopLength && focus.last.endTick < loopLength) {
      return true;
    }
    return !isPlausibleMoverLinearSpan(row.startTick, row.endTick, loopLength);
  }
  return false;
}

NOTE_EDIT_MEM void removeInvalidMoverPreCommitRows(EditPassVec& rows, const NoteEditFocus& focus,
                                                   uint32_t loopLength) {
  const auto invalid = [&](const EditPass& row) {
    return shouldRejectMoverPreCommitRow(row, focus, loopLength);
  };
  rows.erase(std::remove_if(rows.begin(), rows.end(), invalid), rows.end());
}

}  // namespace

NOTE_EDIT_MEM EditPassVec buildCommitOverlapRowsFromCurrentState(
    const NoteEditFocus& focus, const NoteEditCurrentState& currentState, uint8_t channel,
    uint32_t loopLength) {
  EditPassVec rows;
  if (!focus.active) {
    return rows;
  }
  (void)channel;

  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }

    const NoteEditCurrentNoteState* row = currentState.find(noteId);
    // §11 step 5.4: commit overlap authority from current-state participation, not Focus latch.
    if (row == nullptr || !currentStateRowIsOverlapParticipant(*row)) {
      continue;
    }
    if (currentStateRowLifecycleIsDeleted(*row) || !currentStateRowIsVisible(*row)) {
      EditPass deleteRow = makeNoteEditRow(EditActionType::Delete, EditPropertyType::None);
      deleteRow.targetNoteId = noteId;
      rows.push_back(std::move(deleteRow));
      continue;
    }

    const NoteBaseline& live = row->currentSpan;
    if (live.pitch != baseline.pitch) {
      continue;
    }

    const ParticipatingNoteState participant = buildParticipatingNoteState(*row);
    if (participatingNoteVisibleOverlapTailInProgress(participant, focus.last)) {
      continue;
    }

    if (loopLength > 0 && !isPlausibleStorageSpan(live.startTick, live.endTick, loopLength)) {
#if defined(SESSION_CAPTURE)
      logger.log(CAT_TRACK, LOG_WARNING,
                 "NOTE_EDIT commit: overlap noteId=%lu current span start=%lu end=%lu implausible; "
                 "skipped",
                 static_cast<unsigned long>(noteId),
                 static_cast<unsigned long>(live.startTick),
                 static_cast<unsigned long>(live.endTick));
#endif
      continue;
    }
    if (live.startTick != baseline.startTick) {
      EditPass updateRow = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
      updateRow.targetNoteId = noteId;
      updateRow.startTick = live.startTick;
      updateRow.endTick = live.endTick;
      rows.push_back(std::move(updateRow));
    } else if (live.endTick != baseline.endTick) {
      EditPass updateRow = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
      updateRow.targetNoteId = noteId;
      updateRow.startTick = baseline.startTick;
      updateRow.endTick = live.endTick;
      rows.push_back(std::move(updateRow));
    }
  }

  for (const auto& [noteId, row] : currentState.rows()) {
    if (!currentStateRowLifecycleIsAdded(row)) {
      continue;
    }
    if (focus.baselineMap.find(noteId) != focus.baselineMap.end()) {
      continue;
    }
    const NoteBaseline& span = row.currentSpan;
    EditPass createRow = makeNoteEditRow(EditActionType::Create, EditPropertyType::None);
    MidiEvent noteOn = MidiEvent::NoteOn(span.startTick, channel, span.pitch, span.velocity);
    noteOn.noteId = noteId;
    createRow.addedEvents.push_back(noteOn);
    createRow.addedEvents.push_back(MidiEvent::NoteOff(span.endTick, channel, span.pitch, 0));
    rows.push_back(std::move(createRow));
  }

  return rows;
}

NOTE_EDIT_MEM EditPassVec buildCommitRowsFromCurrentState(const NoteEditFocus& focus,
                                                          const NoteEditCurrentState& currentState,
                                                          uint8_t channel, uint32_t loopLength) {
  EditPassVec rows;
  if (loopLength > 0 && !currentState.empty()) {
    rows = buildCommitOverlapRowsFromCurrentState(focus, currentState, channel, loopLength);
  }
  if (!focus.active) {
    return rows;
  }
  (void)channel;

  const bool startChanged = focus.last.startTick != focus.commitBaseline.startTick;
  const bool endChanged = focus.last.endTick != focus.commitBaseline.endTick;
  const bool pitchChanged = focus.last.pitch != focus.commitBaseline.pitch;

  if (startChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
    row.targetNoteId = focus.movingNoteId;
    row.startTick = focus.last.startTick;
    row.endTick = focus.last.endTick;
    rows.push_back(row);
  } else if (endChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
    row.targetNoteId = focus.movingNoteId;
    row.startTick = focus.commitBaseline.startTick;
    row.endTick = focus.last.endTick;
    rows.push_back(row);
  }

  if (pitchChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Pitch);
    row.targetNoteId = focus.movingNoteId;
    row.pitch = focus.last.pitch;
    rows.push_back(row);
  }

  if (loopLength > 0) {
    removeInvalidMoverPreCommitRows(rows, focus, loopLength);
  }

  return rows;
}

NOTE_EDIT_MEM EditPassVec buildPreCommitEditPasses(const NoteEditFocus& focus, uint8_t channel,
                                                   const MidiEventVec* sessionStoreEvents,
                                                   uint32_t loopLength,
                                                   const NoteEditCurrentState* currentState) {
  EditPassVec rows;
  if (sessionStoreEvents != nullptr && loopLength > 0) {
    rows = buildPreCommitBaselineLiveDiffOverlapPasses(focus, *sessionStoreEvents, channel,
                                                       loopLength, currentState);
  }
  if (!focus.active) {
    return rows;
  }
  (void)channel;

  const bool startChanged = focus.last.startTick != focus.commitBaseline.startTick;
  const bool endChanged = focus.last.endTick != focus.commitBaseline.endTick;
  const bool pitchChanged = focus.last.pitch != focus.commitBaseline.pitch;

  if (startChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
    row.targetNoteId = focus.movingNoteId;
    row.startTick = focus.last.startTick;
    row.endTick = focus.last.endTick;
    rows.push_back(row);
  } else if (endChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
    row.targetNoteId = focus.movingNoteId;
    row.startTick = focus.commitBaseline.startTick;
    row.endTick = focus.last.endTick;
    rows.push_back(row);
  }

  if (pitchChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Pitch);
    row.targetNoteId = focus.movingNoteId;
    row.pitch = focus.last.pitch;
    rows.push_back(row);
  }

  if (loopLength > 0) {
    removeInvalidMoverPreCommitRows(rows, focus, loopLength);
  }

  return rows;
}
