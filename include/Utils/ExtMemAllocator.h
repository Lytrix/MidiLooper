//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <new>
#include <limits>
#include <Arduino.h>

/**
 * @brief Custom C++ Allocator that prioritizes internal RAM and spills over to PSRAM.
 * 
 * The system utilizes an ultra-fast internal RAM buffer capable of holding approximately 
 * 25,000 active MIDI events across all tracks. If a recording session exceeds this capacity, 
 * the system automatically and seamlessly spills over into the 8MB PSRAM, extending the 
 * capacity to over 600,000 additional events without interrupting playback.
 * 
 * - allocate: Attempts standard malloc (internal RAM) first. If it returns nullptr, 
 *             it falls back to extmem_malloc (PSRAM).
 * - deallocate: Checks the memory address. If >= 0x70000000 (Teensy 4.1 PSRAM start), 
 *               it uses extmem_free. Otherwise, it uses standard free.
 */
template <class T>
struct ExtMemAllocator {
    typedef T value_type;

    ExtMemAllocator() = default;
    
    template <class U> constexpr ExtMemAllocator(const ExtMemAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }

        std::size_t bytes = n * sizeof(T);
        
        // 1. Try internal RAM first (fastest)
        void* p = std::malloc(bytes);
        
        // 2. If internal RAM is full, spill over to PSRAM
        if (!p) {
            p = extmem_malloc(bytes);
        }

        // 3. If both are full (or PSRAM not installed), fail gracefully
        if (!p) {
            throw std::bad_alloc();
        }

        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (!p) return;

        // Teensy 4.1 PSRAM is mapped starting at 0x70000000
        uintptr_t address = reinterpret_cast<uintptr_t>(p);
        if (address >= 0x70000000) {
            extmem_free(p);
        } else {
            std::free(p);
        }
    }
};

template <class T, class U>
bool operator==(const ExtMemAllocator<T>&, const ExtMemAllocator<U>&) { return true; }

template <class T, class U>
bool operator!=(const ExtMemAllocator<T>&, const ExtMemAllocator<U>&) { return false; }
