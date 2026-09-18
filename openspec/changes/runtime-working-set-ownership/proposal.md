## Why

The target is **~60 recorded slots at up to 64 bars** (envelope: 8 tracks x 8 slots = 64, so 60 is
nearly the whole library). The current architecture cannot get there, and the recent arm failure is a
symptom rather than the problem.

**The scaling law is wrong.** Today both the runtime working state and the whole-loop derived state are
**per recorded slot, for the slot's whole lifetime**:

| Per-slot resident | Where | Measured |
|---|---|---|
| `LoopPlaybackRuntime` working set (views, capture staging, companion owners, prepared pair) | `TrackPlaybackRuntime::runtimeBySlot_`, one `LoopPlaybackRuntime` per slot | **~845 KB** reserved in one all-or-nothing `prewarmViews` chain; tagged sum for one admitted slot ~1.47 MB |
| Whole-loop `VisualCache` + published snapshot root | per `Loop` (`publishedSnapshotBox_`) | ~2.6 MB of snapshot slabs for a loaded workspace, i.e. ~325 KB per loop |
| Capture/stop capacity (staging roles, stop jobs, transfer horizon) | per slot runtime | ~500 KB of the tagged total |

so `60 x ~1.4 MB (runtime) + 60 x ~0.3 MB (derived) > 100 MB` against an **8 MB** PSRAM pool. The
current law supports on the order of **four** resident slot bundles, not sixty. Trimming 64 KiB
allocations cannot close a two-order-of-magnitude gap.

Two pieces of direct evidence from this session:

- After only a few arm attempts the pool already held **two** `TagViewPrewarm` bundles (1,430 KB) and the
  next arm could not fit its ~845 KB chain, so it exited arm entirely
  (`#MEM_DENIED,op=record_start,reason=prewarm_failed`, `#MEM_EXT_REFUSED,bytes=65536,tag=12`).
- A loaded workspace already carried 83 snapshot slabs (~2.6 MB) before any recording.

The desired law is:

```
persistent/canonical content:  scales with total saved material
runtime working state:         scales with ACTIVE tracks/slots and their current windows
not:                           every recorded slot's whole-loop derived views
```

Brownfield context:
[`docs/Runtime/CURRENT_WORK.md`](../../../docs/Runtime/CURRENT_WORK.md),
[`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md) (ownership),
[`docs/Authority/DELIVERY_RULES.md`](../../../docs/Authority/DELIVERY_RULES.md),
`openspec/specs/playback-runtime-prewarm/spec.md` (the prewarm contract this change re-scopes),
`openspec/specs/lazy-slot-hydration/spec.md` (the closest existing precedent),
`openspec/specs/loop-event-pool-admission/spec.md` (canonical capacity).

## What Changes

- **Ownership rule.** Derived playback/capture working sets become **active-runtime-role owned**, not
  slot-lifetime owned. Inactive slots retain only canonical/pass metadata, persistence identity and
  location, and bounded selection/index metadata. See the requested decision in `design.md`.
- **Acquire on activation.** A slot acquires a bounded working set when it becomes active
  (selected, about to play, armed, recording, overdubbing, or holding a live deferred owner).
- **Release on deactivation.** When a slot leaves the active set and no live playback/capture/deferred
  owner remains, its working set returns to the pool.
- **Reuse across slots.** The pool is provisioned for the **active envelope** (a configured cap, e.g.
  no more than N simultaneously working slots across the library), and capacity released by one slot is
  reused by the next. Working RAM becomes a function of `active slots`, not `recorded slots`.
- **Inactive slots drop whole-loop derived state.** A non-active slot does not keep a published
  snapshot root or a whole-loop `VisualCache`; it is re-derived (or re-hydrated from canonical content)
  on activation, on the existing lazy-hydration path.
- **Canonical content streams.** The resident event store cannot hold 60 x 64-bar loops (the chunk-pool
  ceiling is 131,072 events -- roughly 28 loops at the observed density), so canonical/event backing for
  inactive slots is loaded on demand from SD rather than held resident.
- **The arm failure is reclassified.** It becomes a symptom of the residency model, and the previously
  recorded per-owner trims become **safeguards**, not the strategy.

**Non-goals:**

- Shrinking individual buffers further as the primary strategy (useful, not sufficient).
- More denial behaviour around an over-resident model.
- Changing canonical/persistence format or SD layout in this change.
- Re-opening the snapshot representation itself (`immutable-loop-snapshot-passes` owns that); this
  change is about **who owns the working copies** and for how long.

## Capabilities

### New Capabilities

- `runtime-working-set-ownership`: derived runtime working sets are acquired by active runtime roles,
  reused across slots, and released when no live owner remains, so working RAM scales with active slots
  rather than with the size of the recorded library.

### Related Capabilities (referenced, not redefined)

- `lazy-slot-hydration`: the existing precedent for keeping an inactive slot cold and hydrating on use.
- `playback-runtime-prewarm`: its "preallocated before transport" guarantee is preserved, but scoped to
  active slots.
- `loop-event-pool-admission`: canonical event capacity; this change makes the resident set bounded.

## Impact

| Area | Primary files |
|------|---------------|
| Slot runtime ownership | `include/TrackPlaybackRuntime.h`, `src/Track/TrackPlaybackRuntime.cpp` |
| Activation/deactivation | `src/Track/TrackPlaybackWindowBuild.cpp`, `src/Track/TrackManager.cpp`, `src/Track/TrackCaptureInput.cpp` |
| Inactive-slot derived state | `include/Loop.h`, `src/Loop/LoopSnapshotLifetime.cpp`, `src/Loop/LoopVisualCache.cpp` |
| Canonical streaming | `src/StorageManager/*`, `src/StorageLoopIo.cpp` |
| Tests | new `test_runtime_working_set_ownership`; extending `test_playback_prewarm` |
| Docs | `docs/Authority/ARCHITECTURE_RULES.md` (ownership transfer), `docs/DECISION_LOG.md` (the requested DEC) |

**Apply order.** Ownership moves here require the transfer protocol in `ARCHITECTURE_RULES.md`
(reassessment + migration + compat-removal trigger) and a `DEC-###` entry. This change supersedes the
"derive smaller capacities" framing of `immutable-loop-snapshot-passes` RC-MEM-2 and reclassifies the
§5.13 arm-exit fix as a safeguard. Independent of the capture-instrumentation work
(`capture-tier-a-budget-profiles`).

**Read the RAM1 line on every task.** ~35 KB free locals but only ~870 B of padding before
`code + padding` crosses into another 32 KB FlexRAM bank.

## Open decisions (TBD)

1. **The active cap.** How many slots may hold a working set simultaneously (per track and globally)?
   16 tracks x 2 active slots is the stated envelope; the global cap and its enforcement point need
   pinning, including what happens when a third activation is requested.
2. **What an inactive slot keeps precisely.** The minimum metadata set (pass table, loop length, note
   count/index, persistence identity) versus what is cheap enough to keep as a bounded preview.
3. **Deferred-owner lifetime.** Which owners may hold a working set alive (capture stop job, deferred
   stop commit, undo roots, prepared playback handover) and for how long, so release is provably safe.
4. **Canonical residency policy.** How many loops' events may be resident at once, and whether that is
   a fixed cap, an LRU over the active set, or derived from loop length.
5. **Whether activation is eager or lazy** for a slot that is merely *selected* (about to play) versus
   one that is sounding.
