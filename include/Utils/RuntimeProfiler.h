//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file RuntimeProfiler.h
 * @brief Level 1 runtime profiler: RAII scopes, a per-identity aggregate table and an over-ceiling
 * trace ring in RAM2 (DEC-239, openspec/changes/runtime-profiling-layer).
 *
 * A scope times one declared operation from construction to destruction, so every exit path,
 * including each early `return false`, is measured. Two primitives:
 *
 * - **short** reads the DWT cycle counter only. Legal only where the site records a structural
 *   guarantee that the body cannot span one counter period (7.158 s at 600 MHz).
 * - **long** also reads `micros()` and reports cycles only while the wall delta is below the
 *   cycle-trust threshold (one period minus a 1 s guard); otherwise it reports the wall duration and
 *   marks the record coarse. Every long site records why it cannot reach one `micros()` period.
 *
 * A declared ceiling decides ring entry and the over-ceiling count only. It bounds nothing and is not
 * evidence of interval validity. Profiler output is never an input to runtime logic.
 *
 * Everything is compiled only with `RUNTIME_PROFILE_SCOPES=1` (the
 * `teensy41-capture-serial-profile` env and native tests). Without it the scope macros declare an
 * empty object whose setters are empty inline functions, so call sites compile unchanged and the
 * image carries no profiler code, data or literal.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#ifndef RUNTIME_PROFILE_SCOPES
#define RUNTIME_PROFILE_SCOPES 0
#endif

namespace RuntimeProfiler {

/// Append-only binary identities. A value that has appeared in device evidence is never renumbered or
/// reused; new operations append before `Count`. Names live in `ProfileDiagRows.cpp`.
enum class ProfileId : uint16_t {
  Calibration = 0,  // reserved: boot self-measurement, cleared after calibration, never dumped
  SelfTest = 1,     // reserved: host fixtures and the slice-1 synthetic identity
  RecordStart_edge = 2,
  PlaybackPrewarm_slot = 3,
  PlaybackPrewarm_views = 4,
  ViewPrewarm_unit = 5,
  PlaybackWindow_build = 6,
  PlaybackViewService_grain = 7,
  SnapshotPublish_step = 8,
  SaveWorkItem_step = 9,
  IdleOwner_unit = 10,
  DisplayBase_gather = 11,
  Display_frame_phase = 12,
  LoadStep = 13,
  OverdubStart_body = 14,
  Count
};

/// Report-only ceiling classes (DEC-239 point 2). `OwnerTurn` is `MidiServiceDrain::kOwnerWorkBudgetUs`.
enum class CeilingClass : uint8_t { Clock, Hot, Frame, Deferred, Cold, OwnerTurn };

constexpr size_t kMaxIds = 64;
constexpr size_t kIdEnvelope = 128;
constexpr size_t kMaxDepth = 16;
constexpr size_t kRingCapacity = 256;
constexpr uint32_t kCycleTrustGuardUs = 1000000u;
constexpr uint32_t kCalibrationSamples = 256;
constexpr uint8_t kNoResult = 0xFF;
constexpr uint8_t kNoContext = 0xFF;
constexpr uint16_t kNoParent = 0xFFFF;

/// Ceiling class values in microseconds. `kOwnerTurnCeilingUs` is pinned to
/// `MidiServiceDrain::kOwnerWorkBudgetUs` by a `static_assert` in `RuntimeProfilerPlatform.cpp`, so the
/// report threshold cannot drift from the enforced budget.
constexpr uint32_t kClockCeilingUs = 50u;
constexpr uint32_t kHotCeilingUs = 250u;
constexpr uint32_t kFrameCeilingUs = 2000u;
constexpr uint32_t kDeferredCeilingUs = 5000u;
constexpr uint32_t kColdCeilingUs = 25000u;
constexpr uint32_t kOwnerTurnCeilingUs = 4000u;
static_assert(kColdCeilingUs < 6000000u, "a ceiling must be far below one counter period");

static_assert(static_cast<size_t>(ProfileId::Count) <= kMaxIds, "ProfileId exceeds the table capacity");
static_assert(kMaxIds <= kIdEnvelope, "identity capacity exceeds its envelope");
static_assert(kCalibrationSamples >= 128 && kCalibrationSamples <= 256, "calibration sample count");

/// Aggregate record flags.
constexpr uint8_t kStatFlagCoarse = 0x01;  // at least one included measurement was wall-clock sourced
/// Ring record flags.
constexpr uint8_t kRingFlagCoarse = 0x01;      // `micros` is the duration; `cycles` is saturated
constexpr uint8_t kRingFlagOverflowed = 0x02;  // entered past the depth stack; `parentId` unknown

/// One per identity, 64-bit fields first so the layout is naturally aligned at 48 bytes.
struct ProfileStat {
  uint64_t inclusiveCycles;
  uint64_t selfCycles;
  uint32_t calls;
  uint32_t maxCycles;
  uint32_t maxUs;
  uint32_t lastCycles;
  uint32_t lastUs;
  uint32_t overCeiling;
  uint32_t refused;
  uint16_t orderFaults;
  uint8_t lastResult;
  uint8_t flags;
};
static_assert(sizeof(ProfileStat) == 48, "ProfileStat layout is pinned at 48 bytes");

struct ProfileRingRecord {
  uint32_t seq;
  uint32_t timestampUs;
  uint32_t cycles;
  uint32_t micros;
  uint16_t id;
  uint16_t parentId;
  uint8_t track;
  uint8_t slot;
  uint8_t result;
  uint8_t flags;
};
static_assert(sizeof(ProfileRingRecord) == 24, "ProfileRingRecord layout is pinned at 24 bytes");

struct BootReport {
  bool sourceAvailable;
  uint32_t cpuHz;
  uint32_t cycleTrustUs;
  uint32_t samples;
  uint32_t shortMinCycles;
  uint32_t shortAvgCycles;
  uint32_t longMinCycles;
  uint32_t longAvgCycles;
};

/// Global counters a dump reports beside the table.
struct ProfileCounters {
  uint32_t nextSeq;        // seq of the next ring record (first record is 1)
  uint32_t dropped;        // ring records overwritten
  uint32_t depthOverflow;  // scopes entered past the depth stack
  uint32_t isrRefused;     // scopes entered in exception context
  uint16_t ringCount;
  uint8_t depth;
  bool poisoned;
};

#if RUNTIME_PROFILE_SCOPES

/// Platform boundary. The device definitions live in `RuntimeProfilerPlatform.cpp`; native fixtures
/// define their own fake clocks. Nothing else in the profiler touches hardware.
namespace Platform {
uint32_t cycles();  // raw DWT CYCCNT, no barriers
uint32_t micros();  // systick-extended microseconds (wrap-safe for 71.6 min)
uint32_t ipsr();    // non-zero inside an exception handler
uint32_t cpuHz();   // boot contract; 0 = cycle counter unavailable
/// One profiler row (`#CAP,...` without CRLF). The device writes directly when USB has room and falls
/// back to the capture ring (DEC-228 K1); fixtures capture the text.
void emitRow(const char* line);
}  // namespace Platform

class Scope {
 public:
  Scope(ProfileId id, bool longScope);
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

