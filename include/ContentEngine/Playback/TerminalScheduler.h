//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Fixed terminal scheduler of one playback track.
//
// Playback windows carry START entries (a NoteOn with its end inline, or an open end), TERMINAL entries and a carry
// list (instances sounding across a window start); NoteOffs are not rows. This scheduler holds every *sounding
// instance* the track actually sent, at most T, and owns its NoteOff:
//   * composed close: an inline end or a TERMINAL entry sets the end;
//   * live close: the writer's NoteOff for a still-held note whose window was composed before that NoteOff existed;
//   * live occupy: a live NoteOn on the pitch ends every sounding instance on it now (any channel, DEC-033), and
//     composed NoteOns on an occupied pitch are suppressed until every live note on it is released (a count, not a
//     bit);
//   * revision transition: instances the new source does not carry are closed now; carried entries for notes never sent
//     are ignored.
// Whichever close comes first sends the NoteOff; later terminals for that instance are suppressed. Nothing is sent into
// the past: an end already behind the current tick fires at the current tick (counted). A started instance always gets
// its NoteOff; a new instance is refused by name when T are sounding (OUT-POLY, T = 16 per track:
// `Config::kMaxSoundingPerTrack`).
//
// Instance identity is (noteId, run): an edit run's replacement row keeps the hidden note's id, so the id alone does
// not tell an old instance from the replacement across a revision switch. `runKey` packs the run descriptor (bits
// 6..15) and the run's pass order in its manifest (bits 0..5, for equal-tick ties, DEC-244). O(T) per call; no
// allocation.

#pragma once

#include <cstdint>

namespace ContentEngine {

constexpr uint32_t kOpenEnd = 0xFFFFFFFFu;

inline uint16_t makeRunKey(uint16_t runId, uint8_t order) {
  return static_cast<uint16_t>((runId << 6) | (order & 0x3Fu));
}
inline uint16_t runIdOf(uint16_t runKey) { return static_cast<uint16_t>(runKey >> 6); }
inline uint8_t orderOf(uint16_t runKey) { return static_cast<uint8_t>(runKey & 0x3Fu); }

enum class StartResult : uint8_t { Started, Duplicate, RefusedFull, SuppressedOccupied };
enum class CloseResult : uint8_t { Closed, EndSet, NotSounding };

template <uint16_t T>
class TerminalScheduler {
 public:
  struct Out {
    uint32_t tick;
    uint32_t noteId;
    uint16_t runKey;
    uint8_t channel;
    uint8_t pitch;
  };
  struct Carry {
    uint32_t noteId;
    uint16_t runId;
    uint32_t end;  // kOpenEnd when the new source supplies it later
  };
  struct Counters {
    uint32_t suppressedTerminals;  // a terminal for an instance that is not sounding (already closed, never sent)
    uint32_t lateTerminals;        // an end learned after its tick: fired at the current tick
    uint32_t suppressedOccupied;   // composed NoteOn on a live-occupied pitch
    uint32_t refusedFull;          // composed NoteOn with T instances sounding (OUT-POLY)
    uint32_t duplicates;           // START for an instance already sounding
    uint32_t transitionClosures;   // instances closed because the new source does not carry them
  };

  void init() {
    count_ = 0;
    for (auto& s : set_) s = Instance{};
    for (auto& o : occupied_) o = 0;
    counters_ = Counters{};
  }

  StartResult start(uint32_t tick, uint32_t noteId, uint16_t runId, uint8_t order, uint8_t channel, uint8_t pitch,
                    uint32_t end) {
    (void)tick;
    if (occupied_[pitch & 0x7F] > 0) {
      ++counters_.suppressedOccupied;
      return StartResult::SuppressedOccupied;
    }
    if (find(noteId, runId) >= 0) {
      ++counters_.duplicates;
      return StartResult::Duplicate;
    }
    if (count_ >= T) {
      ++counters_.refusedFull;
      return StartResult::RefusedFull;
    }
    set_[count_++] = Instance{noteId, end, makeRunKey(runId, order), channel, pitch};
    return StartResult::Started;
  }

  /// Composed end (TERMINAL entry) for a sounding instance.
  CloseResult composedEnd(uint32_t noteId, uint16_t runId, uint32_t end) {
    const int i = find(noteId, runId);
    if (i < 0) {
      ++counters_.suppressedTerminals;
      return CloseResult::NotSounding;
    }
    set_[i].end = end;
    return CloseResult::EndSet;
  }

  /// The live writer's NoteOff for a note that may be sounding as a playback instance: it ends now.
  CloseResult liveNoteOff(uint32_t noteId, uint16_t runId, uint32_t now) {
    const int i = find(noteId, runId);
    if (i < 0) {
      ++counters_.suppressedTerminals;
      return CloseResult::NotSounding;
    }
    if (set_[i].end == kOpenEnd || set_[i].end > now) set_[i].end = now;
    return CloseResult::Closed;
  }

