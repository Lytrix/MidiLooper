//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstddef>
#include <cstdlib>  // abort()

/**
 * @file InternalHeapFirstAllocator.h
 * @brief Internal-heap-first allocator with external-memory fallback.
 */

// extmem_malloc / extmem_free are provided by the Teensy core headers.
// Guard against non-Teensy builds (e.g. native unit-test toolchain).
#if defined(ARDUINO) && defined(__IMXRT1062__)
  #include <Arduino.h>
  extern "C" void* extmem_malloc(size_t size);
  extern "C" void  extmem_free(void* ptr);
  #define EXTMEM_PSRAM_START 0x70000000UL
  #define EXTMEM_PSRAM_END   0x78000000UL  // 8 MB ceiling
  #define EXTMEM_AVAILABLE 1

  /** True when @p ptr lies in the external memory pool address range. */
  inline bool isInExternalMemoryPool(const void* ptr) {
    if (!ptr) return false;
    const auto addr = reinterpret_cast<uintptr_t>(ptr);
    return addr >= EXTMEM_PSRAM_START && addr < EXTMEM_PSRAM_END;
  }
#else
  // Native / test builds: external memory is not available. All allocations go
  // to standard malloc so tests still compile and run on host.
  #include <cstdlib>
  static inline void* extmem_malloc(size_t size) { return nullptr; }
  static inline void  extmem_free(void* /*ptr*/) {}
  #define EXTMEM_PSRAM_START 0UL
  #define EXTMEM_PSRAM_END   0UL
  #define EXTMEM_AVAILABLE 0

  inline bool isInExternalMemoryPool(const void*) { return false; }
#endif

#if defined(PIO_UNIT_TEST_NATIVE)
/// Native allocation-pressure seam (owner ruling 2026-11-04, memory-pressure host RED): models the device arena's
/// **largest free block**, so a request above it is refused by the internal heap and takes the allocator's own
/// external fallback (served from the host heap when `externalAvailable`). Records the outcome; nothing else reads it.
struct NativeInternalHeapPressure {
  std::size_t largestInternalBlock = static_cast<std::size_t>(-1);
  bool externalAvailable = true;
  unsigned internalRefusals = 0;
  unsigned externalFallbacks = 0;
  std::size_t largestRefused = 0;
};
inline NativeInternalHeapPressure& nativeInternalHeapPressure() {
  static NativeInternalHeapPressure pressure;
  return pressure;
}
#endif

template <typename T>
class InternalHeapFirstAllocator {
 public:
  using value_type = T;

  // Required for std::allocator_traits compatibility
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  template <typename U>
  struct rebind {
    using other = InternalHeapFirstAllocator<U>;
  };

  InternalHeapFirstAllocator() noexcept = default;

  template <typename U>
  InternalHeapFirstAllocator(const InternalHeapFirstAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    const std::size_t bytes = n * sizeof(T);

#if defined(PIO_UNIT_TEST_NATIVE)
    NativeInternalHeapPressure& pressure = nativeInternalHeapPressure();
    if (bytes > pressure.largestInternalBlock) {
      ++pressure.internalRefusals;
      if (bytes > pressure.largestRefused) pressure.largestRefused = bytes;
      if (!pressure.externalAvailable) abort();
      ++pressure.externalFallbacks;
      return static_cast<T*>(malloc(bytes));
    }
#endif
    // 1. Try fast internal heap first.
    void* ptr = malloc(bytes);
    if (ptr) return static_cast<T*>(ptr);

    // 2. Internal heap exhausted: spill over to external memory pool.
    ptr = extmem_malloc(bytes);
    if (ptr) return static_cast<T*>(ptr);

    // 3. Both exhausted (or no external memory pool available).
#if defined(ARDUINO)
    Serial.println(
        "[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!");
    Serial.flush();
#endif
    abort();
  }

  void deallocate(T* ptr, std::size_t /*n*/) noexcept {
    if (!ptr) return;
    if (isInExternalMemoryPool(ptr)) {
      extmem_free(ptr);
    } else {
      free(ptr);
    }
  }

  template <typename U>
  bool operator==(const InternalHeapFirstAllocator<U>&) const noexcept {
    return true;
  }

  template <typename U>
  bool operator!=(const InternalHeapFirstAllocator<U>&) const noexcept {
    return false;
  }
};
