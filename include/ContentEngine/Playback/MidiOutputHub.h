//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Physical MIDI output (fixed-memory-engine CE-C1, memory-ownership correction 2026-11-04).
//
// One `MidiOutputHub` owns the output state of all destinations (DIN, USB device, USB host); fixed memory, no allocation.
//   * Logical notes are shared: one table sized tracks x 16 (`Notes`), not ports x tracks x 16. A note routed to
//     several ports keeps one identity with a 2-bit state per port (queued / playing / off pending), so each port sends
//     and terminates it independently.
//   * Each port keeps only what is destination-specific: its bounded ordered TX queue, timing, essential-state overflow
//     (dense per channel/state, because essential state must never be dropped) and RPN/NRPN overflow, and a *sparse*
//     automation-lane table holding only the controller lanes in use on that port.
//
// Semantics per port (unchanged from the previous OutputPort; see test_content_engine_output):
//   * NoteOn: admitted only if a logical note entry exists or can be taken and the port's ordered queue has note space;
//     otherwise suppressed by name for that port (the stored event is unchanged).
//   * NoteOff: needs no queue entry; owed by the port's note state, sent with priority once due and never before its own
//     NoteOn.
//   * Expired-note cancellation (owner decision 2026-11-04): a NoteOn that has not begun transmission when its scheduled
//     end is reached is never transmitted on that port: either its NoteOff arrives while it is still queued, or the
//     transmitter reaches it at or after its `endMicros`. The instance and its pending state are released there and
//     counted as `expiredBeforeTransmit` (apart from capacity refusal, polyphony suppression and lateness). Once a NoteOn
//     has started on the wire its NoteOff obligation is kept. Per destination; recorded events are untouched.
//   * Essential state (Bank Select, Program Change, CC64-69, CC120-127): ordered with notes, never dropped; coalesces only
//     without a dependent NoteOn between; overflow waits per channel/state and holds back new NoteOns.
//   * Parameter (CC6/38/96-101): ordered, never coalesced; 64-message overflow; beyond it counted as dropped.
//   * Continuous automation: coalesced per lane to its latest value, unchanged values not re-sent, final value sent.
//     A lane exists only while in use; a clean lane (nothing pending) is evicted for a new one when the table is full;
//     only if every lane has a pending value is the new value counted (`automationLanesFull`) and suppressed.
//   * Scheduling per port: due NoteOffs, then the ordered queue, with automation one slot in four while both wait.

#pragma once

#include <cstdint>

#include "ContentEngine/Content/MidiClass.h"

namespace ContentEngine {

constexpr uint32_t kDinMessageMicros = 960;  // 3 bytes x 10 bits at 31,250 baud
constexpr uint32_t kUsbMessageMicros = 125;  // declared estimate for USB full-speed MIDI; measured on the device (E0.7)
constexpr uint32_t kNoEndMicros = 0xFFFFFFFFu;  // NoteOn without a known scheduled end (live input, open composed end)

enum class PortResult : uint8_t { Queued, Coalesced, Deduplicated, Suppressed, CancelledUnsent, NotAdmitted };
enum class OutClass : uint8_t { NoteOff, NoteOn, Essential, Parameter, Continuous };

template <uint16_t Notes = 128, uint8_t Ports = 3, uint16_t Ordered = 64, uint16_t EssentialReserve = 16,
          uint16_t Lanes = 256>
class MidiOutputHub {
  static_assert(Ports <= 4, "per-port note state is 2 bits in one byte");

 public:
  static constexpr uint16_t kStateSlots = 17;  // per channel: CC0, CC32, program, CC64-69, CC120-127
  static constexpr uint16_t kParameterOverflow = 64;

  struct Sent {
    uint32_t micros;
    uint32_t key;
    uint8_t status, data1, data2;
    OutClass cls;
  };
  struct Counters {
    uint32_t suppressedNoteOn;       // queue-capacity / note-table refusal at admission
    uint32_t expiredBeforeTransmit;  // NoteOn whose end came before it could start (never sent, NoteOff not owed)
    uint32_t coalesced;
    uint32_t deduplicated;
    uint32_t stateOverflowCoalesced;
    uint32_t parameterDropped;
    uint32_t automationLanesFull;
    uint32_t maxNoteOnLateMicros, maxNoteOffLateMicros;
    uint64_t sumNoteOnLateMicros, sumNoteOffLateMicros;
    uint32_t sent[5];
  };

