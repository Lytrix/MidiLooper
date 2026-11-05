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
// * E9.4: mute / solo gate a track (`setAudible`): going silent ends what it sounds on every port at once and frees its
//   keys; while silent its NoteOns are dropped (counted) and their NoteOffs find no key. Live MIDI thru (`thruNoteOn` /
//   `thruNoteOff` / `thruEvent`) plays the input on a track's ports; its notes hold `kThruSlots` keys of their own with
//   generations (top bit set: never a composed key), so a fast repeated note is a new note, not swallowed by the
//   previous one's pending NoteOff. Thru is not gated by mute: the musician hears what they play.
// * Output channel (E9.7b, the legacy playback rule `remappedPlaybackChannel`): a track with a MIDI channel set sends
//   every channel message (notes, controllers, pitch bend, program change, aftertouch) on that channel, whatever channel
//   it was recorded on; `kAsRecorded` keeps the recorded channel. Thru follows the same rule.
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
  static constexpr uint8_t kThruSlots = 16;
  static constexpr uint8_t kAsRecorded = 0;  // `setChannel`: keep each message's recorded channel

  struct Stats {
    uint32_t refusedNoKey;  // NoteOns with no free key slot
    uint32_t unmatchedOff;  // NoteOffs for an instance holding no key
    uint32_t silenced;      // NoteOns dropped on a muted track
    uint32_t thruRefused;   // live NoteOns beyond kThruSlots held at once
  };

  void init(Hub& hub) {
    hub_ = &hub;
    for (auto& t : keys_)
      for (auto& k : t) k = Key{0, kFree, 0};
    for (auto& m : mask_) m = kAllPorts;
    for (auto& a : audible_) a = true;
    for (auto& c : channel_) c = kAsRecorded;
    for (auto& x : thru_) x = Thru{0, 0, false, 0};
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
        if (!audible_[x.track]) {
          ++stats_.silenced;
          continue;
        }
        const uint32_t key = open(x.track, x.noteId, runIdOf(x.runKey));
        if (key == kNoKey) {
          ++stats_.refusedNoKey;
          continue;
        }
        const uint32_t end = x.endTick == kOpenEnd ? kNoEnd : nowUs + (x.endTick - x.tick) * usPerTick;
        const uint8_t status = remap(x.track, x.status);
        for (uint8_t p = 0; p < Ports; ++p)
          if (mask & (1u << p)) hub_->noteOn(nowUs, p, status, x.pitch, x.data2, key, end);
      } else if (x.cls == 0) {
        const uint32_t key = close(x.track, x.noteId, runIdOf(x.runKey));
        if (key == kNoKey) {
          ++stats_.unmatchedOff;
          continue;
        }
        for (uint8_t p = 0; p < Ports; ++p) hub_->noteOff(nowUs, p, key);
      } else if (audible_[x.track]) {
        const uint8_t status = remap(x.track, x.status);
        for (uint8_t p = 0; p < Ports; ++p)
          if (mask & (1u << p)) hub_->control(nowUs, p, status, x.pitch, x.data2);
      }
    }
  }

  /// The track's MIDI channel, 1..16 (kAsRecorded: as recorded).
  void setChannel(uint8_t t, uint8_t channel) { channel_[t] = channel <= 16 ? channel : kAsRecorded; }
  uint8_t channel(uint8_t t) const { return channel_[t]; }

  /// Mute / solo: a track going silent ends what it sounds now, on every port.
  void setAudible(uint8_t t, bool audible, uint32_t nowUs) {
    if (audible_[t] == audible) return;
    audible_[t] = audible;
    if (audible) return;
    for (uint8_t s = 0; s < PerTrack; ++s) {
      Key& k = keys_[t][s];
      if (k.runId == kFree) continue;
      k.runId = kFree;
      for (uint8_t p = 0; p < Ports; ++p) hub_->noteOff(nowUs, p, keyOf(t, s, k.generation));
    }
  }
  bool audible(uint8_t t) const { return audible_[t]; }

  /// Live input played through on track `t`'s ports (the musician hears what they play). A NoteOn on a pitch already
  /// held on that channel ends the held one first.
  void thruNoteOn(uint8_t t, uint8_t status, uint8_t pitch, uint8_t velocity, uint32_t nowUs) {
    const uint16_t note = thruNote(status, pitch);
    thruNoteOff(t, status, pitch, nowUs);
    for (uint8_t s = 0; s < kThruSlots; ++s) {
      Thru& x = thru_[s];
      if (x.used) continue;
      x = Thru{note, t, true, static_cast<uint16_t>(x.generation + 1)};
      const uint8_t out = remap(t, status);
      for (uint8_t p = 0; p < Ports; ++p)
        if (mask_[t] & (1u << p)) hub_->noteOn(nowUs, p, out, pitch, velocity, thruKey(s), kNoEnd);
      return;
    }
    ++stats_.thruRefused;
  }
  void thruNoteOff(uint8_t t, uint8_t status, uint8_t pitch, uint32_t nowUs) {
    const uint16_t note = thruNote(status, pitch);
    for (uint8_t s = 0; s < kThruSlots; ++s) {
      Thru& x = thru_[s];
      if (!x.used || x.track != t || x.note != note) continue;
      x.used = false;
      for (uint8_t p = 0; p < Ports; ++p) hub_->noteOff(nowUs, p, thruKey(s));
      return;
    }
  }
  void thruEvent(uint8_t t, uint8_t status, uint8_t d1, uint8_t d2, uint32_t nowUs) {
    const uint8_t out = remap(t, status);
    for (uint8_t p = 0; p < Ports; ++p)
      if (mask_[t] & (1u << p)) hub_->control(nowUs, p, out, d1, d2);
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

  /// Channel messages (0x80..0xEF) take the track's channel when it has one.
  uint8_t remap(uint8_t t, uint8_t status) const {
    if (channel_[t] == kAsRecorded || status < 0x80 || status >= 0xF0) return status;
    return static_cast<uint8_t>((status & 0xF0) | ((channel_[t] - 1) & 0x0F));
  }
  struct Thru {
    uint16_t note;  // channel << 7 | pitch
    uint8_t track;
    bool used;
    uint16_t generation;
  };
  static uint16_t thruNote(uint8_t status, uint8_t pitch) {
    return static_cast<uint16_t>(((status & 0x0F) << 7) | (pitch & 0x7F));
  }
  uint32_t thruKey(uint8_t s) const { return 0x80000000u | (static_cast<uint32_t>(thru_[s].generation) << 4) | s; }
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
  bool audible_[Tracks];
  uint8_t channel_[Tracks];
  Thru thru_[kThruSlots];
  Stats stats_{};
};

}  // namespace ContentEngine
