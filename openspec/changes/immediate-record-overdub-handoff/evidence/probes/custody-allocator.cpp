// Native allocator/ownership measurement only; run from the repo root with -I. and -fno-access-control.
#include <cstdio>
#include <cassert>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif
#include "LoopEventStore.h"
#include "PersistenceQueue.h"
#include "src/LoopEventStore.cpp"

static size_t usable(const void* p) {
#if defined(__APPLE__)
  return malloc_size(p);
#else
  return 0; // Not measured on this platform.
#endif
}
static void show(const char* edge, const LoopEventStore& producer,
                 const LoopEventStore::CaptureRange& range) {
  std::printf("%s used=%u free=%u blocks=%u backed=%zu producerRows=%zu directory=%zu usable=%zu "
      "rangeRows=%zu rangeChunks=%zu rangeDirectory=%zu ready=%u armed=%u generation=%llu\n", edge,
      LoopEventStore::usedChunkCount(), LoopEventStore::freeChunkCount(),
      LoopEventStore::poolBlockCount(), LoopEventStore::poolBackedBytes(), producer.size(),
      producer.chunkIds_.capacity() * sizeof(uint16_t), usable(producer.chunkIds_.data()),
      range.size(), range.chunkCount(), range.storage_.chunkIds_.capacity() * sizeof(uint16_t),
      unsigned(range.ready()), unsigned(range.reservedEmpty()),
      static_cast<unsigned long long>(range.generation()));
}
int main() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore producer;
  LoopEventStore::CaptureRange a, b, c;
  assert(producer.reserveCaptureTail());
  assert(a.reserveTail() && b.reserveTail() && c.reserveTail());
  show("preowned_A_B_C", producer, a);
  for (int i = 0; i < 257; ++i) { MidiEvent e{}; e.tick = i; assert(producer.append(e)); }
  const auto* directory = producer.chunkIds_.data();
  const auto* spare = a.storage_.chunkIds_.data();
  assert(producer.canSealCaptureRange(a) && producer.sealCaptureRange(a));
  assert(a.storage_.chunkIds_.data() == directory && producer.chunkIds_.data() == spare);
  show("seal_A257_no_copy", producer, a);
  assert(!producer.canSealCaptureRange(a));
  MidiEvent e{};
  assert(producer.append(e) && producer.sealCaptureRange(b));
  assert(producer.append(e) && producer.sealCaptureRange(c));
  assert(LoopEventStore::usedChunkCount() == 5);
  show("A257_B1_C1_liveEmpty", producer, a);
  MidiEvent row{}; bool end = false;
  const auto generation = a.generation();
  assert(a.read(generation, 0, 0, row, end));
  assert(!a.releaseGrain(1));
  assert(!a.read(generation, 0, 0, row, end));
  show("A_first_release_invalidates", producer, a);
  assert(a.releaseGrain(1));
  show("A_release_complete", producer, a);
  assert(a.reserveTail());
  show("A_rearmed", producer, a);
  LoopEventStore::ChunkReservation credits;
  assert(credits.reserveChunks(8));
  assert(!credits.reserveChunks(513) && credits.availableChunks() == 8);
  while (!credits.releaseGrain(1)) {}
  const auto held = c.storage_.chunkIds_.front();
  LoopEventStore::retainChunkReferenceForTests(held);
  const auto used = LoopEventStore::usedChunkCount();
  assert(c.releaseGrain(1) && LoopEventStore::usedChunkCount() == used);
  LoopEventStore::releaseChunkReference(held);
  LoopEventStore::tryFreeChunk(held);
  assert(LoopEventStore::usedChunkCount() == used - 1);
  LoopEventStore::ChunkReservation pressure;
  const size_t free = LoopEventStore::freeChunkCount();
  assert(pressure.reserveChunks(free - 1));
  assert(!pressure.reserveChunks(free + 1) && pressure.availableChunks() == free - 1);
  show("partial_growth_rollback", producer, a);
  assert(pressure.releaseGrain(512));
  show("pressure_released_blocks_retained", producer, a);
}
