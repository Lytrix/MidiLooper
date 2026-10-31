//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// Runtime profiler names and row formats (DEC-239). Every profiler string literal lives in this TU:
// `linker/imxrt1062_t41_lcr.ld` places `*ProfileDiagRows.cpp.o(.rodata.str*)` in FLASH, because a few
// new literals elsewhere can cross the DTCM align edge (+4,096 B, DEC-216/DEC-225). Read-only; the TU
// owns no mutable state.

#include "Utils/ProfileDiagRows.h"

#if RUNTIME_PROFILE_SCOPES

#include <cstdio>
#include <cstring>

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

namespace {

using RuntimeProfiler::counters;
using RuntimeProfiler::DumpCursors;
using RuntimeProfiler::dumpCursors;
using RuntimeProfiler::ProfileRingRecord;
using RuntimeProfiler::ProfileStat;
namespace Platform = RuntimeProfiler::Platform;

PDR_FLASH void emitFormatted(char* line, int n) {
  if (n > 0 && static_cast<size_t>(n) < kRowCapacity) Platform::emitRow(line);
}

PDR_FLASH const char* nameOrNone(uint16_t id) {
  return id < static_cast<uint16_t>(ProfileId::Count) ? profileName(static_cast<ProfileId>(id)) : "none";
}

PDR_FLASH void emitStatRow(size_t index) {
  const ProfileStat& st = RuntimeProfiler::statAt(index);
  const uint64_t avg = st.calls == 0 ? 0 : RuntimeProfiler::cyclesToUs(st.inclusiveCycles / st.calls);
  const uint64_t avgSelf = st.calls == 0 ? 0 : RuntimeProfiler::cyclesToUs(st.selfCycles / st.calls);
  char line[kRowCapacity];
  const int n = std::snprintf(
      line, sizeof(line),
      "#CAP,%lu,DIAG,profile_stat,%u,%s,calls=%lu,avg_us=%lu,avg_self_us=%lu,max_us=%lu,last_us=%lu,"
      "over=%lu,ref=%lu,order=%u,coarse=%u,last_result=%u",
      static_cast<unsigned long>(Platform::micros()), static_cast<unsigned>(index),
      profileName(static_cast<ProfileId>(index)), static_cast<unsigned long>(st.calls),
      static_cast<unsigned long>(avg), static_cast<unsigned long>(avgSelf),
      static_cast<unsigned long>(st.maxUs), static_cast<unsigned long>(st.lastUs),
      static_cast<unsigned long>(st.overCeiling), static_cast<unsigned long>(st.refused),
      static_cast<unsigned>(st.orderFaults),
      static_cast<unsigned>((st.flags & RuntimeProfiler::kStatFlagCoarse) != 0 ? 1u : 0u),
      static_cast<unsigned>(st.lastResult));
  emitFormatted(line, n);
}

PDR_FLASH void emitRingRow(const ProfileRingRecord& r) {
  char line[kRowCapacity];
  const int n = std::snprintf(
      line, sizeof(line),
      "#CAP,%lu,DIAG,profile_ring,%lu,t_us=%lu,id=%u,name=%s,parent=%s,dur_us=%lu,trk=%u,slot=%u,"
      "result=%u,src=%u",
      static_cast<unsigned long>(Platform::micros()), static_cast<unsigned long>(r.seq),
      static_cast<unsigned long>(r.timestampUs), static_cast<unsigned>(r.id), nameOrNone(r.id),
      nameOrNone(r.parentId), static_cast<unsigned long>(r.micros), static_cast<unsigned>(r.track),
      static_cast<unsigned>(r.slot), static_cast<unsigned>(r.result),
      static_cast<unsigned>((r.flags & RuntimeProfiler::kRingFlagCoarse) != 0 ? 1u : 0u));
  emitFormatted(line, n);
}

}  // namespace

PDR_FLASH size_t dumpStatsBatch(size_t maxRows) {
  DumpCursors& c = dumpCursors();
  const bool continuing = c.statsActive;
  const size_t count = static_cast<size_t>(ProfileId::Count);
  size_t index = continuing ? c.statsNext : 0;
  size_t emitted = 0;
  while (index < count && emitted < maxRows) {
    if (index != static_cast<size_t>(ProfileId::Calibration)) {  // reserved; never dumped
      emitStatRow(index);
      ++emitted;
    }
    ++index;
  }
  const bool resume = index < count;
  char line[kRowCapacity];
  const auto& k = counters();
  const int n = std::snprintf(
      line, sizeof(line),
      "#CAP,%lu,DIAG,profile_stat,end,count=%u,resume=%u,multi_batch=%u,poisoned=%u,"
      "depth_overflow=%lu,isr_refused=%lu",
      static_cast<unsigned long>(Platform::micros()), static_cast<unsigned>(emitted),
      resume ? 1u : 0u, (continuing || resume) ? 1u : 0u, k.poisoned ? 1u : 0u,
      static_cast<unsigned long>(k.depthOverflow), static_cast<unsigned long>(k.isrRefused));
  emitFormatted(line, n);
  c.statsActive = resume;
  c.statsMultiBatch = continuing || resume;
  c.statsNext = static_cast<uint16_t>(resume ? index : 0);
  return emitted;
}

