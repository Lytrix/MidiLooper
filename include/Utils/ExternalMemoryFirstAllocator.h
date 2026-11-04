//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdlib>  // abort()

#include "Utils/InternalHeapFirstAllocator.h"

/**
 * @file ExternalMemoryFirstAllocator.h
 * @brief External-memory-first allocator for Teensy 4.1 EXTMEM.
 *
 * Allocation strategy:
 *   1. Try extmem_malloc() first (external memory pool).
 *   2. Fall back to malloc() (internal heap) when external memory is unavailable/exhausted.
 *   3. If both fail, emit a diagnostic and abort().
 *
 * Deallocation strategy:
 *   Uses isInExternalMemoryPool() so free routing stays consistent.
 */
#if defined(PIO_UNIT_TEST_NATIVE)
/// Native external-pool pressure seam (owner ruling 2026-11-04, PSRAM exhaustion). Models the device moment where a
/// request finds neither external memory nor internal heap: any request of `refuseAtOrAboveBytes` or more is refused.
/// An unchecked growth that reaches the fatal branch records `abortReached` instead of terminating the test process
/// (and is then served, so the suite can report it); a checked probe (`CheckedCapacity`) sees the refusal and backs off.
struct NativeExternalPoolPressure {
  std::size_t refuseAtOrAboveBytes = static_cast<std::size_t>(-1);
  unsigned abortReached = 0;
  unsigned probeRefusals = 0;
  /// The pools' total capacity: a request above it can never be served (permanently insufficient).
  std::size_t totalCapacityBytes = static_cast<std::size_t>(-1);
};
inline NativeExternalPoolPressure& nativeExternalPoolPressure() {
  static NativeExternalPoolPressure pressure;
  return pressure;
}
#endif

template <typename T>
class ExternalMemoryFirstAllocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  template <typename U>
  struct rebind {
    using other = ExternalMemoryFirstAllocator<U>;
  };

  ExternalMemoryFirstAllocator() noexcept = default;

  template <typename U>
  ExternalMemoryFirstAllocator(const ExternalMemoryFirstAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    const std::size_t bytes = n * sizeof(T);
#if defined(PIO_UNIT_TEST_NATIVE)
    if (bytes >= nativeExternalPoolPressure().refuseAtOrAboveBytes) {
      ++nativeExternalPoolPressure().abortReached;  // the device would abort() here
      return static_cast<T*>(std::malloc(bytes));
    }
#endif

    // 1) Prefer external memory for length-scaling, non-hot buffers.
    void* ptr = extmem_malloc(bytes);
    if (ptr) {
      return static_cast<T*>(ptr);
    }

    // 2) Fallback to internal heap when external memory is unavailable/exhausted.
    ptr = std::malloc(bytes);
    if (ptr) {
      return static_cast<T*>(ptr);
    }

    // 3) Both allocators exhausted.
#if defined(ARDUINO)
    Serial.println(
        "[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!");
    Serial.flush();
#endif
    abort();
  }

  void deallocate(T* ptr, std::size_t /*n*/) noexcept {
    if (!ptr) {
      return;
    }
    if (isInExternalMemoryPool(ptr)) {
      extmem_free(ptr);
      return;
    }
    std::free(ptr);
  }

  template <typename U>
  bool operator==(const ExternalMemoryFirstAllocator<U>&) const noexcept {
    return true;
  }

  template <typename U>
  bool operator!=(const ExternalMemoryFirstAllocator<U>&) const noexcept {
    return false;
  }
};

/**
 * Cold-path allocator for committed chunk-id lists (seal / commit / SD load).
 * Same routing as ExternalMemoryFirstAllocator but never abort() — callers
 * must pre-check headroom and treat null allocate as failure.
 */
template <typename T>
class CommittedChunkIdAllocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  template <typename U>
  struct rebind {
    using other = CommittedChunkIdAllocator<U>;
  };

  CommittedChunkIdAllocator() noexcept = default;

  template <typename U>
  CommittedChunkIdAllocator(const CommittedChunkIdAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    const std::size_t bytes = n * sizeof(T);
    void* ptr = extmem_malloc(bytes);
    if (ptr) {
      return static_cast<T*>(ptr);
    }
    ptr = std::malloc(bytes);
    return static_cast<T*>(ptr);
  }

  void deallocate(T* ptr, std::size_t /*n*/) noexcept {
    if (!ptr) {
      return;
    }
    if (isInExternalMemoryPool(ptr)) {
      extmem_free(ptr);
      return;
    }
    std::free(ptr);
  }

  template <typename U>
  bool operator==(const CommittedChunkIdAllocator<U>&) const noexcept {
    return true;
  }

  template <typename U>
  bool operator!=(const CommittedChunkIdAllocator<U>&) const noexcept {
    return false;
  }
};
