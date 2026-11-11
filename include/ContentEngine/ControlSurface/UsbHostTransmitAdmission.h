//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The USB host port's transmit admission (the DROID's LED, motor and bank feedback).
//
// USBHost_t36's `MIDIDeviceBase::write_packed` appends to one of two transmit buffers and spins in `while (1)`, with no
// timeout, while both are full (a buffer is full once queued, or once the driver's partial-buffer timer queues it, and
// empty again only when the device completes the transfer). A DROID that stops taking packets (seen right after a
// reboot) therefore hangs the main loop. The engine reads the driver's state with interrupts off
// and sends only when `hasRoom` says `write_packed` returns at once; otherwise the message is dropped (counted), and
// the LED stage waits instead (the surface keeps the LED it did not send).

#pragma once

#include <cstdint>

namespace ContentEngine {
namespace hosttx {

/// The driver's transmit state: an open pipe, each buffer's packet count and the packets a buffer holds.
struct State {
  bool pipe;
  uint32_t tx1, tx2, txMax;
};

/// True when `write_packed` would store a packet without waiting: no pipe (it returns at once), or a buffer not full.
inline bool hasRoom(const State& s) { return !s.pipe || s.tx1 < s.txMax || s.tx2 < s.txMax; }

}  // namespace hosttx
}  // namespace ContentEngine
