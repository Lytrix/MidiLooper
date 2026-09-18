## Context

Two per-slot residencies break the 60-slot target. `TrackPlaybackRuntime` holds one `LoopPlaybackRuntime`
per slot (`runtimeBySlot_`; `MAX_LOOPS_PER_TRACK = 8` x `NUM_TRACKS = 8` = 64), allocated lazily and
destroyed only with the runtime; and every `Loop` holds a whole-loop `VisualCache` plus a
`publishedSnapshotBox_`. Trimming allocations inside those structures cannot change the law.

The requested decision, **including the revisions from review**:

> **Derived runtime views and capture companion storage are owned by active runtime roles, not
> permanently by loop slots. Inactive slots retain only canonical/persistence state and bounded
> metadata. Runtime working sets are window-sized, leased on activation/admission under
> generation-qualified identity, reused across slots, and released when no live playback/capture/deferred
> owner remains.**

An ownership move of this size needs the transfer protocol in `docs/Authority/ARCHITECTURE_RULES.md` and
a `DEC-###`; the canonical-residency phase needs its own `DEC` amendment.

## Invariants (binding on this change)

Distilled from the design work; the owning authority for each is cited so none of these is decorative. A task
below is not complete if it violates one, and any invariant without an enforcement point is a gap, not a note.

| # | Invariant | Enforced by |
|---|---|---|
| 1 | Working memory scales with **active roles and bounded windows**, never with recorded-slot count or loop length | spec "library growth does not grow working memory"; S2.3 |
| 2 | **Loop length must not change resident cost** (64 bars ~= 4 bars) | spec length-independence scenario; S2.5 |
| 3 | Every capacity number is an **output** of arithmetic plus measurement, never an assertion | S2.3, S2.4, S2.7, S2.8 |
| 4 | Every resident store declares **class, cardinality, cap and eviction policy** | spec standby/visual caps; S4d.3, S4e.2 |
| 5 | A **bounded exception** is named and capped, or it becomes the residency that was removed | spec standby and envelope requirements |
| 6 | Identity is `owner + generation + role`; release is by identity; a mismatched release is a rejected no-op | spec generation-qualified leases; S4.2 |
| 7 | Release requires **proof no live owner remains**; liveness is explicit per owner kind | spec release requirement; S3.2 |
| 8 | **Selection is cheap**; only about-to-sound / arm / preload admits a working set | spec selection requirement; S3.3 |
| 9 | Inactive state is **descriptors + bounded metadata + capped envelope** | spec inactive-slot requirement; S4e.1 |
| 10 | Output paths consume **immutable state only** | spec "never reads a partially updated representation" |
| 11 | Immutable readiness is owed **per affected tick, before the playhead returns to it** | spec readiness requirement |
| 12 | A missed deadline keeps the **previous version** authoritative for that pass: never torn, held or timing-deferred | spec scenario; S4c.3 (decided) |
| 13 | Currency is decided by **coverage/identity**, not a timestamp or a "saved" flag | DEC-073 coverage contract (`publishedSnapshotIsCurrent`) |
| 14 | **Durability gates eviction**; dirty pins residency; persistence failure reduces capacity, never discards accepted input | spec dirty-generation requirement; S6.1a, S6.2a |
| 15 | The looper **never stops playing** | spec role-transfer requirement; S7.5 |
| 16 | **No allocation and no blocking on a timing-critical edge** | existing hot-path rule; S4c.0 and S3 gates |
| 17 | Role transfer provisions the inbound owner **before** releasing the outbound one, allocation-free at the switch | spec transfer scenario; S4b.2 |
| 18 | Failed admission is **non-destructive**: no partial authority, state preserved, reported as wait/denial | spec non-destructive admission; G.2 |
| 19 | An occupied role **denies non-destructively**; the wait is reported and the owner's state is preserved (never a silent drop, never a partial stop) | spec finalization denial scenario; S4.5 |
| 20 | Transient pressure **parks, never aborts** | DEC-075 (landed) |
| 21 | Budget instrumentation is **live and attributed** per owner/class, not cumulative and aggregate | slab census (landed); spec coverage-report requirement |
| 22 | Verdicts declare required evidence; degraded evidence yields **DEGRADED, never an unconditional PASS** | spec host-verdict requirement; harness rule |
| 23 | Degradation is **counted and named**; only unrecoverable loss is fatal | RC-MEM-7 (landed); spec scenarios |
| 24 | Instrumentation **may not perturb what it measures** (FLASH-resident, allocation-free, deliberate drain policy) | existing capture gate |
| 25 | **Measure before designing** -- a premise is a hypothesis until measured | S1 red characterisation |
| 26 | **Falsify the plan against the tree before building**; an unsatisfiable requirement is a defect | FALSIFICATION.md; S0 gate |
| 27 | Distinguish a **product target** from an existing capability; never price the former as the latter | FALSIFICATION F1; S0 envelope |
| 28 | **Reuse before invention**; duplication is a recorded failure mode | S4c.2, S4e.4, S0.6 |
| 29 | **One owner per concept**; a second authority for the same concept is a defect | Revision 12 (F5); S0.1 |
| 30 | **Retrofit opportunistically**; never big-bang a timing-path owner | S0.6 non-goal |