  /// A per-destination view with the single-port API.
  class Port {
   public:
    using Sent = typename MidiOutputHub::Sent;
    using Counters = typename MidiOutputHub::Counters;
    Port() = default;
    Port(MidiOutputHub* hub, uint8_t p) : hub_(hub), p_(p) {}
    bool canAdmitNoteOn() const { return hub_->canAdmitNoteOn(p_); }
    PortResult noteOn(uint32_t now, uint8_t status, uint8_t pitch, uint8_t vel, uint32_t key,
                      uint32_t endMicros = kNoEndMicros) {
      return hub_->noteOn(now, p_, status, pitch, vel, key, endMicros);
    }
    PortResult noteOff(uint32_t now, uint32_t key) { return hub_->noteOff(now, p_, key); }
    PortResult control(uint32_t now, uint8_t status, uint8_t d1, uint8_t d2) {
      return hub_->control(now, p_, status, d1, d2);
    }
    uint8_t transmit(uint32_t now, Sent* out, uint8_t max) { return hub_->transmit(now, p_, out, max); }
    uint16_t soundingCount() const { return hub_->soundingCount(p_); }
    uint16_t orderedQueued() const { return hub_->ports_[p_].count; }
    const Counters& counters() const { return hub_->ports_[p_].counters; }
    bool check() const { return hub_->check(); }

   private:
    MidiOutputHub* hub_ = nullptr;
    uint8_t p_ = 0;
  };

  void init() {
    for (auto& n : notes_) n = Note{};
    for (uint8_t p = 0; p < Ports; ++p) initPort(ports_[p], kDinMessageMicros);
  }
  void configure(uint8_t p, uint32_t messageMicros) { initPort(ports_[p], messageMicros); }
  Port port(uint8_t p) { return Port(this, p); }

  bool canAdmitNoteOn(uint8_t p) const {
    const PortData& d = ports_[p];
    return d.noteOnsQueued < Ordered - EssentialReserve && d.count < Ordered && d.stateCount == 0 &&
           d.paramCount == 0;
  }

  /// Admits a NoteOn on port `p`. The logical note (by key) is shared: routing the same key to another port attaches
  /// that port to the same entry.
  PortResult noteOn(uint32_t now, uint8_t p, uint8_t status, uint8_t pitch, uint8_t vel, uint32_t key,
                    uint32_t endMicros = kNoEndMicros) {
    PortData& d = ports_[p];
    int n = find(key);
    if (n >= 0 && stateOf(notes_[n], p) != State::None) return PortResult::Queued;  // already on this port
    if (!canAdmitNoteOn(p) || (n < 0 && (n = freeNote()) < 0)) {
      ++d.counters.suppressedNoteOn;
      return PortResult::Suppressed;
    }
    Note& note = notes_[n];
    if (note.portStates == 0) note = Note{key, endMicros, 0, status, pitch, 0};
    setState(note, p, State::QueuedOn);
    push(d, Item{Kind::NoteOn, status, pitch, vel, static_cast<uint16_t>(n), now});
    ++d.noteOnsQueued;
    return PortResult::Queued;
  }

  PortResult noteOff(uint32_t now, uint8_t p, uint32_t key) {
    const int n = find(key);
    if (n < 0) return PortResult::NotAdmitted;
    Note& note = notes_[n];
    PortData& d = ports_[p];
    switch (stateOf(note, p)) {
      case State::QueuedOn:
        for (uint16_t k = 0; k < d.count; ++k) {
          Item& it = d.ring[(d.head + k) % Ordered];
          if (it.kind == Kind::NoteOn && it.note == n) it.kind = Kind::Tombstone;
        }
        --d.noteOnsQueued;
        setState(note, p, State::None);
        ++d.counters.expiredBeforeTransmit;
        return PortResult::CancelledUnsent;
      case State::Playing:
        setState(note, p, State::OffPending);
        note.dueMicros = now;
        return PortResult::Queued;
      case State::OffPending:
        return PortResult::Queued;
      default:
        return PortResult::NotAdmitted;
    }
  }

