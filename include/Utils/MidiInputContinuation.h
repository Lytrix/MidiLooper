// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>
#include "Utils/MidiDispatchOrder.h"
#include "Utils/MidiIngressReceipt.h"

// Fixed working state used only by MidiHandler's native qualification branch.
// No clock, publication, allocation, transport state machine or scheduler here.
struct MidiInputContinuation {
  struct Row { uint8_t type, channel, data1, data2; };
  static constexpr uint16_t kCapacity = 128;
  static constexpr uint16_t kScanQuantum = 16;
  enum class Phase : uint8_t { Transport, Remainder, Fifo };
  Row rows[kCapacity];
  uint32_t readAtUs[kCapacity];
#if MIDI_INGRESS_RECEIPTS_ENABLED
  MidiIngressReceipt ingress[kCapacity];
  MidiIngressReceipt ingressReceiptForDispatch() const {
    return dispatching && cursor != 0 ? ingress[cursor - 1] : MidiIngressReceipt{};
  }
#endif
  uint64_t generation = 1;
  uint32_t acquiredAtUs = 0;
  uint32_t acquiredClockEpoch = 0; // Observation only; never discards already-read input.
  uint16_t count = 0;
  uint16_t cursor = 0;
  uint16_t dispatched = 0;
  uint8_t route = 0;
  Phase phase = Phase::Transport;
  uint8_t frozenCap = kCapacity;
  uint8_t nextRoute = 0;
  bool acquiringHost = false;
  bool acquiring = false;
  bool dispatching = false;
  bool active = false;

  bool advanceGeneration() {
    if (generation != 0) generation = generation == UINT64_MAX ? 0 : generation + 1;
    return generation != 0;
  }
  // Explicit cold-session reset, never a MIDI Stop/source-change cancellation.
  void resetForSetup() {
    advanceGeneration();
    count = cursor = dispatched = 0;
    route = nextRoute = 0;
    phase = Phase::Transport;
    acquiringHost = acquiring = dispatching = active = false;
  }
  bool begin(uint8_t source, uint8_t cap, uint32_t epoch, uint32_t nowUs) {
    if (active || acquiring || dispatching || source > 2 || cap == 0 || cap > kCapacity || !advanceGeneration()) return false;
    route = source; frozenCap = cap;
    phase = source == 2 ? Phase::Fifo : Phase::Transport;
    acquiredClockEpoch = epoch; acquiredAtUs = nowUs;
    count = cursor = dispatched = 0;
    nextRoute = static_cast<uint8_t>((source + 1) % 3);
    return true;
  }
  bool append(Row row, uint32_t nowUs
#if MIDI_INGRESS_RECEIPTS_ENABLED
              , const MidiIngressReceipt& receipt = {}
#endif
              ) {
    if (generation == 0 || active || dispatching || count >= frozenCap) return false;
    rows[count] = row;
    readAtUs[count] = nowUs;
#if MIDI_INGRESS_RECEIPTS_ENABLED
    ingress[count] = receipt;
#endif
    ++count;
    return true;
  }
  uint16_t pending() const { return static_cast<uint16_t>(count - dispatched); }
  bool next(Row& row) {
    if (!active || acquiring || dispatching || generation == 0) return false;
    uint16_t inspected = 0;
    while (active) {
      if (cursor == count) {
        if (phase == Phase::Transport) { phase = Phase::Remainder; cursor = 0; }
        else { assert(dispatched == count); active = false; return false; }
      }
      if (inspected == kScanQuantum) return false;
      const Row candidate = rows[cursor++];
      ++inspected;
      const bool transport = MidiDispatchOrder::isSequenceTransport(candidate.type);
      if (phase == Phase::Fifo || (phase == Phase::Transport ? transport : !transport)) {
        row = candidate;
        dispatching = true;
        return true;
      }
    }
    return false;
  }
  void acknowledge() {
    assert(dispatching && dispatched < count);
    ++dispatched;
    dispatching = false;
    if (dispatched == count) active = false;
  }
};
