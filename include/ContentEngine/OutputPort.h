//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Physical MIDI output port (fixed-memory-engine CE-C1). One instance per port (DIN, USB device, USB host), each with
// its own message time; fixed memory, no allocation.
//
// Message classes and what the port guarantees for each:
//   * NoteOn: admitted only if the port's sounding table has an entry (<= 8 tracks x 16 = 128) and the ordered queue
//     has note space; otherwise suppressed by name (the stored event is unchanged).
//   * NoteOff: needs no queue entry. It is owed by the sounding table, sent with priority once due, and never before its
//     own NoteOn. A NoteOff whose NoteOn is still queued cancels both: nothing is sent for a note that never sounded.
//     Long held notes therefore hold a sounding entry, never a FIFO slot.
//   * Essential state: Bank Select (CC0/32), Program Change, sustain-family pedals (CC64-69), channel mode (CC120-127).
//     Ordered with notes (state before dependent NoteOns), never dropped. A state message coalesces with the same
//     queued state of its channel only when no NoteOn of that channel sits between them. Beyond the ordered queue it
//     waits in a per-(channel, state) table in arrival order, and new NoteOns are held back until it is queued (causal).
//   * Parameter (RPN/NRPN select, data entry, increment/decrement: CC6/38/96-101): ordered, never coalesced; beyond the
//     queue a 64-message overflow ring; only past that is one counted as dropped (`parameterDropped`).
//   * Continuous automation (all other CCs, pitch bend, channel pressure, poly aftertouch): coalesced per
//     (channel, controller) to the latest pending value; a value equal to the last sent one is not re-sent; the final
//     value always goes out.
// Transmit scheduling (per message slot, causal): due NoteOffs first; then the ordered queue, with continuous automation
// guaranteed one slot in four while both are pending. Shares are of actual transmissions. Lateness is measured per class
// (NoteOn: since queued; NoteOff: since due) and is what the port can do, not a promise beyond its bandwidth.

#pragma once

#include <cstdint>

namespace ContentEngine {

constexpr uint32_t kDinMessageMicros = 960;  // 3 bytes x 10 bits at 31,250 baud
constexpr uint32_t kUsbMessageMicros = 125;  // declared estimate for USB full-speed MIDI; measured on the device (E0.7)

enum class PortResult : uint8_t { Queued, Coalesced, Deduplicated, Suppressed, CancelledUnsent, NotAdmitted };
enum class OutClass : uint8_t { NoteOff, NoteOn, Essential, Parameter, Continuous };

template <uint16_t Sounding = 128, uint16_t Ordered = 64, uint16_t EssentialReserve = 16>
class OutputPort {
 public:
  static constexpr uint16_t kContinuousSlots = 16 * 258;  // per channel: 128 CC, bend, pressure, 128 poly aftertouch
  static constexpr uint16_t kStateSlots = 17;             // per channel: CC0, CC32, program, CC64-69, CC120-127
  static constexpr uint16_t kParameterOverflow = 64;

  struct Sent {
    uint32_t micros;
    uint32_t key;
    uint8_t status, data1, data2;
    OutClass cls;
  };
  struct Counters {
    uint32_t suppressedNoteOn;
    uint32_t cancelledUnsent;
    uint32_t coalesced;
    uint32_t deduplicated;
    uint32_t stateOverflowCoalesced;
    uint32_t parameterDropped;
    uint32_t maxNoteOnLateMicros, maxNoteOffLateMicros;
    uint64_t sumNoteOnLateMicros, sumNoteOffLateMicros;
    uint32_t sent[5];
  };

  void init(uint32_t messageMicros) {
    messageMicros_ = messageMicros;
    txFreeAt_ = 0;
    for (auto& e : sounding_) e = Entry{};
    for (auto& q : ring_) q = Item{};
    head_ = count_ = noteOnsQueued_ = 0;
    for (auto& v : pending_) v = 0;
    for (auto& v : lastSent_) v = kUnknown;
    dirtyHead_ = dirtyCount_ = 0;
    for (auto& v : statePending_) v = 0;
    stateHead_ = stateCount_ = 0;
    paramHead_ = paramCount_ = 0;
    sinceContinuous_ = 0;
    counters_ = Counters{};
  }

  bool canAdmitNoteOn() const {
    return freeEntry() >= 0 && noteOnsQueued_ < Ordered - EssentialReserve && count_ < Ordered && stateCount_ == 0 &&
           paramCount_ == 0;
  }

