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

enum class EditPassType : uint8_t { Note, ControlChange, Audio };

enum class EditActionType : uint8_t { Create, Update, Delete };

enum class EditPropertyType : uint8_t {
  None,
  Pitch,
  Length,
  NoteRange,
  Velocity,
  Tick,
  Value,
};

/// Stable control-change target for scoped CC edit rows.
struct ControlChangeRef {
  uint8_t channel = 0;
  uint8_t controller = 0;
  uint32_t tick = 0;
  uint8_t value = 0;
};

using EditPassIdList = std::vector<EditPassId, InternalHeapFirstAllocator<EditPassId>>;

struct EditPass {
  EditPassId id = kInvalidEditPassId;
  EditPassType passType = EditPassType::Note;
  uint8_t editPassIndex = 0;
  EditActionType actionType = EditActionType::Update;
  EditPropertyType propertyType = EditPropertyType::None;
  EditPassState state = EditPassState::Active;

  NoteId targetNoteId = kInvalidNoteId;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  MidiEventVec addedEvents;
};

using EditPassVec = std::vector<EditPass, InternalHeapFirstAllocator<EditPass>>;
