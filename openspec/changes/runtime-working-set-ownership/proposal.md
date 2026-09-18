## Why

The target is **~60 recorded slots at up to 64 bars** (envelope: 8 tracks x 8 slots = 64). The
architecture cannot get there, and the recent arm failure is a symptom rather than the problem.

**The scaling law is wrong.** Runtime working state and whole-loop derived state are owned **per recorded
slot, for the slot's lifetime**, so the ~8 MB pool supports on the order of **four** resident slot
bundles while the library target is sixty:

| Per-slot resident | Where | Measured |
|---|---|---|
| `LoopPlaybackRuntime` working set (views, capture staging, companion owners, prepared pair) | `TrackPlaybackRuntime::runtimeBySlot_`, one per slot | **~845 KB** in one all-or-nothing `prewarmViews` chain; tagged total for one admitted slot ~1.47 MB |
| Whole-loop `VisualCache` + published snapshot root | per `Loop` (`publishedSnapshotBox_`) | ~2.6 MB of snapshot slabs for a loaded workspace, i.e. ~325 KB per loop |
| Capture/stop capacity (staging roles, stop jobs, transfer horizon) | per slot runtime | ~500 KB of the tagged total |

Direct evidence from this session: after a few arm attempts the pool held **two** `TagViewPrewarm`
bundles (1,430 KB) and the next arm could not fit its ~845 KB chain, so it exited arm entirely
(`#MEM_DENIED,op=record_start,reason=prewarm_failed`,
`#MEM_EXT_REFUSED,bytes=65536,tag=12`); and a loaded workspace already carried 83 snapshot slabs
(~2.6 MB) before any recording.

**The law must change from `recorded slots x whole-loop state` to `active roles x bounded window`.**

**But the law change alone is not sufficient, and this review changed the plan.** Moving from
`recorded slots` to `active slots` still multiplies today's per-slot cost by the product envelope
(16 tracks x 2 active slots):

```
32 x  845 KB  ~=  27 MB
32 x 1.47 MB  ~=  47 MB
```

against ~8 MB PSRAM. So **window-derived working-set sizing is a prerequisite of the role-pool
architecture, not a safeguard**: the lease must be small enough that the resident set fits. Three
distinct quantities must be designed separately:

| Quantity | Value | Backed by |
|---|---|---|
| **Library residency** | ~60+ recorded slots | canonical metadata + persisted content |
| **Logical active slots** | product envelope, e.g. 16 x 2 | roles/tiers -- not all necessarily resident |
| **Resident working-set leases** | must fit PSRAM | bounded, window-sized |

If window sizing cannot make the third serve all 32 logical active slots, the plan must **state the
actual simultaneous-playback cap** rather than hide it behind a "configured N".

