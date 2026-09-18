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
| Tracks | **8** today (`NUM_TRACKS = 8`), shown as banks of 8; the target is **bank switching to 16 MIDI channels** (F4) |
| Slots sounding per track | **1** today. The target is that slots behave as **virtual tracks** (16 MIDI channels x 8 slots) and can sound together whenever set active; a typical scenario may be 2 per track, but that is a working number, not a limit (F1) |
| Slots sounding simultaneously | **N, derived** -- the memory law must compute the largest N the budget supports and must **not preclude 16 / 32 / 48 / 64** sounding slots |
| Slots holding a **playback window lease** | up to **32** (2 per track) |
| Slots holding the **live capture** role (record / overdub, resumable small-packet logic inside MIDI boundaries) | **1** |
| Slots holding **deferred finalization** | **1** -- one *finalizing predecessor* alongside one *live successor*, **not** two simultaneously finalizing captures |
| Slots holding the **interactive edit / undo / display** role | **1**, unless evidence requires more |
| **Transfer overlap** | measured and explicitly budgeted |
| Slots that may be selected or browsed | the whole library (~60 recorded) |
| Transport continuity | the looper **never stops playing** across track/slot switching, record/overdub start, or undo/redo, with no MIDI timing degradation |

So the resident set is **N playback-window leases plus one of each specialized role**, where N today is 8 (8 tracks x 1 slot) and the design must not bound it artificially. While the specialized roles may stay large, the lease that gets multiplied by N is what matters, and at today's cost:

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
| **Logical active slots** | **N playback** (derived; 8 today, must not preclude 16/32/48/64) + live capture 1 + deferred finalization 1 admission (queue 4) + interactive 1 | composable leases, one per role class |
| **Resident working-set leases** | must fit PSRAM | bounded, window-sized |

The third quantity is **per class**, and the constraint is:

```
fixed  +  N x PlaybackWindowLease  +  LiveCaptureLease  +  DeferredFinalizationLease
       +  InteractiveLease  +  transfer overlap  +  reserve   <=   PSRAM budget
```

Only `PlaybackWindowLease` is multiplied by N, so only it must be small; the specialized roles may stay
large. **The plan's output is N** -- the largest sounding-slot count the budget supports, stated as a
capability (today 8 at one slot per track; the target is that 16 / 32 / 48 / 64 are reachable by policy
rather than blocked by memory).

