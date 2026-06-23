//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "Utils/InternalHeapFirstAllocator.h"

using PassId = uint32_t;
constexpr PassId kInvalidPassId = 0;

using EditPassId = PassId;
constexpr EditPassId kInvalidEditPassId = kInvalidPassId;

enum class EditPassState : uint8_t { Active, Disabled };

enum class EditSessionType : uint8_t { Note, ControlChange, Audio };

enum class EditActionType : uint8_t { Create, Update, Delete };

enum class EditPropertyType : uint8_t {
  None,
  Pitch,
  Length,
  StartTick,
  EndTick,
  Tick,
  Value,
};

/// Legacy row discriminant kept for v4 read migration.
enum class EditPassKind : uint8_t { NoteEdit, ControlChange };

/// Legacy note-edit payload action kind used by EditChange during migration.
enum class EditChangeType : uint8_t {
  DeleteNote,
  AddNote,
  MoveNote,
  ChangePitch,
  ChangeLength,
};

/// Stable note target inside an EditChange (not a display index).
struct NoteRef {
  uint8_t channel = 0;
  uint8_t note = 0;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};

/// Stable control-change target for scoped CC edit rows.
struct ControlChangeRef {
  uint8_t channel = 0;
  uint8_t controller = 0;
  uint32_t tick = 0;
  uint8_t value = 0;
};

/// Legacy note-edit payload entry used until scoped payload migration is complete.
struct EditChange {
  EditChangeType type = EditChangeType::DeleteNote;
  NoteRef target{};
  uint8_t newPitch = 0;
  uint32_t newStartTick = 0;
  uint32_t newEndTick = 0;
  MidiEventVec addedEvents;
};

using EditChangeList = std::vector<EditChange, InternalHeapFirstAllocator<EditChange>>;
using EditPassIdList = std::vector<EditPassId, InternalHeapFirstAllocator<EditPassId>>;

struct EditPass {
  EditPassId id = kInvalidEditPassId;
  EditSessionType sessionType = EditSessionType::Note;
  uint8_t editPassIndex = 0;
  EditActionType actionType = EditActionType::Update;
  EditPropertyType propertyType = EditPropertyType::None;

  // Legacy fields retained during scoped payload migration.
  EditPassKind kind = EditPassKind::NoteEdit;
  uint8_t noteEditPassIndex = 0;
  EditPassState state = EditPassState::Active;

  // Legacy note-edit payload retained until scoped target/payload rows replace it.
  EditChangeList changes;
};

using EditPassVec = std::vector<EditPass, InternalHeapFirstAllocator<EditPass>>;
