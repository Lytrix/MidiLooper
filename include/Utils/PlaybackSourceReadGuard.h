// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstdint>
#ifndef PLAYBACK_SOURCE_MASK_QUALIFICATION
#define PLAYBACK_SOURCE_MASK_QUALIFICATION 0
#endif
#if PLAYBACK_SOURCE_MASK_QUALIFICATION && (!defined(PIO_UNIT_TEST_NATIVE) || defined(__IMXRT1062__))
#error "Playback source mask qualification is a native hardware boundary only"
#endif
#if PLAYBACK_SOURCE_MASK_QUALIFICATION
namespace PlaybackSourceNativeMask {
uint32_t enter();
void restore(uint32_t saved);
}
#endif

// Fixed metadata or one raw-row copy only. Never encloses fill/order/retirement
// loops. Caller still retains the Loop and owns its private candidate state.
class PlaybackSourceReadGuard {
 public:
  explicit PlaybackSourceReadGuard(bool enabled = true) : enabled_(enabled) {
    if (!enabled_) return;
#if defined(__IMXRT1062__)
    __asm__ volatile("mrs %0, primask" : "=r"(saved_) :: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    __asm__ volatile("dmb" ::: "memory");
#elif PLAYBACK_SOURCE_MASK_QUALIFICATION
    saved_ = PlaybackSourceNativeMask::enter();
#endif
  }
  ~PlaybackSourceReadGuard() {
    if (!enabled_) return;
#if defined(__IMXRT1062__)
    __asm__ volatile("dmb" ::: "memory");
    __asm__ volatile("msr primask, %0" :: "r"(saved_) : "memory");
#elif PLAYBACK_SOURCE_MASK_QUALIFICATION
    PlaybackSourceNativeMask::restore(saved_);
#endif
  }
  PlaybackSourceReadGuard(const PlaybackSourceReadGuard&) = delete;
  PlaybackSourceReadGuard& operator=(const PlaybackSourceReadGuard&) = delete;
 private:
  uint32_t saved_ = 0;
  bool enabled_;
};
