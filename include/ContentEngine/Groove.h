//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Groove timing (DEC-287, groove design `docs/Plans/groove_quantize_design.md` §§ 4, 6): built-in templates and where a
// note moves. Pure functions over ticks of the stored loop (DEC-283 frame); 768 ticks a bar, 48 a 16th.
//
// A template is a grid `g` and an offset per grid line (its swung place). A note at `t` goes toward the nearest
// *swung* target among the neighbouring grid lines — not the nearest straight line first (grid 48, 75 % swing: a note
// at 73 belongs to the swung 72, not to 96) — a tie to the earlier target; then `amount` % of the way there. Distances
// and results wrap around the stored loop.
//
// Classic swing (Roger Linn's rule, MPC60 / 3000, SP-1200): the second 16th (or 8th) of each pair is delayed by
// `(p − 50) / 50 × grid`.

#pragma once

#include <cstdint>

namespace ContentEngine {
namespace groove {

constexpr uint32_t kBar = 768, kSixteenth = 48;

/// Built-in templates (DEC-287; the card's library comes later).
enum Template : uint8_t {
  kStraight = 0,
  kMpc16_54, kMpc16_58, kMpc16_62, kMpc16_66, kMpc16_71,
  kMpc8_58, kMpc8_66,
  kSp16_54, kSp16_58, kSp16_63, kSp16_67, kSp16_71,
  kTriplet8, kTriplet16,
  kJazzRide, kBossa, kLaidBack, kPush,
  kTemplateCount
};

/// Where a groove applies (DEC-284's ranges).
enum Scope : uint8_t { kFull = 0, kRangeA = 1, kRangeB = 2 };

/// A groove setting, packed into the groove marker row's `seq` (0: no groove): template, amount 0–100, scope, AUTO.
struct Settings {
  uint8_t tmpl = kStraight;
  uint8_t amount = 0;  // percent; 0 = off
  uint8_t scope = kFull;
  bool autoGroove = false;
  uint32_t pack() const {
    return amount == 0 ? 0u
                       : static_cast<uint32_t>(tmpl) | (static_cast<uint32_t>(amount) << 8) |
                             (static_cast<uint32_t>(scope) << 16) | (autoGroove ? 1u << 18 : 0u);
  }
  static Settings unpack(uint32_t v) {
    Settings s;
    s.tmpl = static_cast<uint8_t>(v & 0xFF);
    s.amount = static_cast<uint8_t>((v >> 8) & 0xFF);
    s.scope = static_cast<uint8_t>((v >> 16) & 0x3);
    s.autoGroove = ((v >> 18) & 1u) != 0;
    if (s.tmpl >= kTemplateCount || s.amount > 100) s.amount = 0;
    return s;
  }
};

/// The template's grid in ticks.
inline uint32_t gridOf(uint8_t t) {
  switch (t) {
    case kMpc8_58: case kMpc8_66: case kJazzRide: return 2 * kSixteenth;  // 8ths
    case kTriplet8: return kBar / 12;    // 64
    case kTriplet16: return kBar / 24;   // 32
    default: return kSixteenth;
  }
}
/// The delay of a pair's second note at swing `p` % on grid `g`.
inline int32_t swing(uint32_t p, uint32_t g) { return static_cast<int32_t>((p - 50) * g / 50); }
/// The offset of grid line `line` (counted from the stored loop's start) from its straight place, in ticks.
inline int32_t offsetOf(uint8_t t, uint32_t line) {
  const bool second = (line & 1u) != 0;  // the second of a pair
  switch (t) {
    case kMpc16_54: case kSp16_54: return second ? swing(54, kSixteenth) : 0;
    case kMpc16_58: case kSp16_58: return second ? swing(58, kSixteenth) : 0;
    case kMpc16_62: return second ? swing(62, kSixteenth) : 0;
    case kSp16_63: return second ? swing(63, kSixteenth) : 0;
    case kMpc16_66: return second ? swing(66, kSixteenth) : 0;
    case kSp16_67: return second ? swing(67, kSixteenth) : 0;
    case kMpc16_71: case kSp16_71: return second ? swing(71, kSixteenth) : 0;
    case kMpc8_58: return second ? swing(58, 2 * kSixteenth) : 0;
    case kMpc8_66: return second ? swing(66, 2 * kSixteenth) : 0;
    case kJazzRide: {  // triplet-swung 8ths; beats 2 and 4 a little late
      const int32_t s = second ? swing(66, 2 * kSixteenth) : 0;
      const bool backbeat = !second && ((line / 2) % 2 == 1);
      return s + (backbeat ? 4 : 0);
    }
    case kBossa: return (line % 8 == 2 || line % 8 == 6) ? -6 : 0;  // the 3rd and 7th 16th pushed
    case kLaidBack: return 6;
    case kPush: return -6;
    default: return 0;  // straight, triplets
  }
}

/// `x` modulo `frame` for a signed `x`.
inline uint32_t wrap(int64_t x, uint32_t frame) {
  const int64_t m = x % static_cast<int64_t>(frame);
  return static_cast<uint32_t>(m < 0 ? m + frame : m);
}
/// The signed shortest way from `a` to `b` around a loop of `frame` ticks (a tie: forward).
inline int32_t towards(uint32_t a, uint32_t b, uint32_t frame) {
  int64_t d = static_cast<int64_t>(b) - static_cast<int64_t>(a);
  const int64_t half = frame / 2;
  if (d > half) d -= frame;
  if (d < -half) d += frame;
  return static_cast<int32_t>(d);
}

/// The swung target nearest to `t` (a tie: the earlier), in the stored loop of `frame` ticks.
inline uint32_t target(uint8_t t, uint32_t at, uint32_t frame) {
  const uint32_t g = gridOf(t);
  const int64_t base = at / g;
  uint32_t best = at;
  int32_t bestD = 0;
  bool found = false;
  for (int64_t line = base - 1; line <= base + 2; ++line) {
    const int64_t straight = line * static_cast<int64_t>(g);
    const uint32_t lineIndex = wrap(line, frame / g == 0 ? 1 : frame / g);
    const uint32_t cand = wrap(straight + offsetOf(t, lineIndex), frame);
    const int32_t d = towards(at, cand, frame);
    const int32_t ad = d < 0 ? -d : d, ab = bestD < 0 ? -bestD : bestD;
    if (!found || ad < ab || (ad == ab && d < bestD)) {
      best = cand;
      bestD = d;
      found = true;
    }
  }
  return best;
}

/// Where a note at stored position `at` plays under `s` (amount % of the way to its target), in the stored loop.
inline uint32_t place(const Settings& s, uint32_t at, uint32_t frame) {
  if (s.amount == 0 || frame == 0) return at;
  const int32_t d = towards(at, target(s.tmpl, at, frame), frame);
  const int64_t moved = d >= 0 ? (static_cast<int64_t>(d) * s.amount + 50) / 100
                               : -((static_cast<int64_t>(-d) * s.amount + 50) / 100);
  return wrap(static_cast<int64_t>(at) + moved, frame);
}

}  // namespace groove
}  // namespace ContentEngine
