//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <vector>
#include <utility>

#include "Utils/ExtMemAllocator.h"

void test_midivec_stress_grow_shrink_clear() {
    using Vec = std::vector<int, ExtMemAllocator<int>>;
    Vec v;
    constexpr int n = 50'000;
    v.reserve(n / 2);
    for (int i = 0; i < n; ++i) {
        v.push_back(i);
    }
    TEST_ASSERT_EQUAL(n, static_cast<int>(v.size()));
    v.erase(v.begin(), v.begin() + 10'000);
    TEST_ASSERT_EQUAL(n - 10'000, static_cast<int>(v.size()));
    v.clear();
    TEST_ASSERT_EQUAL(0u, v.size());
}

void test_midivec_move_assign() {
    using Vec = std::vector<uint32_t, ExtMemAllocator<uint32_t>>;
    Vec a;
    for (int i = 0; i < 1000; ++i) {
        a.push_back(static_cast<uint32_t>(i));
    }
    Vec b;
    b = std::move(a);
    TEST_ASSERT_EQUAL(0u, a.size());
    TEST_ASSERT_EQUAL(1000u, b.size());
    TEST_ASSERT_EQUAL_UINT32(999u, b[999]);
}

void test_midivec_resize_down_and_up() {
    using Vec = std::vector<uint8_t, ExtMemAllocator<uint8_t>>;
    Vec v;
    v.resize(20'000, 0x5A);
    TEST_ASSERT_EQUAL(20'000u, v.size());
    v.resize(100, 0x5A);
    TEST_ASSERT_EQUAL(100u, v.size());
    v.resize(5'000, 0xBB);
    TEST_ASSERT_EQUAL(5'000u, v.size());
    for (size_t i = 0; i < 5'000; ++i) {
        uint8_t expect = (i < 100) ? 0x5Au : 0xBBu;
        TEST_ASSERT_EQUAL_HEX8(expect, v[i]);
    }
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_midivec_stress_grow_shrink_clear);
    RUN_TEST(test_midivec_move_assign);
    RUN_TEST(test_midivec_resize_down_and_up);
    return UNITY_END();
}
