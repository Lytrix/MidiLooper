//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Fixed terminal scheduler of one playback track (fixed-memory-engine CE-B1, DEC-148, DEC-244).
//
// Playback windows carry START entries (a NoteOn with its end inline, or an open end) and TERMINAL / carry entries for
// notes sounding across a window start; NoteOffs are not rows. This scheduler holds every *sounding instance* the track
// actually sent, at most T, and owns its NoteOff:
//   * composed close: an inline end or a TERMINAL / carry entry sets the end;
//   * live close: the writer's NoteOff for a still-held note whose window was composed before that NoteOff existed;
//   * live occupy: a live NoteOn on the pitch ends every sounding instance on it now (DEC-033: any channel), and
//     composed NoteOns on an occupied pitch are suppressed until every live note on it is released (a count, not a bit);
//   * revision transition: instances the new source does not carry are closed now; carried entries for notes never sent
//     are ignored.
// Whichever close comes first sends the NoteOff; later terminals for that instance are suppressed. Nothing is sent into
// the past: an end already behind the current tick fires at the current tick (counted). A started instance always gets
// its NoteOff; a new instance is refused by name when T are sounding. O(T) per call; no allocation.

#pragma once

#include <cstdint>

namespace ContentEngine {

constexpr uint32_t kOpenEnd = 0xFFFFFFFFu;

enum class StartResult : uint8_t { Started, Duplicate, RefusedFull, SuppressedOccupied };
enum class CloseResult : uint8_t { Closed, EndSet, NotSounding };

template <uint16_t T>
class TerminalScheduler {
 public:
  struct Out {
    uint32_t tick;
    uint32_t noteId;
    uint8_t channel;
    uint8_t pitch;
    uint8_t run;
  };
  struct Carry {
    uint32_t noteId;
    uint32_t end;  // kOpenEnd when still unknown
  };
  struct Counters {
    uint32_t suppressedTerminals;  // a terminal for an instance that is not sounding (already closed, never sent)
    uint32_t lateTerminals;        // an end learned after its tick: fired at the current tick
    uint32_t suppressedOccupied;   // composed NoteOn on a live-occupied pitch
    uint32_t refusedFull;          // composed NoteOn with T instances sounding (output-overload policy OUT-POLY)
    uint32_t duplicates;           // START for an id already sounding
    uint32_t transitionClosures;   // instances closed because the new source does not carry them
  };

  void init() {
    count_ = 0;
    for (auto& s : set_) s = Instance{};
    for (auto& o : occupied_) o = 0;
    counters_ = Counters{};
    lastTick_ = 0;
  }

  StartResult start(uint32_t tick, uint32_t noteId, uint8_t channel, uint8_t pitch, uint8_t run, uint32_t end,
                    uint32_t identity) {
    lastTick_ = tick;
    if (occupied_[pitch & 0x7F] > 0) {
      ++counters_.suppressedOccupied;
      return StartResult::SuppressedOccupied;
    }
    if (find(noteId) >= 0) {
      ++counters_.duplicates;
      return StartResult::Duplicate;
    }
    if (count_ >= T) {
      ++counters_.refusedFull;
      return StartResult::RefusedFull;
    }
    set_[count_++] = Instance{noteId, end, identity, channel, pitch, run};
    return StartResult::Started;
  }

  /// Composed end (TERMINAL / carry entry) for a sounding instance.
  CloseResult composedEnd(uint32_t noteId, uint32_t end, uint32_t identity) {
    const int i = find(noteId);
    if (i < 0) {
      ++counters_.suppressedTerminals;
      return CloseResult::NotSounding;
    }
    set_[i].end = end;
    set_[i].identity = identity;
    return CloseResult::EndSet;
  }

  /// The live writer's NoteOff for a note that may be sounding as a playback instance: it ends now.
  CloseResult liveNoteOff(uint32_t noteId, uint32_t now) {
    const int i = find(noteId);
    if (i < 0) {
      ++counters_.suppressedTerminals;
      return CloseResult::NotSounding;
    }
    if (set_[i].end == kOpenEnd || set_[i].end > now) set_[i].end = now;
    return CloseResult::Closed;
  }

  /// Sends every NoteOff due at `tick` (ends at or before it), in DEC-244 order (pass, then id). Returns the count.
  uint8_t due(uint32_t tick, Out* out, uint8_t max) {
    lastTick_ = tick;
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
    lastTick_ = now;
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
  uint8_t transition(uint32_t tick, const Carry* carry, uint16_t carryCount, uint32_t identity, Out* out, uint8_t max) {
    lastTick_ = tick;
    uint8_t n = 0;
    for (uint16_t i = 0; i < count_ && n < max;) {
      int k = -1;
      for (uint16_t c = 0; c < carryCount; ++c)
        if (carry[c].noteId == set_[i].noteId) k = c;
      if (k < 0) {
        ++counters_.transitionClosures;
        out[n++] = emit(i, tick);
      } else {
        set_[i].end = carry[k].end;
        set_[i].identity = identity;
        ++i;
      }
    }
    order(out, n);
    return n;
  }

  uint16_t sounding() const { return count_; }
  const Counters& counters() const { return counters_; }

 private:
  struct Instance {
    uint32_t noteId = 0;
    uint32_t end = kOpenEnd;
    uint32_t identity = 0;
    uint8_t channel = 0;
    uint8_t pitch = 0;
    uint8_t run = 0;
  };

  static bool before(const Instance& a, const Instance& b) {
    if (a.run != b.run) return a.run < b.run;
    return a.noteId < b.noteId;
  }

  /// DEC-244 among NoteOffs sent at one tick: pass, then id. Insertion sort over at most `max` entries.
  static void order(Out* out, uint8_t n) {
    for (uint8_t i = 1; i < n; ++i) {
      const Out x = out[i];
      uint8_t j = i;
      while (j > 0 && (out[j - 1].run > x.run || (out[j - 1].run == x.run && out[j - 1].noteId > x.noteId))) {
        out[j] = out[j - 1];
        --j;
      }
      out[j] = x;
    }
  }

  int find(uint32_t noteId) const {
    for (uint16_t i = 0; i < count_; ++i)
      if (set_[i].noteId == noteId) return i;
    return -1;
  }

  Out emit(uint16_t i, uint32_t tick) {
    const Out o{tick, set_[i].noteId, set_[i].channel, set_[i].pitch, set_[i].run};
    set_[i] = set_[--count_];
    set_[count_] = Instance{};
    return o;
  }

  Instance set_[T];
  uint16_t count_;
  uint8_t occupied_[128];
  Counters counters_;
  uint32_t lastTick_;
};

}  // namespace ContentEngine