  void setResult(uint8_t result) { result_ = result; }
  void setContext(uint8_t track, uint8_t slot) {
    track_ = track;
    slot_ = slot;
  }

  // Internal state, read by the record path.
  uint32_t startCycles_ = 0;
  uint32_t startUs_ = 0;
  uint16_t id_;
  uint8_t frame_ = 0;
  uint8_t state_ = 0;
  uint8_t result_ = kNoResult;
  uint8_t track_ = kNoContext;
  uint8_t slot_ = kNoContext;
  bool long_;
};

/// Reference form: reads `resultVar` (a bool or small enum) at exit.
template <typename T>
class ResultScope : public Scope {
 public:
  ResultScope(ProfileId id, bool longScope, const T& resultVar) : Scope(id, longScope), var_(resultVar) {}
  ~ResultScope() { setResult(static_cast<uint8_t>(var_)); }

 private:
  const T& var_;
};

/// Clears all state, reads the CPU rate, calibrates both primitives and emits the boot row once.
/// Called from `setup()` before the first instrumented owner can run (DMAMEM is NOLOAD).
void initAtBoot();
/// Clears table, ring and counters only (no calibration, no row). Refused unless depth is zero.
bool reset();

const ProfileStat& stat(ProfileId id);
const ProfileStat& statAt(size_t index);
const ProfileCounters& counters();
const BootReport& bootReport();
uint32_t ceilingUs(ProfileId id);
/// Ring access by age: 0 = oldest retained record. Returns false past `ringCount`.
bool ringAt(size_t age, ProfileRingRecord& out);

#define PROFILE_SCOPE_SHORT(name, id) ::RuntimeProfiler::Scope name((id), false)
#define PROFILE_SCOPE_LONG(name, id) ::RuntimeProfiler::Scope name((id), true)
#define PROFILE_SCOPE_LONG_RESULT(name, id, var) \
  ::RuntimeProfiler::ResultScope<decltype(var)> name((id), true, (var))

#else  // RUNTIME_PROFILE_SCOPES

class Scope {
 public:
  void setResult(uint8_t) {}
  void setContext(uint8_t, uint8_t) {}
};
inline void initAtBoot() {}

#define PROFILE_SCOPE_SHORT(name, id) [[maybe_unused]] ::RuntimeProfiler::Scope name
#define PROFILE_SCOPE_LONG(name, id) [[maybe_unused]] ::RuntimeProfiler::Scope name
#define PROFILE_SCOPE_LONG_RESULT(name, id, var) [[maybe_unused]] ::RuntimeProfiler::Scope name

#endif  // RUNTIME_PROFILE_SCOPES

}  // namespace RuntimeProfiler
