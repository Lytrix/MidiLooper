//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RtcTime.h"

#include <cstdio>

#if defined(ARDUINO)
#include <Arduino.h>
#include <core_pins.h>
#endif

namespace RtcTime {
namespace {

constexpr uint32_t kFolderNamingEpochUnix = 1767225600UL;  // 2026-01-01 00:00:00 UTC

#if defined(PIO_UNIT_TEST_NATIVE)
uint32_t testUnixTime = 0;
bool testOverride = false;
#endif

bool unixToUtcDateTime(uint32_t unixTime, int& year, unsigned& month, unsigned& day,
                       unsigned& hour, unsigned& minute) {
  if (unixTime == 0) {
    return false;
  }
  const int64_t daysSinceEpoch = static_cast<int64_t>(unixTime / 86400UL);
  int64_t z = daysSinceEpoch + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t dayOfEra = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
  const uint32_t dayOfYear =
      dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const uint32_t monthPrime = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
  month = monthPrime + (monthPrime < 10 ? 3 : -9);
  year += (month <= 2);
  const uint32_t secondsOfDay = unixTime % 86400UL;
  hour = secondsOfDay / 3600UL;
  minute = (secondsOfDay % 3600UL) / 60UL;
  return true;
}

}  // namespace

void init() {
  // SNVS SRTC is battery-backed on Teensy 4.1; no init required for read.
}

uint32_t getUnixTime() {
#if defined(PIO_UNIT_TEST_NATIVE)
  if (testOverride) {
    return testUnixTime;
  }
  return 0;
#elif defined(ARDUINO) && defined(__IMXRT1062__)
  return Teensy3Clock.get();
#elif defined(ARDUINO)
  return 0;
#else
  return 0;
#endif
}

bool hasValidDateForFolderNaming() {
  const uint32_t unixTime = getUnixTime();
  return unixTime >= kFolderNamingEpochUnix;
}

void formatDetailDateTime(uint32_t unixTime, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  out[0] = '\0';
  if (unixTime == 0) {
    return;
  }
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  if (!unixToUtcDateTime(unixTime, year, month, day, hour, minute) || month < 1 || month > 12) {
    return;
  }
  static const char* kMonthNames[] = {"January",   "February", "March",    "April",
                                      "May",       "June",     "July",     "August",
                                      "September", "October",  "November", "December"};
  std::snprintf(out, outSize, "%u %s %d %02u:%02u", day, kMonthNames[month - 1], year, hour,
                minute);
}

void formatLastActive(uint32_t unixTime, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  out[0] = '\0';
  if (unixTime == 0) {
    return;
  }
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  if (!unixToUtcDateTime(unixTime, year, month, day, hour, minute) || month < 1 || month > 12) {
    return;
  }
  static const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  std::snprintf(out, outSize, "%u %s %d %02u:%02u", day, kMonthNames[month - 1], year, hour,
                minute);
}

#if defined(PIO_UNIT_TEST_NATIVE)
void setUnixTimeForTest(uint32_t unixTime) {
  testUnixTime = unixTime;
  testOverride = true;
}

void resetForTest() {
  testUnixTime = 0;
  testOverride = false;
}
#endif

}  // namespace RtcTime
