## Context

Neither of the two per-slot residencies survives the 60-slot target. `TrackPlaybackRuntime` holds one
`LoopPlaybackRuntime` per slot (`runtimeBySlot_`, `MAX_LOOPS_PER_TRACK = 8` x `NUM_TRACKS = 8` = 64),
allocated lazily on first use and destroyed only with the runtime itself; and every `Loop` holds a
whole-loop `VisualCache` plus a `publishedSnapshotBox_`. Trimming individual allocations inside those
structures cannot change the scaling law.

The requested decision:

> **Derived runtime views and capture companion storage are owned by active runtime roles, not
> permanently by loop slots. Inactive slots retain only canonical/persistence state and bounded
> metadata. Runtime working sets are acquired on activation/admission, reused across slots, and released
> when no live playback/capture/deferred owner remains.**

This change requests that decision as a `DEC-###` entry and implements it. Ownership moves of this size
require the transfer protocol in `docs/Authority/ARCHITECTURE_RULES.md` (reassessment, migration,
compatibility-removal trigger) -- not an incidental refactor.

## The scaling law, quantified

| Quantity | Value | Source |
|---|---|---|
| Slot envelope | 64 (8 tracks x 8 slots) | `include/Globals.h` |
| Target | ~60 slots, up to 64 bars | user requirement |
| One admitted slot's tagged runtime total | ~1.47 MB | `tags=...` in the RC-MEM-2 A/B (`ViewPrewarm` 715 + `CaptureStaging` 148 + `CanonicalPassInputs` 144 + `CompanionPasses` 96 + `CaptureStopJobs` 87 + `BridgeMap` 68 + `Apply` 66 + `Reconstruction` 64 + `PlaybackOrder` 58 + small) |
| One record-arm chain | ~845 KB | the §5.13 diagnosis |
| Whole-loop derived per loop | ~325 KB | 83 snapshot slabs (~2.6 MB) for a loaded workspace |
| PSRAM pool | ~8 MB | `pool=8182 KB` |
| **Resident slot bundles the pool supports** | **~4** | 8 MB / ~2 MB per slot including derived |
| Chunk-pool ceiling | 131,072 events (512 x 256) | `LoopEventStoreConfig` |
| 64-bar loop at observed density | ~4,600 events (72/bar x 64) | record 384 + overdub 157 + 35 over 8 bars |
| **Loops the resident event store can hold** | **~28** | 131,072 / 4,600 |

So the gap is not a few hundred kilobytes: both the runtime working state and the resident canonical
event store are slot-scaled where they must be active-scaled or streamed.

## Ownership split

**Inactive recorded slot (cold):**

- canonical pass metadata and chunk references;
- loop length, note/event counts, pitch and bar index sufficient to select and plan a load;
- persistence identity and location;
- a bounded preview/index if one is needed for the browser.

**Not retained while inactive:** the runtime view bundle, companion reconstruction/apply/bridge-map
capacity, canonical pass-input scratch, transfer horizon, capture staging roles, stop-job capacity, the
published snapshot root, and the whole-loop `VisualCache`.

**Active slot (selected / armed / recording / overdubbing / sounding / holding a live deferred owner):**
acquires a bounded working set from a pool provisioned for the **active envelope**, not the library.

**On leaving the active set:** the working set is released back to the pool once no live owner remains,
and the next activation reuses that capacity. The pool therefore has a fixed ceiling and a bounded
number of simultaneous holders.

## Options considered

