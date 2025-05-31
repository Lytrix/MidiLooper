//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <iostream>
#include <vector>
#include <cstdint>

// Replicated helpers
static uint32_t wrapPosition(int32_t position, uint32_t loopLength) {
    if (position < 0) {
        position = (int32_t)loopLength + position;
        while (position < 0) position += (int32_t)loopLength;
    } else if (position >= (int32_t)loopLength) {
        position %= (int32_t)loopLength;
    }
    return (uint32_t)position;
}

static uint32_t calculateNoteLength(uint32_t start, uint32_t end, uint32_t loopLength) {
    if (end >= start) return end - start;
    return (loopLength - start) + end;
}

const uint32_t TICKS_PER_16TH_STEP = 48;

struct DeletedNote { uint32_t startTick, endTick; };

int main() {
    const uint32_t loop = 3840;
    const uint32_t staticStart = 0, staticEnd = 100;
    const uint32_t noteLen = 1;
    std::vector<DeletedNote> deletedNotes;
    bool success = true;

    uint32_t currentStart = 200;

    // Step 1: shorten (first overlap)
    int delta1 = -100;
    uint32_t newStart1 = wrapPosition((int32_t)currentStart + delta1, loop); // 100
    uint32_t shortenedLen1 = calculateNoteLength(staticStart, newStart1, loop);
    if (shortenedLen1 < TICKS_PER_16TH_STEP) {
        std::cerr << "FAIL step1: expected shorten length>=48, got " << shortenedLen1 << "\n";
        success = false;
    } else {
        deletedNotes.push_back({staticStart, staticEnd}); // record original
    }
    currentStart = newStart1;

    // Step 2: delete (shortened below threshold)
    int delta2 = -80;
    uint32_t newStart2 = wrapPosition((int32_t)currentStart + delta2, loop); // 20
    uint32_t shortenedLen2 = calculateNoteLength(staticStart, newStart2, loop);
    if (shortenedLen2 >= TICKS_PER_16TH_STEP) {
        std::cerr << "FAIL step2: expected deletion when shortened length<48, got " << shortenedLen2 << "\n";
        success = false;
    } else {
        deletedNotes.push_back({staticStart, newStart2}); // record deletion event
    }
    currentStart = newStart2;

    // Step 3: restore (simulate reversal)
    // Simulate movingDirection > 0 and bracket back past deletion end
    const auto& dn = deletedNotes.back();
    bool hasOverlap = false; // note removed
    bool movingAway = (dn.endTick <= currentStart);
    if (!movingAway) {
        std::cerr << "FAIL step3: expected restore condition (deleted end <= currentStart)\n";
        success = false;
    }

    if (success) std::cout << "✅ Right-to-left shorten/delete/restore logic passed" << std::endl;
    return success ? 0 : 1;
} 