//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Span playback window and its player (fixed-memory-engine E3, CE-B1).
//
// A window holds, in tick order:
//   * START entries: a NoteOn admitted by the composer's OUT-POLY simulation (with its length inline when its end falls
//     in the window, else open), or a non-note event;
//   * TERMINAL entries: the end of an admitted note that was sounding at the window start.
// plus a carry list: the admitted notes sounding at the window start (<= T), so a player switching source here can
// reconcile its actual sounding set. Bound (CE-B1): count <= START rows + T.
//
// The player feeds the track's TerminalScheduler, which is authoritative for what actually sounds: live occupancy,
// output suppression and revision switches can make it differ from the composer's simulation. Disagreement is safe by
// construction: a TERMINAL or carry entry for an instance that is not sounding is ignored, and an instance the new
// source does not carry is closed at the switch. No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/TerminalScheduler.h"

namespace ContentEngine {

constexpr uint16_t kOpenLength = 0xFFFFu;

/// 16 bytes. NoteOn START: `length` inline or kOpenLength. TERMINAL: status 0x8n, `length` unused. Event: any other
/// status. `runKey` = run descriptor and pass order (makeRunKey).
struct SpanEntry {
  uint32_t tick;
  uint32_t noteId;
  uint16_t length;
  uint16_t runKey;
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
  uint8_t terminal;  // 1 for TERMINAL entries
};
static_assert(sizeof(SpanEntry) == 16, "a span entry is 16 bytes");

struct CarryRef {
  uint32_t noteId;
  uint16_t runKey;
};

template <uint16_t Capacity>
struct SpanWindow {
  uint32_t identity;   // the source the window was composed from
  uint32_t start, end;  // positions in the loop
  uint32_t cycleBase;   // timeline tick of this cycle's position 0 (entry ticks are positions)
  uint16_t count;
  uint16_t startCount, terminalCount, refusedCount;
  uint8_t carryCount;
  CarryRef carry[Config::kMaxSoundingPerTrack];
  SpanEntry entries[Capacity];

  void reset(uint32_t id, uint32_t s, uint32_t e) {
    identity = id;
    start = s;
    end = e;
    cycleBase = 0;
    count = startCount = terminalCount = refusedCount = 0;
    carryCount = 0;
  }
};

template <uint16_t T>
class WindowPlayer {
 public:
  using Scheduler = TerminalScheduler<T>;
  struct Emit {
    uint32_t tick;
    uint8_t cls;  // 0 NoteOff, 1 NoteOn, 2 other
    uint32_t noteId;
    uint16_t runKey;
    uint8_t channel;  // 1..16
    uint8_t pitch;
  };

  void init(Scheduler& s) {
    sched_ = &s;
    identity_ = 0;
    base_ = 0;
    entries_ = nullptr;
    count_ = 0;
    next_ = 0;
  }

  /// Starts playing `w`. A different source identity is a transition: instances `w` does not carry close now.
  template <uint16_t Cap>
  uint8_t beginWindow(const SpanWindow<Cap>& w, Emit* out, uint8_t max) {
    uint8_t n = 0;
    if (identity_ != 0 && w.identity != identity_) {
      typename Scheduler::Carry carry[Config::kMaxSoundingPerTrack];
      for (uint8_t i = 0; i < w.carryCount; ++i)
        carry[i] = {w.carry[i].noteId, runIdOf(w.carry[i].runKey), kOpenEnd};
      typename Scheduler::Out offs[T];
      const uint8_t closed =
          sched_->transition(w.cycleBase + w.start, carry, w.carryCount, offs, static_cast<uint8_t>(T));
      for (uint8_t i = 0; i < closed && n < max; ++i) out[n++] = fromOut(offs[i]);
    }
    identity_ = w.identity;
    base_ = w.cycleBase;
    entries_ = w.entries;
    count_ = w.count;
    next_ = 0;
    return n;
  }

  /// One tick (timeline ticks): composed ends due now, NoteOffs, then this tick's STARTs (DEC-244: NoteOff before
  /// NoteOn). Entry positions are offset by the window's cycle base.
  uint8_t tick(uint32_t t, Emit* out, uint8_t max) {
    uint8_t n = 0;
    // A TERMINAL for `pos - 1` (an OVL-1 shorten or a Q-LEN-1 re-strike decided at `pos`) is appended after entries a
    // lower pass already wrote at `pos`, so entries are tick-ordered except that a TERMINAL may sit one tick late: look
    // one tick ahead for TERMINALs due exactly now.
    for (uint16_t i = next_; i < count_ && base_ + entries_[i].tick <= t + 1; ++i)
      if (entries_[i].terminal && base_ + entries_[i].tick == t)
        sched_->composedEnd(entries_[i].noteId, runIdOf(entries_[i].runKey), t);
    typename Scheduler::Out offs[T];
    const uint8_t d = sched_->due(t, offs, static_cast<uint8_t>(T));
    for (uint8_t i = 0; i < d && n < max; ++i) out[n++] = fromOut(offs[i]);
    while (next_ < count_ && base_ + entries_[next_].tick <= t) {
      const SpanEntry& e = entries_[next_++];
      if (e.terminal) continue;
      const uint8_t type = e.status & 0xF0;
      if (type == 0x90 && e.data2 != 0) {
        const uint32_t end = e.length == kOpenLength ? kOpenEnd : base_ + e.tick + e.length;
        if (sched_->start(t, e.noteId, runIdOf(e.runKey), orderOf(e.runKey), static_cast<uint8_t>((e.status & 0x0F) + 1),
                          e.data1, end) == StartResult::Started &&
            n < max)
          out[n++] = Emit{t, 1, e.noteId, e.runKey, static_cast<uint8_t>((e.status & 0x0F) + 1), e.data1};
      } else if (n < max) {
        out[n++] = Emit{t, 2, e.noteId, e.runKey, static_cast<uint8_t>((e.status & 0x0F) + 1), e.data1};
      }
    }
    return n;
  }

  /// The live writer's NoteOff for one of its notes that may be sounding here; it goes out at the next `tick`.
  void liveNoteOff(uint32_t noteId, uint16_t runId, uint32_t now) { sched_->liveNoteOff(noteId, runId, now); }

 private:
  static Emit fromOut(const typename Scheduler::Out& o) {
    return Emit{o.tick, 0, o.noteId, o.runKey, o.channel, o.pitch};
  }

  Scheduler* sched_;
  uint32_t identity_;
  uint32_t base_;
  const SpanEntry* entries_;
  uint16_t count_;
  uint16_t next_;
};

}  // namespace ContentEngine
