#pragma once
#include <cstddef>
#include <cstdint>

namespace StoppedHeapDiagnostics {
inline bool recentClock(uint32_t now, uint32_t last, bool seen) {
  return seen && static_cast<uint32_t>(now - last) < 1000000u;
}
struct Gate {
  bool transport = false, clock = false, tracks = false, storage = false;
  const char* refusal() const {
    if (transport) return "transport_active";
    if (clock) return "external_clock";
    if (tracks) return "track_work";
    if (storage) return "storage_work";
    return nullptr;
  }
};
template <class Collect>
bool collectIfStopped(const Gate& gate, Collect collect) {
  if (gate.refusal()) return false;
  collect();
  return true;
}
struct Internal {
  uint32_t free = 0, largest = 0, top = 0, blocks = 0;
  bool complete = false;
};
// Pinned 32-bit newlib dlmalloc ABI. Read is invoked only for validated addresses.
// Raw chunk bytes include metadata; arena tail has not been obtained from sbrk.
template <class Read>
Internal scanInternal(uintptr_t bins, uintptr_t start, uintptr_t end,
                      uint32_t arena, Read read) {
  Internal out;
  if (arena == 0) { out.complete = true; return out; }
  if (end < start || arena > end - start) return out;
  auto valid = [=](uintptr_t p) { return p >= start && p <= end && end - p >= 16 && (p & 7) == 0; };
  auto add = [&](uintptr_t p, bool top) {
    if (!valid(p) || out.blocks >= 4096) return false;
    const uint32_t size = read(p + 4) & ~uint32_t(3);
    if (size < 16 || (size & 7) || size > end - p || size > arena - out.free) return false;
    out.free += size;
    if (size > out.largest) out.largest = size;
    if (top) out.top = size;
    ++out.blocks;
    return true;
  };
  const uintptr_t top = read(bins + 8);
  if (!add(top, true)) return out;
  for (size_t i = 0; i < 127; ++i) {
    const uintptr_t head = bins + 8 + i * 8;
    uintptr_t previous = head;
    uintptr_t node = read(head + 12);
    while (node != head) {
      if (!valid(node) || read(node + 8) != previous || !add(node, false)) return out;
      previous = node;
      node = read(node + 12);
    }
    if (read(head + 8) != previous) return out;
  }
  out.complete = true;
  return out;
}
struct External {
  uint32_t payload = 0, occupied = 0, free = 0, largestGap = 0, largestRequest = 0, blocks = 0;
  bool complete = false;
};
// Provider returns occupied span (header+rounded body+trailer), or zero for a free
// header position. No allocation probes; same 12-byte geometry as pinned smalloc.
template <class Block>
External scanExternal(uint32_t bytes, Block block) {
  External out;
  if (bytes % 12) return out;
  uint32_t offset = 0, gap = 0;
  auto finishGap = [&]() {
    out.free += gap;
    if (gap > out.largestGap) out.largestGap = gap;
    gap = 0;
  };
  while (offset < bytes) {
    uint32_t payload = 0;
    const uint32_t span = block(offset, payload);
    if (span) {
      if (span < 24 || span % 12 || span > bytes - offset || payload > span - 24) return out;
      finishGap();
      out.occupied += span; out.payload += payload; ++out.blocks;
      offset += span;
    } else { offset += 12; gap += 12; }
  }
  finishGap();
  out.largestRequest = out.largestGap > 24 ? out.largestGap - 24 : 0;
  out.complete = true;
  return out;
}
}  // namespace StoppedHeapDiagnostics

namespace MemoryMonitor {
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
extern volatile uint32_t heapDiagnosticLastClockUs;
extern volatile bool heapDiagnosticClockSeen;
void emitStoppedHeapStats(const StoppedHeapDiagnostics::Gate& gate);
#endif
}
