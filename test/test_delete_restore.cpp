#include <iostream>
#include <vector>
#include <cstdint>

// Replicated helpers from EditStartNoteState.cpp
static uint32_t wrapPosition(int32_t position, uint32_t loopLength) {
    if (position < 0) {
        position = (int32_t)loopLength + position;
        while (position < 0) position += (int32_t)loopLength;
    } else if (position >= (int32_t)loopLength) {
        position %= (int32_t)loopLength;
    }
    return (uint32_t)position;
}

// Overlap detection
static bool notesOverlap(uint32_t start1, uint32_t end1,
                         uint32_t start2, uint32_t end2,
                         uint32_t loopLength) {
    // Simplest unwrapped overlap for test
    return (start1 < end2) && (start2 < end1);
}

struct DeletedNote { uint32_t startTick, endTick; };

int main() {
    const uint32_t loop = 3840;
    const uint32_t staticStart = 150, staticEnd = 200;
    const uint32_t noteLen = 100;
    std::vector<DeletedNote> deletedNotes;

    uint32_t currentStart = 100;
    int deltas[] = { 50, 50, 51 };
    bool success = true;

    for (int i = 0; i < 3; ++i) {
        int delta = deltas[i];
        int movementDirection = (delta > 0) ? 1 : (delta < 0 ? -1 : 0);
        uint32_t newStart = wrapPosition((int32_t)currentStart + delta, loop);
        uint32_t newEnd = newStart + noteLen;

        if (i == 0) {
            // Step 1: should overlap and delete
            bool overlap = notesOverlap(newStart, newEnd, staticStart, staticEnd, loop);
            if (!overlap) {
                std::cerr << "FAIL step1: expected overlap for deletion\n";
                success = false;
            } else {
                deletedNotes.push_back({staticStart, staticEnd});
            }
        } else {
            // Steps 2 & 3: restoration logic
            const auto& dn = deletedNotes[0];
            bool hasOverlap = notesOverlap(newStart, newEnd, dn.startTick, dn.endTick, loop);
            bool movingAway = (dn.endTick <= currentStart);
            if (i == 1) {
                // Step 2: should NOT restore
                if (hasOverlap || movingAway) {
                    std::cerr << "FAIL step2: unexpected restore condition\n";
                    success = false;
                }
            } else {
                // Step 3: should restore
                if (hasOverlap || !movingAway) {
                    std::cerr << "FAIL step3: expected restore condition\n";
                    success = false;
                }
            }
        }
        currentStart = wrapPosition((int32_t)currentStart + deltas[i], loop);
    }

    if (success) std::cout << "✅ Left-to-right delete/restore logic passed" << std::endl;
    return success ? 0 : 1;
} 