Brownfield context:
[`docs/Runtime/CURRENT_WORK.md`](../../../docs/Runtime/CURRENT_WORK.md),
[`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md) (ownership
transfer), `openspec/specs/playback-runtime-prewarm/spec.md` (the prewarm contract this re-scopes),
`openspec/specs/lazy-slot-hydration/spec.md` (closest precedent),
`openspec/specs/loop-event-pool-admission/spec.md` (canonical capacity).

## Name and scope

**The change id names the thesis, not the whole architecture.** `runtime-working-set-ownership` is the law whose
violation justifies the change -- recorded working state must not scale with recorded-slot count -- and it is
where the plan starts. The change also carries the stages that law forces: window-derived sizing (S2), the
work-item contract (S0.6), role transfer (S4b), immutable readiness at re-entry (S4c), the standby tier (S4d),
the visual tier (S4e) and persistence-backed canonical residency (S6). The architecture they collectively build
is **Playhead-scheduled MVCC** ([`docs/Authority/NAMING.md`](../../../docs/Authority/NAMING.md)); the DEC is
[DEC-076](../../../docs/DECISION_LOG.md).

This split is deliberate (decided 2026-09-18): a change id that named the architecture noun would have to be
re-derived whenever a stage is added, whereas the thesis is stable. Read the stage list in `tasks.md` for scope.

## What Changes

- **Ownership rule.** Derived playback/capture working sets become **active-runtime-role owned**, not
  slot-lifetime owned. Inactive slots retain only canonical descriptors, loop/pass summary, persistence
  identity and location, and tiny browser metadata.
- **One uniform contract for resumable work items, and the new stages conform to it.** The tree already
  contains many small owners doing the same thing -- display cache rebuild, overlap/companion resolution,
  snapshot publication, persistence drain, session close, store rewrite, read cursors, the LCR device gate --
  and they have **independently converged on the same shape**: a `{Pending, Complete, Denied}` terminal trio
  (three headers already declare it), a `Phase` state machine, and a `required*GrainsUpperBound(...)` cost
  declaration. What they lack is a shared **identity** (`owner + generation + role`), a declared **deadline**,
  and one vocabulary, which is why the scheduler needs per-module glue. This change formalises the contract as
  header-only vocabulary and a convention, and requires **new** work items (windowed readiness, standby, visual
  envelope) to conform -- so the plan does not add a sixth variant. Retrofit of existing modules is
  opportunistic and explicitly out of scope here.
- **A two-level visual tier, reusing the display machinery already present.** Visual switching has the same
  problem as MIDI switching, and most of the answer exists: a **compact per-bar envelope** for *every* slot
  (the analogue of a pre-rendered waveform file -- ~64 B/slot, so all 64-128 slots cost ~8-16 KB, affordable
  unconditionally) plus a **capped rendered window** for slots that may be shown or switched to, reusing the
  existing bar-sliced resumable renderer (`rebuildVisualCacheIdleSlice`), the existing validity identity
  (`builtFromSnapshotIdentity` / `builtFromRevision`), the draw-source reuse keys, and the disconnected
  `TrackManager::prewarmSelectedDisplayVisualCache` hook as the entry point.
- **A bounded standby tier, scheduled against a deadline you already know.** When a recording stops, the
  loop's start tick is known, so the playhead's next arrival at every tick of that loop is known -- and the
  same is true after any mutation. Preparation can therefore be scheduled against a **known deadline** (up to a
  full loop away) instead of being urgent at the switch, and the existing deferred idle machinery is what runs
  it. The reusable artefact is a **small per-slot standby excerpt**: a pre-resolved, immutable window held for
  slots that may be switched to, so activation and queued switching need no preparation at the boundary.
  Sizing is real but not free: at the observed density (~72 events/bar, ~35 B/event) a 1-bar standby costs
  ~2.5 KB/slot (~148 KB for 60 slots), while at the representable ceiling (4,096 refs/bar) the same 1-bar
  window costs ~140 KB/slot (~8.4 MB for 60) -- so the tier is **byte-capped with eviction**, never
  "all slots unconditionally", and the window sweep decides how much of the library can be on standby.
- **Window size becomes configurable and must be swept.** Windows are bar-based today (2-bar merged playback
  window = 1,536 ticks), but **queued slot switching quantised at 1/16 or 1/32 beat for jamming** needs the
  inbound slot's immutable state ready at a ~16-31 ms boundary (12 and 6 ticks at 192 PPQN). Window size
  therefore trades resident bytes (how many slots can sound) against switch granularity against publication
  load -- the third is the one that can fail, so the size must be measured, not assumed.
- **The immutable-readiness guarantee becomes per-tick, not per-loop (F2, refined).** The invariant is:
  *after a recording, overdub or edit changes content at a tick, an updated **immutable** state must be ready
  by the time the current tick moves over that tick again.* Playback stays stable precisely because it is
  **only ever allowed to consume immutable events**. Today that immutable state is forced to be a whole loop,
  which conflicts with available memory -- and the codebase already carries a lot of machinery that bridges
  the gap as **resumable small deferred work inside MIDI boundaries**, which this change **reuses** rather
  than replaces. So a published root narrows to the **checkpoint authority for undo and save**, while the
  *playback* authority becomes the resident window/interval set, bounded by the lookahead and the affected
  intervals rather than by N whole loops. Removing the `N x whole-root` term is therefore a change of
  **unit and deadline**, not a new streaming subsystem.
- **Role tiers, not one "active".** `COLD` / `SELECTED` / `PREPARED-ACTIVE` / `CAPTURE-ACTIVE` /
  `DEFERRED`. **Plain UI selection must stay cheap**: it acquires cold metadata plus a bounded index or
  preview, never the playback/capture working set, so browsing 60 slots causes no allocation churn and
  selection latency does not depend on memory. "About to sound", arm, or an explicit preload is the cold
  admission boundary.
- **Window-derived lease sizing is on the critical path.** Capacities are derived from the resolved
  window instead of the 2048-event ceiling so a slot's cost is **independent of its loop length**, and the
  feasible resident cap is computed from PSRAM arithmetic rather than chosen as a policy guess.
- **Role classes with explicit cardinalities, and they are transferable.** Playback window leases scale with
  the envelope (N, derived from the budget); live capture is one; deferred finalization is one admission with a
  serialized queue; interactive edit/undo/display is
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
- **A generic framework, service layer, or virtual dispatch.** The contract is a vocabulary and a convention
  with concrete types and preallocated state; indirection costs ITCM/RAM1 on this target, where a single
  non-trivial constructor has already cost a 32 KB FlexRAM bank. "Reusable building blocks" here means *uniform
  contract with explicit ownership*, not message passing.
- **A big-bang refactor of the existing owners.** They keep their concrete types; they are aligned when touched.
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

## Invariants

The change is bound by the invariants listed in [`design.md`](design.md) § Invariants (30, each with its
enforcement point). They are the frame the tasks are judged against, not a summary: a task that violates one is
not complete, and an invariant with no enforcement point is recorded as a gap.

## Falsification review (open)

[`FALSIFICATION.md`](FALSIFICATION.md) attacks this plan against the current tree. **The thesis survives; the
envelope and the budget do not, as written.** Four items gate S0/S2:

- **F1 (critical)** -- "2 sounding slots per track" is a **missing capability**, not a memory question: a
  track has exactly one active/sounding slot (`activeLoopIndex`; only it may be RECORDING/OVERDUBBING), and
  no task here creates multi-slot playback. The 32x budget multiplier is therefore unearned until the
  envelope is restated (1 per track) or the capability is added as work.
- **F2 (major)** -- the budget omits a **mandatory per-sounding-slot published root**: windowed playback is
  gathered *out of* the whole-loop root (`tryGatherPublishedSnapshotEventsInWindow` returns false without
  it), ~325 KB per loop, so 32 sounding slots carry ~10 MB that no term accounts for.
- **F3 (major)** -- the finalization cardinality of 1 is asserted without citing the existing admission
  guard (`CaptureStopJobs::kCapacity = 4`, "additional admission stays guarded"): it must be verified as
  equal to today's behaviour, not assumed, since pinning one lease may be a *reduction*.
- **F4 (major, resolved below)** -- the spec *then* required "up to sixteen tracks" while `NUM_TRACKS = 8`
  and no task changed it:
  a requirement the change cannot satisfy.

F5 (the role tiers create a second authority for "active" alongside `activeLoopIndex`/`CapturePhase`/
`TrackState`) and F6 (the scaling acceptance is non-diagnostic until F1/F2 are resolved) follow.

## Open decisions (TBD)

1. **Undo history at cold.** Which of the four candidate policies in `design.md` applies, and if undo is
   retained, what its global budget is. Blocking for claiming flat cold-slot memory.
2. **Bank switching to 16 MIDI channels (F4).** The UI shows 8 tracks today; the target switches between
   8-track banks. Confirm whether that UI/bank work lands here or as its own change, while this one only
   guarantees the memory law does not preclude it.
3. **How deep the transfer overlap must be**, including whether a specialized role overlaps the playing set
   (its holder may also be one of the sounding slots). Finalization cardinality is decided at 1 with a
   `finalization_lease_stop_denied` counter as the evidence path to a second lease; live capture 1 and
   interactive 1 are confirmed.
3a. **Queue depth and serialization.** One admission with jobs run in series (`CaptureStopJobs::kCapacity = 4`
   is queue depth, F3): confirm the depth and what a *full* queue does (the denial case), and whether the
   `finalization_lease_stop_denied` counter should instead count queue waits.
4. **Whether a second finalization lease is ever warranted**, decided from `finalization_lease_stop_denied`
   evidence rather than reserved preemptively (the pathological shapes are very short captures and rapid
   punch-stop/start/stop).
5. **The SELECTED tier's content.** What bounded index/preview is cheap enough to keep for browsing.
5. **Canonical residency policy** (fixed cap, LRU over the active set, or derived from loop length) and
   whether it needs a length cap at capture admission.
5a. **Window size default and the achievable switch quantisation.** Sweep from bar-based down toward 1/32
   beat and record the smallest window that still meets the readiness deadline under load; that number also
   bounds how fine queued slot switching can be.
5b. **Retrofit scope and sequencing.** Which existing owners get the uniform identity/deadline first, and
   whether the retrofit is its own change (recommended) rather than riding on the memory work.
6. **Whether any authority genuinely requires a full published root** for an active long loop, or whether
   the bounded runtime representation suffices everywhere.
