// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstdint>
#include "Utils/PlaybackDeadlineClock.h"

enum class PhysicalCheckpointResult : uint8_t {
  Allowed, ReferenceUnavailable, CostUnavailable, PositionInvalidated,
  LogicalDeadlinePassed, PhysicalDeadlinePassed, InsufficientTime
};
struct PhysicalCheckpointCost {
  uint32_t maximumGrainUs = 0;
  uint32_t maximumReturnUs = 0;
  bool established = false; // Producer must enforce/classify the chosen owner's indivisible cost.
};
// Extended independent producer stamp: never an implicitly widened micros32.
// A reset/recycled producer must change era; zero is unqualified.
struct PhysicalClockStamp {
  uint64_t microseconds = 0;
  uint32_t era = 0;
};
// Passive snapshot, not a clock/arrival observer. Both endpoints and uncertainty
// belong to the independent producer; read/dispatch restamping cannot qualify it.
struct PlaybackPhysicalDeadline {
  uint64_t anchorTick = 0;
  uint64_t adoptionTick = 0;
  uint32_t clockEpoch = 0;
  uint32_t physicalEra = 0;
  uint64_t anchorUs = 0;
  uint64_t leadUs = 0;
  uint32_t uncertaintyUs = 0;
  bool independent = false;

  PhysicalCheckpointResult admit(PlaybackDeadlineClock current, PhysicalClockStamp now,
                                 const PhysicalCheckpointCost& cost) const {
    if (!independent || clockEpoch == 0 || physicalEra == 0 || leadUs == 0 ||
        adoptionTick <= anchorTick || anchorUs > UINT64_MAX - leadUs)
      return PhysicalCheckpointResult::ReferenceUnavailable;
    if (!cost.established || cost.maximumGrainUs == 0 || cost.maximumReturnUs == 0)
      return PhysicalCheckpointResult::CostUnavailable;
    if (current.epoch != clockEpoch || current.epoch == 0 || current.tick < anchorTick ||
        now.era != physicalEra || now.era == 0 || now.microseconds < anchorUs)
      return PhysicalCheckpointResult::PositionInvalidated;
    if (current.tick >= adoptionTick) return PhysicalCheckpointResult::LogicalDeadlinePassed;
    const uint64_t elapsed = now.microseconds - anchorUs;
    if (elapsed >= leadUs) return PhysicalCheckpointResult::PhysicalDeadlinePassed;
    const uint64_t required = uint64_t(cost.maximumGrainUs) + cost.maximumReturnUs + uncertaintyUs;
    return required < leadUs - elapsed
        ? PhysicalCheckpointResult::Allowed : PhysicalCheckpointResult::InsufficientTime;
  }
};