  /// Sends every NoteOff due at `tick` (ends at or before it), in equal-tick order (pass, then id; DEC-244). Returns
  /// the count. With `max` below the due count (port backpressure) the rest stay owned and go out on later calls.
  uint8_t due(uint32_t tick, Out* out, uint8_t max) {
    uint8_t n = 0;
    while (n < max) {
      int best = -1;
      for (uint16_t i = 0; i < count_; ++i) {
        if (set_[i].end == kOpenEnd || set_[i].end > tick) continue;
        if (best < 0 || before(set_[i], set_[best])) best = i;
      }
      if (best < 0) break;
      if (set_[best].end < tick) ++counters_.lateTerminals;
      out[n++] = emit(static_cast<uint16_t>(best), tick);
    }
    return n;
  }

  /// A live NoteOn on `pitch`: every sounding instance on it ends now (any channel); the pitch stays occupied until
  /// `release`. Returns the NoteOffs sent; the caller sends the live NoteOn after them.
  uint8_t occupy(uint8_t pitch, uint32_t now, Out* out, uint8_t max) {
    uint8_t n = 0;
    for (uint16_t i = 0; i < count_ && n < max;) {
      if (set_[i].pitch == pitch)
        out[n++] = emit(i, now);  // emit compacts the set: re-check the same index
      else
        ++i;
    }
    if (occupied_[pitch & 0x7F] < 0xFF) ++occupied_[pitch & 0x7F];
    order(out, n);
    return n;
  }

  void release(uint8_t pitch) {
    if (occupied_[pitch & 0x7F] > 0) --occupied_[pitch & 0x7F];
  }

  /// Switch to a new source at `tick`: keep instances it carries (taking its end), close the others now.
  uint8_t transition(uint32_t tick, const Carry* carry, uint16_t carryCount, Out* out, uint8_t max) {
    uint8_t n = 0;
    for (uint16_t i = 0; i < count_ && n < max;) {
      int k = -1;
      for (uint16_t c = 0; c < carryCount; ++c)
        if (carry[c].noteId == set_[i].noteId && carry[c].runId == runIdOf(set_[i].runKey)) k = c;
      if (k < 0) {
        ++counters_.transitionClosures;
        out[n++] = emit(i, tick);
      } else {
        set_[i].end = carry[k].end;
        ++i;
      }
    }
    order(out, n);
    return n;
  }

  /// The run of the sounding instance of `noteId` on `pitch`, whatever its run (-1: none) — a NOTE_EDIT join keeps a
  /// note the edited source still sounds there on that instance.
  int32_t soundingRun(uint32_t noteId, uint8_t pitch) const {
    for (uint16_t i = 0; i < count_; ++i)
      if (set_[i].noteId == noteId && set_[i].pitch == pitch) return runIdOf(set_[i].runKey);
    return -1;
  }

  uint16_t sounding() const { return count_; }
  const Counters& counters() const { return counters_; }

 private:
  struct Instance {
    uint32_t noteId = 0;
    uint32_t end = kOpenEnd;
    uint16_t runKey = 0;
    uint8_t channel = 0;
    uint8_t pitch = 0;
  };
  static_assert(sizeof(Instance) == 12, "a sounding instance is 12 bytes (owner budget: 8 tracks x 16 x 12 B)");

  static bool before(const Instance& a, const Instance& b) {
    if (orderOf(a.runKey) != orderOf(b.runKey)) return orderOf(a.runKey) < orderOf(b.runKey);
    return a.noteId < b.noteId;
  }

  /// The equal-tick order among NoteOffs sent at one tick: pass, then id. Insertion sort over at most `max` entries.
  static void order(Out* out, uint8_t n) {
    for (uint8_t i = 1; i < n; ++i) {
      const Out x = out[i];
      uint8_t j = i;
      while (j > 0 && (orderOf(out[j - 1].runKey) > orderOf(x.runKey) ||
                       (orderOf(out[j - 1].runKey) == orderOf(x.runKey) && out[j - 1].noteId > x.noteId))) {
        out[j] = out[j - 1];
        --j;
      }
      out[j] = x;
    }
  }

  int find(uint32_t noteId, uint16_t runId) const {
    for (uint16_t i = 0; i < count_; ++i)
      if (set_[i].noteId == noteId && runIdOf(set_[i].runKey) == runId) return i;
    return -1;
  }

  Out emit(uint16_t i, uint32_t tick) {
    const Out o{tick, set_[i].noteId, set_[i].runKey, set_[i].channel, set_[i].pitch};
    set_[i] = set_[--count_];
    set_[count_] = Instance{};
    return o;
  }

  Instance set_[T];
  uint16_t count_;
  uint8_t occupied_[128];
  Counters counters_;
};

}  // namespace ContentEngine