  PortResult control(uint32_t now, uint8_t p, uint8_t status, uint8_t d1, uint8_t d2) {
    const uint8_t type = status & 0xF0, ch = status & 0x0F;
    switch (controlClass(status, d1)) {
      case ControlClass::Essential:
        return essential(ports_[p], now, status, d1, d2, ch, static_cast<uint8_t>(stateSlot(type, d1)));
      case ControlClass::Parameter:
        return parameter(ports_[p], now, status, d1, d2);
      case ControlClass::Continuous:
        break;
    }
    return continuous(ports_[p], status, d1, d2);
  }

  uint8_t transmit(uint32_t now, uint8_t p, Sent* out, uint8_t max) {
    PortData& d = ports_[p];
    refillFromOverflow(d, now);
    uint8_t n = 0;
    while (n < max && d.txFreeAt <= now) {
      Sent s;
      if (!next(d, p, s)) break;
      out[n++] = s;
      refillFromOverflow(d, now);
    }
    return n;
  }

  uint16_t soundingCount(uint8_t p) const {
    uint16_t c = 0;
    for (const auto& n : notes_) c += stateOf(n, p) != State::None ? 1 : 0;
    return c;
  }

  bool check() const {
    for (uint8_t p = 0; p < Ports; ++p) {
      const PortData& d = ports_[p];
      uint16_t queuedOn = 0;
      for (const auto& n : notes_) queuedOn += stateOf(n, p) == State::QueuedOn ? 1 : 0;
      if (d.count > Ordered || d.noteOnsQueued != queuedOn || d.noteOnsQueued > Ordered - EssentialReserve ||
          d.laneCount > Lanes || d.dirtyCount > Lanes || d.stateCount > 16 * kStateSlots ||
          d.paramCount > kParameterOverflow)
        return false;
    }
    return true;
  }

 private:
  static constexpr uint16_t kPendingBit = 0x8000;
  static constexpr uint16_t kNoNote = 0xFFFF;
  enum class State : uint8_t { None = 0, QueuedOn = 1, Playing = 2, OffPending = 3 };
  enum class Kind : uint8_t { Tombstone, NoteOn, Essential, Parameter };

  struct Note {  // 16 B, shared by every port the note is routed to
    uint32_t key = 0;
    uint32_t endMicros = kNoEndMicros;  // scheduled end: a NoteOn not started by then is never sent
    uint32_t dueMicros = 0;
    uint8_t status = 0, pitch = 0;
    uint8_t portStates = 0;  // 2 bits per port
    uint8_t pad = 0;
  };
  struct Item {  // 12 B
    Kind kind = Kind::Tombstone;
    uint8_t status = 0, data1 = 0, data2 = 0;
    uint16_t note = kNoNote;
    uint32_t queuedMicros = 0;
  };
  struct Param {  // 4 B
    uint8_t status, data1, data2, pad;
  };
  struct Lane {  // 8 B: one controller lane in use on this port
    uint16_t key;       // channel x 258 + slot (128 CC, bend, pressure, 128 poly aftertouch)
    uint16_t lastSent;  // 0xFFFF unknown
    uint16_t pending;   // kPendingBit | value
    uint16_t pad;
  };
  struct PortData {
    uint32_t messageMicros, txFreeAt;
    Item ring[Ordered];
    uint16_t head, count, noteOnsQueued;
    uint16_t statePending[16 * kStateSlots];
    uint16_t stateOrder[16 * kStateSlots];
    uint16_t stateHead, stateCount;
    Param params[kParameterOverflow];
    uint16_t paramHead, paramCount;
    Lane lanes[Lanes];
    uint16_t laneCount;
    uint16_t dirty[Lanes];
    uint16_t dirtyHead, dirtyCount;
    uint8_t sinceContinuous;
    Counters counters;
  };

