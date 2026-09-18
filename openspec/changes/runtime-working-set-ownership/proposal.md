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

**The envelope has to be pinned, and this revision pins it.** The requirement is:

| Envelope | Value |
|---|---|
| Tracks | **16** (the target envelope; `NUM_TRACKS` is 8 today -- open decision 2) |
| Slots sounding per track | **2** |
| Slots sounding simultaneously | therefore **up to 32** (2 x 16), **regardless of each slot's loop length** |
| Slots holding a **playback window lease** | up to **32** (2 per track) |
| Slots holding the **live capture** role (record / overdub, resumable small-packet logic inside MIDI boundaries) | **1** |
| Slots holding **deferred finalization** | **1** -- one *finalizing predecessor* alongside one *live successor*, **not** two simultaneously finalizing captures |
| Slots holding the **interactive edit / undo / display** role | **1**, unless evidence requires more |
| **Transfer overlap** | measured and explicitly budgeted |
| Slots that may be selected or browsed | the whole library (~60 recorded) |
| Transport continuity | the looper **never stops playing** across track/slot switching, record/overdub start, or undo/redo, with no MIDI timing degradation |

So the resident set is **up to 32 playback-window leases plus one of each specialized role**. While the specialized roles may stay large, the lease that gets multiplied 32 times is what matters, and at today's cost:

```
32 x  845 KB  ~=  27 MB
32 x 1.47 MB  ~=  47 MB
```

against ~8 MB PSRAM if every one of those 32 were a full complex working set -- which is precisely why the
budget is **not** `32 x worst-case complex lease`. **Window sizing is a prerequisite, not a safeguard**: it
must make the *playback* lease both length-independent (a 64-bar slot costs what a 4-bar slot costs, with
playback crossing window boundaries seamlessly) and small enough that 32 of them fit, while the one-or-two
specialized roles may remain considerably larger. That is the difference between a feasible envelope and
pathological micro-optimization of every operation.

Three distinct quantities are still designed separately:

| Quantity | Value | Backed by |
|---|---|---|
| **Library residency** | ~60+ recorded slots | canonical metadata + persisted content |
| **Logical active slots** | up to 32 playback + live capture 1 + deferred finalization 1 + interactive 1 | composable leases, one per role class |
| **Resident working-set leases** | must fit PSRAM | bounded, window-sized |

The third quantity is **per class**, and the constraint is:

```
fixed  +  32 x PlaybackWindowLease  +  LiveCaptureLease  +  DeferredFinalizationLease
       +  InteractiveLease  +  transfer overlap  +  reserve   <=   PSRAM budget
```

Only `PlaybackWindowLease` is multiplied 32 times, so only it must be small; the specialized roles may stay
large. If the measured playback lease cannot fit that sum, the plan states the achievable
simultaneous-playback bound (`k` per track, `k < 2`) rather than claiming 2.

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
  window instead of the 2048-event ceiling so a slot's cost is **independent of its loop length**, and the
  feasible resident cap is computed from PSRAM arithmetic rather than chosen as a policy guess.
- **Role classes with explicit cardinalities, and they are transferable.** Playback window leases scale with
  the envelope (up to 32); live capture is one; deferred finalization is one; interactive edit/undo/display is
  one unless evidence requires more. A slot may hold several classes at once, and selecting another slot or
  track **transfers the relevant role quickly** without a transport stop. Classes are composable rather than
  one monolithic lease, so 32 playback slots do not each carry fields only one live operation uses.
- **The finalization lease rotates; a second stop waits for it.** Its sufficiency does **not** rest on the
  Teensy having one core -- the finalization job is resumable and retains ownership across many service turns,
  so another capture runs while it is alive. The rotation is: `STOP A` -> A takes the finalization lease;
  switch to B and `START B` -> B takes the live-capture lease and records normally while A finalizes; A
  finishes -> releases; `STOP B` -> B takes that same lease. So the supported concurrency is **one finalizing
  predecessor + one live successor**, and a `STOP B` that arrives before A releases is **denied
  non-destructively with B still recording/overdubbing**; the next stop succeeds once the lease is free.
  Only the pathological case (very short captures, or rapid punch-stop/start/stop) is constrained. The
  initial envelope therefore does **not** pay memory for a second finalization lease: instead it counts
  `finalization_lease_stop_denied`, so a second lease can be justified by evidence if real use shows
  frequent denials.
- **The looper never stops playing.** Track/slot switching, record/overdub start and undo/redo all happen
  while playback continues, so the pool must **pre-provision the inbound role before releasing the
  outbound one**, the switch itself must be allocation-free, and it must land on a MIDI boundary. The
  transfer overlap is what the transient headroom in the budget exists for.
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
2. **16 tracks as the target.** `NUM_TRACKS` is 8 today, so the envelope implies either a config change or
   a workstream of its own (per-track runtime structures, display, UI all scale with it). Confirm the track
   count and whether it lands inside this change.
3. **How deep the transfer overlap must be**, including whether a specialized role overlaps the playing set
   (its holder may also be one of the sounding slots). Finalization cardinality is decided at 1 with a
   `finalization_lease_stop_denied` counter as the evidence path to a second lease; live capture 1 and
   interactive 1 are confirmed.
4. **Whether a second finalization lease is ever warranted**, decided from `finalization_lease_stop_denied`
   evidence rather than reserved preemptively (the pathological shapes are very short captures and rapid
   punch-stop/start/stop).
5. **The SELECTED tier's content.** What bounded index/preview is cheap enough to keep for browsing.
5. **Canonical residency policy** (fixed cap, LRU over the active set, or derived from loop length) and
   whether it needs a length cap at capture admission.
6. **Whether any authority genuinely requires a full published root** for an active long loop, or whether
   the bounded runtime representation suffices everywhere.
