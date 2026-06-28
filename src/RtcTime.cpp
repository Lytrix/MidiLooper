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

bool isWallClockValid(uint32_t unixTime) {
  return unixTime >= kFolderNamingEpochUnix;
}

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

int monthFromCompileDateToken(const char* monthToken) {
  static const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (monthToken == nullptr) {
    return 0;
  }
  for (int i = 0; i < 12; ++i) {
    if (monthToken[0] == kMonthNames[i][0] && monthToken[1] == kMonthNames[i][1] &&
        monthToken[2] == kMonthNames[i][2]) {
      return i + 1;
    }
  }
  return 0;
}

uint32_t dateTimeToUnixUtc(int year, unsigned month, unsigned day, unsigned hour,
                           unsigned minute, unsigned second) {
  int adjustedYear = year;
  unsigned adjustedMonth = month;
  if (adjustedMonth < 3) {
    adjustedYear -= 1;
    adjustedMonth += 12;
  }
  const int64_t era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
  const int64_t yearOfEra = adjustedYear - era * 400;
  const int64_t dayOfYear =
      (153 * static_cast<int64_t>(adjustedMonth) - 457) / 5 + static_cast<int64_t>(day) - 1;
  const int64_t dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  const int64_t daysSinceEpoch = era * 146097 + dayOfEra - 719468;
  if (daysSinceEpoch < 0) {
    return 0;
  }
  const int64_t unixTime =
      daysSinceEpoch * 86400LL + static_cast<int64_t>(hour) * 3600LL +
      static_cast<int64_t>(minute) * 60LL + static_cast<int64_t>(second);
  if (unixTime < 0 || unixTime > 0xFFFFFFFFLL) {
    return 0;
  }
  return static_cast<uint32_t>(unixTime);
}

uint32_t compileTimeUnixUtc() {
  const int month = monthFromCompileDateToken(__DATE__);
  if (month == 0) {
    return 0;
  }
  const unsigned day =
      __DATE__[4] == ' ' ? static_cast<unsigned>(__DATE__[5] - '0')
                         : static_cast<unsigned>((__DATE__[4] - '0') * 10 + (__DATE__[5] - '0'));
  const int year = (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 +
                   (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');
  const unsigned hour =
      static_cast<unsigned>((__TIME__[0] - '0') * 10 + (__TIME__[1] - '0'));
  const unsigned minute =
      static_cast<unsigned>((__TIME__[3] - '0') * 10 + (__TIME__[4] - '0'));
  const unsigned second =
      static_cast<unsigned>((__TIME__[6] - '0') * 10 + (__TIME__[7] - '0'));
  return dateTimeToUnixUtc(year, static_cast<unsigned>(month), day, hour, minute, second);
}

#if defined(ARDUINO) && defined(__IMXRT1062__)
uint32_t readHardwareWallClockUnix() {
  return Teensy3Clock.get();
}

bool writeHardwareWallClockUnix(uint32_t unixTime) {
  if (unixTime == 0) {
    return false;
  }
  Teensy3Clock.set(unixTime);
  return true;
}
#else
uint32_t readHardwareWallClockUnix() {
#if defined(PIO_UNIT_TEST_NATIVE)
  return testOverride ? testUnixTime : 0;
#else
  return 0;
#endif
}

bool writeHardwareWallClockUnix(uint32_t unixTime) {
#if defined(PIO_UNIT_TEST_NATIVE)
  if (unixTime == 0) {
    return false;
  }
  testUnixTime = unixTime;
  testOverride = true;
  return true;
#else
  (void)unixTime;
  return false;
#endif
}
#endif

bool seedWallClockFromCompileTime() {
  const uint32_t compileUnix = compileTimeUnixUtc();
  if (!isWallClockValid(compileUnix)) {
    return false;
  }
  if (!writeHardwareWallClockUnix(compileUnix)) {
    return false;
  }
#if defined(ARDUINO)
  static bool loggedCompileSeed = false;
  if (!loggedCompileSeed) {
    loggedCompileSeed = true;
    Serial.println("[RtcTime] SNVS corrected from firmware build time (RTC was invalid)");
  }
#endif
  return true;
}

}  // namespace

void init() {
  ensureWallClockValid();
}

bool ensureWallClockValid() {
  const uint32_t now = readHardwareWallClockUnix();
  if (isWallClockValid(now)) {
    return true;
  }
  return seedWallClockFromCompileTime();
}

void raiseWallClockToAtLeast(uint32_t floorUnix) {
  if (!isWallClockValid(floorUnix)) {
    return;
  }
  ensureWallClockValid();
  const uint32_t now = readHardwareWallClockUnix();
  if (isWallClockValid(now) && now >= floorUnix) {
    return;
  }
  if (!writeHardwareWallClockUnix(floorUnix)) {
    return;
  }
#if defined(ARDUINO)
  static bool loggedSdRaise = false;
  if (!loggedSdRaise) {
    loggedSdRaise = true;
    Serial.print("[RtcTime] SNVS raised to SD timestamp floor unix=");
    Serial.println(floorUnix);
  }
#endif
}

uint32_t getUnixTime() {
#if defined(PIO_UNIT_TEST_NATIVE)
  if (testOverride) {
    return testUnixTime;
  }
  return 0;
#elif defined(ARDUINO) && defined(__IMXRT1062__)
  uint32_t now = readHardwareWallClockUnix();
  if (!isWallClockValid(now)) {
    ensureWallClockValid();
    now = readHardwareWallClockUnix();
  }
  return isWallClockValid(now) ? now : 0;
#elif defined(ARDUINO)
  return 0;
#else
  return 0;
#endif
}

bool hasValidDateForFolderNaming() {
  return isWallClockValid(getUnixTime());
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

void formatLoadSaveDetailDateParts(uint32_t unixTime, LoadSaveDetailDateParts& parts) {
  parts = {};
  std::snprintf(parts.year, sizeof(parts.year), "--");
  std::snprintf(parts.monthDay, sizeof(parts.monthDay), "--");
  std::snprintf(parts.timeOfDay, sizeof(parts.timeOfDay), "--");
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
  static const char* kMonthAbbrev[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  std::snprintf(parts.year, sizeof(parts.year), "%d", year);
  std::snprintf(parts.monthDay, sizeof(parts.monthDay), "%u %s", day, kMonthAbbrev[month - 1]);
  std::snprintf(parts.timeOfDay, sizeof(parts.timeOfDay), "%02u:%02u", hour, minute);
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
