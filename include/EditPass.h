//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "Utils/ExtMemAllocator.h"

using PassId = uint32_t;
constexpr PassId kInvalidPassId = 0;

using EditPassId = PassId;
constexpr EditPassId kInvalidEditPassId = kInvalidPassId;

enum class EditPassState : uint8_t { Active, Disabled };

enum class EditPassKind : uint8_t { NoteEdit, ControlChange };

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

struct EditChange {
  EditChangeType type = EditChangeType::DeleteNote;
  NoteRef target{};
  uint8_t newPitch = 0;
  uint32_t newStartTick = 0;
  uint32_t newEndTick = 0;
  MidiEventVec addedEvents;
};

using EditChangeList = std::vector<EditChange, ExtMemAllocator<EditChange>>;
using EditPassIdList = std::vector<EditPassId, ExtMemAllocator<EditPassId>>;

struct EditPass {
  EditPassId id = kInvalidEditPassId;
  EditPassKind kind = EditPassKind::NoteEdit;
  uint8_t noteEditPassIndex = 0;
  EditPassState state = EditPassState::Active;
  EditChangeList changes;
};

using EditPassVec = std::vector<EditPass, ExtMemAllocator<EditPass>>;
