//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// RC-C A: Start/Stop/Continue are transport-first; Clock stays FIFO with channel messages.
// Host tests use MidiTypesNative values; firmware uses Arduino midi::MidiType (same numbers).

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(PIO_UNIT_TEST_NATIVE)
#include "MidiTypesNative.h"
#else
#include <MIDI.h>
#endif

namespace MidiDispatchOrder {

/// Sequence transport only — not Clock. Used for transport-first dispatch pass.
inline bool isSequenceTransport(uint8_t type) {
  return type == midi::Start || type == midi::Stop || type == midi::Continue;
}

/**
 * Fill outIndices[0..count) with dispatch order for a drained MIDI batch:
 * 1) all Start/Stop/Continue in wire order;
 * 2) all remaining messages (including Clock and channel) in wire order.
 *
 * Wire Clock, NoteOn, Clock must NOT become Clock, Clock, NoteOn.
 */
inline void planDispatchOrder(const uint8_t* types, size_t count, size_t* outIndices) {
  if (types == nullptr || outIndices == nullptr) {
    return;
  }
  size_t write = 0;
  for (size_t i = 0; i < count; ++i) {
    if (isSequenceTransport(types[i])) {
      outIndices[write++] = i;
    }
  }
  for (size_t i = 0; i < count; ++i) {
    if (!isSequenceTransport(types[i])) {
      outIndices[write++] = i;
    }
  }
}

}  // namespace MidiDispatchOrder
