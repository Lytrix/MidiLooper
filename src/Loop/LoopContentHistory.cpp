//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopContentHistory.h"

#include <algorithm>

namespace {

UndoEntryKind undoEntryKindFromContentUnit(ContentUndoUnitKind kind) {
  switch (kind) {
    case ContentUndoUnitKind::RecordPassAdded:
      return UndoEntryKind::RecordPassAdded;
    case ContentUndoUnitKind::OverdubPassAdded:
      return UndoEntryKind::OverdubPassAdded;
    case ContentUndoUnitKind::NoteEditPassClosed:
      return UndoEntryKind::NoteEditPassClosed;
    case ContentUndoUnitKind::ControlChangeEditPassClosed:
      return UndoEntryKind::ControlChangeEditPassClosed;
    case ContentUndoUnitKind::LoopBoundaryChange:
      return UndoEntryKind::LoopBoundaryChange;
  }
  return UndoEntryKind::RecordPassAdded;
}

const LoopGeometry* findLoopGeometryById(const LoopPasses& passes, PassId id) {
  for (const LoopGeometry& geometry : passes.loopGeometries) {
    if (geometry.id == id) {
      return &geometry;
    }
  }
  return nullptr;
}

}  // namespace

LOOP_CONTENT_HISTORY_MEM void deriveContentUndoUnits(const LoopPasses& passes,
                                                     std::vector<ContentUndoUnit>& out,
                                                     bool effectiveOnly) {
  out.clear();

  struct Row {
    enum class Type : uint8_t { Record, Overdub, Edit, Geometry };
    Type type = Type::Record;
    PassId id = kInvalidPassId;
    uint8_t editPassIndex = 0;
    EditPassType editPassType = EditPassType::Note;
  };

  std::vector<Row> rows;
  if (passes.hasRecordPass() &&
      (!effectiveOnly || passes.recordPass.state == CapturePassState::Active)) {
    Row row;
    row.type = Row::Type::Record;
    row.id = passes.recordPass.id;
    rows.push_back(row);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id == kInvalidPassId) {
      continue;
    }
    if (effectiveOnly && pass.state != CapturePassState::Active) {
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
    if (effectiveOnly && pass.state != EditPassState::Active) {
      continue;
    }
    Row row;
    row.type = Row::Type::Edit;
    row.id = pass.id;
    row.editPassIndex = pass.editPassIndex;
    row.editPassType = pass.passType;
    rows.push_back(row);
  }
  for (const LoopGeometry& geometry : passes.loopGeometries) {
    if (geometry.id == kInvalidPassId) {
      continue;
    }
    if (effectiveOnly && geometry.state != LoopGeometryState::Active) {
      continue;
    }
    Row row;
    row.type = Row::Type::Geometry;
    row.id = geometry.id;
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
    if (row.type == Row::Type::Geometry) {
      ContentUndoUnit unit;
      unit.kind = ContentUndoUnitKind::LoopBoundaryChange;
      unit.primaryPassId = row.id;
      out.push_back(std::move(unit));
      ++i;
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

LOOP_CONTENT_HISTORY_MEM void deriveEffectiveContentUndoUnits(const LoopPasses& passes,
                                                              std::vector<ContentUndoUnit>& out) {
  deriveContentUndoUnits(passes, out, true);
}

LOOP_CONTENT_HISTORY_MEM void buildContentUndoEntries(const LoopPasses& passes, uint8_t slotIndex,
                                                      LoopId loopId, UndoEntryVec& out) {
  out.clear();
  std::vector<ContentUndoUnit> units;
  deriveEffectiveContentUndoUnits(passes, units);
  out.reserve(units.size());
  for (const ContentUndoUnit& unit : units) {
    UndoEntry entry;
    entry.kind = undoEntryKindFromContentUnit(unit.kind);
    entry.slotIndex = slotIndex;
    entry.loopId = loopId;
    entry.passId = unit.primaryPassId;
    entry.editPassIndex = unit.editPassIndex;
    entry.editPassIds = unit.editPassIds;
    if (unit.kind == ContentUndoUnitKind::ControlChangeEditPassClosed) {
      entry.editPassType = EditPassType::ControlChange;
    } else if (unit.kind == ContentUndoUnitKind::OverdubPassAdded ||
               unit.kind == ContentUndoUnitKind::NoteEditPassClosed) {
      entry.editPassType = EditPassType::Note;
    }
    if (unit.kind == ContentUndoUnitKind::LoopBoundaryChange) {
      const LoopGeometry* geometry = findLoopGeometryById(passes, unit.primaryPassId);
      if (geometry != nullptr) {
        entry.beforeLoopStartTick = geometry->beforeLoopStartTick;
        entry.beforeLoopLengthTicks = geometry->beforeLoopLengthTicks;
        entry.afterLoopStartTick = geometry->loopStartTick;
        entry.afterLoopLengthTicks = geometry->loopLengthTicks;
      }
    }
    out.push_back(std::move(entry));
  }
}
