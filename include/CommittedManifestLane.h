//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

// C5.3 (DEC-246 amendments 19-22): the committed rolling-window lane's manifest seam.
//
// - Routing is static: the active slot of `REVISION_MANIFEST_READER_TRACK` reads its committed windows from the
//   RevisionManifest; every other track and slot keeps the snapshot reader and never consults this seam.
// - The reader owner (`RevisionShadow` on the device, a fixture on the host) imports, indexes and holds the one window
//   job. The lane only asks it for metadata (`current`) and, on its own deferred producer turn, for the window read.
// - The tick never calls the reader: it reads the main-loop-maintained `gServable` words (DEC-241, consume-only).
// - Data only in this header (inline variables): no inline function bodies, so nothing lands in ITCM by inlining.

#include <cstdint>

#include "CommittedWindowRowSource.h"

#if REVISION_MANIFEST_READER

class Loop;

namespace CommittedManifestLane {

/// The named states of DEC-246 amendment 19. `Ready` is the only servable one.
enum class State : uint8_t {
  Ready = 0,
  NoCommittedSource = 1,  // the slot has no committed passes yet (restore not applied, or empty)
  ImportOwed = 2,         // the published manifest is not the import of this loop's current frozen source key
  IndexOwed = 3,          // the manifest has hides and its edit index is not Ready (never Scan)
  WindowOwed = 4,         // the window for the interval is still preparing
  CapacityDenied = 5,     // the window does not fit, both index slots are pinned, or the reader's pool was refused
  SourceMoved = 6,        // the manifest changed during preparation: the candidate is discarded
};

struct Source {
  State state = State::ImportOwed;
  uint32_t identity = 0;          // the published manifest's identity (Ready only)
  uint32_t revision = 0;          // its revision
  uint32_t sourceRevision = 0;    // the frozen source key's playbackRevision it was imported from
};

/// The reader owner. Implemented by `RevisionShadow` (device, flag 1) and by native fixtures.
class Reader {
 public:
  virtual ~Reader() = default;
  /// Metadata only: never imports, indexes or reads content.
  virtual Source current(const Loop& loop) = 0;
  /// Begin the reader's one window read for exactly `identity`/`revision`. The reader's own background window work
  /// yields. Returns nullptr with a named refusal otherwise; never imports or builds an index.
  virtual CommittedWindowRowSource* beginWindow(uint32_t identity, uint32_t revision, uint32_t startTick,
                                                uint32_t lengthTicks, uint32_t loopLengthTicks, State& refusal) = 0;
  /// The named reason the last read ended without completing (`Ready` while reading or complete).
  virtual State windowResult() const = 0;
  /// Metadata for pricing a window before it is read: the most rows any window of `spanTicks` starting on a bar of the
  /// manifest `identity` can emit (non-wrapping, clamped at the loop end: the snapshot bound's own shape). 0 when
  /// `identity` is not the reader's current manifest. Never imports, indexes or starts a window job.
  virtual uint32_t densestWindowRows(uint32_t identity, uint32_t loopLengthTicks, uint32_t spanTicks) = 0;
  /// End the read and release the manifest pin. Idempotent.
  virtual void endWindow() = 0;
};

/// The installed reader; nullptr routes nothing (host suites that do not install one keep the snapshot lane).
inline Reader* gReader = nullptr;
/// The lane runtime (`LoopPlaybackRuntime*`) that holds the reader's one window read, or nullptr.
inline const void* gWindowOwner = nullptr;
/// The read that owner holds (the reader's own cursor), valid while `gWindowOwner` is set.
inline CommittedWindowRowSource* gWindowSource = nullptr;

/// Tick-readable servability of the routed slot, written by the main-loop owner only (`Track::refreshManifestLane`).
struct Servable {
  volatile uint32_t identity;        // the Ready manifest identity, 0 when not servable
  volatile uint32_t revision;        // that manifest's revision
  volatile uint32_t sourceRevision;  // the source key's playbackRevision that identity was imported from
  volatile uint8_t state;            // `State`
};
inline Servable gServable{0, 0, 0, static_cast<uint8_t>(State::ImportOwed)};

/// Report-only counters (C5.4 routing proof). `snapshotReads` must stay 0 for the routed slot.
struct Counters {
  uint32_t windowsBegun = 0;
  uint32_t windowsCompleted = 0;
  uint32_t windowsPublished = 0;
  uint32_t snapshotReads = 0;
  /// Routed-slot ticks whose committed-lane miss was refused the merged (content-sized, root-read) fallback: a named,
  /// contained miss instead.
  uint32_t legacyRefused = 0;
  /// Routed-slot ticks served by the merged fallback while the rolling lane was gated (overdub, note edit, a deferred
  /// stop, a queued start): outside C5's reader, reported so C5.4 can separate them.
  uint32_t legacyGated = 0;
  /// Routed-slot gap entries that closed notes the previous window left sounding (their NoteOff fell in the gap).
  uint32_t gapClosures = 0;
  uint32_t owed[7] = {};  // by `State`, counted on each named refusal transition
};
/// RAM2 (`.dmabuffers`, NOLOAD) on the target, so the report-only counters cost no DTCM. NOLOAD has no initial image
/// (the `GateRowLastState` trap): the reader owner zeroes them before it installs itself, and nothing counts while no
/// reader is installed.
#if defined(__IMXRT1062__)
extern Counters gCounters;  // defined once, DMAMEM, in TrackPlaybackRuntime.cpp
#else
inline Counters gCounters{};
#endif

}  // namespace CommittedManifestLane

#endif
