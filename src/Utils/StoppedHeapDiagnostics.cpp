#include "Utils/StoppedHeapDiagnostics.h"
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#include <Arduino.h>
#include <malloc.h>
#include <smalloc_i.h>
extern "C" {
extern char* __brkval;
extern unsigned long _heap_start, _heap_end;
extern uint32_t __malloc_av_[258];
extern struct mallinfo __malloc_current_mallinfo;
}
namespace MemoryMonitor {
DMAMEM volatile uint32_t heapDiagnosticLastClockUs;
DMAMEM volatile bool heapDiagnosticClockSeen;
FLASHMEM void emitStoppedHeapStats(const StoppedHeapDiagnostics::Gate& gate) {
  if (const char* reason = gate.refusal()) {
    Serial.printf("#HEAP_STATS,refused=%s\n", reason);
    return;
  }
  StoppedHeapDiagnostics::collectIfStopped(gate, [] {
    static_assert(sizeof(void*) == 4 && sizeof(smalloc_hdr) == 12 && sizeof(struct mallinfo) == 40,
                  "Revalidate allocator inspection ABI after toolchain/core changes");
    const uint32_t started = micros();
    const uintptr_t start = reinterpret_cast<uintptr_t>(&_heap_start);
    const uintptr_t end = reinterpret_cast<uintptr_t>(__brkval);
    const uintptr_t limit = reinterpret_cast<uintptr_t>(&_heap_end);
    const uint32_t arena = __malloc_current_mallinfo.arena;
    const auto internal = StoppedHeapDiagnostics::scanInternal(
        reinterpret_cast<uintptr_t>(__malloc_av_), start, end, arena,
        [](uintptr_t address) { return *reinterpret_cast<const uint32_t*>(address); });
    auto& pool = extmem_smalloc_pool;
    StoppedHeapDiagnostics::External external;
    if (pool.pool && pool.pool_size <= 16u * 1024u * 1024u) {
      external = StoppedHeapDiagnostics::scanExternal(pool.pool_size,
          [&](uint32_t offset, uint32_t& payload) -> uint32_t {
            const size_t remaining = pool.pool_size - offset;
            if (remaining < 24) return 0;
            auto* header = reinterpret_cast<smalloc_hdr*>(static_cast<char*>(pool.pool) + offset);
            // Validate trailer bounds BEFORE smalloc_is_alloc reads that trailer.
            if (!header->rsz || header->rsz % 12 || header->rsz > remaining - 24 ||
                header->usz > header->rsz || !smalloc_is_alloc(&pool, header)) return 0;
            payload = header->usz;
            return header->rsz + 24;
          });
    }
    const uint32_t elapsed = micros() - started;
    // Direct stopped-only reply: the capture ring is not a reliable request/reply channel.
    Serial.printf("#HEAP_STATS,v=1,units=bytes,internal_complete=%u,arena=%lu,used_chunk_bytes=%lu,free_chunk_bytes=%lu,largest_chunk=%lu,top_chunk=%lu,free_blocks=%lu,heap_tail=%lu,external_complete=%u,pool=%lu,payload=%lu,occupied=%lu,free=%lu,largest_gap=%lu,largest_request=%lu,blocks=%lu,elapsed_us=%lu\n",
      internal.complete, (unsigned long)arena,
      (unsigned long)(internal.complete ? arena - internal.free : 0),
      (unsigned long)internal.free, (unsigned long)internal.largest, (unsigned long)internal.top,
      (unsigned long)internal.blocks, (unsigned long)(end <= limit ? limit - end : 0),
      external.complete, (unsigned long)pool.pool_size, (unsigned long)external.payload,
      (unsigned long)external.occupied, (unsigned long)external.free,
      (unsigned long)external.largestGap, (unsigned long)external.largestRequest,
      (unsigned long)external.blocks, (unsigned long)elapsed);
  });
}
}
#endif
