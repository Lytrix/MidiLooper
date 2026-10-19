// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstddef>
#include <cstdint>

namespace MidiIngress {
// Passive qualification arithmetic only: no producer, resident instance, locks,
// receiver wiring or timing guarantee. Caller serializes all aggregate access.
// These labels do not establish that a future probe covers the named boundary.
enum class IngressCostLane : uint8_t { HookCall, MaskBracket, Publication, EmptyProbe };
enum class IngressCostResult : uint8_t { Accepted, NotPending, InvalidSample, Saturated };

struct IngressCycleEndpoint {
  // The cycle read must lie INSIDE this extended-time bracket, in physicalEra.
  uint64_t beforeUs = 0, afterUs = 0;
  uint32_t cycles = 0, cpuHz = 0, physicalEra = 0, configuration = 0;
};
struct IngressCounterContract {
  // Bound in the producer's time units, not a calibrated Hz claim. Caller must
  // establish continuous advance (no resets/halts) at <= this rate over the
  // WHOLE interval. Matching endpoint Hz/configuration alone cannot establish it.
  uint32_t maximumCounterHz = 0;
  bool continuous = false;
};
struct IngressCostStats {
  uint64_t sumCycles = 0;
  uint32_t accepted = 0, rejected = 0;
  uint32_t minimumCycles = UINT32_MAX, maximumCycles = 0;
  bool saturated = false; // Terminal: never update accepted statistics again.
};
struct IngressCostAggregate {
  IngressCostStats lanes[4]{};
};

namespace detail {
inline bool ingressCycleIntervalValid(const IngressCycleEndpoint& first,
                                      const IngressCycleEndpoint& last,
                                      const IngressCounterContract& contract) {
  if (!contract.continuous || contract.maximumCounterHz == 0 ||
      first.physicalEra == 0 || first.physicalEra != last.physicalEra ||
      first.cpuHz == 0 || first.cpuHz != last.cpuHz ||
      first.cpuHz > contract.maximumCounterHz || first.configuration == 0 ||
      first.configuration != last.configuration || first.beforeUs > first.afterUs ||
      first.afterUs > last.beforeUs || last.beforeUs > last.afterUs)
    return false;
  // Use the enclosing interval, not the gap between brackets. Strictly less
  // than 2^32 possible cycles excludes full-wrap resurrection. Divide a fixed
  // 64-bit constant rather than multiplying an arbitrary extended duration.
  constexpr uint64_t cycleWrapTimesMillion = (uint64_t(1) << 32) * 1000000;
  const uint64_t maximumEnclosingUs =
      (cycleWrapTimesMillion - 1) / contract.maximumCounterHz;
  return last.afterUs - first.beforeUs <= maximumEnclosingUs;
}
inline void rejectIngressCost(IngressCostStats& stats) {
  if (stats.rejected != UINT32_MAX) ++stats.rejected;
}
} // namespace detail

// One noncopyable stack-local token per boundary, including nested boundaries.
// No destructor publication: interrupted/unfinished tokens cannot fabricate a
// sample. A consumed token can begin a new observation. Aggregate/session
// lifetime and protected publication remain the future caller's responsibility.
class IngressCostToken {
 public:
  IngressCostToken() = default;
  IngressCostToken(const IngressCostToken&) = delete;
  IngressCostToken& operator=(const IngressCostToken&) = delete;
  IngressCostToken(IngressCostToken&&) = delete;
  IngressCostToken& operator=(IngressCostToken&&) = delete;

  bool pending() const { return pending_; }
  bool begin(IngressCostLane lane, const IngressCycleEndpoint& first) {
    if (pending_ || size_t(lane) >= 4) return false;
    first_ = first;
    lane_ = lane;
    pending_ = true;
    return true;
  }
  IngressCostResult finish(IngressCostAggregate& aggregate,
                           const IngressCycleEndpoint& last,
                           const IngressCounterContract& contract) {
    if (!pending_) return IngressCostResult::NotPending;
    pending_ = false; // Faults consume too; double finish has no statistics effect.
    auto& stats = aggregate.lanes[size_t(lane_)];
    if (stats.saturated) {
      detail::rejectIngressCost(stats);
      return IngressCostResult::Saturated;
    }
    if (!detail::ingressCycleIntervalValid(first_, last, contract)) {
      detail::rejectIngressCost(stats);
      return IngressCostResult::InvalidSample;
    }
    const uint32_t elapsedCycles = last.cycles - first_.cycles;
    if (stats.accepted == UINT32_MAX || stats.sumCycles > UINT64_MAX - elapsedCycles) {
      stats.saturated = true;
      detail::rejectIngressCost(stats);
      return IngressCostResult::Saturated;
    }
    ++stats.accepted;
    stats.sumCycles += elapsedCycles;
    if (elapsedCycles < stats.minimumCycles) stats.minimumCycles = elapsedCycles;
    if (elapsedCycles > stats.maximumCycles) stats.maximumCycles = elapsedCycles;
    return IngressCostResult::Accepted;
  }

 private:
  IngressCycleEndpoint first_{};
  IngressCostLane lane_ = IngressCostLane::HookCall;
  bool pending_ = false;
};
} // namespace MidiIngress