  static State stateOf(const Note& n, uint8_t p) { return static_cast<State>((n.portStates >> (2 * p)) & 3u); }
  static void setState(Note& n, uint8_t p, State s) {
    n.portStates = static_cast<uint8_t>((n.portStates & ~(3u << (2 * p))) | (static_cast<uint8_t>(s) << (2 * p)));
  }

  static void initPort(PortData& d, uint32_t messageMicros) {
    d.messageMicros = messageMicros;
    d.txFreeAt = 0;
    for (auto& q : d.ring) q = Item{};
    d.head = d.count = d.noteOnsQueued = 0;
    for (auto& v : d.statePending) v = 0;
    for (auto& v : d.stateOrder) v = 0;
    d.stateHead = d.stateCount = 0;
    for (auto& v : d.params) v = Param{0, 0, 0, 0};
    d.paramHead = d.paramCount = 0;
    for (auto& l : d.lanes) l = Lane{0, 0xFFFF, 0, 0};
    d.laneCount = 0;
    for (auto& v : d.dirty) v = 0;
    d.dirtyHead = d.dirtyCount = 0;
    d.sinceContinuous = 0;
    d.counters = Counters{};
  }

  int find(uint32_t key) const {
    for (uint16_t i = 0; i < Notes; ++i)
      if (notes_[i].portStates != 0 && notes_[i].key == key) return i;
    return -1;
  }
  int freeNote() const {
    for (uint16_t i = 0; i < Notes; ++i)
      if (notes_[i].portStates == 0) return i;
    return -1;
  }

  static int stateSlot(uint8_t type, uint8_t d1) {
    if (type == 0xC0) return 2;
    if (type != 0xB0) return -1;
    if (d1 == 0) return 0;
    if (d1 == 32) return 1;
    if (d1 >= 64 && d1 <= 69) return 3 + (d1 - 64);
    if (d1 >= 120) return 9 + (d1 - 120);
    return -1;
  }
  static uint8_t stateStatus(uint16_t idx) {
    const uint8_t ch = static_cast<uint8_t>(idx / kStateSlots), slot = static_cast<uint8_t>(idx % kStateSlots);
    return static_cast<uint8_t>((slot == 2 ? 0xC0 : 0xB0) | ch);
  }

  static void push(PortData& d, const Item& it) {
    d.ring[(d.head + d.count) % Ordered] = it;
    ++d.count;
  }

  PortResult essential(PortData& d, uint32_t now, uint8_t status, uint8_t d1, uint8_t d2, uint8_t ch, uint8_t slot) {
    for (int k = static_cast<int>(d.count) - 1; k >= 0; --k) {
      Item& it = d.ring[(d.head + k) % Ordered];
      if (it.kind == Kind::NoteOn && (it.status & 0x0F) == ch) break;
      if (it.kind == Kind::Essential && it.status == status && ((status & 0xF0) == 0xC0 || it.data1 == d1)) {
        it.data1 = d1;
        it.data2 = d2;
        ++d.counters.coalesced;
        return PortResult::Coalesced;
      }
    }
    if (d.count < Ordered && d.stateCount == 0 && d.paramCount == 0) {
      push(d, Item{Kind::Essential, status, d1, d2, kNoNote, now});
      return PortResult::Queued;
    }
    const uint16_t idx = static_cast<uint16_t>(ch * kStateSlots + slot);
    if (d.statePending[idx] & kPendingBit)
      ++d.counters.stateOverflowCoalesced;
    else
      d.stateOrder[(d.stateHead + d.stateCount++) % (16 * kStateSlots)] = idx;
    d.statePending[idx] = static_cast<uint16_t>(kPendingBit | (d1 << 7) | (d2 & 0x7F));
    return PortResult::Queued;
  }

