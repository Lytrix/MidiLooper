//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "Utils/InternalHeapFirstAllocator.h"

#if EXTMEM_AVAILABLE && defined(__IMXRT1062__)

void test_extmem_routed_free_via_internal_heap_first_allocator() {
    constexpr size_t kBytes = 64;
    void* p = extmem_malloc(kBytes);
    if (!p) {
        TEST_IGNORE_MESSAGE("extmem_malloc returned nullptr (no PSRAM or allocation failed)");
    }
    const auto addr = reinterpret_cast<uintptr_t>(p);
    TEST_ASSERT_TRUE(addr >= EXTMEM_PSRAM_START && addr < EXTMEM_PSRAM_END);
    InternalHeapFirstAllocator<uint8_t> alloc;
    alloc.deallocate(static_cast<uint8_t*>(p), kBytes);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_extmem_routed_free_via_internal_heap_first_allocator);
    return UNITY_END();
}

#else

void test_extmem_dealloc_skips_without_psram_target() { TEST_PASS(); }

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_extmem_dealloc_skips_without_psram_target);
    return UNITY_END();
}

#endif
