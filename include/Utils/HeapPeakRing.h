//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Task 4.20 per-`_sbrk` accounting -- the bounded event ring, as pure logic (no Arduino, no allocation) so the host
// can pin its semantics. The firmware half (`Utils/HeapPeakTrace.cpp`) owns the storage, the linker wrappers and the
// row emitter; this header owns only "record, overwrite the oldest, drain in order, say how many were lost".
//
// Why it exists: `_sbrk` is a pure bump, so the arena tail never recovers and every later admission is judged against
// the peak some earlier transient set. `evidence/2026-10-27-DEVICE-DISPLAY-OK-OVERDUB-HEAP-DENIED.md` §23/§25 and
// DEC-169 left the peak-setter unnamed; this ring is the measurement that names it (who raised the arena, when, by how
// much), without allocating from the arena it observes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace HeapPeakTrace {

/// One observation. `kind` 's' = the arena frontier moved (`incr` bytes); 'm' = a large request that was served
/// without moving it (it consumed a free-list hole instead, which is how the largest free chunk shrinks while the tail
/// stays flat). `request` is the `malloc` size in flight when the frontier moved (0 when the break came from another
/// path); `caller` is the wrapper's return address -- resolve it against the exact flashed ELF.
// No default member initialisers on purpose: they would give `Ring` a startup constructor, which would run after
// early `malloc` calls have already recorded and wipe those events (the ring is lazily initialised by `ensure`).
struct Event {
  uint32_t seq;
  uint32_t atUs;
  uint32_t incr;
  uint32_t request;
  uint32_t caller;
  uint32_t tailAfter;
  char kind;
  uint8_t context;
};

template <size_t N>
struct Ring {
  static constexpr uint32_t kMagic = 0x48504B54u;  // 'HPKT'
  uint32_t magic;
  uint32_t nextSeq;
  uint32_t drainedSeq;
  uint32_t lastUs;
  Event events[N];

  /// Lazily (re)initialise. Backing storage may be uninitialised RAM2 at power-on, and a warm reset keeps stale
  /// content: a rewound clock or a missing magic both mean "this is a new run".
  void ensure(uint32_t nowUs) {
    if (magic != kMagic || nowUs < lastUs) {
      magic = kMagic;
      nextSeq = 0;
      drainedSeq = 0;
      for (size_t i = 0; i < N; ++i) events[i] = Event{};  // value-init: all zero
    }
    lastUs = nowUs;
  }

  void record(uint32_t nowUs, char kind, uint32_t incr, uint32_t request, uint32_t caller,
              uint32_t tailAfter, uint8_t context = 0) {
    ensure(nowUs);
    Event& slot = events[nextSeq % N];
    slot.seq = nextSeq;
    slot.atUs = nowUs;
    slot.incr = incr;
    slot.request = request;
    slot.caller = caller;
    slot.tailAfter = tailAfter;
    slot.kind = kind;
    slot.context = context;
    ++nextSeq;
  }

  /// Oldest undrained event, in recording order. `lost` is the number of events overwritten before they could be
  /// drained since the previous call, so a gap is visible rather than silent.
  bool drainOne(Event& out, uint32_t& lost) {
    lost = 0;
    if (magic != kMagic || drainedSeq == nextSeq) return false;
    if (nextSeq - drainedSeq > N) {
      lost = nextSeq - drainedSeq - static_cast<uint32_t>(N);
      drainedSeq = nextSeq - static_cast<uint32_t>(N);
    }
    out = events[drainedSeq % N];
    ++drainedSeq;
    return true;
  }

  bool hasPending() const { return magic == kMagic && drainedSeq != nextSeq; }
};

}  // namespace HeapPeakTrace