  PortResult noteOn(uint32_t now, uint8_t status, uint8_t pitch, uint8_t velocity, uint32_t key) {
    if (!canAdmitNoteOn()) {
      ++counters_.suppressedNoteOn;
      return PortResult::Suppressed;
    }
    const int e = freeEntry();
    sounding_[e] = Entry{key, now, 0, status, pitch, State::QueuedOn};
    push(Item{Kind::NoteOn, status, pitch, velocity, static_cast<uint8_t>(e), now});
    ++noteOnsQueued_;
    return PortResult::Queued;
  }

  PortResult noteOff(uint32_t now, uint32_t key) {
    for (uint16_t i = 0; i < Sounding; ++i) {
      Entry& e = sounding_[i];
      if (e.state == State::Free || e.key != key) continue;
      if (e.state == State::QueuedOn) {
        for (uint16_t k = 0; k < count_; ++k) {
          Item& it = ring_[(head_ + k) % Ordered];
          if (it.kind == Kind::NoteOn && it.entry == i) it.kind = Kind::Tombstone;
        }
        --noteOnsQueued_;
        e = Entry{};
        ++counters_.cancelledUnsent;
        return PortResult::CancelledUnsent;
      }
      if (e.state == State::Playing) {
        e.state = State::OffPending;
        e.dueMicros = now;
      }
      return PortResult::Queued;
    }
    return PortResult::NotAdmitted;
  }

  PortResult control(uint32_t now, uint8_t status, uint8_t d1, uint8_t d2) {
    const uint8_t type = status & 0xF0, ch = status & 0x0F;
    const int state = stateSlot(type, d1);
    if (state >= 0) return essential(now, status, d1, d2, ch, static_cast<uint8_t>(state));
    if (type == 0xB0 && isParameter(d1)) return parameter(now, status, d1, d2);
    return continuous(status, d1, d2);
  }

  /// Starts every message the port can start by `now`, in priority and causal order. Returns how many were started.
  uint8_t transmit(uint32_t now, Sent* out, uint8_t max) {
    refillFromOverflow(now);
    uint8_t n = 0;
    while (n < max && txFreeAt_ <= now) {
      Sent s;
      if (!next(now, s)) break;
      out[n++] = s;
      refillFromOverflow(now);
    }
    return n;
  }

  uint16_t soundingCount() const {
    uint16_t n = 0;
    for (const auto& e : sounding_) n += e.state != State::Free ? 1 : 0;
    return n;
  }
  uint16_t orderedQueued() const { return count_; }
  const Counters& counters() const { return counters_; }

  bool check() const {
    uint16_t queuedOn = 0;
    for (const auto& e : sounding_) queuedOn += e.state == State::QueuedOn ? 1 : 0;
    return count_ <= Ordered && noteOnsQueued_ == queuedOn && noteOnsQueued_ <= Ordered - EssentialReserve &&
           dirtyCount_ <= kContinuousSlots && stateCount_ <= 16 * kStateSlots && paramCount_ <= kParameterOverflow;
  }

 private:
  static constexpr uint16_t kUnknown = 0xFFFF;
  static constexpr uint16_t kPendingBit = 0x8000;
  enum class State : uint8_t { Free, QueuedOn, Playing, OffPending };
  enum class Kind : uint8_t { Tombstone, NoteOn, Essential, Parameter };
  struct Entry {
    uint32_t key = 0;
    uint32_t queuedMicros = 0;
    uint32_t dueMicros = 0;
    uint8_t status = 0, pitch = 0;
    State state = State::Free;
  };
  struct Item {
    Kind kind = Kind::Tombstone;
    uint8_t status = 0, data1 = 0, data2 = 0;
    uint8_t entry = 0;
    uint32_t queuedMicros = 0;
  };
  struct Param {
    uint8_t status, data1, data2;
    uint32_t queuedMicros;
  };

  static int stateSlot(uint8_t type, uint8_t d1) {
    if (type == 0xC0) return 2;
    if (type != 0xB0) return -1;
    if (d1 == 0) return 0;
    if (d1 == 32) return 1;
    if (d1 >= 64 && d1 <= 69) return 3 + (d1 - 64);
    if (d1 >= 120) return 9 + (d1 - 120);
    return -1;
  }
  static bool isParameter(uint8_t d1) { return d1 == 6 || d1 == 38 || (d1 >= 96 && d1 <= 101); }

  int freeEntry() const {
    for (uint16_t i = 0; i < Sounding; ++i)
      if (sounding_[i].state == State::Free) return i;
    return -1;
  }

  void push(const Item& it) {
    ring_[(head_ + count_) % Ordered] = it;
    ++count_;
  }

