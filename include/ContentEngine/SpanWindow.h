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

/// Plays one track's windows. Per tick: due NoteOffs first, then the window's rows in order (the composer already
/// limited continuous controllers to 32 per tick). A row is consumed only if what it emits fits the caller's buffer;
/// the rest stay for
/// the next tick (counted as spill), so nothing is consumed without being emitted; due NoteOffs that do not fit stay due
/// in the scheduler. `beginWindow`'s buffer must hold T (a transition may close every sounding instance).
template <uint16_t T>
class WindowPlayer {
 public:
  using Scheduler = TerminalScheduler<T>;
  struct Stats {
    uint32_t spills;  // ticks that left due rows for the next tick (the caller's buffer was full)
  };
  struct Emit {
    uint32_t tick;
    uint8_t cls;  // 0 NoteOff, 1 NoteOn, 2 other
    uint32_t noteId;
    uint16_t runKey;
    uint8_t channel;  // 1..16
    uint8_t pitch;    // data1 for other events
    // E9.3, what physical output needs:
    uint8_t status;    // the full status byte (type | channel - 1)
    uint8_t data2;     // velocity, controller value, ...
    uint8_t track;     // set by the transport (the player does not know its track)
    uint32_t endTick;  // a NoteOn's scheduled end (kOpenEnd: open); a NoteOff's own tick
  };

  void init(Scheduler& s) {
    sched_ = &s;
    identity_ = 0;
    base_ = 0;
    entries_ = nullptr;
    count_ = 0;
    next_ = 0;
    stats_ = Stats{};
  }
  const Stats& stats() const { return stats_; }
  /// Rows of the playing window not yet consumed (a spill at the window's end is reported by the caller).
  uint16_t pending() const { return static_cast<uint16_t>(count_ - next_); }

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

  /// Bounded late join (owner ruling 2026-11-05): `w` starts playing at tick `t`, inside its span. The join state was
  /// prepared by worker grains: `keep` are the instances the new source has sounding at `t` (with their ends: open for a
  /// carried one whose end comes later), `starts` the START entries begun before `t` and still sounding then, `events`
  /// the essential / parameter events of the missed span. Instances not kept close now; a kept instance already
  /// sounding continues with its new end; the others in `starts` begin now with their original end; play resumes at
  /// entry `from` (the first at or after `t`). Notes wholly in the missed span are not emitted. `out` must hold T plus
  /// the starts and events. The Clock work is bounded by T and the prepared lists.
  template <uint16_t Cap>
  uint8_t beginWindowLate(const SpanWindow<Cap>& w, uint16_t from, const typename Scheduler::Carry* keep, uint8_t nKeep,
                          const uint16_t* starts, uint8_t nStarts, const uint16_t* events, uint8_t nEvents, uint32_t t,
                          Emit* out, uint8_t max) {
    uint8_t n = 0;
    typename Scheduler::Out offs[T];
    const uint8_t closed = sched_->transition(t, keep, nKeep, offs, static_cast<uint8_t>(T));
    for (uint8_t i = 0; i < closed && n < max; ++i) out[n++] = fromOut(offs[i]);
    identity_ = w.identity;
    base_ = w.cycleBase;
    entries_ = w.entries;
    count_ = w.count;
    next_ = from;
    for (uint8_t k = 0; k < nStarts; ++k) {
      const SpanEntry& e = w.entries[starts[k]];
      const uint32_t end = e.length == kOpenLength ? kOpenEnd : base_ + e.tick + e.length;
      const uint8_t ch = static_cast<uint8_t>((e.status & 0x0F) + 1);
      if (sched_->start(t, e.noteId, runIdOf(e.runKey), orderOf(e.runKey), ch, e.data1, end) == StartResult::Started &&
          n < max)
        out[n++] = Emit{t, 1, e.noteId, e.runKey, ch, e.data1, static_cast<uint8_t>(0x90 | (e.status & 0x0F)), e.data2,
                        0, end};
    }
    for (uint8_t k = 0; k < nEvents && n < max; ++k) {
      const SpanEntry& e = w.entries[events[k]];
      out[n++] = Emit{t, 2, e.noteId, e.runKey, static_cast<uint8_t>((e.status & 0x0F) + 1), e.data1, e.status,
                      e.data2, 0, kOpenEnd};
    }
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
    const uint8_t room = static_cast<uint8_t>(max - n < T ? max - n : T);  // NoteOffs that do not fit stay due
    const uint8_t d = sched_->due(t, offs, room);
    for (uint8_t i = 0; i < d; ++i) out[n++] = fromOut(offs[i]);
    while (next_ < count_ && base_ + entries_[next_].tick <= t) {
      const SpanEntry& e = entries_[next_];
      if (e.terminal) {
        ++next_;
        continue;
      }
      if (n >= max) {  // nothing is consumed without being emitted: the rest waits for the next tick
        ++stats_.spills;
        break;
      }
      ++next_;
      const uint8_t type = e.status & 0xF0;
      if (type == 0x90 && e.data2 != 0) {
        const uint32_t end = e.length == kOpenLength ? kOpenEnd : base_ + e.tick + e.length;
        if (sched_->start(t, e.noteId, runIdOf(e.runKey), orderOf(e.runKey), static_cast<uint8_t>((e.status & 0x0F) + 1),
                          e.data1, end) == StartResult::Started &&
            n < max)
          out[n++] = Emit{t, 1, e.noteId, e.runKey, static_cast<uint8_t>((e.status & 0x0F) + 1), e.data1,
                          static_cast<uint8_t>(0x90 | (e.status & 0x0F)), e.data2, 0, end};
      } else if (n < max) {
        out[n++] = Emit{t, 2, e.noteId, e.runKey, static_cast<uint8_t>((e.status & 0x0F) + 1), e.data1, e.status,
                        e.data2, 0, kOpenEnd};
      }
    }
    return n;
  }

