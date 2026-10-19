// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstddef>
#include <cstdint>
#include "Utils/MidiIngressHooks.h"
#include "Utils/PlaybackPhysicalDeadline.h"
#include "Utils/MidiIngressCostProbe.h"

// Receive observation, never sender time or an automatically qualified deadline.
struct MidiIngressReceipt {
  enum class Clock : uint8_t { Missing, RawMicros32, IndependentCounter };
  enum Flag : uint8_t { Associated = 1, CurrentQueue = 2, LateHardwareRead = 4 };
  uint64_t observedUs = 0;
  uint64_t receiveSequence = 0;
  uint32_t queueGeneration = 0;
  uint32_t clockEra = 0;
  uint32_t rawObservedUs = 0;
  uint16_t eventOffset = 0;
  Clock clock = Clock::Missing;
  uint8_t flags = 0;
  bool associated() const { return (flags & Associated) != 0; }
  bool current() const { return (flags & CurrentQueue) != 0; }
  bool independentObservation() const {
    return associated() && current() && clock == Clock::IndependentCounter && clockEra != 0;
  }
  // Correlation/IRQ/FIFO/USB delay bounds belong to the future reference owner.
  // This struct intentionally supplies no 'physical deadline qualified' predicate.
};
static_assert(sizeof(MidiIngressReceipt) == 32, "Price ingress metadata explicitly");

namespace MidiIngress {
#if MIDI_INGRESS_MASK_QUALIFICATION
namespace NativeMask {
uint32_t enter();
void restore(uint32_t saved);
}
#endif
constexpr uint8_t kUsbDevice = 0, kDin = 1, kUsbHost = 2;
constexpr size_t kUsbCells = 6, kDinCells = 64, kHostCells = 400;
using ClockSampler = PhysicalClockStamp (*)();
#if MIDI_INGRESS_RECEIPTS_ENABLED
// Cold, non-aborting external-first provisioning. Does not clear transport payload.
void setup(const void* serialOwner, const void* hostOwner);
// ClockManager setup installs its bounded Run-mode counter sampler in the ingress
// profile. Missing/faulted source falls back to labeled raw micros32 observation;
// counter mechanics do not establish sender arrival/correlation or deadline bounds.
void setClockSampler(ClockSampler sampler);
MidiIngressReceipt lastReceipt(uint8_t route);
MidiIngressReceipt lastDispatchedReceipt(uint8_t route);
void noteDispatch(uint8_t route, const MidiIngressReceipt& receipt);
const void* registeredOwner(uint8_t route);
size_t stateBytes();
bool currentReceipt(uint8_t route, const MidiIngressReceipt& receipt);
bool ready();
#if defined(PIO_UNIT_TEST_NATIVE)
void denyPrewarmForTests(bool deny);
void setCountersForTests(uint8_t route, uint32_t generation, uint64_t sequence);
#endif
#else
inline MidiIngressReceipt lastReceipt(uint8_t) { return {}; }
inline MidiIngressReceipt lastDispatchedReceipt(uint8_t) { return {}; }
#endif

namespace detail {
struct Cell { MidiIngressReceipt receipt{}; uint16_t extent = 0, nextOffset = 0; bool present = false; };
template <size_t Capacity> struct Queue {
  const void* owner = nullptr;
  uint32_t generation = 1;
  uint64_t sequence = 0;
  bool capacityMatches = true;
  bool packetActive = false;
  MidiIngressReceipt packet{}, last{}, dispatched{};
  Cell cells[Capacity]{};

  void invalidate() {
    if (generation != 0) generation = generation == UINT32_MAX ? 0 : generation + 1;
    packetActive = false;
    last.flags &= ~MidiIngressReceipt::CurrentQueue;
    dispatched.flags &= ~MidiIngressReceipt::CurrentQueue;
  }
  void reset(uint32_t capacity) { invalidate(); capacityMatches = capacity == Capacity; last = {}; }
  bool canCapture(uint32_t capacity) const {
    return generation != 0 && sequence != UINT64_MAX && capacityMatches && capacity == Capacity;
  }
  void begin(MidiIngressReceipt observed, uint32_t capacity, bool late) {
    packetActive = false;
    if (!canCapture(capacity)) {
      if (capacity != Capacity) capacityMatches = false;
      if (sequence == UINT64_MAX) generation = 0;
      last.flags &= ~MidiIngressReceipt::CurrentQueue;
      return;
    }
    observed.receiveSequence = ++sequence;
    observed.queueGeneration = generation;
    observed.flags = MidiIngressReceipt::CurrentQueue | (late ? MidiIngressReceipt::LateHardwareRead : 0);
    packet = observed;
    packetActive = true;
  }
  void publish(uint32_t slot, uint32_t offset, uint32_t count) {
    if (slot >= Capacity) { invalidate(); return; }
    auto& cell = cells[slot];
    cell.present = false;
    if (!packetActive || count == 0 || count > UINT16_MAX || offset > UINT16_MAX ||
        count - 1 > UINT16_MAX - offset || packet.queueGeneration != generation) return;
    cell.receipt = packet;
    cell.receipt.eventOffset = static_cast<uint16_t>(offset);
    cell.receipt.flags |= MidiIngressReceipt::Associated;
    cell.nextOffset = 0;
    cell.extent = static_cast<uint16_t>(count);
    cell.present = true;
  }
  void consume(uint32_t slot, uint32_t offset) {
    last = {};
    if (slot >= Capacity) return;
    auto& cell = cells[slot];
    if (!cell.present || offset >= cell.extent) return;
    if (offset != cell.nextOffset) { cell.present = false; return; }
    last = cell.receipt;
    last.eventOffset = static_cast<uint16_t>(last.eventOffset + offset);
    if (generation == 0 || !capacityMatches || last.queueGeneration != generation)
      last.flags &= ~MidiIngressReceipt::CurrentQueue;
    if (offset + 1 == cell.extent) cell.present = false;
    ++cell.nextOffset;
  }
};
struct State {
  Queue<kUsbCells> usb;
  Queue<kDinCells> din;
  Queue<kHostCells> host;
  ClockSampler sampler = nullptr;
#if MIDI_INGRESS_COST_QUALIFICATION
  IngressCostAggregate costs[3]{};
#endif
};
} // namespace detail
} // namespace MidiIngress