Vocabulary for 1-14 is governed in [`docs/Authority/NAMING.md`](../../../docs/Authority/NAMING.md) under
**Playhead-scheduled MVCC** and **Pipeline**.

## Revision 1 — the envelope, and why windowing is a prerequisite

The requirement, pinned:

| Envelope | Value |
|---|---|
| Tracks | **8** today; target **bank switching to 16 MIDI channels** (F4) |
| Slots sounding per track | **1** today; target is slots-as-virtual-tracks, sounding together when active (F1) |
| Slots sounding simultaneously | **N, derived** -- must not preclude 16 / 32 / 48 / 64 |
| Slots holding the complex role (record / overdub / passes / undo / redo / display, resumable small-packet logic inside MIDI boundaries) | **exactly 1**, transferable |
| Transport | **never stops** -- switching, record/overdub start and undo/redo happen while playing |

So the resident set is **N playing leases plus the specialized roles** plus transfer overlap. Today N = 8
(8 tracks x 1 sounding slot); the design must not cap it artificially, and at today's cost even N = 8 full
working sets is 8 x 845 KB ~= **6.8 MB**, or 8 x 1.47 MB ~= **11.8 MB**, against ~8 MB -- so windowing is
required at the *current* capability, not only at a future one. It must also make the cost
length-independent: a 64-bar slot costs what a 4-bar slot costs, with playback crossing window boundaries
seamlessly. Consequently:

| Quantity | Value | Backed by |
|---|---|---|
| Library residency | ~60+ recorded slots | canonical descriptors + persisted content |
| Logical active slots | **N sounding, derived** (8 today: 8 tracks x 1) | role tiers (not all necessarily resident) |
| Resident working-set leases | must fit PSRAM | bounded, window-sized |

**The arithmetic that must close before `N` is chosen:**

```
fixed PSRAM overhead
+ canonical resident allowance
+ N x worst-case window lease
+ transient / deferred headroom
<= safe PSRAM budget
```

**The budget is per class, not per worst-case slot.** Conflating them would demand squeezing every 845 KB
operation down to ~100 KB, which is both unnecessary and the wrong target:

```
fixed  +  32 x PlaybackWindowLease   (the only term multiplied 32 times)
       +  LiveCaptureLease           (1)
       +  DeferredFinalizationLease  (1, concurrent with a live successor)
       +  InteractiveLease           (1, edit / undo / display)
       +  transfer overlap           (measured, explicit)
       +  canonical allowance  +  safety reserve
       <=  PSRAM budget
```

So `PlaybackWindowLease` must be small and length-independent, while the specialized extensions may remain
sizable. S2 measures each class separately. If the playback term cannot fit the sum, the honest outcome is a
derived simultaneous-playback bound rather than a claim that the envelope is met. Today is **8 tracks x 1
sounding slot**; multi-slot-per-track playback is a future capability this law must not preclude, and the
budget projects toward N = 16/32/48/64 rather than asserting it.

## Revision 8 — the never-stop transfer requirement

The primary goal is continuity: **the looper must never be required to stop playing**, and switching
tracks/slots, starting record/overdub, and undo/redo must not degrade MIDI timing. That makes role
transfer a first-class mechanism rather than a consequence of the pool:

```
pre-provision inbound role   (cold boundary, allocation permitted)
        |
atomic switch at a MIDI boundary   (allocation-free; no transport stop)
        |
outbound role released only after the last live owner retires
```

- The same logic -- the resumable small-packet path already used at MIDI boundaries -- serves
  record/overdub/passes/undo/redo/display, so a switch **transfers the role** rather than reconstructing it.
- Transfer must **not steal resources from a still-live capture or deferred owner**: the inbound role is
  provisioned *in addition to* whatever stays pinned, which is exactly what the overlap term budgets.
- The switch path must perform **no allocation**: the inbound lease comes from the pool, which is why the
  pool must be provisioned for the envelope *plus* overlap.
- The **transfer overlap is the transient headroom** in the budget arithmetic; it is not slack.
- A switch is a **cold admission boundary** by construction (it happens before the boundary it lands on),
  which is what keeps it compatible with the no-allocation-on-a-timing-edge rule.
- The existing MIDI-boundary verifier (`call_site_gate_ok`, `overflow_free`, active-interval overflow
  check) is the acceptance instrument for "no timing degradation".

## Revision 2 — role tiers: selection must stay cheap

Do not let one "active" state acquire the large bundle on plain UI selection, or browsing 60 slots
creates allocation/release churn and makes selection latency memory-dependent.

| Tier | Retains | Acquires |
|---|---|---|
| `COLD` | persistence identity, loop/pass summary, tiny browser metadata | -- |
| `SELECTED` | cold metadata | bounded index/preview only; **no playback or capture working set** |
| `PREPARED-ACTIVE` | + identity of the lease | bounded playback window lease, acquired **before sounding** |
| `CAPTURE-ACTIVE` | + capture identity | playback lease **plus** capture/deferred resources |
| `DEFERRED` | only what the deferred job actually owns | pins the lease it needs until retirement |

The cold admission boundary is therefore "about to sound", arm, or an explicit preload -- never selection.

## Revision 3 — generation-qualified leases (first-class invariant)

The pool must not become `runtimeBySlot_` under a new name. Authority is a lease identity:

