//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstddef>
#include <cstdlib>  // abort()

/**
 * @file ExtMemAllocator.h
 * @brief Spillover C++ allocator for Teensy 4.1 PSRAM (EXTMEM).
 *
 * Allocation strategy:
 *   1. Try standard malloc() (internal OCRAM, ~300 KB free heap).
 *      Handles roughly 25,000 MidiEvent objects (12 bytes each) before
 *      internal RAM is exhausted.
 *   2. If malloc returns nullptr (heap full), fall back to extmem_malloc()
 *      (8 MB PSRAM chip), extending capacity by ~600,000 additional events.
 *   3. If extmem_malloc also fails (PSRAM full or not installed), print a
 *      diagnostic to Serial and call abort() — no silent hard fault.
 *
 * Deallocation strategy:
 *   The Teensy 4.1 always maps PSRAM starting at address 0x70000000.
 *   deallocate() checks the pointer: addresses in [0x70000000, 0x78000000)
 *   are freed via extmem_free(); all others via free().
 *
 * Fallback (no PSRAM installed):
 *   extmem_malloc() returns nullptr when no PSRAM chip is soldered. The
 *   allocator will call abort() (with a Serial diagnostic) on large
 *   allocations that exhaust internal RAM, rather than crashing silently.
 *
 * Usage:
 *   std::vector<MidiEvent, ExtMemAllocator<MidiEvent> > v;
 *   std::deque<Foo,        ExtMemAllocator<Foo> >        d;
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
#else
  // Native / test builds: PSRAM is not available. All allocations go to
  // standard malloc so tests can still compile and run on the host.
  #include <cstdlib>
  static inline void* extmem_malloc(size_t size) { return nullptr; }
  static inline void  extmem_free(void* /*ptr*/) {}
  #define EXTMEM_PSRAM_START 0UL
  #define EXTMEM_PSRAM_END   0UL
  #define EXTMEM_AVAILABLE 0
#endif

template <typename T>
class ExtMemAllocator {
public:
    using value_type = T;

    // Required for std::allocator_traits compatibility
    using pointer            = T*;
    using const_pointer      = const T*;
    using reference          = T&;
    using const_reference    = const T&;
    using size_type          = std::size_t;
    using difference_type    = std::ptrdiff_t;

    template <typename U>
    struct rebind { using other = ExtMemAllocator<U>; };

    ExtMemAllocator() noexcept = default;

    template <typename U>
    ExtMemAllocator(const ExtMemAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        const std::size_t bytes = n * sizeof(T);

        // 1. Try fast internal RAM first.
        void* ptr = malloc(bytes);
        if (ptr) return static_cast<T*>(ptr);

        // 2. Internal RAM exhausted: spill over to PSRAM.
        ptr = extmem_malloc(bytes);
        if (ptr) return static_cast<T*>(ptr);

        // 3. Both exhausted (or no PSRAM installed).
        // Exceptions are disabled in the Teensy/Arduino build. Halt with a
        // diagnostic message so the failure is visible over Serial and in
        // crash-log rather than silently producing undefined behaviour.
#if defined(ARDUINO)
        Serial.println("[ExtMemAllocator] FATAL: both internal RAM and PSRAM are exhausted!");
        Serial.flush();
#endif
        abort();
    }

    void deallocate(T* ptr, std::size_t /*n*/) noexcept {
        if (!ptr) return;
        const auto addr = reinterpret_cast<unsigned long>(ptr);
        if (addr >= EXTMEM_PSRAM_START && addr < EXTMEM_PSRAM_END) {
            extmem_free(ptr);
        } else {
            free(ptr);
        }
    }

    // Equality: all instances of the same type share the same allocation source.
    template <typename U>
    bool operator==(const ExtMemAllocator<U>&) const noexcept { return true; }

    template <typename U>
    bool operator!=(const ExtMemAllocator<U>&) const noexcept { return false; }
};
