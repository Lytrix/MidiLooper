//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstdint>
#include <cstddef>  // For size_t
#include <vector>

/**
 * @namespace ValidationUtils
 * @brief Common validation patterns to reduce code duplication
 */
namespace ValidationUtils {

/**
 * @brief Validates loop length and returns early if invalid
 * @param loopLength The loop length to validate
 * @return true if valid, false if should return early
 */
inline bool validateLoopLength(uint32_t loopLength) {
    return loopLength != 0;
}

/**
 * @brief Validates note index and returns early if out of bounds
 * @param noteIdx The note index to validate  
 * @param notesSize The size of the notes collection
 * @return true if valid, false if should return early
 */
inline bool validateNoteIndex(int noteIdx, std::size_t notesSize) {
    return noteIdx >= 0 && noteIdx < static_cast<int>(notesSize);
}

/**
 * @brief Combines common validation checks
 * @param loopLength The loop length to validate
 * @param noteIdx The note index to validate (-1 means skip note validation)
 * @param notesSize The size of the notes collection (ignored if noteIdx is -1)
 * @return true if all validations pass, false if should return early
 */
inline bool validateBasicParams(uint32_t loopLength, int noteIdx = -1, std::size_t notesSize = 0) {
    if (!validateLoopLength(loopLength)) {
        return false;
    }
    if (noteIdx >= 0 && !validateNoteIndex(noteIdx, notesSize)) {
        return false; 
    }
    return true;
}

/**
 * @brief Removes consecutive duplicates from a sorted vector
 * @param positions Vector to remove duplicates from (modified in place)
 */
template<typename T>
void removeDuplicates(std::vector<T>& positions) {
    if (positions.size() <= 1) return;
    
    // Remove consecutive duplicates (assumes vector is sorted)
    for (int i = static_cast<int>(positions.size()) - 1; i > 0; i--) {
        if (positions[i] == positions[i-1]) {
            positions.erase(positions.begin() + i);
        }
    }
}

} // namespace ValidationUtils 