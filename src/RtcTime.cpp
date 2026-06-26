//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RtcTime.h"

#include <cstdio>
#include <ctime>

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

void formatLastActive(uint32_t unixTime, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  out[0] = '\0';
  if (unixTime == 0) {
    return;
  }
#if defined(ARDUINO)
  const time_t t = static_cast<time_t>(unixTime);
  struct tm tmBuf {};
  if (localtime_r(&t, &tmBuf) == nullptr) {
    return;
  }
  // e.g. "25 June 2026"
  strftime(out, outSize, "%-d %B %Y", &tmBuf);
#else
  // Portable fallback for native tests with known fixture times.
  struct tm tmBuf {};
  const time_t t = static_cast<time_t>(unixTime);
#if defined(_WIN32)
  gmtime_s(&tmBuf, &t);
#else
  gmtime_r(&t, &tmBuf);
#endif
  std::snprintf(out, outSize, "%d %02d %04d", tmBuf.tm_mday, tmBuf.tm_mon + 1, tmBuf.tm_year + 1900);
#endif
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
