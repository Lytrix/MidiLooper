//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Tier classification for capture text lines. Tier-A lines survive the timing-critical
// flush budget and may only be displaced by other Tier-A lines in the capture ring.
// Header-only so host tests can cover the parse without Arduino.

#pragma once

#include <cstring>

namespace CaptureLineTier {

/**
 * Tag field of a "#CAP,<micros>,<tag>,..." line, or nullptr when the line is not a
 * capture line. One comma separates micros from the tag.
 */
inline const char* tagOf(const char* line) {
  if (line == nullptr || strncmp(line, "#CAP,", 5) != 0) {
    return nullptr;
  }
  const char* separator = strchr(line + 5, ',');
  return separator == nullptr ? nullptr : separator + 1;
}

inline bool tagStartsWith(const char* line, const char* prefix) {
  const char* tag = tagOf(line);
  if (tag == nullptr || prefix == nullptr) {
    return false;
  }
  return strncmp(tag, prefix, strlen(prefix)) == 0;
}

/**
 * Transport state, persistence, record stage, session header, visual cache coverage, and
 * timing telemetry lines.
 */
inline bool isTierALine(const char* line) {
  const char* tag = tagOf(line);
  if (tag == nullptr) {
    return false;
  }
  return strncmp(tag, "ST,", 3) == 0 || strncmp(tag, "PERS,", 5) == 0 ||
         strncmp(tag, "RECS,", 5) == 0 || strncmp(tag, "HDR,", 4) == 0 ||
         strncmp(tag, "ODUB,stop,", 10) == 0 ||
         strncmp(tag, "VCACHE,", 7) == 0 ||
         strncmp(tag, "DIAG,timing_max,", 16) == 0 || strncmp(tag, "DIAG,midi_gap,", 14) == 0 ||
         strncmp(tag, "DIAG,midi_input,", 16) == 0 ||
         strncmp(tag, "DIAG,msi,", 9) == 0 || strncmp(tag, "DIAG,midisvc,", 13) == 0 ||
         strncmp(tag, "DIAG,clk,", 9) == 0 ||
         strncmp(tag, "DIAG,tracks,", 12) == 0 || strncmp(tag, "DIAG,usbdev,", 12) == 0 ||
         strncmp(tag, "DIAG,din,", 9) == 0 || strncmp(tag, "DIAG,hosttask,", 14) == 0 ||
         strncmp(tag, "DIAG,hostdrain,", 15) == 0 || strncmp(tag, "DIAG,usbread,", 13) == 0 ||
         strncmp(tag, "DIAG,usbdisp,", 13) == 0 || strncmp(tag, "DIAG,usbcap,", 12) == 0 ||
         strncmp(tag, "DIAG,usbthru,", 13) == 0 || strncmp(tag, "DIAG,usbclk,", 12) == 0 ||
         strncmp(tag, "DIAG,usbnote,", 13) == 0 || strncmp(tag, "DIAG,usbcc,", 11) == 0 ||
         strncmp(tag, "DIAG,usbtrans,", 14) == 0 || strncmp(tag, "DIAG,noteappend,", 16) == 0 ||
         strncmp(tag, "DIAG,notechg,", 13) == 0 || strncmp(tag, "DIAG,noterecon,", 15) == 0 ||
         strncmp(tag, "DIAG,notepair,", 14) == 0 || strncmp(tag, "DIAG,clockrate,", 15) == 0 ||
         strncmp(tag, "DIAG,idle_maint,", 16) == 0 || strncmp(tag, "DIAG,load_frame,", 16) == 0 ||
         strncmp(tag, "DIAG,persist_save,", 18) == 0 || strncmp(tag, "DIAG,loop_rem,", 14) == 0 ||
         strncmp(tag, "DIAG,odub_stop,", 15) == 0 ||
         strncmp(tag, "DIAG,stored_notes,", 18) == 0 ||
         strncmp(tag, "DIAG,lcr,", 9) == 0 ||
         strncmp(tag, "DIAG,lcr,skip,", 14) == 0 ||
         strncmp(tag, "DIAG,overlap_hold,", 18) == 0 ||
         strncmp(tag, "DIAG,late_on,", 13) == 0 || strncmp(tag, "DIAG,late_off,", 14) == 0 ||
         strncmp(tag, "DIAG,late_clk,", 14) == 0 || strncmp(tag, "DIAG,late_event,", 16) == 0 ||
         strncmp(tag, "DIAG,playback_build,", 20) == 0;
}

}  // namespace CaptureLineTier
