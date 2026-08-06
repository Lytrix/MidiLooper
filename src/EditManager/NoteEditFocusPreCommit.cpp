//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "NoteEditFocusInternal.h"

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
    uint32_t loopLength) {
  EditPassVec rows;
  if (!focus.active) {
    return rows;
  }
  (void)loopLength;

  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    NoteBaseline live{};
    const bool hasLive =
        readLiveBaselineForOverlapDiff(sessionEvents, noteId, baseline, channel, loopLength,
                                       focus.movingNoteId, live);
    if (!hasLive) {
      // Delete authority: only a note the geometry pipeline hid may be removed. An unresolved
      // baseline entry (pass-materialize noteId vs session-store noteId) is preserved and
      // reported — a lookup miss must never destroy a note.
      if (!hasChangedOverlapNote(focus, noteId)) {
#if defined(SESSION_CAPTURE)
        logger.log(CAT_TRACK, LOG_WARNING,
                   "NOTE_EDIT pre-commit: baseline noteId=%lu pitch=%u start=%lu unresolved in "
                   "live store; preserved (no Delete row)",
                   static_cast<unsigned long>(noteId),
                   static_cast<unsigned>(baseline.pitch),
                   static_cast<unsigned long>(baseline.startTick));
#endif
        continue;
      }
      EditPass row = makeNoteEditRow(EditActionType::Delete, EditPropertyType::None);
      row.targetNoteId = noteId;
      rows.push_back(row);
      continue;
    }
    if (live.pitch != baseline.pitch) {
      continue;
    }
    if (!hasChangedOverlapNote(focus, noteId)) {
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

NOTE_EDIT_MEM EditPassVec buildPreCommitEditPasses(const NoteEditFocus& focus, uint8_t channel,
                                                   const MidiEventVec* sessionStoreEvents,
                                                   uint32_t loopLength) {
  EditPassVec rows;
  if (sessionStoreEvents != nullptr && loopLength > 0) {
    rows = buildPreCommitBaselineLiveDiffOverlapPasses(focus, *sessionStoreEvents, channel,
                                                       loopLength);
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