| # | Option | Verdict |
|---|--------|---------|
| A | Trim individual allocations (window-derived capacities, block the 64 KiB reconstruction buffer, partial-fit arm admission) | **Safeguard, not strategy.** Reduces the per-slot constant; does not change `slots x whole-loop` into `active x window`. Keeps the recorded RC-MEM-2 / §5.13 value. |
| B | **Active-role-owned, reusable working sets** (+ inactive slots demoted to canonical + metadata) | **This change.** Changes the law: working RAM becomes a function of active slots. |
| C | Evict under pressure only (reclaim the largest idle bundle when the pool is tight) | **Insufficient alone.** Non-deterministic: memory becomes a function of history rather than of the active set, and the 60-slot target still cannot be reasoned about. May be a stepping stone, but not the architecture. |
| D | More denial behaviour around the existing model | **Rejected as a strategy.** Makes failures graceful; leaves the residency unchanged. |
| E | Keep everything resident and stream/compress harder | **Rejected.** Cannot fit 100+ MB of designed residency into 8 MB by compression; the residency itself has to change. |

## Migration shape (staged, each independently reviewable)

1. **Measure the law first.** Instrument per-slot and per-loop residence (working-set bytes by owner,
   per-loop derived bytes) and assert the *scaling* claim -- boot and steady-state memory as the library
   grows -- before changing ownership. Without this the change cannot be shown to have worked.
2. **Introduce the role pool** and the acquire/release/recycle lifecycle, with the live-owner guard, but
   keep the existing residency behaviour as the acquire path (i.e. no functional change yet).
3. **Release on deactivation** once the live-owner guard is proven (capture stop jobs, deferred stop
   commit, undo roots, prepared handover).
4. **Demote inactive slots**: drop the published snapshot root and whole-loop `VisualCache` for slots
   that leave the active set, re-deriving on activation via the lazy-hydration path.
5. **Bound canonical residency** and stream inactive loops' events from SD.
6. **Run the scaling test** (below) as the acceptance gate.

## Risks and their mitigations

- **Releasing while a deferred owner still needs the buffer.** The guard must be an explicit owner
  predicate, not a heuristic: capture stop job ownership, deferred stop commit, undo/redo roots and the
  prepared playback handover each declare whether they hold the slot's working set.
- **Latency on activation.** Re-acquiring a working set on selection could add work to a
  transport-adjacent boundary. The existing prewarm contract (`playback-runtime-prewarm`) says
  allocation happens before transport uses it; activation must therefore be a *cold* boundary, or the
  pool must guarantee the active envelope is always satisfiable without allocation.
- **Re-derivation cost for a demoted slot.** Dropping the published root and visual cache means an
  inactive slot that becomes active again must re-derive them; this is the same work lazy hydration
  already does, but it must not land on a timing-critical edge.
- **Correctness of "no live owner".** A wrong predicate is a use-after-free. The guard needs a native
  fixture per owner type, and a device soak.

## Verification

**The scaling test is the acceptance gate**, and it is a memory test, not a content test:

```
load/create 60+ long slots (up to 64 bars)
  -> boot and steady-state memory stay roughly flat apart from canonical metadata/content

activate slot A            -> one bounded working set appears
activate slot B            -> a second appears
deactivate A               -> A's working set returns to the pool
activate C                 -> C reuses A's capacity (no net growth)
```

Reported per step: pool `live`, per-owner tagged bytes, working-set holder count, and the per-loop
derived bytes. The change is only complete when the curve is flat in *recorded* slots and stepped in
*active* slots.

Plus: native fixtures for the acquire/release/recycle lifecycle and for each deferred-owner guard;
existing playback-prewarm and lazy-hydration gates unchanged; a HITL run proving a demoted slot
re-activates and plays its content exactly (content parity via the existing content-verification
pipeline).

## Relationship to the earlier work

- `immutable-loop-snapshot-passes` RC-MEM-2 (whole-take reservation) and RC-MEM-5 (lazy chunk pool)
  remain valuable and are **not** reverted: they bought headroom and removed dead reservation.
- RC-MEM-2's outstanding item ("derive the per-window capacity from the resolved window, and prewarm
  only the armed slot") is **absorbed here** as part of the per-slot constant reduction, i.e. option A.
- `immutable-loop-snapshot-passes` §5.13 (record arm exits) is reclassified as a **symptom**; its three
  fix items stay as safeguards and are not the strategy.
