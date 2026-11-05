//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Global revision manifests of the content engine. A manifest is the immutable, complete run list of one revision of
// one slot. `identity` is physical, monotonic and never reused (a window or tile stamp names it); the table slot index
// is reused. References come from slot currents, history entries, writer bases and reader pins (pins are a subset of
// refs). Releasing the last reference releases each listed run exactly once.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"

namespace ContentEngine {

template <uint16_t ManifestCount, uint8_t MaxRuns>
class ManifestTable {
 public:
  static constexpr uint16_t kManifestCount = ManifestCount;
  static constexpr uint8_t kMaxRuns = MaxRuns;

  struct Manifest {
    uint32_t identity;
    uint32_t revision;
    uint32_t loopLengthTicks;
    RunId runs[MaxRuns];
    uint16_t refs;
    uint16_t readerPins;
    uint8_t runCount;
    uint8_t slot;
    bool live;
  };

  void init() {
    for (uint16_t i = 0; i < ManifestCount; ++i) {
      clear(table_[i]);
      nextFree_[i] = static_cast<ManifestId>(i + 1 < ManifestCount ? i + 1 : kNoManifest);
    }
    freeHead_ = ManifestCount ? 0 : kNoManifest;
    freeCount_ = ManifestCount;
    nextIdentity_ = 1;
  }

  /// O(runCount). Creates a live manifest with one reference (its creator) and adds one reference to every listed run.
  /// `kNoManifest` when the table is full or the run list is too long; callers reserve a slot before accepting the
  /// interaction that will create it.
  template <typename Runs>
  ManifestId create(uint8_t slot, uint32_t revision, uint32_t loopLengthTicks, const RunId* runs, uint8_t runCount,
                    Runs& runTable) {
    if (freeHead_ == kNoManifest || runCount > MaxRuns) return kNoManifest;
    const ManifestId id = freeHead_;
    freeHead_ = nextFree_[id];
    --freeCount_;
    Manifest& m = table_[id];
    clear(m);
    m.identity = nextIdentity_++;
    m.revision = revision;
    m.loopLengthTicks = loopLengthTicks;
    m.slot = slot;
    m.runCount = runCount;
    for (uint8_t i = 0; i < runCount; ++i) {
      m.runs[i] = runs[i];
      runTable.addRef(runs[i]);
    }
    m.refs = 1;
    m.live = true;
    return id;
  }

  void addRef(ManifestId id) { ++table_[id].refs; }

  /// A reader pin is also a reference, counted separately so diagnostics can tell holders apart.
  void pin(ManifestId id) {
    ++table_[id].refs;
    ++table_[id].readerPins;
  }
  template <typename Runs>
  bool unpin(ManifestId id, Runs& runTable) {
    if (table_[id].readerPins == 0) return false;
    --table_[id].readerPins;
    return release(id, runTable);
  }

  /// Drops one reference. At zero, releases each listed run once and frees the slot. Returns true when freed.
  template <typename Runs>
  bool release(ManifestId id, Runs& runTable) {
    Manifest& m = table_[id];
    if (!m.live || m.refs == 0) return false;
    if (--m.refs != 0) return false;
    for (uint8_t i = 0; i < m.runCount; ++i) runTable.release(m.runs[i]);
    clear(m);
    nextFree_[id] = freeHead_;
    freeHead_ = id;
    ++freeCount_;
    return true;
  }

  const Manifest& manifest(ManifestId id) const { return table_[id]; }
  uint16_t freeCount() const { return freeCount_; }
  /// The identity the next created manifest gets: every manifest with a lower identity already exists or existed.
  uint32_t nextIdentity() const { return nextIdentity_; }

  /// References a run receives from live manifests. Must equal the run's refs minus any writer reference.
  uint32_t referencesTo(RunId run) const {
    uint32_t n = 0;
    for (uint16_t i = 0; i < ManifestCount; ++i) {
      const Manifest& m = table_[i];
      if (!m.live) continue;
      for (uint8_t k = 0; k < m.runCount; ++k) n += m.runs[k] == run ? 1u : 0u;
    }
    return n;
  }

  static constexpr size_t tableBytes() { return sizeof(Manifest) * ManifestCount + sizeof(ManifestId) * ManifestCount + 8; }

 private:
  static void clear(Manifest& m) {
    m.identity = 0;
    m.revision = 0;
    m.loopLengthTicks = 0;
    for (auto& r : m.runs) r = kNoRun;
    m.refs = 0;
    m.readerPins = 0;
    m.runCount = 0;
    m.slot = 0xFF;
    m.live = false;
  }

  Manifest table_[ManifestCount];
  ManifestId nextFree_[ManifestCount];
  ManifestId freeHead_;
  uint16_t freeCount_;
  uint32_t nextIdentity_;
};

}  // namespace ContentEngine