  /// Transport stop at timeline tick `t`: every sounding instance ends now (`out` must hold T); nothing plays after.
  uint8_t stopAll(uint32_t t, Emit* out, uint8_t max) {
    typename Scheduler::Out offs[T];
    const uint8_t closed = sched_->transition(t, nullptr, 0, offs, static_cast<uint8_t>(T));
    uint8_t n = 0;
    for (uint8_t i = 0; i < closed && n < max; ++i) out[n++] = fromOut(offs[i]);
    identity_ = 0;
    entries_ = nullptr;
    count_ = next_ = 0;
    return n;
  }

  /// Live input on this track (the writer accepted a NoteOn on `pitch`): every sounding instance on the pitch ends now
  /// (returned in `out`, sent before the live NoteOn) and composed NoteOns on it are suppressed until `liveRelease`.
  uint8_t liveOccupy(uint8_t pitch, uint32_t now, Emit* out, uint8_t max) {
    typename Scheduler::Out offs[T];
    const uint8_t closed = sched_->occupy(pitch, now, offs, static_cast<uint8_t>(T));
    uint8_t n = 0;
    for (uint8_t i = 0; i < closed && n < max; ++i) out[n++] = fromOut(offs[i]);
    return n;
  }
  /// The live note on `pitch` was released (the writer accepted its NoteOff).
  void liveRelease(uint8_t pitch) { sched_->release(pitch); }

  /// The live writer's NoteOff for one of its notes that may be sounding here; it goes out at the next `tick`.
  void liveNoteOff(uint32_t noteId, uint16_t runId, uint32_t now) { sched_->liveNoteOff(noteId, runId, now); }

 private:
  static Emit fromOut(const typename Scheduler::Out& o) {
    return Emit{o.tick, 0, o.noteId, o.runKey, o.channel, o.pitch,
                static_cast<uint8_t>(0x80 | ((o.channel - 1) & 0x0F)), 0, 0, o.tick};
  }

  Scheduler* sched_;
  Stats stats_;
  uint32_t identity_;
  uint32_t base_;
  const SpanEntry* entries_;
  uint16_t count_;
  uint16_t next_;
};

}  // namespace ContentEngine
