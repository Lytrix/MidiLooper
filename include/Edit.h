//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "Utils/ExtMemAllocator.h"

using EditId = uint32_t;
constexpr EditId kInvalidEditId = 0;

enum class EditState : uint8_t { Active, Disabled };

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
using EditIdList = std::vector<EditId, ExtMemAllocator<EditId>>;

struct Edit {
  EditId id = kInvalidEditId;
  uint8_t spanIndex = 0;
  EditState state = EditState::Active;
  EditChangeList changes;
};

using EditVec = std::vector<Edit, ExtMemAllocator<Edit>>;
