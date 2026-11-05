//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The content engine's storage core at production capacity (fixed-memory-engine milestone E1): page pool, global run
// and manifest tables and the row-reservation ledger, plus the conservation check and the region census.
//
// Placement: the pool is one fixed object for EXTMEM and the tables are fixed objects for RAM2 (DMAMEM); neither region
// is zeroed at reset, so `Core::init()` must run once before any use. Instances are defined by the integration
// milestone; this header only fixes types, sizes and checks.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/Composer.h"
#include "ContentEngine/EditIndexStore.h"
#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/ManifestTable.h"
#include "ContentEngine/PagePool.h"
#include "ContentEngine/RowReservations.h"
#include "ContentEngine/RunTable.h"

namespace ContentEngine {

/// Pages, runs and manifests agree: the free stack holds exactly the free pages; every held page is tagged with the
/// run that lists it and each run's held count equals its tag count; no page is tagged by a free descriptor; every
/// live run carries at least the references live manifests give it. O(pages + runs × manifests); tests only.
template <typename Pool, typename Runs, typename Manifests>
bool checkConservation(const Pool& pool, const Runs& runs, const Manifests& manifests) {
  if (!pool.checkFreeStack()) return false;
  uint32_t tagged[Runs::kRunCount] = {};
  uint32_t other = 0;
  for (PageId p = 0; p < Pool::kPageCount; ++p) {
    const PageOwner o = pool.ownerOf(p);
    if (o == kOwnerFree) continue;
    if (o < Runs::kRunCount)
      ++tagged[o];
    else
      ++other;
  }
  uint32_t held = 0;
  for (RunId id = 0; id < Runs::kRunCount; ++id) {
    const auto& r = runs.run(id);
    if (r.state == RunState::Free) {
      if (tagged[id] != 0) return false;
      continue;
    }
    if (tagged[id] != runs.heldPages(id)) return false;
    held += tagged[id];
    if (r.state != RunState::Reclaiming && r.refs < manifests.referencesTo(id)) return false;
  }
  return pool.freeCount() + held + other == Pool::kPageCount;
}

/// Production capacities (plan revision 3 § 4, § 5).
using ProductionPool = PagePool<Config::kPageCount, Config::kRowsPerPage>;
using ProductionRuns = RunTable<Config::kRunCount, ProductionPool>;
using ProductionManifests = ManifestTable<Config::kManifestCount, Config::kRunsPerManifest>;
using ProductionReservations = RowReservations<ProductionPool, ProductionRuns>;

/// Region sizes the census reports and the budget checks pin.
struct RegionBytes {
  size_t poolPages;
  size_t poolMetadata;
  size_t runTable;
  size_t manifestTable;
  size_t reservations;
};

constexpr RegionBytes productionRegions() {
  return {ProductionPool::storageBytes(), ProductionPool::metadataBytes(), ProductionRuns::tableBytes(),
          ProductionManifests::tableBytes(), sizeof(ProductionReservations)};
}

// Budget pins (plan revision 3 § 4.1 / § 4.2). A change here is a budget change and must update the plan clause.
static_assert(ProductionPool::storageBytes() == 4176896u, "page pool data is 4,079 x 1 KiB (one writer, two edit runs)");
static_assert(ProductionPool::metadataBytes() <= 16386u, "free stack + owner tags");
static_assert(sizeof(ProductionRuns::Run) <= 128u, "RAM2 budget assumes <= 128 B per run descriptor");
static_assert(sizeof(ProductionManifests::Manifest) <= 148u, "RAM2 budget assumes <= 148 B per manifest");
static_assert(ProductionRuns::kMaxPagesPerRun >= Config::kContentQuotaPages,
              "one run may hold the whole content quota through its directory pages");

/// Composition (E3.3): 18 compose cursors (16 playback + 2 display) at <= 12 KiB each (221,184 B budget); the edit-hide
/// index is shared per manifest identity, one store of 18 slots (each cursor pins at most one), not per cursor.
using ProductionComposer = Composer<ProductionPool, ProductionRuns>;
using ProductionEditIndex = EditIndexStore<ProductionPool, ProductionRuns>;
constexpr uint8_t kComposeCursors = 18;
static_assert(sizeof(ProductionComposer) <= 12288u, "a compose cursor stays within its 12 KiB budget");
static_assert(kComposeCursors * sizeof(ProductionComposer) <= 221184u, "compose cursors fit the 221,184 B budget");
static_assert(ProductionEditIndex::kSlots >= kComposeCursors, "every cursor can pin a distinct identity: Busy is unreachable");
static_assert(ProductionEditIndex::kPages == Config::kIndexReservePages,
              "the shared index is exactly the pool's edit-index partition (E5.3)");
static_assert(sizeof(ProductionEditIndex) <= 4096u, "the store object holds headers and page lists only");
/// Init order (E5.3): pool, then the edit-index store (takes its partition, tagged kOwnerIndex), then the reservation
/// ledger with only the compaction partition as fixed reserve: the index pages are already off the free stack.
constexpr uint32_t kLedgerFixedReservePages = Config::kCompactionReservePages;

/// Restart checkpoints (E5): one table per slot, 8 checkpoints at 8-bar spacing (plan § 4.1: 262,144 B).
// Restart checkpoints (owner decision 2026-11-05, E6.2d): per slot the current source's set and one historical set
// (least recently used), 8 checkpoints each; about 416 KB intended.
using ProductionCheckpoints = CheckpointTable<ProductionComposer::Checkpoint, Config::kSlotCount, 8, 2>;
static_assert(sizeof(ProductionCheckpoints) <= 425984u, "checkpoint sets fit the owner's ~416 KB PSRAM budget");

}  // namespace ContentEngine
