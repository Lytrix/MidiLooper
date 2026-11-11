//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Wall-clock time for the engine's milestones and saves: real dates only.
//
// * A time is a Unix-style second count from the RTC — local time on this board, as the Teensy loader sets it at
//   upload. It is **valid** only within [`kFirstValid`, `kLastValid`]: a clock that lost its battery reads near 0,
//   and the engine never substitutes the firmware's build time (that makes dates look real when they are not). An
//   invalid time is shown as `TIME NOT SET`.
// * Shown with the owner's offset in minutes (`kMinOffset`–`kMaxOffset`; 0 while the RTC already holds local time).
// * `local` breaks a time into its calendar fields (days-from-civil, no tables); `formatStamp` writes "8 NOV 2026 14:20"
//   and `formatDay` "8 NOV". No allocation, no library time functions.

#pragma once

#include <cstdint>
#include <cstdio>

namespace ContentEngine {
namespace wallclock {

constexpr uint32_t kFirstValid = 1735689600u;  // 2025-01-01 00:00 UTC
constexpr uint32_t kLastValid = 4102444799u;   // 2099-12-31 23:59:59 UTC
constexpr int16_t kMinOffset = -12 * 60, kMaxOffset = 14 * 60;

inline bool valid(uint32_t unix) { return unix >= kFirstValid && unix <= kLastValid; }

struct Local {
  uint16_t year;
  uint8_t month, day, hour, minute, second;  // month 1–12, day 1–31
};

/// `unix` (UTC) plus `offsetMinutes`, as calendar fields.
inline Local local(uint32_t unix, int16_t offsetMinutes) {
  const int64_t t = static_cast<int64_t>(unix) + static_cast<int64_t>(offsetMinutes) * 60;
  int64_t days = t / 86400;
  int64_t secs = t % 86400;
  if (secs < 0) {
    secs += 86400;
    --days;
  }
  // H. Hinnant's civil_from_days.
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const int64_t m = mp < 10 ? mp + 3 : mp - 9;
  const int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);
  return Local{static_cast<uint16_t>(y), static_cast<uint8_t>(m), static_cast<uint8_t>(d),
               static_cast<uint8_t>(secs / 3600), static_cast<uint8_t>(secs % 3600 / 60), static_cast<uint8_t>(secs % 60)};
}

/// The day number (days since 1970-01-01) of a calendar date (H. Hinnant's days_from_civil): month starts for the
/// browser's timeline.
inline int64_t dayOf(int64_t y, int64_t m, int64_t d) {
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

inline const char* monthName(uint8_t month) {
  static const char* const kNames[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  return month >= 1 && month <= 12 ? kNames[month - 1] : "---";
}

/// "8 NOV 2026 14:20", or "TIME NOT SET" for an invalid time. `n` ≥ 18.
inline void formatStamp(uint32_t unix, int16_t offsetMinutes, char* out, size_t n) {
  if (!valid(unix)) {
    snprintf(out, n, "TIME NOT SET");
    return;
  }
  const Local l = local(unix, offsetMinutes);
  snprintf(out, n, "%u %s %u %02u:%02u", l.day, monthName(l.month), l.year, l.hour, l.minute);
}

/// "8 NOV", or "--" for an invalid time.
inline void formatDay(uint32_t unix, int16_t offsetMinutes, char* out, size_t n) {
  if (!valid(unix)) {
    snprintf(out, n, "--");
    return;
  }
  const Local l = local(unix, offsetMinutes);
  snprintf(out, n, "%u %s", l.day, monthName(l.month));
}

/// "14:32", or "--:--" for an invalid time.
inline void formatClock(uint32_t unix, int16_t offsetMinutes, char* out, size_t n) {
  if (!valid(unix)) {
    snprintf(out, n, "--:--");
    return;
  }
  const Local l = local(unix, offsetMinutes);
  snprintf(out, n, "%02u:%02u", l.hour, l.minute);
}

inline int16_t clampOffset(int32_t minutes) {
  return static_cast<int16_t>(minutes < kMinOffset ? kMinOffset : minutes > kMaxOffset ? kMaxOffset : minutes);
}

}  // namespace wallclock
}  // namespace ContentEngine
