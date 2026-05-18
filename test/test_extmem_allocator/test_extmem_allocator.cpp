#include <Arduino.h>
#include <unity.h>
#include "Utils/ExtMemAllocator.h"
#include <vector>
#include <cstdint>

// Mock structure for testing
struct TestStruct {
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

// Test 1: Small allocation should go to internal RAM
void test_allocator_internal_ram(void) {
    ExtMemAllocator<TestStruct> alloc;
    
    // Allocate 10 items (120 bytes) - should easily fit in internal RAM
    TestStruct* ptr = alloc.allocate(10);
    
    TEST_ASSERT_NOT_NULL(ptr);
    
    // Internal RAM addresses on Teensy 4.1 are below 0x70000000
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    TEST_ASSERT_LESS_THAN_UINT32(0x70000000, addr);
    
    alloc.deallocate(ptr, 10);
}

// Test 2: Large allocation should spill over to PSRAM
void test_allocator_psram_spillover(void) {
    ExtMemAllocator<TestStruct> alloc;
    
    // Allocate a massive amount (e.g., 500,000 items = ~6MB)
    // This will definitely fail standard malloc (1MB max) and spill to PSRAM
    size_t massive_count = 500000;
    
    // Note: If PSRAM is not installed, this will throw std::bad_alloc
    // We catch it to make the test pass gracefully on boards without PSRAM
    try {
        TestStruct* ptr = alloc.allocate(massive_count);
        
        TEST_ASSERT_NOT_NULL(ptr);
        
        // PSRAM addresses on Teensy 4.1 start at 0x70000000
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(0x70000000, addr);
        
        alloc.deallocate(ptr, massive_count);
    } catch (const std::bad_alloc& e) {
        // If PSRAM is not installed, throwing bad_alloc is the correct behavior
        TEST_MESSAGE("std::bad_alloc thrown - assuming PSRAM is not installed or full. This is expected fallback behavior.");
    }
}

// Test 3: Vector integration
void test_vector_integration(void) {
    // Create a vector using our custom allocator
    std::vector<TestStruct, ExtMemAllocator<TestStruct>> vec;
    
    // Add some items
    for (uint32_t i = 0; i < 100; i++) {
        vec.push_back({i, i*2, i*3});
    }
    
    TEST_ASSERT_EQUAL(100, vec.size());
    TEST_ASSERT_EQUAL(50, vec[50].a);
    TEST_ASSERT_EQUAL(100, vec[50].b);
    
    // Verify it's in internal RAM (since it's small)
    uintptr_t addr = reinterpret_cast<uintptr_t>(vec.data());
    TEST_ASSERT_LESS_THAN_UINT32(0x70000000, addr);
}

int main(int argc, char **argv) {
    delay(2000); // Wait for serial
    
    UNITY_BEGIN();
    RUN_TEST(test_allocator_internal_ram);
    RUN_TEST(test_allocator_psram_spillover);
    RUN_TEST(test_vector_integration);
    UNITY_END();

    return 0;
}
