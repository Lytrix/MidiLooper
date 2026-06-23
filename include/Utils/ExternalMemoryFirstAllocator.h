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
