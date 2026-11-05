//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The Clock path's last step (fixed-memory-engine E9.3, plan § 9.2): playback events of every track into the output
// hub, on the track's ports.
//
// * Keys: the hub identifies a logical note by one 32-bit key; a track's sounding instance is (noteId, run). Each track
//   has `PerTrack` key slots (its sounding bound T); a NoteOn takes a free slot and a new generation, its NoteOff frees
//   it. Key = generation (16 bits) | track (3) | slot (4): unique among notes the hub still holds, so a re-strike of the
//   same instance (NoteOff and NoteOn on one tick, Q-LEN-1, or an edit replacement keeping its id across a transition)
//   is a new note in the hub, never swallowed by the old one's pending NoteOff.
// * NoteOn: to each port in the track's mask, with its scheduled end in microseconds (expired-note cancellation, C.7);
//   an open end is none. NoteOff: to every port (a port that never took the NoteOn ignores it), so a routing change
//   cannot strand a note. Other events: to the track's ports, classified by the hub.
// * A NoteOn with no free key slot (more than T sounding — the terminal scheduler already bounds that) is refused and
//   counted; a NoteOff for an instance with no key (its NoteOn was refused) is counted and dropped.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/TerminalScheduler.h"

namespace ContentEngine {

template <typename Hub, typename Emit, uint8_t Tracks, uint8_t PerTrack, uint8_t Ports>
class ClockOutput {
  static_assert(Tracks <= 8 && PerTrack <= 16, "track (3 bits) and slot (4 bits) pack under the generation");

 public:
  static constexpr uint32_t kNoKey = 0xFFFFFFFFu;
  static constexpr uint8_t kAllPorts = static_cast<uint8_t>((1u << Ports) - 1u);

  struct Stats {
    uint32_t refusedNoKey;  // NoteOns with no free key slot
    uint32_t unmatchedOff;  // NoteOffs for an instance holding no key
  };

  void init(Hub& hub) {
    hub_ = &hub;
    for (auto& t : keys_)
      for (auto& k : t) k = Key{0, kFree, 0};
    for (auto& m : mask_) m = kAllPorts;
    stats_ = Stats{};
  }

  /// The ports track `t` plays to (bit p: port p).
  void setPorts(uint8_t t, uint8_t mask) { mask_[t] = static_cast<uint8_t>(mask & kAllPorts); }
  uint8_t ports(uint8_t t) const { return mask_[t]; }

  /// `n` events at `nowUs` (Clock ticks of `usPerTick` µs).
  void route(const Emit* e, uint8_t n, uint32_t nowUs, uint32_t usPerTick) {
    for (uint8_t i = 0; i < n; ++i) {
      const Emit& x = e[i];
      const uint8_t mask = mask_[x.track];
      if (x.cls == 1) {
        const uint32_t key = open(x.track, x.noteId, runIdOf(x.runKey));
        if (key == kNoKey) {
          ++stats_.refusedNoKey;
          continue;
        }
        const uint32_t end = x.endTick == kOpenEnd ? kNoEnd : nowUs + (x.endTick - x.tick) * usPerTick;
        for (uint8_t p = 0; p < Ports; ++p)
          if (mask & (1u << p)) hub_->noteOn(nowUs, p, x.status, x.pitch, x.data2, key, end);
      } else if (x.cls == 0) {
        const uint32_t key = close(x.track, x.noteId, runIdOf(x.runKey));
        if (key == kNoKey) {
          ++stats_.unmatchedOff;
          continue;
        }
        for (uint8_t p = 0; p < Ports; ++p) hub_->noteOff(nowUs, p, key);
      } else {
        for (uint8_t p = 0; p < Ports; ++p)
          if (mask & (1u << p)) hub_->control(nowUs, p, x.status, x.pitch, x.data2);
      }
    }
  }

  /// Key slots in use on track `t` (instances whose NoteOff has not been routed).
  uint8_t open(uint8_t t) const {
    uint8_t c = 0;
    for (const Key& k : keys_[t]) c += k.runId != kFree ? 1 : 0;
    return c;
  }
  const Stats& stats() const { return stats_; }

 private:
  static constexpr uint16_t kFree = 0xFFFFu;
  static constexpr uint32_t kNoEnd = 0xFFFFFFFFu;  // the hub's kNoEndMicros
  struct Key {
    uint32_t noteId;
    uint16_t runId;  // kFree: slot free
    uint16_t generation;
  };

  static uint32_t keyOf(uint8_t t, uint8_t slot, uint16_t generation) {
    return (static_cast<uint32_t>(generation) << 7) | (static_cast<uint32_t>(t) << 4) | slot;
  }
  uint32_t open(uint8_t t, uint32_t noteId, uint16_t runId) {
    for (uint8_t s = 0; s < PerTrack; ++s) {
      Key& k = keys_[t][s];
      if (k.runId != kFree) continue;
      k = Key{noteId, runId, static_cast<uint16_t>(k.generation + 1)};
      return keyOf(t, s, k.generation);
    }
    return kNoKey;
  }
  uint32_t close(uint8_t t, uint32_t noteId, uint16_t runId) {
    for (uint8_t s = 0; s < PerTrack; ++s) {
      Key& k = keys_[t][s];
      if (k.runId != runId || k.noteId != noteId) continue;
      k.runId = kFree;
      return keyOf(t, s, k.generation);
    }
    return kNoKey;
  }

  Hub* hub_ = nullptr;
  Key keys_[Tracks][PerTrack];
  uint8_t mask_[Tracks];
  Stats stats_{};
};

}  // namespace ContentEngine