  PortResult parameter(PortData& d, uint32_t now, uint8_t status, uint8_t d1, uint8_t d2) {
    if (d.count < Ordered && d.stateCount == 0 && d.paramCount == 0) {
      push(d, Item{Kind::Parameter, status, d1, d2, kNoNote, now});
      return PortResult::Queued;
    }
    if (d.paramCount >= kParameterOverflow) {
      ++d.counters.parameterDropped;
      return PortResult::Suppressed;
    }
    d.params[(d.paramHead + d.paramCount++) % kParameterOverflow] = Param{status, d1, d2, 0};
    return PortResult::Queued;
  }

  PortResult continuous(PortData& d, uint8_t status, uint8_t d1, uint8_t d2) {
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
    } else {
      slot = static_cast<uint16_t>(130 + (d1 & 0x7F));
      value = d2 & 0x7F;
    }
    const uint16_t key = static_cast<uint16_t>(ch * 258 + slot);
    int l = -1;
    for (uint16_t i = 0; i < d.laneCount; ++i)
      if (d.lanes[i].key == key) {
        l = i;
        break;
      }
    if (l < 0) {
      if (d.laneCount < Lanes) {
        l = d.laneCount++;
      } else {
        for (uint16_t i = 0; i < Lanes; ++i)  // evict a clean lane (its dedupe memory is all that is lost)
          if (!(d.lanes[i].pending & kPendingBit)) {
            l = i;
            break;
          }
        if (l < 0) {
          ++d.counters.automationLanesFull;
          return PortResult::Suppressed;
        }
      }
      d.lanes[l] = Lane{key, 0xFFFF, 0, 0};
    }
    Lane& lane = d.lanes[l];
    if (lane.pending & kPendingBit) {
      lane.pending = static_cast<uint16_t>(kPendingBit | value);
      ++d.counters.coalesced;
      return PortResult::Coalesced;
    }
    if (lane.lastSent == value) {
      ++d.counters.deduplicated;
      return PortResult::Deduplicated;
    }
    lane.pending = static_cast<uint16_t>(kPendingBit | value);
    d.dirty[(d.dirtyHead + d.dirtyCount++) % Lanes] = static_cast<uint16_t>(l);
    return PortResult::Queued;
  }

  static void refillFromOverflow(PortData& d, uint32_t now) {
    while (d.count < Ordered && d.stateCount > 0) {
      const uint16_t idx = d.stateOrder[d.stateHead];
      d.stateHead = static_cast<uint16_t>((d.stateHead + 1) % (16 * kStateSlots));
      --d.stateCount;
      const uint16_t v = d.statePending[idx];
      d.statePending[idx] = 0;
      push(d, Item{Kind::Essential, stateStatus(idx), static_cast<uint8_t>((v >> 7) & 0x7F),
                   static_cast<uint8_t>(v & 0x7F), kNoNote, now});
    }
    while (d.count < Ordered && d.stateCount == 0 && d.paramCount > 0) {
      const Param& p = d.params[d.paramHead];
      push(d, Item{Kind::Parameter, p.status, p.data1, p.data2, kNoNote, now});
      d.paramHead = static_cast<uint16_t>((d.paramHead + 1) % kParameterOverflow);
      --d.paramCount;
    }
  }

  static bool orderedReady(PortData& d) {
    while (d.count > 0 && d.ring[d.head].kind == Kind::Tombstone) {
      d.head = static_cast<uint16_t>((d.head + 1) % Ordered);
      --d.count;
    }
    return d.count > 0;
  }

