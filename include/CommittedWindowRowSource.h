//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "LoopSnapshot.h"

/// C5 gate (DEC-246 amendment 20): the manifest-backed committed reader exists only where it is enabled. Device images
/// default to 0, so the default image carries none of its code; the native suites always build it.
#ifndef REVISION_MANIFEST_READER
#if defined(PIO_UNIT_TEST_NATIVE)
#define REVISION_MANIFEST_READER 1
#else
#define REVISION_MANIFEST_READER 0
#endif
#endif

/// C5 (DEC-246 amendment 19): a committed coverage source other than the published snapshot, i.e. the RevisionManifest
/// window. It honours the read contract of `LoopSnapshot::EventWindowCursor`, so the view fill and every later build
/// phase (canonical order, ranks, playback order) are the snapshot path's own:
///   * each row carries `canonicalOrder` and `eventId` keyed as the snapshot keys them: pass-major in merge order, then
///     the row's position within its pass (`pass << 16 | event`; `pass` is never `kLiveOverlayPassOrigin`). The view's
///     canonical-order phase sorts by it, so ranks number exactly as a snapshot window's do;
///   * one `step` call is one read unit, and `readUnits()` counts them;
///   * `traversalGeneration()` moves when the source is invalidated mid-read, which denies the fill;
///   * the caller owns its retirement.
class CommittedWindowRowSource {
 public:
  using ReadResult = LoopSnapshot::EventWindowCursor::ReadResult;

  virtual ~CommittedWindowRowSource() = default;
  virtual bool idle() const = 0;
  virtual bool complete() const = 0;
  virtual bool retiring() const = 0;
  virtual uint64_t traversalGeneration() const = 0;
  virtual uint32_t startTick() const = 0;
  virtual uint32_t lengthTicks() const = 0;
  virtual uint32_t loopLengthTicks() const = 0;
  virtual size_t readUnits() const = 0;
  virtual ReadResult step(LoopSnapshotEvent& out) = 0;
};