Brownfield context:
[`docs/Runtime/CURRENT_WORK.md`](../../../docs/Runtime/CURRENT_WORK.md),
[`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md) (ownership
transfer), `openspec/specs/playback-runtime-prewarm/spec.md` (the prewarm contract this re-scopes),
`openspec/specs/lazy-slot-hydration/spec.md` (closest precedent),
`openspec/specs/loop-event-pool-admission/spec.md` (canonical capacity).

## What Changes

- **Ownership rule.** Derived playback/capture working sets become **active-runtime-role owned**, not
  slot-lifetime owned. Inactive slots retain only canonical descriptors, loop/pass summary, persistence
  identity and location, and tiny browser metadata.
- **Role tiers, not one "active".** `COLD` / `SELECTED` / `PREPARED-ACTIVE` / `CAPTURE-ACTIVE` /
  `DEFERRED`. **Plain UI selection must stay cheap**: it acquires cold metadata plus a bounded index or
  preview, never the playback/capture working set, so browsing 60 slots causes no allocation churn and
  selection latency does not depend on memory. "About to sound", arm, or an explicit preload is the cold
  admission boundary.
- **Window-derived lease sizing is on the critical path.** Capacities are derived from the resolved
  window instead of the 2048-event ceiling, and the feasible active cap `N` is computed from PSRAM
  arithmetic rather than chosen as a policy guess.
- **Generation-qualified leases.** The unit is a `WorkingSetLease` whose identity is
  `slot + generation + role`; leases transfer and release by identity, and a late cleanup from a previous
  generation cannot release or mutate storage now owned by a newer generation. This is a first-class
  invariant, not an implementation detail.
- **Undo history gets an explicit cold policy.** Undo/redo roots can hold snapshot residency alive after
  the runtime is released, so the plan cannot claim flat cold-slot memory until this is pinned. Candidate
  policies (see `design.md`): bounded global history budget, compaction to pass identities on demotion,
  sealing/dropping runtime undo on demotion, or evict-and-reconstruct. **This is a product-behaviour
  decision and is left open** rather than smuggled into memory cleanup.
- **Canonical residency is a separate gated phase.** Unloading cold canonical events and rehydrating from
  SD is a different authority boundary from derived/runtime ownership, and it changes the cold contract:
  before it a cold slot keeps its canonical events resident; after it persistence is the backing
  authority and only descriptors are resident. It stays in this change because the 60-slot target needs
  it, but behind its own architecture gate and `DEC` amendment.
- **Cold identity is a descriptor, not a resident chunk reference.** An inactive slot holds canonical
  pass identities/descriptors sufficient to locate or rehydrate persisted content -- not raw chunk
  references into a resident event store that may no longer hold them.
- **Reactivation derives only what the active consumer needs.** Demotion drops the whole-loop derived
  state, and reactivation derives the **bounded runtime representation the active consumer requires**; a
  full published root is reconstructed only if some authority actually requires one. "Drop whole root,
  then rebuild whole root on activation" must not become a permanent requirement.

**Non-goals:**

- Shrinking individual buffers *as the strategy* -- except window-derived sizing, which is promoted to a
  prerequisite (S2).
- More denial behaviour around an over-resident model.
- Changing the canonical/persistence format or SD layout.
- Re-opening the snapshot representation itself (`immutable-loop-snapshot-passes` owns that).

## Capabilities

### New Capabilities

- `runtime-working-set-ownership`: window-sized runtime working sets are leased to active runtime roles
  under generation-qualified identity, reused across slots, released when no live owner remains, so
  working RAM scales with bounded active roles rather than with the recorded library.

### Related Capabilities (referenced, not redefined)

- `lazy-slot-hydration`: the existing precedent for cold slots and on-use hydration.
- `playback-runtime-prewarm`: its before-transport guarantee is preserved, scoped to active roles.
- `loop-event-pool-admission`: canonical capacity; this change makes the resident set bounded.

## Impact

| Area | Primary files |
|------|---------------|
| Lease identity and pool | `include/TrackPlaybackRuntime.h`, `src/Track/TrackPlaybackRuntime.cpp` |
| Role tiers / activation | `src/Track/TrackPlaybackWindowBuild.cpp`, `src/Track/TrackManager.cpp`, `src/Track/TrackCaptureInput.cpp`, `src/DisplayManager/*` (selection) |
| Window-derived sizing | `src/Track/RuntimeCompanion*.cpp`, `src/Track/RuntimeCanonicalPassInputs.cpp`, `include/RuntimePlaybackView.h` |
| Inactive derived state | `include/Loop.h`, `src/Loop/LoopSnapshotLifetime.cpp`, `src/Loop/LoopVisualCache.cpp` |
| Canonical residency (gated) | `src/StorageManager/*`, `src/StorageLoopIo.cpp`, `src/LoopEventStore.cpp` |
| Undo cold policy | `src/TrackUndo.cpp`, `src/PassReclaim.cpp` |
| Tests | new `test_runtime_working_set_ownership`; extending `test_playback_prewarm` |
| Docs | `docs/Authority/ARCHITECTURE_RULES.md`, `docs/DECISION_LOG.md` |

**Apply order.** The ownership move needs the transfer protocol in `ARCHITECTURE_RULES.md` plus a
`DEC-###`; the canonical phase needs its own `DEC` amendment. This change supersedes
`immutable-loop-snapshot-passes` RC-MEM-2's strategy framing, absorbs its outstanding sizing item as
prerequisite S2, and reclassifies §5.13 as a symptom.

**Read the RAM1 line on every task** (~870 B of padding before `code + padding` crosses a FlexRAM bank).

## Open decisions (TBD)

1. **Undo history at cold.** Which of the four candidate policies in `design.md` applies, and if undo is
   retained, what its global budget is. Blocking for claiming flat cold-slot memory.
2. **The active cap `N` and its arithmetic.** Derived from S2's measured lease size; must be stated as a
   capability bound (how many slots can sound at once) rather than a configured number.
3. **The SELECTED tier's content.** What bounded index/preview is cheap enough to keep for browsing.
4. **Canonical residency policy** (fixed cap, LRU over the active set, or derived from loop length) and
   whether it needs a length cap at capture admission.
5. **Whether any authority genuinely requires a full published root** for an active long loop, or whether
   the bounded runtime representation suffices everywhere.