  bool next(PortData& d, uint8_t p, Sent& s) {
    int off = -1;
    for (uint16_t i = 0; i < Notes; ++i)
      if (stateOf(notes_[i], p) == State::OffPending && (off < 0 || notes_[i].dueMicros < notes_[off].dueMicros)) off = i;
    if (off >= 0) {
      Note& n = notes_[off];
      const uint32_t start = d.txFreeAt > n.dueMicros ? d.txFreeAt : n.dueMicros;
      account(start - n.dueMicros, d.counters.maxNoteOffLateMicros, d.counters.sumNoteOffLateMicros);
      s = Sent{start, n.key, static_cast<uint8_t>(0x80 | (n.status & 0x0F)), n.pitch, 0, OutClass::NoteOff};
      setState(n, p, State::None);  // the entry frees itself when no port holds it
      return sendAt(d, start, s, false);
    }
    bool ordered = orderedReady(d);
    const bool cont = d.dirtyCount > 0;
    if (cont && (!ordered || d.sinceContinuous >= 3)) return sendContinuous(d, s);
    // Expire queued NoteOns at the head whose scheduled end has been reached by the time they could start.
    while (ordered && d.ring[d.head].kind == Kind::NoteOn) {
      const Item& h = d.ring[d.head];
      const uint32_t start = d.txFreeAt > h.queuedMicros ? d.txFreeAt : h.queuedMicros;
      Note& n = notes_[h.note];
      if (n.endMicros == kNoEndMicros || start < n.endMicros) break;
      setState(n, p, State::None);
      --d.noteOnsQueued;
      ++d.counters.expiredBeforeTransmit;
      d.head = static_cast<uint16_t>((d.head + 1) % Ordered);
      --d.count;
      ordered = orderedReady(d);
    }
    if (!ordered) return cont ? sendContinuous(d, s) : false;
    Item& it = d.ring[d.head];
    const uint32_t start = d.txFreeAt > it.queuedMicros ? d.txFreeAt : it.queuedMicros;
    if (it.kind == Kind::NoteOn) {
      Note& n = notes_[it.note];
      setState(n, p, State::Playing);
      --d.noteOnsQueued;
      account(start - it.queuedMicros, d.counters.maxNoteOnLateMicros, d.counters.sumNoteOnLateMicros);
      s = Sent{start, n.key, it.status, it.data1, it.data2, OutClass::NoteOn};
    } else {
      s = Sent{start, 0, it.status, it.data1, it.data2,
               it.kind == Kind::Essential ? OutClass::Essential : OutClass::Parameter};
    }
    d.head = static_cast<uint16_t>((d.head + 1) % Ordered);
    --d.count;
    return sendAt(d, start, s, cont);
  }

  static bool sendContinuous(PortData& d, Sent& s) {
    while (d.dirtyCount > 0) {
      const uint16_t l = d.dirty[d.dirtyHead];
      d.dirtyHead = static_cast<uint16_t>((d.dirtyHead + 1) % Lanes);
      --d.dirtyCount;
      Lane& lane = d.lanes[l];
      if (!(lane.pending & kPendingBit)) continue;
      const uint16_t value = lane.pending & 0x3FFF;
      lane.pending = 0;
      lane.lastSent = value;
      const uint8_t ch = static_cast<uint8_t>(lane.key / 258);
      const uint16_t slot = lane.key % 258;
      if (slot < 128)
        s = Sent{d.txFreeAt, 0, static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(slot),
                 static_cast<uint8_t>(value), OutClass::Continuous};
      else if (slot == 128)
        s = Sent{d.txFreeAt, 0, static_cast<uint8_t>(0xE0 | ch), static_cast<uint8_t>(value & 0x7F),
                 static_cast<uint8_t>(value >> 7), OutClass::Continuous};
      else if (slot == 129)
        s = Sent{d.txFreeAt, 0, static_cast<uint8_t>(0xD0 | ch), static_cast<uint8_t>(value), 0, OutClass::Continuous};
      else
        s = Sent{d.txFreeAt, 0, static_cast<uint8_t>(0xA0 | ch), static_cast<uint8_t>(slot - 130),
                 static_cast<uint8_t>(value), OutClass::Continuous};
      d.sinceContinuous = 0;
      return sendAt(d, d.txFreeAt, s, false);
    }
    return false;
  }

  static bool sendAt(PortData& d, uint32_t start, const Sent& s, bool continuousWaiting) {
    d.txFreeAt = start + d.messageMicros;
    ++d.counters.sent[static_cast<uint8_t>(s.cls)];
    if (continuousWaiting && d.sinceContinuous < 0xFF) ++d.sinceContinuous;
    return true;
  }

  static void account(uint32_t late, uint32_t& max, uint64_t& sum) {
    if (late > max) max = late;
    sum += late;
  }

  Note notes_[Notes];
  PortData ports_[Ports];
};

}  // namespace ContentEngine
