//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// Runtime profiler record path (DEC-239). Portable: every hardware read goes through
// `RuntimeProfiler::Platform`, so native fixtures drive the same code with fake clocks.
//
// Memory: the whole state is one `DMAMEM` object (RAM2, NOLOAD), cleared by `initAtBoot()`. This TU
// holds no string literal and no initialized data, so it adds nothing to DTCM. Code is `FLASHMEM`:
// ITCM headroom is a few hundred bytes, and a scope's cost is measured at boot instead of assumed.

#include "Utils/RuntimeProfiler.h"

#if RUNTIME_PROFILE_SCOPES

#include <cstring>

#include "Utils/ProfileDiagRows.h"

#if defined(ARDUINO) && !defined(PIO_UNIT_TEST_NATIVE)
#include <Arduino.h>
#define RP_FLASH FLASHMEM
#define RP_DMAMEM DMAMEM
#else
#define RP_FLASH
#define RP_DMAMEM
#endif

namespace RuntimeProfiler {
namespace detail {

struct Frame {
  uint16_t id;
  uint16_t pad0;
  uint32_t pad1;
  uint64_t childCycles;  // normalized duration of measured children
};
static_assert(sizeof(Frame) == 16, "depth frame layout");

constexpr uint32_t kInitMagic = 0x50524F46u;  // "PROF": set last by initAtBoot()

struct State {
  // DMAMEM is NOLOAD. The magic rejects cold-boot garbage; after a warm reset the previous boot's
  // state (magic included) survives until `initAtBoot()` wipes it, so every index below is also
  // bounded on use: a scope reached early can only write stale counters that init then clears.
  uint32_t initMagic;
  ProfileStat stats[kMaxIds];
  ProfileRingRecord ring[kRingCapacity];
  Frame stack[kMaxDepth];
  ProfileCounters counters;
  DumpCursors cursors;
  BootReport boot;
  uint32_t cpuHz;
  uint32_t cycleTrustUs;
  uint16_t ringHead;      // next write index
  uint8_t overflowDepth;  // live scopes entered past the stack
};

RP_DMAMEM State gState;

constexpr uint8_t kPushed = 0x01;
constexpr uint8_t kRefused = 0x02;
constexpr uint8_t kOverflowed = 0x04;
constexpr uint8_t kNoRecord = 0x08;

struct Measured {
  uint64_t cycles64;   // normalized duration in cycles (u64: a coarse value can exceed u32)
  uint32_t cyclesField;  // exact cycles, or UINT32_MAX when coarse
  uint32_t us;
  bool coarse;
};

RP_FLASH uint32_t cycleTrustFor(uint32_t hz) {
  if (hz == 0) return 0;
  const uint64_t periodUs = ((uint64_t{1} << 32) * 1000000ull) / hz;
  return periodUs > kCycleTrustGuardUs ? static_cast<uint32_t>(periodUs - kCycleTrustGuardUs) : 0;
}

RP_FLASH Measured measure(const Scope& scope, uint32_t endCycles, uint32_t endUs) {
  const State& s = gState;
  const uint32_t cycles = endCycles - scope.startCycles_;
  Measured m{};
  if (scope.long_) {
    const uint32_t wallUs = endUs - scope.startUs_;
    if (wallUs >= s.cycleTrustUs) {
      m.coarse = true;
      m.us = wallUs;
      m.cycles64 = (static_cast<uint64_t>(wallUs) * s.cpuHz) / 1000000ull;
      m.cyclesField = UINT32_MAX;
      return m;
    }
  }
  m.cycles64 = cycles;
  m.cyclesField = cycles;
  m.us = static_cast<uint32_t>((static_cast<uint64_t>(cycles) * 1000000ull) / s.cpuHz);
  return m;
}

RP_FLASH void pushRing(const Scope& scope, const Measured& m, uint16_t parent, uint8_t flags,
                       uint32_t nowUs) {
  State& s = gState;
  static_assert((kRingCapacity & (kRingCapacity - 1u)) == 0, "ring capacity must be a power of two");
  ProfileRingRecord& r = s.ring[s.ringHead & (kRingCapacity - 1u)];
  r.seq = s.counters.nextSeq++;
  r.timestampUs = nowUs;
  r.cycles = m.cyclesField;
  r.micros = m.us;
  r.id = scope.id_;
  r.parentId = parent;
  r.track = scope.track_;
  r.slot = scope.slot_;
  r.result = scope.result_;
  r.flags = static_cast<uint8_t>(flags | (m.coarse ? kRingFlagCoarse : 0));
  s.ringHead = static_cast<uint16_t>((s.ringHead + 1u) & (kRingCapacity - 1u));
  if (s.counters.ringCount < kRingCapacity) {
    ++s.counters.ringCount;
  } else {
    ++s.counters.dropped;
  }
}

RP_FLASH void record(const Scope& scope, const Measured& m, uint64_t childCycles, uint16_t parent,
                     uint8_t ringFlags, uint32_t endUs) {
  ProfileStat& st = gState.stats[scope.id_];
  ++st.calls;
  st.inclusiveCycles += m.cycles64;
  st.selfCycles += m.cycles64 > childCycles ? m.cycles64 - childCycles : 0;
  st.lastCycles = m.cyclesField;
  st.lastUs = m.us;
  if (m.cyclesField > st.maxCycles) st.maxCycles = m.cyclesField;
  if (m.us > st.maxUs) st.maxUs = m.us;
  st.lastResult = scope.result_;
  if (m.coarse) st.flags = static_cast<uint8_t>(st.flags | kStatFlagCoarse);
  if (m.us > ceilingUs(static_cast<ProfileId>(scope.id_))) {
    ++st.overCeiling;
    pushRing(scope, m, parent, ringFlags, scope.long_ ? endUs : Platform::micros());
  }
}

RP_FLASH void clearMeasurements() {
  State& s = gState;
  std::memset(s.stats, 0, sizeof(s.stats));
  std::memset(s.ring, 0, sizeof(s.ring));
  std::memset(s.stack, 0, sizeof(s.stack));
  const uint32_t nextSeq = s.counters.nextSeq;
  const uint32_t isrRefused = s.counters.isrRefused;
  std::memset(&s.counters, 0, sizeof(s.counters));
  s.counters.nextSeq = nextSeq == 0 ? 1u : nextSeq;  // seq stays monotonic across a reset
  s.counters.isrRefused = isrRefused;  // a wiring fault, not a measurement: D8 does not clear it
  std::memset(&s.cursors, 0, sizeof(s.cursors));
  s.ringHead = 0;
  s.overflowDepth = 0;
}

RP_FLASH void calibrate(bool longScope, uint32_t& minOut, uint32_t& avgOut) {
  uint32_t minimum = UINT32_MAX;
  uint64_t sum = 0;
  for (uint32_t i = 0; i < kCalibrationSamples; ++i) {
    const uint32_t before = Platform::cycles();
    { Scope sample(ProfileId::Calibration, longScope); }
    const uint32_t cost = Platform::cycles() - before;
    if (cost < minimum) minimum = cost;
    sum += cost;
  }
  minOut = minimum;
  avgOut = static_cast<uint32_t>(sum / kCalibrationSamples);
}

}  // namespace detail

using detail::gState;

RP_FLASH Scope::Scope(ProfileId id, bool longScope) : id_(static_cast<uint16_t>(id)), long_(longScope) {
  detail::State& s = gState;
  if (Platform::ipsr() != 0) {
    // Exception context: the stack belongs to the interrupted main-lane scope; touch no nesting state.
    // The per-identity `refused` count names which owner an interrupt reached (2026-11-01: isr_refused=24
    // on device with no owner named).
    ++s.counters.isrRefused;
    if (id_ < kMaxIds && s.initMagic == detail::kInitMagic) ++s.stats[id_].refused;
    state_ = detail::kRefused;
    return;
  }
  if (s.initMagic != detail::kInitMagic) {
    // Cold-boot garbage before `initAtBoot()` (see the note on `State::initMagic`).
    state_ = detail::kRefused;
    return;
  }
  if (s.cpuHz == 0 || id_ >= kMaxIds) {
    if (id_ < kMaxIds) ++s.stats[id_].refused;
    state_ = detail::kRefused;
    return;
  }
  if (s.counters.poisoned) state_ = detail::kNoRecord;
  if (s.counters.depth < kMaxDepth) {
    frame_ = s.counters.depth;
    s.stack[frame_] = detail::Frame{id_, 0, 0, 0};
    ++s.counters.depth;
    state_ = static_cast<uint8_t>(state_ | detail::kPushed);
  } else {
    ++s.overflowDepth;
    ++s.counters.depthOverflow;
    state_ = static_cast<uint8_t>(state_ | detail::kOverflowed);
  }
  if (long_) startUs_ = Platform::micros();
  startCycles_ = Platform::cycles();  // last: the entry bookkeeping is not part of the interval
}

RP_FLASH Scope::~Scope() {
  const uint32_t endCycles = Platform::cycles();  // first: the exit bookkeeping is not part of it
  const uint32_t endUs = long_ ? Platform::micros() : 0;
  if ((state_ & detail::kRefused) != 0) return;
  detail::State& s = gState;
  const detail::Measured m = detail::measure(*this, endCycles, endUs);

  if ((state_ & detail::kOverflowed) != 0) {
    --s.overflowDepth;
    // Only the outermost overflowed scope reports to the deepest tracked frame; nested overflowed
    // children are already inside its interval.
    if (s.overflowDepth == 0 && s.counters.depth > 0 && s.counters.depth <= kMaxDepth) {
      s.stack[s.counters.depth - 1].childCycles += m.cycles64;
    }
    if ((state_ & detail::kNoRecord) == 0 && !s.counters.poisoned) {
      detail::record(*this, m, 0, kNoParent, kRingFlagOverflowed, endUs);
    }
    return;
  }

  const uint8_t depth = s.counters.depth > kMaxDepth ? kMaxDepth : s.counters.depth;
  if (depth == 0 || frame_ != depth - 1 || s.stack[frame_].id != id_ || s.overflowDepth != 0) {
    // Out-of-order exit: no duration for anyone, and nesting is poisoned until an explicit reset.
    ProfileStat& st = s.stats[id_];
    if (st.orderFaults != UINT16_MAX) ++st.orderFaults;
    s.counters.poisoned = true;
    if (frame_ < depth) s.counters.depth = frame_;  // drop this frame and everything above it
    s.overflowDepth = 0;  // overflowed scopes still live are orphaned with it
    return;
  }
  const uint64_t childCycles = s.stack[frame_].childCycles;
  s.counters.depth = frame_;
  uint16_t parent = kNoParent;
  if (frame_ > 0) {
    detail::Frame& up = s.stack[frame_ - 1];
    parent = up.id;
    up.childCycles += m.cycles64;
  }
  if ((state_ & detail::kNoRecord) != 0 || s.counters.poisoned) return;
  detail::record(*this, m, childCycles, parent, 0, endUs);
}

RP_FLASH void initAtBoot() {
  std::memset(&gState, 0, sizeof(gState));
  detail::State& s = gState;
  s.counters.nextSeq = 1;
  s.cpuHz = Platform::cpuHz();
  s.cycleTrustUs = detail::cycleTrustFor(s.cpuHz);
  s.initMagic = detail::kInitMagic;
  BootReport& b = s.boot;
  b.sourceAvailable = s.cpuHz != 0;
  b.cpuHz = s.cpuHz;
  b.cycleTrustUs = s.cycleTrustUs;
  if (b.sourceAvailable) {
    b.samples = kCalibrationSamples;
    detail::calibrate(false, b.shortMinCycles, b.shortAvgCycles);
    detail::calibrate(true, b.longMinCycles, b.longAvgCycles);
    std::memset(&s.stats[static_cast<size_t>(ProfileId::Calibration)], 0, sizeof(ProfileStat));
  }
  char line[ProfileDiagRows::kBootRowCapacity];
  if (ProfileDiagRows::formatBootRow(line, sizeof(line), b, Platform::micros())) {
    Platform::emitRetainedRow(line);
  }
}

RP_FLASH bool reset() {
  if (gState.counters.depth != 0 || gState.overflowDepth != 0) return false;
  detail::clearMeasurements();
  return true;
}

RP_FLASH uint32_t ceilingUs(ProfileId id) {
  switch (id) {
    case ProfileId::RecordStart_edge:
    case ProfileId::PlaybackPrewarm_slot:
    case ProfileId::PlaybackPrewarm_views:
    case ProfileId::OverdubStart_body:
      return kColdCeilingUs;
    case ProfileId::ViewPrewarm_unit:
    case ProfileId::PlaybackViewService_grain:
    case ProfileId::Display_frame_phase:  // draw/resolve routinely ~3 ms: FRAME would flood the ring
    case ProfileId::SnapshotPublish_step:
    case ProfileId::SaveWorkItem_step:
    case ProfileId::LoadStep:
      return kDeferredCeilingUs;
    case ProfileId::IdleOwner_unit:
      return kOwnerTurnCeilingUs;
    case ProfileId::OverdubStart_handoff:
    case ProfileId::OverdubStart_staging:
    case ProfileId::OverdubStart_workingSet:
    case ProfileId::OverdubStart_memAdmit:
    case ProfileId::OverdubStart_interval:
    case ProfileId::OverdubStart_capture:
    case ProfileId::OverdubStart_undo:
    case ProfileId::OverdubStart_openSession:
    case ProfileId::OverdubStart_beginCapture:
    case ProfileId::OverdubStart_bindPredecessor:
      return kHotCeilingUs;  // children of a 66 ms edge: any child over 250 us is worth a ring record
    case ProfileId::PlaybackWindow_build:
    case ProfileId::DisplayBase_gather:
    case ProfileId::SelfTest:
      return kFrameCeilingUs;
    case ProfileId::Calibration:
    case ProfileId::Count:
      break;
  }
  return UINT32_MAX;  // never enters the ring
}

RP_FLASH const ProfileStat& statAt(size_t index) {
  return gState.stats[index < kMaxIds ? index : 0];
}

RP_FLASH const ProfileStat& stat(ProfileId id) { return statAt(static_cast<size_t>(id)); }

RP_FLASH const ProfileCounters& counters() { return gState.counters; }

RP_FLASH const BootReport& bootReport() { return gState.boot; }

RP_FLASH uint32_t liveDepth() {
  return static_cast<uint32_t>(gState.counters.depth) + gState.overflowDepth;
}

RP_FLASH DumpCursors& dumpCursors() { return gState.cursors; }

RP_FLASH uint64_t cyclesToUs(uint64_t cycles) {
  return gState.cpuHz == 0 ? 0 : (cycles * 1000000ull) / gState.cpuHz;
}

RP_FLASH bool ringAt(size_t age, ProfileRingRecord& out) {
  const detail::State& s = gState;
  if (age >= s.counters.ringCount) return false;
  const size_t oldest = (s.ringHead + kRingCapacity - s.counters.ringCount) % kRingCapacity;
  out = s.ring[(oldest + age) % kRingCapacity];
  return true;
}

}  // namespace RuntimeProfiler

#endif  // RUNTIME_PROFILE_SCOPES
