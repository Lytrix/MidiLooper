//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Tier classification for capture text lines. Tier-A lines survive the timing-critical
// flush budget and may only be displaced by other Tier-A lines in the capture ring.
// Header-only so host tests can cover the parse without Arduino.

#pragma once

#include <cstring>

#ifndef SESSION_CAPTURE_SNAPSHOT_IDLE
#define SESSION_CAPTURE_SNAPSHOT_IDLE 0
#endif

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

// A one-shot root-cause record must outlive a prolonged Tier-A timing stream.
// Keep this deliberately narrow: normal stop telemetry remains Tier-A.
inline bool isCriticalLine(const char* line) {
  return tagStartsWith(line, "ODUB,stop,bridge_begin,");
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
  // `hold_empty` is the expected no-committed-source probe during a first take.
  // It is intentionally lossy so it cannot monopolize the timing-critical ring.
  if (strncmp(tag, "DIAG,playback_build,hold_empty,",
              strlen("DIAG,playback_build,hold_empty,")) == 0) {
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
         strncmp(tag, "DIAG,disp_", 10) == 0 || strncmp(tag, "DIAG,lframe_", 12) == 0 ||
         strncmp(tag, "DIAG,cer_", 9) == 0 ||
         strncmp(tag, "DIAG,odub_stop,", 15) == 0 ||
         strncmp(tag, "DIAG,stored_notes,", 18) == 0 ||
         strncmp(tag, "DIAG,lcr,", 9) == 0 ||
         strncmp(tag, "DIAG,lcr,skip,", 14) == 0 ||
         strncmp(tag, "DIAG,overlap_hold,", 18) == 0 ||
         strncmp(tag, "DIAG,oh,", 8) == 0 ||
         strncmp(tag, "DIAG,late_on,", 13) == 0 || strncmp(tag, "DIAG,late_off,", 14) == 0 ||
         strncmp(tag, "DIAG,late_clk,", 14) == 0 || strncmp(tag, "DIAG,late_event,", 16) == 0 ||
         strncmp(tag, "DIAG,stop_phase,", 16) == 0 ||
         strncmp(tag, "DIAG,stop_grain,", 16) == 0 ||
         strncmp(tag, "DIAG,stop_state,", 16) == 0 ||
         strncmp(tag, "DIAG,stop_service,", 18) == 0 ||
         strncmp(tag, "DIAG,stop_emit,", 15) == 0 ||
         strncmp(tag, "DIAG,stop_trace_drop,", 21) == 0 ||
         strncmp(tag, "DIAG,extclk_arrival,", 20) == 0 ||
         strncmp(tag, "DIAG,extclk_dispatch,", 21) == 0 ||
         strncmp(tag, "DIAG,clk_batch,", 15) == 0 ||
         strncmp(tag, "DIAG,service_period,", 20) == 0 ||
         strncmp(tag, "DIAG,usb_batch,", 15) == 0 ||
         strncmp(tag, "DIAG,usb_bkt_size,", 18) == 0 ||
         strncmp(tag, "DIAG,usb_bkt_clk,", 17) == 0 ||
         strncmp(tag, "DIAG,usb_bkt_disp,", 18) == 0 ||
         strncmp(tag, "DIAG,track_rebuild,", 19) == 0 ||
         strncmp(tag, "DIAG,track_order,", 17) == 0 ||
         strncmp(tag, "DIAG,track_send,", 16) == 0 ||
         strncmp(tag, "DIAG,track_slot,", 16) == 0 ||
         strncmp(tag, "DIAG,track_resid,", 17) == 0 ||
         strncmp(tag, "DIAG,playback_build,", 20) == 0 ||
         strncmp(tag, "DIAG,playback_order,", 20) == 0 ||
         strncmp(tag, "DIAG,snapshot_alloc,", 20) == 0 ||
         strncmp(tag, "DIAG,snapshot_unit,", 19) == 0 ||
         strncmp(tag, "DIAG,snapshot_slab_fail,", 24) == 0 ||
         strncmp(tag, "DIAG,capture_prewarm,", 21) == 0 ||
         strncmp(tag, "DIAG,boot_reset,", 16) == 0
#if SESSION_CAPTURE_SNAPSHOT_IDLE
         || strncmp(tag, "DIAG,snapshot_idle,", 19) == 0
#endif
         ;
}

}  // namespace CaptureLineTier
