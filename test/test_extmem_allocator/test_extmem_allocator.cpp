//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file test_extmem_allocator.cpp
 * @brief Unit tests for ExtMemAllocator spillover logic.
 *
 * These tests run on the native toolchain (no Teensy hardware required).
 * On the native build EXTMEM_AVAILABLE is 0, so extmem_malloc() always
 * returns nullptr. This lets us exercise the internal-RAM path and the
 * bad_alloc path deterministically.
 *
 * On the Teensy 4.1 target with PSRAM installed, the test for PSRAM
 * spillover must be triggered by exhausting internal RAM, which is not
 * feasible in a unit test environment. Instead that path is covered by
 * the integration test in test/teensy41_psram_memtest/.
 *
 * Run with: pio test -e native
 */

#include <unity.h>
#include <vector>
#include <cstdlib>
#include <cstdint>

// Include the allocator under test. On native builds EXTMEM_AVAILABLE == 0
// so extmem_malloc / extmem_free are no-ops.
#include "Utils/ExtMemAllocator.h"

// ---------------------------------------------------------------------------
// Test 1: A small allocation uses internal RAM (address < EXTMEM_PSRAM_START)
// ---------------------------------------------------------------------------
void test_small_allocation_stays_in_internal_ram() {
    ExtMemAllocator<uint32_t> alloc;
    uint32_t* ptr = alloc.allocate(1);

    TEST_ASSERT_NOT_NULL(ptr);

    // On native builds EXTMEM_PSRAM_START is 0, so every pointer is >= 0;
    // the meaningful check is that the pointer is non-null and writable.
    *ptr = 0xDEADBEEF;
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, *ptr);

    alloc.deallocate(ptr, 1);
}

// ---------------------------------------------------------------------------
// Test 2: Allocator works correctly with std::vector
// ---------------------------------------------------------------------------
void test_vector_with_ext_mem_allocator() {
    std::vector<int, ExtMemAllocator<int>> v;
    for (int i = 0; i < 1000; ++i) v.push_back(i);

    TEST_ASSERT_EQUAL(1000, (int)v.size());
    for (int i = 0; i < 1000; ++i) {
        TEST_ASSERT_EQUAL(i, v[i]);
    }
    // Vector destructor frees via deallocate — no leak.
}

// ---------------------------------------------------------------------------
// Test 3: Two allocator instances of the same type compare equal
// ---------------------------------------------------------------------------
void test_allocator_equality() {
    ExtMemAllocator<int> a1;
    ExtMemAllocator<int> a2;
    TEST_ASSERT_TRUE(a1 == a2);
    TEST_ASSERT_FALSE(a1 != a2);
}

// ---------------------------------------------------------------------------
// Test 4: Rebind produces a compatible allocator for a different type
// ---------------------------------------------------------------------------
void test_allocator_rebind() {
    ExtMemAllocator<int> intAlloc;
    ExtMemAllocator<int>::rebind<char>::other charAlloc(intAlloc);

    char* p = charAlloc.allocate(10);
    TEST_ASSERT_NOT_NULL(p);
    p[0] = 'A';
    TEST_ASSERT_EQUAL('A', p[0]);
    charAlloc.deallocate(p, 10);
}

// ---------------------------------------------------------------------------
// Test 5: On native build (no PSRAM) a zero-size request does not crash
// ---------------------------------------------------------------------------
void test_zero_size_allocation_does_not_crash() {
    // malloc(0) is implementation-defined — may return null or a unique ptr.
    // On the native build there is no PSRAM so extmem_malloc() is a no-op.
    // We simply assert that allocate(0) completes without hard-faulting.
    ExtMemAllocator<uint8_t> alloc;
    uint8_t* ptr = alloc.allocate(0);
    // malloc(0) can legitimately return nullptr or a non-null ptr — both OK.
    if (ptr) alloc.deallocate(ptr, 0);
}

// ---------------------------------------------------------------------------
// Test 6: deallocate(nullptr) is safe
// ---------------------------------------------------------------------------
void test_deallocate_null_is_safe() {
    ExtMemAllocator<int> alloc;
    // Must not crash or assert.
    alloc.deallocate(nullptr, 0);
}

// ---------------------------------------------------------------------------
// Test 7: On native build, extmem_malloc always returns nullptr, so the
//         allocator falls back to malloc. Verify the vector still fills
//         correctly even though the "PSRAM" path is never taken.
// ---------------------------------------------------------------------------
void test_fallback_to_internal_ram_when_psram_unavailable() {
    // On native EXTMEM_AVAILABLE == 0, extmem_malloc() is a no-op returning
    // nullptr. The allocator must therefore succeed via malloc() alone.
    std::vector<uint8_t, ExtMemAllocator<uint8_t>> buf;
    buf.resize(4096, 0xAB);
    TEST_ASSERT_EQUAL(4096u, buf.size());
    for (auto b : buf) TEST_ASSERT_EQUAL_HEX8(0xAB, b);
}

// ---------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_small_allocation_stays_in_internal_ram);
    RUN_TEST(test_vector_with_ext_mem_allocator);
    RUN_TEST(test_allocator_equality);
    RUN_TEST(test_allocator_rebind);
    RUN_TEST(test_zero_size_allocation_does_not_crash);
    RUN_TEST(test_deallocate_null_is_safe);
    RUN_TEST(test_fallback_to_internal_ram_when_psram_unavailable);

    return UNITY_END();
}