```
WorkingSetLease { owner = slot + generation + role }
```

and it is the invariant in the DEC that leases transfer and release **by identity**, because the failure
mode this design exists to prevent is stale cleanup:

```
slot A lease generation 17
A releases
slot C reuses the same storage as generation 18
late cleanup from A -> must NOT release or mutate generation 18
```

Each owner kind (capture stop job, deferred stop commit, undo/redo root, prepared playback handover)
declares liveness explicitly; a release whose generation does not match is a rejected no-op, reported.

## Revision 4 — cold undo policy: hot roots compact to logical descriptors (DECIDED)

Undo/redo roots can keep snapshot residency alive after the runtime is released, so `60 cold slots x several
descriptor-backed roots` defeats the law even with the playback runtime released. Dropping undo on demotion
would solve memory but break a live session's expectation (record an overdub, switch away, switch back, and
Undo no longer works) -- and the persistence contract already accepts zero undo depth after reboot, so the
compromise is unnecessary.

**Decision: two-tier undo.**

| Tier | Contents |
|---|---|
| **Hot** (active / prepared slot) | `HotUndoEntry`: before/after snapshot or root refs allowed, immediate undo/redo, normal bounded hot depth |
| **Cold** (slot left the active set) | `ColdUndoEntry`: committed pass-selection identity, required edit/macro identity, revision/generation metadata. **No `shared_ptr<LoopSnapshot>`, no snapshot-slab ownership.** |

On demotion, hot entries are **compacted** to cold descriptors and every undo/redo root belonging **only** to
that slot is released. On reactivation, the stack is **not** eagerly reconstructed: only the slot's *current*
musical state is prepared for playback, and historical states stay compact until an Undo/Redo actually requests
one -- which reconstructs or selects that historical state **off-clock** and then publishes a fresh runtime
root/window. So the invariant is:

> **Undo history scales with logical history metadata, not with immutable snapshot representation size.**

**Global bounds (compact descriptors are not unbounded either).** Two limits are pinned: a **per-slot cold
undo depth** and a **device-wide cold-undo byte budget**. Under pressure the **oldest cold undo entries** are
evicted first. The eviction priority, most-protected first:

```
current musical state        never evicted by undo pressure
dirty / unpersisted canonical  never evicted
active hot undo              preferred
recent cold undo             retained within budget
oldest cold undo             first pressure victim
```

That finally gives `overUndoMemoryPressure` something meaningful to manage (RC-MEM-3) without using undo
trimming to hide other memory problems.

**Redo has an explicit rule.** Redo history is preserved compactly too, and **cold demotion alone does not
invalidate it** -- otherwise merely browsing away from a slot would change editing semantics. Redo is
invalidated by a **new committed edit or capture**, as normal.

**A cold descriptor describes authority, not a cached root.** It must not carry a snapshot pointer, a slab id
or a `playbackRevision` as its claim to be authoritative. It carries enough to *reproduce* the logical selected
state -- the committed pass identity/set, pass ordering or selection revision, the note-edit commit identity
where applicable, and the capture/session generation needed for validation. The exact field set needs an audit
of `TrackUndo` (S5.2a); the principle is:

> **A cold undo entry says which canonical state to select or reconstruct, not which runtime snapshot object
> to resurrect.**

This aligns with DEC-073: a reconstructed root acquires a **truthful** `coveredPassRevision`, and the old
runtime root is never treated as authority.

**Failure rule (prepare then commit).** If reconstruction cannot be admitted because memory is temporarily
unavailable:

```
Undo request -> remains unapplied
             -> current musical state unchanged
             -> explicit `undo_resource_denied`
```

The cold descriptor is **never consumed or popped before reconstruction succeeds**. This is the same
prepare-then-commit pattern the rest of the change uses.

**Not preserved:** runtime undo history is not persisted; load continues with **undo depth zero** unless that
is deliberately changed later.

## Revision 5 — S1's scaling assertion must be RED first

Today's architecture must **fail** the scaling assertion -- that is the bug being fixed. The native
fixture is therefore a characterisation/detector, not a green test under "no behaviour change":

```
current:            recorded slot count up  ->  working-set residence up
target after S5/S6: recorded slot count up  ->  working-set residence flat
```

The same fixture is reused as the green acceptance test after implementation.

## Revision 6 — the 60-slot device baseline is not available yet

The resident event store holds roughly 28 sixty-four-bar loops (131,072-event ceiling / ~4,600 events at
the observed density), so a 60-slot device baseline cannot be produced before the canonical phase. S1
therefore records **the slope and the failure point** as the library grows, plus a synthetic
metadata-only 60-slot characterisation; the genuine 60+ x 64-bar device acceptance runs after S6. A
prerequisite must not depend on a capability the change exists to create.

## Revision 7 — canonical residency is a separate gate, and it changes the cold contract

S2-S5 are derived/runtime ownership. S6 is canonical content residency and SD-backed activation: another
authority boundary, and a larger semantic transition than it first appears:

```
before S6:  cold slot -> canonical events resident, derived runtime absent
after  S6:  cold slot -> canonical event body possibly NOT resident;
                        persistence is the backing authority;
                        only descriptors/identity resident
```

Consequences already folded into the proposal and spec:

