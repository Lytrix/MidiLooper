// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "Utils/MidiIngressReceipt.h"
#if MIDI_INGRESS_RECEIPTS_ENABLED
#include <Arduino.h>
#include <cstdlib>
#include <new>
#if defined(__IMXRT1062__)
#include "Utils/ExternalMemoryFirstAllocator.h"
#define INGRESS_MEM FLASHMEM
#else
#define INGRESS_MEM
#endif
#if MIDI_INGRESS_COST_QUALIFICATION
#define INGRESS_NOINLINE __attribute__((noinline))
#else
#define INGRESS_NOINLINE
#endif
namespace {
// Startup-cleared pointer: DMAMEM is NOLOAD and could retain a stale heap address.
// Only the cold-provisioned metadata block is external-first allocated.
MidiIngress::detail::State* ingressState = nullptr;
#if defined(PIO_UNIT_TEST_NATIVE)
bool prewarmDenied = false;
#endif
class IngressMask {
 public:
  IngressMask() {
#if defined(__IMXRT1062__)
    __asm__ volatile("mrs %0, primask" : "=r"(saved_));
    __asm__ volatile("cpsid i" ::: "memory");
#elif MIDI_INGRESS_MASK_QUALIFICATION
    saved_ = MidiIngress::NativeMask::enter();
#endif
  }
  ~IngressMask() {
#if defined(__IMXRT1062__)
    __asm__ volatile("msr primask, %0" :: "r"(saved_) : "memory");
#elif MIDI_INGRESS_MASK_QUALIFICATION
    MidiIngress::NativeMask::restore(saved_);
#endif
  }
 private:
  uint32_t saved_ = 0;
};
template <typename Action> inline void withQueue(uint8_t route, Action action) {
  if (!ingressState) return;
  switch (route) {
    case MidiIngress::kUsbDevice: action(ingressState->usb); break;
    case MidiIngress::kDin: action(ingressState->din); break;
    case MidiIngress::kUsbHost: action(ingressState->host); break;
    default: break;
  }
}
#if MIDI_INGRESS_COST_QUALIFICATION
// Conditional Run-mode bench contract, not a calibrated rate or timing budget.
constexpr MidiIngress::IngressCounterContract costCounterContract{1200000000u, true};
INGRESS_MEM MidiIngress::IngressCycleEndpoint costEndpoint() {
  MidiIngress::ClockSampler sampler = nullptr;
  {
    const IngressMask mask;
    if (ingressState) sampler = ingressState->sampler;
  }
  if (!sampler) return {};
  const auto before = sampler();
  const auto cpu = MidiIngress::CostPlatform::cpuHz();
  const auto config = MidiIngress::CostPlatform::configuration();
  const auto cycles = MidiIngress::CostPlatform::cycles();
  const auto after = sampler();
  if (before.era == 0 || before.era != after.era || after.microseconds == UINT64_MAX ||
      cpu != 600000000u || cpu != MidiIngress::CostPlatform::cpuHz() ||
      config == 0 || config != MidiIngress::CostPlatform::configuration()) return {};
  // PIT conversion floors; ceiling the trailing read encloses the cycle read.
  return {before.microseconds, after.microseconds + 1, cycles, cpu, before.era, config};
}
INGRESS_MEM void updateCost(uint8_t route, MidiIngress::IngressCostLane lane,
                            const MidiIngress::IngressCycleEndpoint& first,
                            const MidiIngress::IngressCycleEndpoint& last) {
  const IngressMask mask;
  if (!ingressState || route >= 3) return;
  MidiIngress::IngressCostToken token;
  (void)token.begin(lane, first);
  (void)token.finish(ingressState->costs[route], last, costCounterContract);
}
INGRESS_MEM void publishCost(uint8_t route, MidiIngress::IngressCostLane lane,
                             const MidiIngress::IngressCycleEndpoint& first,
                             const MidiIngress::IngressCycleEndpoint& last) {
  if (route >= 3) return;
  const auto publicationStart = costEndpoint();
  updateCost(route, lane, first, last);
  const auto publicationEnd = costEndpoint();
  // This second saved-mask self-accounting tail is NOT timed recursively.
  updateCost(route, MidiIngress::IngressCostLane::Publication, publicationStart, publicationEnd);
}
INGRESS_MEM void emptyCostProbe(uint8_t route) {
  const auto first = costEndpoint();
  const auto last = costEndpoint();
  publishCost(route, MidiIngress::IngressCostLane::EmptyProbe, first, last);
}
#endif
INGRESS_MEM MidiIngressReceipt observe() {
  MidiIngressReceipt result;
  result.rawObservedUs = micros();
  result.clock = MidiIngressReceipt::Clock::RawMicros32;
  if (ingressState->sampler) {
    const auto stamp = ingressState->sampler();
    if (stamp.era != 0) {
      result.observedUs = stamp.microseconds;
      result.clockEra = stamp.era;
      result.clock = MidiIngressReceipt::Clock::IndependentCounter;
    }
  }
  return result;
}
}
namespace MidiIngress {
INGRESS_MEM void setup(const void* serialOwner, const void* hostOwner) {
  if (!ingressState) {
#if defined(PIO_UNIT_TEST_NATIVE)
    if (prewarmDenied) return;
#endif
#if defined(__IMXRT1062__)
    CommittedChunkIdAllocator<detail::State> allocator;
    auto* memory = allocator.allocate(1);
#else
    auto* memory = static_cast<detail::State*>(std::malloc(sizeof(detail::State)));
#endif
    if (!memory) return;
    auto* constructed = new (memory) detail::State;
    const IngressMask mask;
    ingressState = constructed; // Construct entirely before IRQ-visible publication.
  }
  const IngressMask mask;
  ingressState->usb.owner = nullptr;
  ingressState->din.owner = serialOwner;
  ingressState->host.owner = hostOwner;
  ingressState->sampler = nullptr;
  withQueue(kUsbDevice, [](auto& q) { q.reset(kUsbCells); q.dispatched = {}; });
  withQueue(kDin, [](auto& q) { q.reset(kDinCells); q.dispatched = {}; });
  withQueue(kUsbHost, [](auto& q) { q.reset(kHostCells); q.dispatched = {}; });
#if MIDI_INGRESS_COST_QUALIFICATION
  for (auto& cost : ingressState->costs) cost = {};
#endif
}
INGRESS_MEM void setClockSampler(ClockSampler sampler) {
  const IngressMask mask;
  if (!ingressState || ingressState->sampler == sampler) return;
  // A new observation source cannot make already queued observations current.
  withQueue(kUsbDevice, [](auto& q) { q.invalidate(); });
  withQueue(kDin, [](auto& q) { q.invalidate(); });
  withQueue(kUsbHost, [](auto& q) { q.invalidate(); });
  ingressState->sampler = sampler;
}
INGRESS_MEM bool currentReceipt(uint8_t route, const MidiIngressReceipt& receipt) {
  const IngressMask mask;
  bool current = false;
  withQueue(route, [&](auto& q) {
    current = receipt.associated() && receipt.current() && q.capacityMatches && q.generation != 0 &&
        receipt.queueGeneration == q.generation;
  });
  return current;
}
INGRESS_MEM MidiIngressReceipt lastReceipt(uint8_t route) {
  const IngressMask mask;
  MidiIngressReceipt result;
  withQueue(route, [&](auto& q) { result = q.last; });
  if (!currentReceipt(route, result)) result.flags &= ~MidiIngressReceipt::CurrentQueue;
  return result;
}
INGRESS_MEM MidiIngressReceipt lastDispatchedReceipt(uint8_t route) {
  const IngressMask mask;
  MidiIngressReceipt result;
  withQueue(route, [&](auto& q) { result = q.dispatched; });
  if (!currentReceipt(route, result)) result.flags &= ~MidiIngressReceipt::CurrentQueue;
  return result;
}
INGRESS_MEM void noteDispatch(uint8_t route, const MidiIngressReceipt& receipt) {
  const IngressMask mask;
  auto current = receipt;
  if (!currentReceipt(route, current)) current.flags &= ~MidiIngressReceipt::CurrentQueue;
  withQueue(route, [&](auto& q) { q.dispatched = current; });
}
INGRESS_MEM const void* registeredOwner(uint8_t route) {
  const IngressMask mask;
  const void* result = nullptr;
  withQueue(route, [&](auto& q) { result = q.owner; });
  return result;
}
INGRESS_MEM size_t stateBytes() { return sizeof(detail::State); }
INGRESS_MEM bool ready() { const IngressMask mask; return ingressState != nullptr; }
#if MIDI_INGRESS_COST_QUALIFICATION
INGRESS_MEM bool snapshotCosts(IngressCostAggregate (&result)[3]) {
  const IngressMask mask;
  if (!ingressState) return false;
  for (size_t route = 0; route < 3; ++route) result[route] = ingressState->costs[route];
  return true;
}
INGRESS_MEM uint32_t costContractBits() {
  const auto endpoint = costEndpoint();
  return 4u | (endpoint.cpuHz == 600000000u && endpoint.configuration != 0 ? 1u : 0u) |
      (endpoint.physicalEra != 0 ? 2u : 0u);
}
#endif
#if defined(PIO_UNIT_TEST_NATIVE)
void denyPrewarmForTests(bool deny) { prewarmDenied = deny; }
void setCountersForTests(uint8_t route, uint32_t generation, uint64_t sequence) {
  withQueue(route, [&](auto& q) { q.generation = generation; q.sequence = sequence; q.packetActive = false; });
}
#endif
}
extern "C" {
INGRESS_MEM void midi_ingress_reset(uint8_t route, const void* owner, uint32_t capacity) {
  const IngressMask mask;
  withQueue(route, [&](auto& q) { if (owner == q.owner) q.reset(capacity); });
}
INGRESS_MEM INGRESS_NOINLINE void midi_ingress_packet(uint8_t route, const void* owner, uint32_t capacity, uint8_t late) {
#if MIDI_INGRESS_COST_QUALIFICATION
  const auto costStart = costEndpoint();
#endif
  {
  const IngressMask mask;
  withQueue(route, [&](auto& q) {
    if (owner != q.owner) return;
    if (!q.canCapture(capacity)) { q.begin({}, capacity, late != 0); return; }
    q.begin(observe(), capacity, late != 0);
  });
  }
#if MIDI_INGRESS_COST_QUALIFICATION
  const auto costEnd = costEndpoint();
  publishCost(route, MidiIngress::IngressCostLane::MaskBracket, costStart, costEnd);
#endif
}
INGRESS_MEM void midi_ingress_publish(uint8_t route, const void* owner, uint32_t slot, uint32_t offset, uint32_t count) {
  const IngressMask mask;
  withQueue(route, [&](auto& q) { if (owner == q.owner) q.publish(slot, offset, count); });
}
INGRESS_MEM void midi_ingress_consume(uint8_t route, const void* owner, uint32_t slot, uint32_t offset) {
  const IngressMask mask;
  withQueue(route, [&](auto& q) { if (owner == q.owner) q.consume(slot, offset); });
}
INGRESS_MEM void midi_ingress_clear_last(uint8_t route, const void* owner) {
  const IngressMask mask;
  withQueue(route, [&](auto& q) { if (owner == q.owner) q.last = {}; });
}
INGRESS_MEM void midi_ingress_invalidate(uint8_t route, const void* owner) {
  const IngressMask mask;
  withQueue(route, [&](auto& q) { if (owner == q.owner) q.invalidate(); });
}
INGRESS_MEM INGRESS_NOINLINE void midi_ingress_direct(uint8_t route, const void* owner) {
#if MIDI_INGRESS_COST_QUALIFICATION
  const auto costStart = costEndpoint();
#endif
  {
  const IngressMask mask;
  withQueue(route, [&](auto& q) {
    if (owner != q.owner) return;
    constexpr uint32_t capacity = sizeof(q.cells) / sizeof(q.cells[0]);
    q.begin(q.canCapture(capacity) ? observe() : MidiIngressReceipt{}, capacity, true);
    q.last = q.packetActive ? q.packet : MidiIngressReceipt{};
    if (q.packetActive) q.last.flags |= MidiIngressReceipt::Associated;
  });
  }
#if MIDI_INGRESS_COST_QUALIFICATION
  const auto costEnd = costEndpoint();
  publishCost(route, MidiIngress::IngressCostLane::MaskBracket, costStart, costEnd);
#endif
}
#if MIDI_INGRESS_COST_QUALIFICATION
INGRESS_MEM void midi_ingress_measured_packet(uint8_t route, const void* owner, uint32_t capacity, uint8_t late) {
  if (!ingressState) { midi_ingress_packet(route, owner, capacity, late); return; }
  emptyCostProbe(route);
  const auto first = costEndpoint();
  midi_ingress_packet(route, owner, capacity, late);
  const auto last = costEndpoint();
  publishCost(route, MidiIngress::IngressCostLane::HookCall, first, last);
}
INGRESS_MEM void midi_ingress_measured_direct(uint8_t route, const void* owner) {
  if (!ingressState) { midi_ingress_direct(route, owner); return; }
  emptyCostProbe(route);
  const auto first = costEndpoint();
  midi_ingress_direct(route, owner);
  const auto last = costEndpoint();
  publishCost(route, MidiIngress::IngressCostLane::HookCall, first, last);
}
#endif
}
#endif
