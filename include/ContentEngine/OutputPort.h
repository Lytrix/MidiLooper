//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Physical MIDI output port admission (fixed-memory-engine CE-C1).
//
// One bounded FIFO of `Capacity` messages per port with protected NoteOff capacity:
//   * a NoteOn is queued only if the queue can hold it *and* its future NoteOff (queued + reserved + 2 <= Capacity);
//     the NoteOn then holds one reservation until its NoteOff is queued;
//   * a NoteOff is queued only against its NoteOn's reservation, so it always fits and is never sent for a note the
//     port did not admit;
//   * any other message (CC, pitch bend, program, aftertouch, clock is not routed here) needs one free unreserved slot
//     *and* may hold at most `OtherShare` queued slots (owner ruling 2026-11-04: NoteOns are guaranteed the rest of the
//     queue, so under saturation note traffic keeps at least (Capacity - OtherShare) / Capacity of the port's
//     throughput); otherwise it is suppressed by name;
//   * strict FIFO: a NoteOff never overtakes its own NoteOn, and DEC-244 order is kept as queued.
// A message leaves the queue when the UART starts it; residence (start - queued) is measured and bounded by
// Capacity x message time. Nothing here claims more than the port can carry (DIN: one 3-byte message per 960 us).
// Stored source is never touched. No allocation.

#pragma once

#include <cstdint>

namespace ContentEngine {

constexpr uint32_t kDinMessageMicros = 960;  // 3 bytes x 10 bits at 31,250 baud

enum class PortResult : uint8_t { Queued, Suppressed, NotReserved };

template <uint16_t Capacity, uint16_t OtherShare = Capacity / 4>
class OutputPort {
 public:
  static constexpr uint16_t kOtherShare = OtherShare;
  static_assert(OtherShare + 2 <= Capacity, "a NoteOn and its NoteOff must always fit beside a full other share");

  struct Sent {
    uint32_t transmitMicros;
    uint32_t noteKey;
    uint8_t status, data1, data2;
  };
  struct Counters {
    uint32_t suppressedNoteOn;
    uint32_t suppressedOther;
    uint32_t maxResidenceMicros;
    uint32_t sent;
    uint16_t maxOtherQueued;
  };

  void init(uint32_t messageMicros) {
    messageMicros_ = messageMicros;
    head_ = 0;
    queued_ = 0;
    reservedCount_ = 0;
    otherQueued_ = 0;
    txFreeAt_ = 0;
    counters_ = Counters{};
    for (auto& m : ring_) m = Msg{};
    for (auto& k : reserved_) k = 0;
  }

  bool canAdmitNoteOn() const { return queued_ + reservedCount_ + 2u <= Capacity; }

  PortResult noteOn(uint32_t nowMicros, uint8_t status, uint8_t d1, uint8_t d2, uint32_t noteKey) {
    if (!canAdmitNoteOn()) {
      ++counters_.suppressedNoteOn;
      return PortResult::Suppressed;
    }
    push(Msg{nowMicros, noteKey, status, d1, d2, false});
    reserved_[reservedCount_++] = noteKey;
    return PortResult::Queued;
  }

  /// The NoteOff of a note this port admitted: uses its reservation, so it always fits.
  PortResult noteOff(uint32_t nowMicros, uint8_t status, uint8_t d1, uint8_t d2, uint32_t noteKey) {
    for (uint16_t i = 0; i < reservedCount_; ++i) {
      if (reserved_[i] != noteKey) continue;
      reserved_[i] = reserved_[--reservedCount_];
      push(Msg{nowMicros, noteKey, status, d1, d2, false});
      return PortResult::Queued;
    }
    return PortResult::NotReserved;
  }

  PortResult other(uint32_t nowMicros, uint8_t status, uint8_t d1, uint8_t d2) {
    if (otherQueued_ >= OtherShare || queued_ + reservedCount_ + 1u > Capacity) {
      ++counters_.suppressedOther;
      return PortResult::Suppressed;
    }
    push(Msg{nowMicros, 0, status, d1, d2, true});
    if (++otherQueued_ > counters_.maxOtherQueued) counters_.maxOtherQueued = otherQueued_;
    return PortResult::Queued;
  }

  /// Starts every message the UART can start by `nowMicros`, in FIFO order. Returns how many left the queue.
  uint8_t drain(uint32_t nowMicros, Sent* out, uint8_t max) {
    uint8_t n = 0;
    while (queued_ > 0 && n < max) {
      const Msg& m = ring_[head_];
      const uint32_t start = txFreeAt_ > m.queuedMicros ? txFreeAt_ : m.queuedMicros;
      if (start > nowMicros) break;
      const uint32_t residence = start - m.queuedMicros;
      if (residence > counters_.maxResidenceMicros) counters_.maxResidenceMicros = residence;
      out[n++] = Sent{start, m.noteKey, m.status, m.data1, m.data2};
      txFreeAt_ = start + messageMicros_;
      if (m.other) --otherQueued_;
      head_ = static_cast<uint16_t>((head_ + 1) % Capacity);
      --queued_;
      ++counters_.sent;
    }
    return n;
  }

  /// queued + reserved <= Capacity at every step.
  bool check() const {
    return queued_ <= Capacity && queued_ + reservedCount_ <= Capacity && otherQueued_ <= OtherShare;
  }
  uint16_t queued() const { return queued_; }
  uint16_t reserved() const { return reservedCount_; }
  const Counters& counters() const { return counters_; }

 private:
  struct Msg {
    uint32_t queuedMicros = 0;
    uint32_t noteKey = 0;
    uint8_t status = 0, data1 = 0, data2 = 0;
    bool other = false;
  };

  void push(const Msg& m) {
    ring_[(head_ + queued_) % Capacity] = m;
    ++queued_;
  }

  Msg ring_[Capacity];
  uint32_t reserved_[Capacity];
  uint32_t messageMicros_;
  uint32_t txFreeAt_;
  uint16_t head_;
  uint16_t queued_;
  uint16_t reservedCount_;
  uint16_t otherQueued_;
  Counters counters_;
};

}  // namespace ContentEngine