- the cold contract says **canonical pass identities/descriptors sufficient to locate or rehydrate
  persisted content**, not "chunk references" -- a raw chunk reference cannot be the durable cold
  identity once the resident store may not hold that chunk;
- **reactivation derives only the bounded runtime representation the active consumer needs**; a full
  published root is reconstructed only if some authority requires one, so "drop whole root, rebuild whole
  root" does not become a permanent requirement;
- S6 carries its own architecture gate and `DEC` amendment rather than being one more task.

## Revision 9 — role classes and cardinalities (preserving successor re-entry)

The original "exactly one complex role" phrasing was incompatible with what is already landed: DEC-067 supports a
**deferred/finalizing predecessor (A) alongside an independently live recording successor (B)**
simultaneously, so the runtime cannot have one indivisible record/overdub/passes/undo/redo/display role.
The architecture already distinguishes live capture ownership from deferred finalization ownership and
allows both concurrently; the new model must not design that away.

| Role class | Cardinality | Notes |
|---|---|---|
| `PlaybackWindowLease` | up to **N** (derived; 8 today) | the term multiplied by N; must be small and length-independent |
| `LiveCaptureLease` | **1** | record/overdub ingress, staging, capture stop jobs |
| `DeferredFinalizationLease` | **1** | one finalizing predecessor alongside one live successor; a second stop while it is occupied is **denied non-destructively** (DEC-067) |
| `InteractiveLease` | **1** | edit / undo / redo / display, unless evidence requires more |
| `TransferOverlap` | measured | explicitly budgeted, not slack |

Classes are **composable**: a slot may hold several at once, and each owns only the resources its operation
needs. Assignment is generation-qualified per class, so releasing a playback lease cannot disturb a live
capture or deferred finalization lease on the same slot.

### Why one finalization lease is sufficient

Not because the Teensy has one core -- the finalization job is **resumable** and retains ownership across many
service turns, so another capture runs while it is still alive. One lease suffices because of the rotation:

```
A recording/overdub -> STOP A -> A acquires the one DeferredFinalizationLease
switch to B         -> START B -> B acquires LiveCaptureLease; B records while A finalizes
A finishes          -> releases DeferredFinalizationLease
STOP B              -> B acquires that same lease
```

Supported concurrency is therefore **1 deferred finalization + 1 live recording/overdub**, without
provisioning two copies of the canonical/bridge/reconstruction machinery. The edge case:

```
STOP A (A still finalizing) -> START B (accepted) -> STOP B before A finishes
```

Under the same non-destructive contract as failed admission, **B's stop is denied and B remains
recording/overdubbing**; once A releases, a subsequent B stop request **may** succeed -- but only if no other
single-instance resource is unavailable at that later instant. Normal use (record for at least a few
seconds after a switch) will have A finished long before B stops; only very short captures and rapid
punch-stop/start/stop sequences are constrained.

The initial envelope therefore pays for **one** finalization lease and adds the counter
`finalization_lease_stop_denied`. If real usage shows frequent denials, a second lease is evaluated **from
that evidence** rather than reserved preemptively.

## Revision 10 — dirty canonical generations pin residency (S6 durability rule)

Once persistence becomes the backing authority, memory pressure must never be able to discard accepted
input. The rule:

> **Canonical content may be evicted only after persistence has durably acknowledged the exact canonical
> generation being evicted. Dirty/unpersisted content pins canonical residency. Failure or delay of
> persistence reduces available residency capacity; it never discards accepted input.**

```
cold + clean/durable      -> eligible for eviction
cold + dirty (unpersisted)-> resident / pinned
save completes for gen G  -> G becomes eviction-eligible
new mutation gen G+1      -> pinned again
```

This is generation identity, not "slot saved": the acknowledgement must name the generation, so a save that
completes for G cannot make G+1 evictable. It belongs in the S6 `DEC` amendment, and it has an adversarial
test (dirty -> demote -> storage deliberately delayed -> pressure requests eviction -> content stays
resident; save completes -> evictable; reload exact).

## Revision 11 — immutable readiness at re-entry, not whole-loop publication (F2, refined)

The requirement is not "streaming" as such. It is a **readiness deadline on an immutable representation**,
and this is the form that must be designed against:

> **After a recording, overdub or edit changes content at a tick, an updated immutable state must be ready by
> the time the current tick moves over that tick again.** Playback may only ever consume immutable events --
> that is *why* the immutable state exists, and it is what keeps MIDI output stable.

Two consequences follow, and they are the memory answer:

