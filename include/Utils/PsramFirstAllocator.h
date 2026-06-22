//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdlib>  // abort()

#include "Utils/ExtMemAllocator.h"

/**
 * @file PsramFirstAllocator.h
 * @brief PSRAM-first C++ allocator for Teensy 4.1 EXTMEM.
 *
 * Allocation strategy:
 *   1. Try extmem_malloc() first (PSRAM).
 *   2. Fall back to malloc() (internal RAM) when PSRAM is unavailable/exhausted.
 *   3. If both fail, emit a diagnostic and abort().
 *
 * Deallocation strategy:
 *   Uses the shared isInPsram() address-range helper so free-routing remains
 *   consistent with existing PSRAM pointer checks used by loop event storage.
 *
 * Usage:
 *   std::vector<MidiEvent, PsramFirstAllocator<MidiEvent>> v;
 */
template <typename T>
class PsramFirstAllocator {
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
    using other = PsramFirstAllocator<U>;
  };

  PsramFirstAllocator() noexcept = default;

  template <typename U>
  PsramFirstAllocator(const PsramFirstAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    const std::size_t bytes = n * sizeof(T);

    // 1) Prefer PSRAM for length-scaling, non-hot buffers.
    void* ptr = extmem_malloc(bytes);
    if (ptr) {
      return static_cast<T*>(ptr);
    }

    // 2) Fallback to internal heap when PSRAM is unavailable/exhausted.
    ptr = std::malloc(bytes);
    if (ptr) {
      return static_cast<T*>(ptr);
    }

    // 3) Both allocators exhausted.
#if defined(ARDUINO)
    Serial.println(
        "[PsramFirstAllocator] FATAL: both PSRAM and internal RAM are exhausted!");
    Serial.flush();
#endif
    abort();
  }

  void deallocate(T* ptr, std::size_t /*n*/) noexcept {
    if (!ptr) {
      return;
    }
    if (isInPsram(ptr)) {
      extmem_free(ptr);
      return;
    }
    std::free(ptr);
  }

  template <typename U>
  bool operator==(const PsramFirstAllocator<U>&) const noexcept {
    return true;
  }

  template <typename U>
  bool operator!=(const PsramFirstAllocator<U>&) const noexcept {
    return false;
  }
};