  PortResult essential(uint32_t now, uint8_t status, uint8_t d1, uint8_t d2, uint8_t ch, uint8_t slot) {
    // Coalesce with the latest queued same-state message of this channel if no NoteOn of the channel follows it.
    for (int k = static_cast<int>(count_) - 1; k >= 0; --k) {
      Item& it = ring_[(head_ + k) % Ordered];
      if (it.kind == Kind::NoteOn && (it.status & 0x0F) == ch) break;
      if (it.kind == Kind::Essential && it.status == status && ((status & 0xF0) == 0xC0 || it.data1 == d1)) {
        it.data1 = d1;
        it.data2 = d2;
        ++counters_.coalesced;
        return PortResult::Coalesced;
      }
    }
    if (count_ < Ordered && stateCount_ == 0 && paramCount_ == 0) {
      push(Item{Kind::Essential, status, d1, d2, 0, now});
      return PortResult::Queued;
    }
    const uint16_t idx = static_cast<uint16_t>(ch * kStateSlots + slot);
    if (statePending_[idx] & kPendingBit) ++counters_.stateOverflowCoalesced;
    else stateOrder_[(stateHead_ + stateCount_++) % (16 * kStateSlots)] = idx;
    statePending_[idx] = static_cast<uint16_t>(kPendingBit | (d1 << 7) | (d2 & 0x7F));
    stateStatus_[idx] = status;
    return PortResult::Queued;
  }

  PortResult parameter(uint32_t now, uint8_t status, uint8_t d1, uint8_t d2) {
    if (count_ < Ordered && stateCount_ == 0 && paramCount_ == 0) {
      push(Item{Kind::Parameter, status, d1, d2, 0, now});
      return PortResult::Queued;
    }
    if (paramCount_ >= kParameterOverflow) {
      ++counters_.parameterDropped;
      return PortResult::Suppressed;
    }
    params_[(paramHead_ + paramCount_++) % kParameterOverflow] = Param{status, d1, d2, now};
    return PortResult::Queued;
  }

  PortResult continuous(uint8_t status, uint8_t d1, uint8_t d2) {
    const uint8_t type = status & 0xF0, ch = status & 0x0F;
    uint16_t slot, value;
    if (type == 0xB0) {
      slot = d1 & 0x7F;
      value = d2 & 0x7F;
    } else if (type == 0xE0) {
      slot = 128;
      value = static_cast<uint16_t>(((d2 & 0x7F) << 7) | (d1 & 0x7F));
    } else if (type == 0xD0) {
      slot = 129;
      value = d1 & 0x7F;
    } else {  // 0xA0 poly aftertouch
      slot = static_cast<uint16_t>(130 + (d1 & 0x7F));
      value = d2 & 0x7F;
    }
    const uint16_t idx = static_cast<uint16_t>(ch * 258 + slot);
    if (pending_[idx] & kPendingBit) {
      pending_[idx] = static_cast<uint16_t>(kPendingBit | value);
      ++counters_.coalesced;
      return PortResult::Coalesced;
    }
    if (lastSent_[idx] == value) {
      ++counters_.deduplicated;
      return PortResult::Deduplicated;
    }
    pending_[idx] = static_cast<uint16_t>(kPendingBit | value);
    dirty_[(dirtyHead_ + dirtyCount_++) % kContinuousSlots] = idx;
    return PortResult::Queued;
  }

  /// Moves waiting state and parameter messages into the ordered queue, oldest first, while it has room.
  void refillFromOverflow(uint32_t now) {
    while (count_ < Ordered && stateCount_ > 0) {
      const uint16_t idx = stateOrder_[stateHead_];
      stateHead_ = static_cast<uint16_t>((stateHead_ + 1) % (16 * kStateSlots));
      --stateCount_;
      const uint16_t v = statePending_[idx];
      statePending_[idx] = 0;
      push(Item{Kind::Essential, stateStatus_[idx], static_cast<uint8_t>((v >> 7) & 0x7F), static_cast<uint8_t>(v & 0x7F),
                0, now});
    }
    while (count_ < Ordered && stateCount_ == 0 && paramCount_ > 0) {
      const Param& p = params_[paramHead_];
      push(Item{Kind::Parameter, p.status, p.data1, p.data2, 0, p.queuedMicros});
      paramHead_ = static_cast<uint16_t>((paramHead_ + 1) % kParameterOverflow);
      --paramCount_;
    }
  }

  bool orderedReady() {
    while (count_ > 0 && ring_[head_].kind == Kind::Tombstone) {
      head_ = static_cast<uint16_t>((head_ + 1) % Ordered);
      --count_;
    }
    return count_ > 0;
  }