1. **The unit of authority becomes an interval/window with a deadline**, not the whole loop. Readiness is owed
   per affected region before the playhead returns to it, so the resident set is bounded by the **lookahead
   plus the affected intervals**. That is what removes the unbudgeted `N x whole-root` term (~325 KB per
   loop, ~2.6 MB at today's N = 8) instead of merely paying it. A published root survives only as the
   **immutable checkpoint authority for undo and save**.
2. **The existing resumable deferred machinery is the mechanism, not new work.** The codebase already
   produces exactly this kind of update in slices across service turns inside MIDI boundaries: the snapshot
   construction/journal path (`LoopSnapshotConstruction`, `stepPendingSnapshotPublish`, the wrap journal),
   the deferred idle-owner scheduling (`MidiServiceDrain`), and the sliced capture-stop jobs. That machinery
   was built so a *whole-loop* unit would fit; **this change alters its unit and gives it a deadline** rather
   than adding a parallel streaming subsystem. Avoiding a second incremental-publication path is an explicit
   goal (S4c.2).

**Missed-deadline rule (must be decided, not assumed).** An update can miss its deadline -- an edit just
behind the playhead, or a heavily loaded service turn. Playback must still see only immutable events, so the
candidate behaviours are:

| # | Rule | User-visible consequence | Verdict |
|---|---|---|---|
|---|---|---|
| a | The **previous immutable generation stays authoritative for that pass**; the new one takes effect on the next pass | The change is heard one pass later; consistent with the existing coverage/currency rules (`publishedSnapshotIsCurrent`, the DEC-073 coverage contract) | **CHOSEN** |
| b | The affected span is **held/suppressed** for that pass | A gap or a held note for one pass | rejected: audible hole |
| c | The **deadline is extended** by deferring the pass | Timing is disturbed | rejected: the never-stop goal forbids it |

**Decision: (a).** Playback always consumes an immutable generation; a missed deadline means the previous
generation remains authoritative until the next pass, so a read is never torn and timing is never disturbed.
The user-visible consequence is explicit: **an edit made just behind the playhead is heard one pass later.**

### Window size is a tunable, and it trades three things

Today the windows are **bar-based** (the merged playback window is 2 bars = 1,536 ticks, and the detailed
display window is capped at 16 bars). The forward requirement -- **queued slot switching quantised at 1/32 or
1/16 beat for jamming** -- pushes toward much shorter windows. At `INTERNAL_PPQN = 192` and
`QUARTERS_PER_BAR = 4` (`TICKS_PER_BAR = 768`, 192 ticks per beat):

| Granularity | Ticks | At 120 BPM |
|---|---|---|
| 1 bar (today's unit) | 768 | 2,000 ms |
| 1/4 beat (16th note) | 48 | 125 ms |
| 1/8 beat (32nd note) | 24 | 62 ms |
| **1/16 beat** | **12** | **31 ms** |
| **1/32 beat** | **6** | **16 ms** |

So the readiness deadline for a jammed switch is on the order of **16-31 ms**, which is what makes the
window size a *tunable with three coupled effects* rather than a constant:

1. **Resident bytes** -- smaller windows shrink the per-slot immutable set, which is what raises N (F1's
   "how crazy can someone get").
2. **Switch granularity** -- smaller windows are what make 1/16 or 1/32 beat quantisation possible at all,
   because the switch needs the inbound slot's immutable state ready by the boundary.
3. **Publication load** -- smaller windows mean more boundary crossings and more frequent incremental work, so
   the deadline gets tighter exactly as the work gets more frequent. This is the term that can fail, and it is
   why the size must be **measured** rather than assumed.

The window size must therefore be **configurable and swept**, not hard-coded to bars, and S2/S4c must find the
smallest size that still meets the deadline under load.

## Revision 14 — a bounded standby tier, prepared against a known deadline

Because the playhead's position and each loop's start tick are known, **the next arrival time of every tick is
knowable**. That converts preparation from an urgent boundary task into a **scheduled one with a deadline**:
after a recording/overdub stop (or any mutation), the affected ticks have a known next-arrival, and slots that
are merely *candidates* to be switched to have an even looser deadline. This is the scheduling input the
readiness contract (Revision 11) needs, and it reuses the deferred idle-owner machinery.

The artefact is a **standby excerpt per candidate slot**: a small, pre-resolved, **immutable** window, so that

- a queued switch has the inbound slot's immutable state ready **by construction**, which is what makes
  1/16- or 1/32-beat switching reachable at all;
- a stop can leave a slot immediately playable from standby rather than waiting on a publication;
- rule (a) remains the fallback for the residual misses.

**Sizing (playback footprint ~35 B/event: `MidiEvent` 20 + order 2 + phase 4 + mergeScratch 2 + retained 1 +
passOrigin 4 + canonicalRank 2):**

| Density | 1/4-bar window | 1-bar window | 2-bar window |
|---|---|---|---|
| typical (~72 events/bar) | 0.6 KB/slot -- 60 slots **37 KB** | 2.5 KB/slot -- **148 KB** | 4.9 KB/slot -- **295 KB** |
| representable ceiling (4,096 refs/bar) | 35 KB/slot -- **2.1 MB** | 140 KB/slot -- **8.4 MB** | 280 KB/slot -- **16.8 MB** |

(128 slot-channels is roughly double the 60-slot column.)

**Therefore standby is a capped, budgeted tier, not a free optimisation:**

- it appears in the budget as its own term: `fixed + standby + N x P + C + D + I + overlap + reserve <= budget`;
- it is **byte-capped with eviction** (LRU or an explicit candidate set: next-up plus recently used). "All
  slots on standby" is a *policy outcome the cap permits*, not a guarantee -- at the ceiling density it is
  unaffordable at 1-bar windows and affordable only at sub-bar windows, which is another reason the window
  sweep (S2.6) gates this;
- it is **immutable**: a mutation covering its ticks invalidates it, and the slot falls back to rule (a) until
  it is re-prepared;
- it must be **derivable without loading a whole loop**, which for cold slots depends on S6's descriptor-based
  rehydration -- so an all-slots standby is a post-S6 capability, while a candidates-only standby is reachable
  earlier;
- it is a **deliberate, bounded exception** to "inactive slots retain descriptors only". The cap is what keeps
  the scaling law; without it, standby silently becomes the per-slot residency this change exists to remove.

## Revision 15 — a visual tier built from what already exists

The visual side has the same shape of problem as the MIDI side (switch a slot, show it instantly, without
per-slot whole-loop residency) and it is better supplied with building blocks. Inventory of what is already in
the tree:

| Building block | What it gives us |
|---|---|
| `rebuildVisualCacheIdleSlice` (+ `markDisplayCachesStale`, `ensureVisualCacheBuilt`) | a **resumable, bar-sliced renderer** -- the pre-render engine, already idle-scheduled |
| `visualCache.builtFromSnapshotIdentity` / `builtFromRevision` | a **validity identity** for a rendered visual, so invalidation is free |
| `selectDrawSourceReuseKind` / `DrawSourceGatherKey`, committed-prefix retention (`liveDisplayCommittedPrefix_`, `retainDrawSourceHandoffCommittedPrefix`) | the **frame reuse** machinery that makes a switch cheap instead of a full rebuild |
| `overviewCaptureBandMask_` (`VisualBarVec`, one entry per bar) | an existing **per-bar mask** already used to draw the overview strip |
| `DisplayNoteCount::countDisplayNotes` | a **compact inventory** (notes, unique ids, max-per-pitch) with no gather or reconstruct |
| `TrackManager::prewarmSelectedDisplayVisualCache` | a **preload entry point that exists but has no caller** -- written for exactly this, currently disconnected |
| windowed gather (`resolveDisplayNotesByDrawSource`, `kWindowedGatherMarginBars`) | bar-windowed detail resolution, already the paint path |

So the design is **two levels**, mirroring the standby tier:

1. **Visual envelope -- for every slot, unconditionally.** A compact per-bar activity/density mask (and
   optionally per-pitch-band counts). At 1 byte per bar a 64-bar loop costs **64 B/slot**, so **all 64-128
   slots cost ~8-16 KB**. This is the pre-rendered-file analogue: cheap enough to hold for the whole library,
   which is what gives instant switch *feedback* and an overview strip without touching canonical content.
2. **Visual window -- capped, like standby.** The rendered `DisplayNote` window for slots that are visible or
   are switch candidates. `DisplayNote` is 16 B, so at the observed density (~36 notes/bar) a 1-bar window is
   **~0.6 KB/slot** (~35 KB for 60 slots), while at the representable ceiling (~2,048 notes/bar) it is
   **~32 KB/slot** (~1.9 MB for 60). Same conclusion as standby: affordable at typical density, **byte-capped
   with eviction** at the ceiling.

**Reuse rules (the point of the inventory).** Render through the existing idle-slice path rather than a new
renderer; validate with the existing snapshot identity/revision rather than a new generation counter; invalidate
on any mutation covering the envelope's bars and fall back to the windowed gather; and revive
`prewarmSelectedDisplayVisualCache` as the visual analogue of the standby scheduler rather than inventing a new
entry point. Two dead display paths (`resolveDisplayNotesCommitted`, `resolveDisplayNotesLiveCapture`) were
already found caller-less by earlier audits -- confirm which mechanism a dev-branch "visual preload" actually
uses before adding a third.

The envelope is, like standby, a **deliberate bounded exception** to "inactive slots retain descriptors only".
It is safe at this size precisely because 64 B/slot does not scale with loop length in a way that threatens the
law; the *window* is the part that must be capped.

## Revision 16 — one contract for resumable work items (the owners already agree)

**Canonical term.** The whole setup this change builds toward is governed in
[`docs/Authority/NAMING.md`](../../../docs/Authority/NAMING.md) as **Playhead-scheduled MVCC** — multi-version
immutable state, resolved by interval projection, materialized incrementally against a playhead deadline, with
epoch-leased working sets — and its stage shape as a **Pipeline of role-owned stages** with declared cost and
deadline. Use those terms rather than new synonyms.

The observation is correct and the evidence is in the tree: many small owners already look like the same
service, and they converged **without** a shared contract.

| Owner | begin | step | terminal vocabulary |
|---|---|---|---|
| `LoopSnapshotConstruction` | ctor(parent, changes) | `step(quantum)` | `Pending` / failed / `freeze()` |
| `Loop::SessionClose` | `beginCloseOverdubSession(work)` | `stepCloseOverdubSession(work, quantum)` | `Phase` incl. `Denied` |
| `RuntimePlaybackViewBuild` | `begin(view, source, coverage, ...)` | `stepFill` / `stepOrder` | `Fill` / `Order` / `Complete` / `Denied` |
| `RuntimePlaybackOrderBuilder` | `begin(view, loopLength)` | `step(quantum)` | `Pending` / `Complete` |
| `LoopEventStoreReadCursor` | `beginReadSealedChunks(...)` | `stepReadSealedChunks(cursor, quantum)` | `Pending` / `Complete` / `Denied` |
| `LoopEventStoreRewriteCursor` | (start) | `stepRewriteFromOrderedSource(...)` | `Pending` / `Complete` / `Denied` |
| `SessionUndoCollection` | `beginCollectOverdubSessionUndoPasses` | `stepCollectOverdubSessionUndoPasses` | `Phase` / `Denied` |
| persistence work item | `admitWork(...)` | `stepPersistenceWorkItem(...)` | ok / drain budget |
| LCR device gate | `beginCapturePass` | `appendCapturePassChunkEventRange` (8 events/slice) | slice complete |

**Already converged, and duplicated by accident:**

- `Loop.h:84` `Result { Pending, Complete, Denied }`, `LoopEventStore.h:62`
  `LoopEventStoreRewriteResult { Pending, Complete, Denied }`, `LoopEventStore.h:64`
  `LoopEventStoreReadResult { Pending, Complete, Denied }` -- **three identical terminal trios**;
- `Phase` state machines with `Denied` as a member (`Loop.h:92`, `:156`, `:407`, `:430`);
- a declared-cost convention: `requiredGrainsUpperBound`, `requiredSelectedGrainsUpperBound`,
  `requiredFrozenOverdubGrainsUpperBound`, `requiredFrozenTailGrainsUpperBound`,
  `requiredRankedGrainsUpperBound`, `requiredSuccessorGrainsUpperBound`, `requiredSessionUndoCollectionGrains`.

**Already shared as machinery:** the scheduler (`MidiServiceDrain::DeferredIdleOwner` with per-turn budgets),
admission (`MemoryMonitor::admitOperationMemory` + `defaultMarginFor` per `OperationKind`), and attribution
(`ExternalPoolAccounting::beginTag`/`endTag` + 17 tags).

**What is missing, and it is the whole gap:**

1. **Identity.** Generation counters are ad-hoc per module (`overdubSessionUndoGeneration_`, `poolLifetime_`,
   `storageGeneration`, `work.generation`), so every staleness check is re-implemented. A single
   `WorkIdentity { owner, generation, role }` -- the same shape the lease model already needs -- would unify
   them and is what makes stale-cleanup protection uniform rather than per-owner folklore.
2. **Deadline.** No owner declares *when* its output must be ready. Revision 11's readiness deadline is the
   new dimension, and it is exactly what would let the scheduler *order* work instead of round-robining it.
3. **One vocabulary.** Three identical trios mean the scheduler needs per-module glue; a shared terminal type
   plus a `Phase` convention removes it.
4. **Universal cost declaration.** Present on the `Runtime*` families, absent elsewhere.

**The contract (vocabulary + convention, not a framework):**

```
WorkIdentity   { owner, generation, role }
WorkResult     { Pending, Complete, Denied }        // one shared type
requiredGrains(...)                                 // declared cost, existing naming
deadlineTicks                                       // NEW: when the output must be immutable
OperationKind + accounting tag                      // admission and attribution, existing enums
failure = no partial authority                      // non-destructive, existing contract (G.2)
```

**Constraints (non-negotiable on this target):** header-only vocabulary; concrete owner types, no vtables or
type erasure; no allocation; FLASH-resident where cold; one owner per concept per `ARCHITECTURE_RULES.md`; and
retrofit is **opportunistic** -- an owner is aligned when it is next touched, never in a big-bang sweep. The
new stages of this change (S4c windowed readiness, S4d standby, S4e visual tier) are required to conform, which
is where the contract earns its keep immediately: it stops the plan from adding a sixth variant of the same
shape.

## Revision 12 — the role tiers fold into `activeLoopIndex` (F5)

The COLD / SELECTED / PREPARED-ACTIVE / CAPTURE-ACTIVE / DEFERRED tiers are a **view of existing
authorities**, not a second state machine: `activeLoopIndex` (the one active slot per track),
`CapturePhase`, and `TrackState`. A tier transition that moves `activeLoopIndex` is an ownership transfer;
one that does not, is not. `ARCHITECTURE_RULES.md` requires one owner per concept, so the tiers must be
derivable from those three rather than competing with them.

## Revision 13 — finalization is one role; a second stop is denied non-destructively (F3, corrected)

An earlier revision of this document read `CaptureStopJobs::kCapacity = 4` as a queue that would service a
second stop automatically. **That is reverted.** Descriptor-ring depth is not evidence of multi-finalizer
storage: this codebase already established that descriptor capacity exceeded the physical frozen/finalization
role capacity, so a second stopped interval has no proven home for its sealed raw range, pending closures,
staged pass, session receipt or canonical transfer state while the first finalizer is still working.

The contract is therefore the **device-proven DEC-067 directional admission**:

```
A recording/overdub -> STOP A -> A holds the one DeferredFinalizationLease
switch to B         -> START B -> B holds LiveCaptureLease and records normally
A finishes          -> releases the lease
STOP B              -> B acquires it

STOP B while A still finalizes
  -> denied non-destructively, no partial stop
  -> B remains recording/overdubbing
  -> a subsequent stop request may succeed once the role frees
     (still subject to the other single-instance resources)
```

**How it actually works (traced 2026-09-18 in `Track::beginDeferredOverdubStop`).** The bound is *not* the
ring depth. Admission takes a descriptor only if the **tail descriptor is free** (`captureStopJobTail()` requires
`count < kCapacity`, and the caller requires the tail's `pendingNotes` to be empty), so a pending stop is never
stacked on an occupied interval. What actually bounds ordinary concurrency is the set of **single-instance
finalization resources**, each an explicit non-destructive denial that takes no descriptor and leaves the
interval as the live recording interval:

| Denial stage | Condition |
|---|---|
| **Ordinary directional-capacity denials** -- expected during normal reentry; each re-checked on every attempt | |
| `finalizer_busy` | `runtime->captureStopOwner != nullptr` -- a predecessor is still finalizing |
| `transfer_denied` | no active overdub transfer admission |
| `canonical_output_busy` | canonical capture output not admitted |
| session-exit handoff | no session-exit receipt slot available |
| **Exceptional denial -- not part of normal reentry** | |
| `queue_full` | no free tail descriptor: **descriptor exhaustion**, documented as *"never ordinary finalization latency"*. It is categorically different from the denials above, must not be used to explain ordinary finalization wait, and reaching it indicates a descriptor-lifetime defect (leaked `pendingNotes`) rather than capacity pressure |

So: **one *active* finalizer**, enforced by shared resources; a denied stop is **not queued and not
auto-serviced** -- it is re-issued by the stop edge (device evidence `successor-reentry-25`: B's stop refused,
then admitted after A's finalization released). `finalization_lease_stop_denied` counts these denials, and real
usage decides whether one finalizer ever constrains normal work (very short captures and rapid
punch-stop/start/stop are the constrained shapes). A queue would be a **separate capacity/ownership change**
that must first show where each per-interval state lives while a predecessor finalizes.

## Options considered

| # | Option | Verdict |
|---|--------|---------|
| A | Per-owner trimming (block the 64 KiB buffer, partial-fit arm admission) | **Safeguard** (G.2/G.3). Does not change the law. |
| B | Window-derived lease sizing | **Prerequisite (S2)**, promoted out of the safeguards: without it the active envelope does not fit. |
| C | Active-role-owned, reusable, generation-qualified leases with role tiers | **This change**, the law change. |
| D | Pressure-only eviction of idle bundles | **Insufficient alone**: memory becomes a function of history, not of the active set; not reason-able for 60 slots. |
| E | More denial behaviour around the current residency | **Rejected as strategy.** Makes failures graceful, leaves residency unchanged. |
| F | Keep everything resident and compress harder | **Rejected.** Cannot fit 100+ MB of designed residency into 8 MB. |

## Risks and mitigations

- **Stale-owner release / use-after-free** -- the recurring ownership failure. Mitigation: generation in
  the lease identity, release rejected on mismatch and reported; a native fixture per owner kind; the
  adversarial acceptance case below.
- **Selection-time churn** -- mitigation: the SELECTED tier holds no working set.
- **Activation latency** -- re-acquiring on activation must be a cold boundary; the pool must be able to
  serve the declared active envelope without allocation on a timing edge.
- **Re-derivation cost after demotion** -- bounded by deriving only what the consumer needs.
- **Undo-history residency** -- pinned by open decision 1 before any flat-memory claim.

## Verification

**Continuity acceptance (S7):** with transport playing, switch track and slot repeatedly, start
record and overdub, and run undo/redo -- the transport never stops, and the MIDI-boundary timing verifier
reports no degradation (`overflow_free`, `call_site_gate_ok`, no active-interval overflow) across every
switch.

**Scaling acceptance (S7), a memory test rather than a content test:**

```
load/create a genuine 60+ slot, up-to-64-bar library
  -> boot and steady state roughly flat apart from canonical metadata/content
activate A            -> one bounded lease appears
activate B            -> a second appears
deactivate A          -> A's lease returns to the pool
activate C            -> C reuses A's capacity, no net growth
```

**Finalization-lease acceptance:** A stops and finalizes; B starts and records; A releases; B stops and takes
the same lease. Separately, in the pathological shape (STOP B while A still finalizes), the stop is denied
non-destructively, B stays recording/overdubbing, and the counter increments; after A releases, the next stop
succeeds. `finalization_lease_stop_denied` is reported per run as the evidence for whether 1 is sufficient.

**Adversarial owner acceptance (top-level, not just a fixture):**

```
A active
A creates a deferred job
A's UI/playback notion deactivates
attempt C activation        -> A's lease MUST remain pinned by the deferred owner
A's deferred job retires    -> lease released
C acquires the recycled capacity
late cleanup from A         -> must NOT touch C's lease
```

Plus: native fixtures for lease lifecycle and each owner guard; the red scaling fixture turned green; a
demoted-then-reactivated slot plays identical content (existing content-verification pipeline); and the
original reproducer (arm a track, external clock + MIDI notes) reaching `RECORDING` on the large library.

## Relationship to earlier work

- `immutable-loop-snapshot-passes` RC-MEM-2 (whole-take reservation) and RC-MEM-5 (lazy chunk pool) stay
  landed and are not reverted; they bought headroom and removed dead reservation.
- RC-MEM-2's outstanding "derive capacity from the resolved window" is **promoted to prerequisite S2**
  here, not left as a safeguard.
- §5.13 (record arm exits) is reclassified as a **symptom**; its remaining items stay safeguards G.2/G.3.
- RC-MEM-3 (PSRAM-aware undo trim) is the adjacent owner for revision 4's budget option.
