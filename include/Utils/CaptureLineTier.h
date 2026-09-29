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

/// The one-shot Record receipts: the take's start (`RECA,`), its stop and stage rows (`RECS,`) and
/// its slot identity (`RID,`). They are the only evidence of a take's geometry and persistence
/// target, which is why the ring reserves space for them instead of letting ordinary traffic, or
/// another take's rows, displace them.
inline bool isRecordFamilyLine(const char* line) {
  return tagStartsWith(line, "RID,") || tagStartsWith(line, "RECA,") ||
         tagStartsWith(line, "RECS,");
}

// One-shot root-cause and Record receipts have priority over ordinary Tier-A traffic.
// They are bounded per take, not per session: another critical row may still evict
// them, and that loss must invalidate capture integrity. Classification alone is
// not evidence that the record family reached or survived the ring.
inline bool isCriticalLine(const char* line) {
  return tagStartsWith(line, "ODUB,stop,bridge_begin,") || isRecordFamilyLine(line);
}

/// What a full capture ring must displace.
///
/// These are three observably different losses and they must not be conflated by the host:
/// routine timing shed; Tier-A text displaced by newer Tier-A text (the ring's documented
/// behaviour that keeps the newest timing windows, so the session stays usable); and critical
/// text loss when critical displaces critical. Only the last eviction kind invalidates a capture. Header-only so the host tests pin the
/// truth table without Arduino.
enum class EvictionKind : uint8_t { TimingShed, TierAText, CriticalText };

/// Classify the record at the ring head, which is the one a full ring would displace.
inline EvictionKind evictionKindFor(bool headIsTierAText, bool headIsCriticalText) {
  if (headIsCriticalText) {
    return EvictionKind::CriticalText;
  }
  if (headIsTierAText) {
    return EvictionKind::TierAText;
  }
  return EvictionKind::TimingShed;
}

/// Whether an incoming line may displace the head record. Tier-A may only be displaced by Tier-A
/// and critical only by critical, so ordinary diagnostics can never evict the evidence a timing
/// verdict is computed from. Rejected non-critical appends are timing shed; rejected critical
/// appends invalidate capture integrity.
inline bool incomingMayDisplace(bool incomingTierA, bool incomingCritical, bool headTierA,
                                bool headCritical) {
  return !((!incomingTierA && headTierA) || (!incomingCritical && headCritical));
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
  // The deferred-stop admission edges carry the ONLY identity-grade evidence of the successor handoff: the
  // denial's slot+interval, the retiring owner's slot+generation, and the finalizer window opening. They were
  // non-Tier-A, so the timing-critical shed -- which drops non-Tier-A diagnostics silently, by design, and
  // does not report it as a loss -- removed them from a completed run's log entirely: the 2026-09-26 rerun had
  // zero `DIAG,reentry_window_open`, zero `DIAG,canonical_release` and no `DIAG,reentry,deny`, while
  // `capture_integrity` reported no eviction and the tail was complete. A handful of one-shot rows per run
  // must not be shed, so they are Tier-A now (Tier-A may only be displaced by Tier-A).
  // Bound each comparison by strlen of the literal itself. Hand-counting the length is a trap: passing it
  // one byte too long includes the literal's NUL, and since strncmp stops at a NUL the match then only
  // succeeds for a tag that ends exactly there -- i.e. it silently never matches a real row.
  // `ws_release` joins them: it names the *working-set lease* release, which `canonical_release` does not
  // (that one reports the retiring occupancy). Without it the lease release is only inferable.
  if (strncmp(tag, "DIAG,reentry", strlen("DIAG,reentry")) == 0 ||
      strncmp(tag, "DIAG,canonical_release,", strlen("DIAG,canonical_release,")) == 0 ||
      strncmp(tag, "DIAG,ws_release,", strlen("DIAG,ws_release,")) == 0) {
    return true;
  }
  return strncmp(tag, "ST,", 3) == 0 || strncmp(tag, "PERS,", 5) == 0 ||
         strncmp(tag, "RECS,", 5) == 0 || strncmp(tag, "RECA,", 5) == 0 ||
         strncmp(tag, "RID,", 4) == 0 ||
         strncmp(tag, "HDR,", 4) == 0 ||
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
         // One-shot post-restore census checkpoint (S1.3/S2b.3). A slope run reads `free_chunks`
         // against the readiness fields, so the marker must survive the timing-critical flush budget.
         strncmp(tag, "DIAG,post_restore_census,", 25) == 0 ||
         // `restored-library-stability` 1.3: the bounded on-demand library observation. It is an
         // explicitly requested one-shot inventory (66 records, ≤2 emitted per invocation), and the
         // whole point is to read it back in the stuck states a dense run produces — so it must survive
         // the timing-critical flush budget. The records are compact by construction because
         // `kMaxCaptureTextBytes` is 192 (a full-name form was silently truncated on device).
         strncmp(tag, "LIBOBS,", 7) == 0 ||
         strncmp(tag, "DIAG,boot_reset,", 16) == 0
#if SESSION_CAPTURE_SNAPSHOT_IDLE
         || strncmp(tag, "DIAG,snapshot_idle,", 19) == 0
#endif
         ;
}

}  // namespace CaptureLineTier