  bool next(uint32_t now, Sent& s) {
    (void)now;
    // 1. Due NoteOffs (their NoteOn has been sent: only Sounding entries become OffPending).
    int off = -1;
    for (uint16_t i = 0; i < Sounding; ++i)
      if (sounding_[i].state == State::OffPending && (off < 0 || sounding_[i].dueMicros < sounding_[off].dueMicros)) off = i;
    if (off >= 0) {
      Entry& e = sounding_[off];
      const uint32_t start = txFreeAt_ > e.dueMicros ? txFreeAt_ : e.dueMicros;
      account(start - e.dueMicros, counters_.maxNoteOffLateMicros, counters_.sumNoteOffLateMicros);
      s = Sent{start, e.key, static_cast<uint8_t>(0x80 | (e.status & 0x0F)), e.pitch, 0, OutClass::NoteOff};
      e = Entry{};
      return sendAt(start, s, false);
    }
    const bool ordered = orderedReady();
    const bool cont = dirtyCount_ > 0;
    if (cont && (!ordered || sinceContinuous_ >= 3)) return sendContinuous(s);
    if (!ordered) return false;
    Item& it = ring_[head_];
    const uint32_t start = txFreeAt_ > it.queuedMicros ? txFreeAt_ : it.queuedMicros;
    if (it.kind == Kind::NoteOn) {
      Entry& e = sounding_[it.entry];
      e.state = State::Playing;
      --noteOnsQueued_;
      account(start - it.queuedMicros, counters_.maxNoteOnLateMicros, counters_.sumNoteOnLateMicros);
      s = Sent{start, e.key, it.status, it.data1, it.data2, OutClass::NoteOn};
    } else {
      s = Sent{start, 0, it.status, it.data1, it.data2,
               it.kind == Kind::Essential ? OutClass::Essential : OutClass::Parameter};
    }
    head_ = static_cast<uint16_t>((head_ + 1) % Ordered);
    --count_;
    return sendAt(start, s, cont);
  }

  bool sendContinuous(Sent& s) {
    while (dirtyCount_ > 0) {
      const uint16_t idx = dirty_[dirtyHead_];
      dirtyHead_ = static_cast<uint16_t>((dirtyHead_ + 1) % kContinuousSlots);
      --dirtyCount_;
      if (!(pending_[idx] & kPendingBit)) continue;
      const uint16_t value = pending_[idx] & 0x3FFF;
      pending_[idx] = 0;
      lastSent_[idx] = value;
      const uint8_t ch = static_cast<uint8_t>(idx / 258);
      const uint16_t slot = idx % 258;
      if (slot < 128)
        s = Sent{txFreeAt_, 0, static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(slot), static_cast<uint8_t>(value),
                 OutClass::Continuous};
      else if (slot == 128)
        s = Sent{txFreeAt_, 0, static_cast<uint8_t>(0xE0 | ch), static_cast<uint8_t>(value & 0x7F),
                 static_cast<uint8_t>(value >> 7), OutClass::Continuous};
      else if (slot == 129)
        s = Sent{txFreeAt_, 0, static_cast<uint8_t>(0xD0 | ch), static_cast<uint8_t>(value), 0, OutClass::Continuous};
      else
        s = Sent{txFreeAt_, 0, static_cast<uint8_t>(0xA0 | ch), static_cast<uint8_t>(slot - 130),
                 static_cast<uint8_t>(value), OutClass::Continuous};
      sinceContinuous_ = 0;
      return sendAt(txFreeAt_, s, false);
    }
    return false;
  }

  bool sendAt(uint32_t start, const Sent& s, bool continuousWaiting) {
    txFreeAt_ = start + messageMicros_;
    ++counters_.sent[static_cast<uint8_t>(s.cls)];
    if (continuousWaiting && sinceContinuous_ < 0xFF) ++sinceContinuous_;
    return true;
  }

  static void account(uint32_t late, uint32_t& max, uint64_t& sum) {
    if (late > max) max = late;
    sum += late;
  }

  uint32_t messageMicros_;
  uint32_t txFreeAt_;
  Entry sounding_[Sounding];
  Item ring_[Ordered];
  uint16_t head_, count_, noteOnsQueued_;
  uint16_t pending_[kContinuousSlots];
  uint16_t lastSent_[kContinuousSlots];
  uint16_t dirty_[kContinuousSlots];
  uint16_t dirtyHead_, dirtyCount_;
  uint16_t statePending_[16 * kStateSlots];
  uint8_t stateStatus_[16 * kStateSlots];
  uint16_t stateOrder_[16 * kStateSlots];
  uint16_t stateHead_, stateCount_;
  Param params_[kParameterOverflow];
  uint16_t paramHead_, paramCount_;
  uint8_t sinceContinuous_;
  Counters counters_;
};

}  // namespace ContentEngine