PDR_FLASH size_t dumpRingBatch(size_t maxRows) {
  DumpCursors& c = dumpCursors();
  const auto& k = counters();
  if (!c.ringActive) {
    // Pin the horizon at the first batch: later breaches belong to the next session.
    const uint32_t latest = k.nextSeq == 0 ? 0 : k.nextSeq - 1u;
    ProfileRingRecord oldest{};
    // A drain consumes: a record an earlier session emitted is never emitted again.
    const uint32_t firstUnread = c.ringDrained + 1u;
    if (latest == 0 || latest < firstUnread || !RuntimeProfiler::ringAt(0, oldest)) {
      c.ringStart = latest + 1u;  // empty range: start > end
      c.ringEnd = latest;
    } else {
      c.ringStart = oldest.seq > firstUnread ? oldest.seq : firstUnread;
      c.ringEnd = latest;
    }
    c.ringNext = c.ringStart;
    c.ringGap = 0;
    c.ringActive = true;
  }
  size_t emitted = 0;
  while (c.ringNext <= c.ringEnd && emitted < maxRows) {
    ProfileRingRecord oldest{};
    if (!RuntimeProfiler::ringAt(0, oldest)) {
      c.ringGap += c.ringEnd - c.ringNext + 1u;
      c.ringDrained = c.ringEnd;
      c.ringNext = c.ringEnd + 1u;
      break;
    }
    if (c.ringNext < oldest.seq) {
      // Part of the pinned range was overwritten before it was read: report it, never skip silently.
      const uint32_t resumeAt = oldest.seq <= c.ringEnd ? oldest.seq : c.ringEnd + 1u;
      c.ringGap += resumeAt - c.ringNext;
      c.ringDrained = resumeAt - 1u;
      c.ringNext = resumeAt;
      continue;
    }
    ProfileRingRecord r{};
    if (!RuntimeProfiler::ringAt(c.ringNext - oldest.seq, r)) break;
    emitRingRow(r);
    c.ringDrained = c.ringNext;
    ++c.ringNext;
    ++emitted;
  }
  const bool resume = c.ringNext <= c.ringEnd;
  char line[kRowCapacity];
  const int n = std::snprintf(
      line, sizeof(line),
      "#CAP,%lu,DIAG,profile_ring,end,count=%u,dropped=%lu,resume=%u,range_start=%lu,range_end=%lu,"
      "gap=%lu,poisoned=%u",
      static_cast<unsigned long>(Platform::micros()), static_cast<unsigned>(emitted),
      static_cast<unsigned long>(k.dropped), resume ? 1u : 0u, static_cast<unsigned long>(c.ringStart),
      static_cast<unsigned long>(c.ringEnd), static_cast<unsigned long>(c.ringGap),
      k.poisoned ? 1u : 0u);
  emitFormatted(line, n);
  if (!resume) c.ringActive = false;
  return emitted;
}

PDR_FLASH bool handleCommand(const char* line) {
  if (line == nullptr || line[0] != '!') return false;
  enum class Kind { Stats, Ring, Reset };
  Kind kind;
  if (std::strcmp(line, "!PROFILE_STATS") == 0) {
    kind = Kind::Stats;
  } else if (std::strcmp(line, "!PROFILE_RING") == 0) {
    kind = Kind::Ring;
  } else if (std::strcmp(line, "!PROFILE_RESET") == 0) {
    kind = Kind::Reset;
  } else {
    return false;
  }
  char row[kRowCapacity];
  const uint32_t depth = RuntimeProfiler::liveDepth();
  if (depth != 0) {
    // A dump or reset under a live scope could corrupt its nesting or create the breach it reports.
    emitFormatted(row, std::snprintf(row, sizeof(row), "#CAP,%lu,DIAG,profile_cmd,refused,depth=%lu",
                                     static_cast<unsigned long>(Platform::micros()),
                                     static_cast<unsigned long>(depth)));
    return true;
  }
  switch (kind) {
    case Kind::Stats:
      (void)dumpStatsBatch(kStatsBatchMax);
      break;
    case Kind::Ring:
      (void)dumpRingBatch(kRingBatchMax);
      break;
    case Kind::Reset:
      (void)RuntimeProfiler::reset();
      emitFormatted(row, std::snprintf(row, sizeof(row), "#CAP,%lu,DIAG,profile_reset,%u",
                                       static_cast<unsigned long>(Platform::micros()),
                                       static_cast<unsigned>(ProfileId::Count)));
      break;
  }
  return true;
}

}  // namespace ProfileDiagRows

#endif  // RUNTIME_PROFILE_SCOPES
