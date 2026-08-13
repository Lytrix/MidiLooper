//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Stage 1 content-only editing derivation (DEC-035). Reads LoopPasses fields only.
// Does not read GlobalUndoStack. Not a persist owner.

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "LoopPasses.h"
#include "PendingNoteChange.h"

enum class ContentUndoUnitKind : uint8_t {
  RecordPassAdded = 0,
  OverdubPassAdded = 1,
  NoteEditPassClosed = 2,
  ControlChangeEditPassClosed = 3,
};

struct ContentUndoUnit {
  ContentUndoUnitKind kind = ContentUndoUnitKind::RecordPassAdded;
  PassId primaryPassId = kInvalidPassId;
  uint8_t editPassIndex = 0;
  EditPassIdList editPassIds;
};

/// Group persisted content records into undo units using only Loop file fields:
/// pass id order, capture vs edit, and editPassIndex (255 = overdub companions).
///
/// Cannot derive ClearSlot (Set last-state) or LoopBoundaryChange (no geometry
/// content record). Session-local editPassIndex reuse collapses adjacent NOTE_EDIT
/// batches that share index 0 — see Stage 1 audit.
inline void deriveContentUndoUnits(const LoopPasses& passes, std::vector<ContentUndoUnit>& out) {
  out.clear();

  struct Row {
    enum class Type : uint8_t { Record, Overdub, Edit };
    Type type = Type::Record;
    PassId id = kInvalidPassId;
    uint8_t editPassIndex = 0;
    EditPassType editPassType = EditPassType::Note;
  };

  std::vector<Row> rows;
  if (passes.hasRecordPass()) {
    Row row;
    row.type = Row::Type::Record;
    row.id = passes.recordPass.id;
    rows.push_back(row);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id == kInvalidPassId) {
      continue;
    }
    Row row;
    row.type = Row::Type::Overdub;
    row.id = pass.id;
    rows.push_back(row);
  }
  for (const EditPass& pass : passes.editPasses) {
    if (pass.id == kInvalidEditPassId) {
      continue;
    }
    Row row;
    row.type = Row::Type::Edit;
    row.id = pass.id;
    row.editPassIndex = pass.editPassIndex;
    row.editPassType = pass.passType;
    rows.push_back(row);
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.id < b.id; });

  size_t i = 0;
  while (i < rows.size()) {
    const Row& row = rows[i];
    if (row.type == Row::Type::Record) {
      ContentUndoUnit unit;
      unit.kind = ContentUndoUnitKind::RecordPassAdded;
      unit.primaryPassId = row.id;
      out.push_back(std::move(unit));
      ++i;
      continue;
    }
    if (row.type == Row::Type::Overdub) {
      ContentUndoUnit unit;
      unit.kind = ContentUndoUnitKind::OverdubPassAdded;
      unit.primaryPassId = row.id;
      unit.editPassIndex = kOverdubCompanionEditPassIndex;
      ++i;
      while (i < rows.size() && rows[i].type == Row::Type::Edit &&
             rows[i].editPassIndex == kOverdubCompanionEditPassIndex) {
        unit.editPassIds.push_back(rows[i].id);
        ++i;
      }
      out.push_back(std::move(unit));
      continue;
    }
    ContentUndoUnit unit;
    unit.kind = (row.editPassType == EditPassType::ControlChange)
                    ? ContentUndoUnitKind::ControlChangeEditPassClosed
                    : ContentUndoUnitKind::NoteEditPassClosed;
    unit.primaryPassId = row.id;
    unit.editPassIndex = row.editPassIndex;
    unit.editPassIds.push_back(row.id);
    ++i;
    while (i < rows.size() && rows[i].type == Row::Type::Edit &&
           rows[i].editPassIndex == unit.editPassIndex &&
           rows[i].editPassType == row.editPassType &&
           rows[i].editPassIndex != kOverdubCompanionEditPassIndex) {
      unit.editPassIds.push_back(rows[i].id);
      ++i;
    }
    out.push_back(std::move(unit));
  }
}
