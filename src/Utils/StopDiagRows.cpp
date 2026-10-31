//  Copyright (c)  2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// Tier-A prefixes of the deferred-stop/owner-turn diagnostics that are classified **outside** the header
// classifier.
//
// They live in their own translation unit **on purpose**: `DebugSessionCapture.cpp` (the classifier's unit)
// and `CaptureLineTier.h` (an inline header list instantiated in every including TU) both keep their
// `.rodata` in DTCM. One more 16-byte literal in either crossed the `_VectorsRam`/periodictable align edge
// (RAM1 `variables` 96,192 -> 100,288 B, locals 34,880 -> 30,784 B, ITCM code byte-identical) -- the
// documented 4 KB align snap of `.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc`, which is not a reason to
// re-base the budget. This unit is carved out of DTCM in `linker/imxrt1062_t41_lcr.ld`, so the literals stay
// in memory-mapped FLASH.
//
// * `DIAG,stop_tail,` -- DEC-22x: one row per stop job whose `CanonicalTail` could not begin (or latched
//   `Denied`) while a same-slot successor held the shared canonical machinery.
// * `DIAG,idle_turn,` -- DEC-216: one row per realtime idle unit that spent more than twice the declared
//   owner budget. Emitted in the timing-critical regime whose flush sheds non-Tier-A rows silently.
#include <Arduino.h>

#include "Utils/DebugSessionCapture.h"

namespace DebugSessionCapture {

SC_MEM_ATTR const char* stopTailTagPrefix() {
  return "DIAG,stop_tail,";
}

SC_MEM_ATTR const char* idleTurnTagPrefix() {
  return "DIAG,idle_turn,";
}

SC_MEM_ATTR void viewPrewarmRequest(uint8_t track, uint8_t slot, bool runtimeExists, uint8_t phase) {
  Serial.printf("#CAP,%lu,DIAG,view_prewarm_request,track=%u,slot=%u,runtime=%u,phase=%u\r\n",
                (unsigned long)micros(), static_cast<unsigned>(track),
                static_cast<unsigned>(slot), static_cast<unsigned>(runtimeExists ? 1u : 0u),
                static_cast<unsigned>(phase));
}

SC_MEM_ATTR void viewPrewarmStep(uint8_t track, uint8_t slot, uint16_t unit, uint32_t us,
                                 bool ready) {
  Serial.printf("#CAP,%lu,DIAG,view_prewarm_step,track=%u,slot=%u,unit=%u,us=%lu,ready=%u\r\n",
                (unsigned long)micros(), static_cast<unsigned>(track),
                static_cast<unsigned>(slot), static_cast<unsigned>(unit),
                (unsigned long)us, static_cast<unsigned>(ready ? 1u : 0u));
}

SC_MEM_ATTR void capturePrewarmSplit(uint8_t track, uint8_t slot, bool restamp, uint8_t okMask,
                                     uint32_t viewsUs, uint32_t canonicalUs, uint32_t horizonUs,
                                     uint32_t stopJobsUs, uint32_t companionUs, uint32_t orderUs,
                                     uint32_t otherUs, uint32_t totalUs) {
  // Direct, after the call: one row per call (record admission + the post-reset restamp). The
  // format lives in this FLASH-carved TU; no DTCM literal is added.
  Serial.printf(
      "#CAP,%lu,DIAG,capture_prewarm_split,track=%u,slot=%u,restamp=%u,okmask=%u,views=%lu,"
      "canonical=%lu,horizon=%lu,stopjobs=%lu,companion=%lu,order=%lu,other=%lu,total=%lu\r\n",
      (unsigned long)micros(), static_cast<unsigned>(track), static_cast<unsigned>(slot),
      static_cast<unsigned>(restamp ? 1u : 0u), static_cast<unsigned>(okMask),
      (unsigned long)viewsUs,
      (unsigned long)canonicalUs, (unsigned long)horizonUs, (unsigned long)stopJobsUs,
      (unsigned long)companionUs, (unsigned long)orderUs, (unsigned long)otherUs,
      (unsigned long)totalUs);
}

SC_MEM_ATTR void clearRefusedImmediate(uint8_t slot, const char* reason) {
  // Direct on purpose: the clear's logger refusal is queued behind the capture ring, and the rig needs
  // the named gate in the same attempt window to retry the 100 ms post-load holdoff quickly.
  Serial.printf("#CAP,%lu,DIAG,clear_refused,%u,%s\r\n", (unsigned long)micros(),
                static_cast<unsigned>(slot), reason != nullptr ? reason : "unknown");
}

}  // namespace DebugSessionCapture
