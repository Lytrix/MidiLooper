#include <iostream>
#include <cstdint>
#include <unity.h>
// Replicated helper functions from EditStartNoteState.cpp
static uint32_t wrapPosition(int32_t position, uint32_t loopLength) {
    if (position < 0) {
        position = (int32_t)loopLength + position;
        while (position < 0) {
            position += (int32_t)loopLength;
        }
    } else if (position >= (int32_t)loopLength) {
        position = position % (int32_t)loopLength;
    }
    return (uint32_t)position;
}

static uint32_t calculateNoteLength(uint32_t start, uint32_t end, uint32_t loopLength) {
    if (end >= start) {
        return end - start;
    } else {
        return (loopLength - start) + end;
    }
}

int main() {
    const uint32_t loop = 3840;
    bool ok = true;

    // Test wrapPosition around negative
    if (wrapPosition(-1, loop) != loop - 1) {
        std::cerr << "FAIL: wrapPosition(-1) = " << wrapPosition(-1, loop) << " (expected " << loop - 1 << ")\n";
        ok = false;
    }
    // Test wrapPosition at boundary
    if (wrapPosition((int32_t)loop, loop) != 0) {
        std::cerr << "FAIL: wrapPosition(loop) = " << wrapPosition(loop, loop) << " (expected 0)\n";
        ok = false;
    }
    // Test wrapPosition beyond boundary
    if (wrapPosition((int32_t)loop + 1, loop) != 1) {
        std::cerr << "FAIL: wrapPosition(loop+1) = " << wrapPosition(loop + 1, loop) << " (expected 1)\n";
        ok = false;
    }
    // Test calculateNoteLength without wrap
    if (calculateNoteLength(100, 200, loop) != 100) {
        std::cerr << "FAIL: calculateNoteLength(100,200) = " << calculateNoteLength(100, 200, loop) << " (expected 100)\n";
        ok = false;
    }
    // Test calculateNoteLength with wrap
    if (calculateNoteLength(3838, 5, loop) != 7) {
        std::cerr << "FAIL: calculateNoteLength(3838,5) = " << calculateNoteLength(3838, 5, loop) << " (expected 7)\n";
        ok = false;
    }

    if (ok) {
        std::cout << "✅ Wrap logic functions passed all tests" << std::endl;
    }
    return ok ? 0 : 1;
} 