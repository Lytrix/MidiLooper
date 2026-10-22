//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Utils/PlaybackDemandArbitration.h"

/// **Slice 3.15e/2b-i — the per-class identity of a demand.** The arbitration contract
/// (`PlaybackDemandArbitration`) decides *who* owns the boundary; this header decides only whether a claim's currency
/// still matches its owner's live state, which is the input that outranks class. Every comparison is **within one class**
/// — a capture generation is never compared to a preview revision, and a committed identity is never "close enough".
///
/// Each provider is pure and takes the two facts it needs: the **bound** currency the owner has (what its window/demand
/// was built under) and the **live** currency the owner has now. `identityCurrent` is true only when both are non-zero and
/// equal, so "no session", "no capture" and "no root yet" are all ineligible rather than accidentally current.
///
/// The four live/bound pairs a caller supplies are the owners' own facts, not new state:
///  * committed: `Loop::playbackRevision` + the Track playback generation (`PlaybackViewSourceIdentity`);
///  * preview: the NOTE_EDIT session's preview revision (`sessionPlaybackPreviewRevision`);
///  * overlay: the live overdub capture content generation (`Loop::captureContentGeneration`, the same value slice 1
///    exposes as `overlayCaptureGeneration()`).
namespace PlaybackDemandClaims {

/// A demand is current only when both halves match and neither is the "absent" value.
inline bool committedIdentityCurrent(uint32_t liveRevision, uint32_t liveGeneration,
                                     uint32_t boundRevision, uint32_t boundGeneration) {
  return liveRevision != 0 && boundRevision == liveRevision && boundGeneration == liveGeneration;
}

inline bool previewIdentityCurrent(uint32_t liveSessionRevision, uint32_t boundSessionRevision) {
  return liveSessionRevision != 0 && boundSessionRevision == liveSessionRevision;
}

inline bool overlayIdentityCurrent(uint64_t liveCaptureGeneration, uint64_t boundCaptureGeneration) {
  return liveCaptureGeneration != 0 && boundCaptureGeneration == liveCaptureGeneration;
}

inline PlaybackDemandArbitration::Claim committed(bool owed, uint32_t liveRevision, uint32_t liveGeneration,
                                                  uint32_t boundRevision, uint32_t boundGeneration,
                                                  bool readyComplete) {
  return PlaybackDemandArbitration::Claim{
      PlaybackDemandArbitration::Class::Committed, owed,
      committedIdentityCurrent(liveRevision, liveGeneration, boundRevision, boundGeneration), readyComplete,
      (static_cast<uint64_t>(boundRevision) << 32) | boundGeneration};
}

inline PlaybackDemandArbitration::Claim preview(bool owed, uint32_t liveSessionRevision, uint32_t boundSessionRevision,
                                                bool readyComplete) {
  return PlaybackDemandArbitration::Claim{
      PlaybackDemandArbitration::Class::Preview, owed,
      previewIdentityCurrent(liveSessionRevision, boundSessionRevision), readyComplete,
      static_cast<uint64_t>(boundSessionRevision)};
}

inline PlaybackDemandArbitration::Claim overlay(bool owed, uint64_t liveCaptureGeneration,
                                                uint64_t boundCaptureGeneration, bool readyComplete) {
  return PlaybackDemandArbitration::Claim{
      PlaybackDemandArbitration::Class::Overlay, owed,
      overlayIdentityCurrent(liveCaptureGeneration, boundCaptureGeneration), readyComplete,
      boundCaptureGeneration};
}

}  // namespace PlaybackDemandClaims
