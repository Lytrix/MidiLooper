//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Host-testable USB-device batch composition counters. Does not change dispatch
// order — MidiDispatchOrder::planDispatchOrder remains the FIFO owner.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(PIO_UNIT_TEST_NATIVE)
#include "MidiTypesNative.h"
#else
#include <MIDI.h>
#endif

namespace MidiBatchComposition {

constexpr uint8_t kBucketCount = 5;

struct Summary {
  uint32_t size = 0;
  uint32_t clockCount = 0;
  uint32_t nonClockCount = 0;
  uint32_t maxConsecutiveClocks = 0;
};

/** Bucket 0=1–8, 1=9–16, 2=17–32, 3=33–64, 4=65–128. size 0 is bucket 0. */
inline uint8_t countBucket(uint32_t count) {
  if (count <= 8) {
    return 0;
  }
  if (count <= 16) {
    return 1;
  }
  if (count <= 32) {
    return 2;
  }
  if (count <= 64) {
    return 3;
  }
  return 4;
}

inline Summary summarize(const uint8_t* types, size_t count) {
  Summary out;
  out.size = static_cast<uint32_t>(count);
  if (types == nullptr || count == 0) {
    return out;
  }
  uint32_t run = 0;
  for (size_t i = 0; i < count; ++i) {
    if (types[i] == midi::Clock) {
      ++out.clockCount;
      ++run;
      if (run > out.maxConsecutiveClocks) {
        out.maxConsecutiveClocks = run;
      }
    } else {
      ++out.nonClockCount;
      run = 0;
    }
  }
  return out;
}

}  // namespace MidiBatchComposition
