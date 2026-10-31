//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// Runtime profiler names and row formats (DEC-239). Every profiler string literal lives in this TU:
// `linker/imxrt1062_t41_lcr.ld` places `*ProfileDiagRows.cpp.o(.rodata.str*)` in FLASH, because a few
// new literals elsewhere can cross the DTCM align edge (+4,096 B, DEC-216/DEC-225). Read-only; the TU
// owns no mutable state.

#include "Utils/ProfileDiagRows.h"

#if RUNTIME_PROFILE_SCOPES

#include <cstdio>

#if defined(ARDUINO) && !defined(PIO_UNIT_TEST_NATIVE)
#include <Arduino.h>
#define PDR_FLASH FLASHMEM
#else
#define PDR_FLASH
#endif

namespace ProfileDiagRows {

using RuntimeProfiler::ProfileId;

PDR_FLASH const char* profileName(ProfileId id) {
  switch (id) {
    case ProfileId::Calibration: return "Calibration";
    case ProfileId::SelfTest: return "SelfTest";
    case ProfileId::RecordStart_edge: return "RecordStart_edge";
    case ProfileId::PlaybackPrewarm_slot: return "PlaybackPrewarm_slot";
    case ProfileId::PlaybackPrewarm_views: return "PlaybackPrewarm_views";
    case ProfileId::ViewPrewarm_unit: return "ViewPrewarm_unit";
    case ProfileId::PlaybackWindow_build: return "PlaybackWindow_build";
    case ProfileId::PlaybackViewService_grain: return "PlaybackViewService_grain";
    case ProfileId::SnapshotPublish_step: return "SnapshotPublish_step";
    case ProfileId::SaveWorkItem_step: return "SaveWorkItem_step";
    case ProfileId::IdleOwner_unit: return "IdleOwner_unit";
    case ProfileId::DisplayBase_gather: return "DisplayBase_gather";
    case ProfileId::Display_frame_phase: return "Display_frame_phase";
    case ProfileId::LoadStep: return "LoadStep";
    case ProfileId::OverdubStart_body: return "OverdubStart_body";
    case ProfileId::Count: break;
  }
  return "Unknown";
}

PDR_FLASH bool formatBootRow(char* out, size_t capacity, const RuntimeProfiler::BootReport& r,
                             uint32_t nowUs) {
  if (out == nullptr || capacity == 0) return false;
  const int n = std::snprintf(
      out, capacity,
      "#CAP,%lu,DIAG,profile_boot,source=%u,cpu_hz=%lu,trust_us=%lu,samples=%lu,short_min_cycles=%lu,"
      "short_avg_cycles=%lu,long_min_cycles=%lu,long_avg_cycles=%lu,ids=%u,ring=%u,depth=%u",
      static_cast<unsigned long>(nowUs), r.sourceAvailable ? 1u : 0u, static_cast<unsigned long>(r.cpuHz),
      static_cast<unsigned long>(r.cycleTrustUs), static_cast<unsigned long>(r.samples),
      static_cast<unsigned long>(r.shortMinCycles), static_cast<unsigned long>(r.shortAvgCycles),
      static_cast<unsigned long>(r.longMinCycles), static_cast<unsigned long>(r.longAvgCycles),
      static_cast<unsigned>(ProfileId::Count), static_cast<unsigned>(RuntimeProfiler::kRingCapacity),
      static_cast<unsigned>(RuntimeProfiler::kMaxDepth));
  return n > 0 && static_cast<size_t>(n) < capacity;
}

}  // namespace ProfileDiagRows

#endif  // RUNTIME_PROFILE_SCOPES
