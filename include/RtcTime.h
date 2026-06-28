//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * SNVS-backed wall clock for CurrentSet timestamps and SavedSet folder naming.
 * When the RTC reads invalid (unset battery, ~2019 default), firmware auto-seeds
 * from build time and raises to the newest timestamp on SD when available.
 */
namespace RtcTime {

void init();

/// Unix epoch seconds; auto-corrects invalid SNVS time before returning on Teensy.
uint32_t getUnixTime();

/// True when unix time is non-zero and on or after 2026-01-01 UTC.
bool hasValidDateForFolderNaming();

/// Seed SNVS from firmware build time when the clock reads invalid.
bool ensureWallClockValid();

/// Raise SNVS time to at least floorUnix when floor is valid and the clock is behind.
void raiseWallClockToAtLeast(uint32_t floorUnix);

/// Format unix time for display (e.g. "25 Jun 2026 19:32", UTC); empty when invalid.
void formatLastActive(uint32_t unixTime, char* out, size_t outSize);

/// Full date and time for set detail (e.g. "16 July 2026 18:42", UTC); empty when invalid.
void formatDetailDateTime(uint32_t unixTime, char* out, size_t outSize);

struct LoadSaveDetailDateParts {
  char year[8] = {};
  char monthDay[12] = {};
  char timeOfDay[8] = {};
};

/// Split unix time for load/save detail date column (year, "D MON", HH:MM).
void formatLoadSaveDetailDateParts(uint32_t unixTime, LoadSaveDetailDateParts& parts);

#if defined(PIO_UNIT_TEST_NATIVE)
void setUnixTimeForTest(uint32_t unixTime);
void resetForTest();
#endif

}  // namespace RtcTime
