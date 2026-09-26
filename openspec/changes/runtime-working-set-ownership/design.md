> **Execution workflow strengthened (2026-09-23, DEC-080):**
> [ARCHITECTURE-REVIEW.md](ARCHITECTURE-REVIEW.md) maps every remaining stage to fixture-first
> transition/ownership oracles and evidence-backed completion. No runtime behavior changed by this update.

> **Current targeted corrections (2026-09-23, DEC-079):**
> [IMPLEMENTATION_CONTRACT.md](IMPLEMENTATION_CONTRACT.md) governs the first executable slice,
> per-holder memory cardinalities, job-retaining regeneration, the uncreated phase-F dependency,
> and interaction composition/supersession. It supersedes conflicting historical revisions below.
> No firmware or device result is claimed by this update.

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
| 31 | **Every user interaction is accepted; *when* it executes is the pipeline's decision.** No interaction (stop, record/overdub start, slot or track switch, undo/redo) is refused because an owner is momentarily busy. Execution timing is resolved by the playhead-scheduled MVCC pipeline against the playhead deadline and the earliest valid interval boundary -- a busy owner delays execution, it never rejects the request, and the user never re-issues | product requirement; spec "every user interaction is accepted"; S4f |

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
fixed + N*P + Cmax*C + Dmax*D + Imax*I + Omax*O
      + canonical + roots + undo + visual + standby + reserve <= PSRAM budget
(holder maxima and current-root accounting: IMPLEMENTATION_CONTRACT.md §2)
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
unavailable, the undo is **accepted and deferred** (invariant 31) rather than refused:

```
Undo request -> accepted, scheduled
             -> current musical state unchanged while it waits
             -> explicit `undo_resource_denied` (names the wait, not a user-visible refusal)
```

The cold descriptor is **never consumed or popped before reconstruction succeeds**, and the accepted undo executes
as soon as preparation is admitted. This is the same prepare-then-commit pattern the rest of the change uses. Under
invariant 31 the earlier reading of `undo_resource_denied` as a *user-visible denial* is superseded: the user never
re-issues, and the code path records the wait and retries.

**Load contract (DEC-088 — load-time undo reconstruction):** runtime undo history is not persisted. Load derives per-slot undo units from saved active pass records, with redo empty. An in-session Undo creates Redo; unrelated background loads preserve it. Original cross-slot chronology and pre-reboot redo are not recovered.

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
| `LiveCaptureLease` | **1 per track** (handoff §2) | record/overdub ingress, staging, capture stop jobs |
| `DeferredFinalizationLease` | **per admitted interval**, device bound in handoff §2 | one finalizing predecessor alongside one live successor; a second stop while it is occupied is **delayed non-destructively**, never refused (DEC-067 capacity; invariant 31) |
| `InteractiveLease` | **1** | edit / undo / redo / display, unless evidence requires more |
| `TransferOverlap` | measured | explicitly budgeted, not slack |

Classes are **composable**: a slot may hold several at once, and each owns only the resources its operation
needs. Assignment is generation-qualified per class, so releasing a playback lease cannot disturb a live
capture or deferred finalization lease on the same slot.

### Historical singleton rationale — superseded by Revision 18 and handoff §2

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

Under the same non-destructive contract as failed admission, **B's stop is accepted at the edge and B remains
recording/overdubbing**; once A releases, the accepted request executes exactly once — no re-press (superseded
by **Revision 18 / DEC-078**; the pre-Revision-18 text read "B's stop is denied … a subsequent stop request may
succeed"). Normal use (record for at least a few
seconds after a switch) will have A finished long before B stops; only very short captures and rapid
punch-stop/start/stop sequences are constrained.

The initial envelope therefore pays for **one** finalization lease and adds the counter
`accepted_stop_finalizer_wait`. If real usage shows frequent denials, a second lease is evaluated **from
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

- it appears in the budget as its own term: `fixed + N*P + Cmax*C + Dmax*D + Imax*I + Omax*O + canonical + roots + undo + visual + standby + reserve <= budget`;
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

## Revision 13 — finalization is one role; a busy owner delays a second stop (F3, corrected)

An earlier revision of this document read `CaptureStopJobs::kCapacity = 4` as a queue that would service a
second stop automatically. **That is reverted.** Descriptor-ring depth is not evidence of multi-finalizer
storage: this codebase already established that descriptor capacity exceeded the physical frozen/finalization
role capacity, so a second stopped interval has no proven home for its sealed raw range, pending closures,
staged pass, session receipt or canonical transfer state while the first finalizer is still working.

The contract is therefore the **device-proven DEC-067 capacity model with unconditional acceptance** (invariant 31):

```
A recording/overdub -> STOP A -> A holds the one DeferredFinalizationLease
switch to B         -> START B -> B holds LiveCaptureLease and records normally
A finishes          -> releases the lease
STOP B              -> B acquires it

STOP B while A still finalizes
  -> accepted, no partial stop, no descriptor taken
  -> B's stop is ACCEPTED and scheduled; B keeps recording/overdubbing
     while A finalizes (the interval stays intact)
  -> the accepted stop executes once the role frees
     (still subject to the other single-instance resources being released)
```

**How it actually works (traced 2026-09-18 in `Track::beginDeferredOverdubStop`).** The bound is *not* the
ring depth. Admission takes a descriptor only if the **tail descriptor is free** (`captureStopJobTail()` requires
`count < kCapacity`, and the caller requires the tail's `pendingNotes` to be empty), so a pending stop is never
stacked on an occupied interval. What actually bounds ordinary concurrency is the set of **single-instance
finalization resources**. These are **busy-owner states**, not user-visible refusals: the interaction is accepted
and scheduled, the interval stays the live recording interval, and execution happens when the owner frees.

| Busy-owner state | Condition |
|---|---|
| **Ordinary directional-capacity denials** -- expected during normal reentry; each re-checked on every attempt | |
| `finalizer_busy` | `runtime->captureStopOwner != nullptr` -- a predecessor is still finalizing |
| `transfer_denied` | no active overdub transfer admission |
| `canonical_output_busy` | canonical capture output not admitted |
| session-exit handoff | no session-exit receipt slot available |
| **Exceptional denial -- not part of normal reentry** | |
| `queue_full` | no free tail descriptor: **descriptor exhaustion**, documented as *"never ordinary finalization latency"*. It is categorically different from the denials above, must not be used to explain ordinary finalization wait, and reaching it indicates a descriptor-lifetime defect (leaked `pendingNotes`) rather than capacity pressure |

So: **one *active* finalizer**, enforced by shared resources. Acceptance is unconditional (invariant 31): a stop
that meets a busy owner is **accepted and scheduled**, and the *pipeline* decides when it executes -- the earliest
valid interval boundary at which the owner has freed. Device evidence `successor-reentry-25` is the **counterfactual
for the defect**: B's stop was refused and only took effect after A's finalization released, i.e. it needed the stop
edge to come again. `accepted_stop_finalizer_wait` counts **waits**, attributed to the holding owner; real usage
decides whether one finalizer ever constrains normal work (very short captures and rapid punch-stop/start/stop are
the constrained shapes). Note what is *not* implied: an accepted request is one small piece of per-interval state,
so this is **not** a queue of stopped intervals -- a queue would still be a **separate capacity/ownership change**
that must first show where each stored interval's sealed raw range, pending closures, staged pass, session receipt
and canonical transfer state live while a predecessor finalizes (four of those five sets have no proven home).

## Revision 17 — per-action execution budgets, and deferred progress presentation

Invariant 31 separates **acceptance** (immediate, unconditional) from **execution** (scheduled by the pipeline).
That promotes "how long from acceptance to execution" to a first-class quantity, and it differs by orders of
magnitude per interaction class. These are **declared expectations to be measured**, not assertions:

| Interaction | Expected acceptance -> execution | Why |
|---|---|---|
| Stop overdub | microseconds | the same service turn or the next playhead boundary -- today's common case |
| Record / overdub start | prewarm-bound; must stay under the arm deadline | admission plus prewarm |
| Slot / track switch | must stay under the window's switch granularity (S4b) | the never-stop requirement depends on it |
| Undo / redo | ~1-2 s when the root is cold | reconstruction of a cold root from its descriptor |
| Load loop | seconds | canonical/SD read plus residency establishment |

Provisional: measurement (S4f.6) replaces every number above.

**Deferred: progress presentation.** When an accepted interaction cannot execute within its expected time and the
delay cannot be mitigated, the honest UI is to show that the action is **being executed** rather than leaving the
user to infer failure. This is deliberately **deferred**, gated on both:

1. the per-action acceptance -> execution latency is **measured on device** against the table above, and
2. a **mitigation review** shows the budget cannot be met by scheduling, prewarm or windowing work.

Two rules bind that later design whenever it is taken: progress presentation displays an **accepted** action, so it
is never a denial and never a cue to press again (invariant 31); and it must never be used to justify a widened
budget or to substitute for mitigation. It also stays off the timing-critical edges -- the display path is not the
audio path.

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

**Finalization-lease acceptance (as amended by Revision 18 / DEC-078):** A stops and finalizes; B starts and
records; A releases; B stops and takes the same lease. In the pathological shape (STOP B while A still finalizes),
B's stop is **accepted at the edge**, B keeps recording/overdubbing with its interval intact, and the accepted
stop executes exactly once after A releases — from **one** user request, never a re-press. The wait is counted as
scheduling (`accepted_stop_finalizer_wait`, attributed to the holding owner), and `accepted_stop_finalizer_wait`
is reported per run as the evidence for whether one finalization occupancy is sufficient.

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

## Revision 18 — red-team G2 input (2026-09-19): acceptance is slot-parameterised, boot is an admission boundary, and residency is per-slot

This revision lands the G2 red-team findings as design input. It **supersedes Revision 13's denial wording and
Revision 9's cardinality reading**, extracts S2.9 to a sibling change, and adds the invariants and the stage the
red team proved were missing. The acceptance rule and the per-interval finalization model are recorded as
DEC-078 (amending DEC-067 and DEC-076 point 2); this revision is the design that consumes it. The G2 verdict is
**BLOCKED** until the contracts below are implemented in `tasks.md` and re-attacked.

### The eleven findings this revision answers

| # | Finding (red team) | Design change |
|---|---|---|
| B1 | `TrackManager::finalizeCaptureAndSelectSlot` is a third stop site: it calls the void `stopOverdubbing()`, ignores a denial, then moves `activeLoopIndex` anyway (`TrackManagerCaptureQueue.cpp:327-335`) and refuses the switch while a job is pending (`:329-332`) | Switching is an accepted interaction (item 1). `activeLoopIndex` may not move while an interval is live unless the stop was admitted and bound to the interval's slot. The refusal is removed. The switch is covered by S4f. |
| B2 | A latch with no destination executes a PLAYING stop after transport stopped (`TrackManagerTransportTick.cpp:69-73`, `TrackOverdubLifecycle.cpp:1628-1633`) | The latch carries `destination` + `transportEpoch`; transport stop retargets/invalidates it; the drive re-derives the destination from live transport state. |
| B3 | `beginDeferredOverdubStop` resolves `getActiveLoop()`/`activeLoopIndex` and sets `job.slot = activeLoopIndex` (`:263-264`, `:452`); no deferred branch dispatches on a slot | Slot-parameterised admission `beginDeferredOverdubStopForSlot(destination, tick, slot, interval)`; the live edge calls it with `activeLoopIndex`. A latch-dispatch branch sits immediately after the stop-grain block in `Track::processDeferredIdleMaintenance`, before the owner-specific branches; `processDeferredCanonicalPrefix` is gated on the latch. |
| B4 | Undo/redo writes `activeLoopIndex` directly (`TrackUndo.cpp:558/582`) on the button/MIDI path | `TrackManager::setActiveLoopIndex` is the single write authority; undo/redo route through the occupancy/pending-switch path and cancel any queued switch. |
| B5 | After demotion the clock-path commit allocates: `TrackManagerTransportTick.cpp:212-223` → `ensurePlaybackMergedEventsForSlot` → `trySlot` allocates (`TrackPlaybackWindowBuild.cpp:575`) | Commit-path readiness includes occupancy; the commit path uses `slotIfAllocated` and keeps the switch pending when unprovisioned; provisioning happens only at cold boundaries. |
| B6 | Release predicate is track-scoped while the resource is slot-scoped; a missing runtime is a permanent stall (`TrackOverdubLifecycle.cpp:787-792`) | Per-slot liveness predicate; a missing runtime regenerates through the cold path or parks non-destructively. |
| B7 | Boot prewarms every committed slot and reserves 8 pool chunks each (`TrackManager.cpp:63-76`, `TrackPlaybackWindowBuild.cpp:524`, `LoopEventStore.cpp:1488-1505`): 41,056 B per slot; ~60 slots = 480/512 chunks | New stage **S2b**: boot/load establishes descriptors only; canonical-copy reservation is lazy at capture admission (DEC-065 pattern). |
| B8 | A load can destroy a pinned finalization's owner and wedge it (`LoadLoopJob.cpp:237` has no pending-stop guard) | Load admission uses the same per-slot liveness predicate and defers/refuses for a pinned slot. |
| B9 | Persistence guards and `resolveCaptureTrackSlotForMidPass` do not know the accepted-but-unexecuted stop and fall back to selected/active slot (`PersistenceAdmit.cpp:259`, `MidPassChunkPersist.cpp:51-55`) | One shared in-flight predicate including the latch; the mid-pass resolver resolves from the interval with no selected/active fallback. |
| B10 | Cold undo descriptors pin canonical chunks via `SlotPassReferences`; ClearSlot has no snapshot-free reconstruction; a stale body silently consumes the descriptor (`TrackUndo.cpp:986-989`) | Cold-descriptor pin lease + byte accounting charges pinned canonical bytes; ClearSlot is excluded from the cold tier; the stale path defers (`undo_resource_denied`) and never erases. |
| B11 | New per-slot/per-track fields land in DTCM: ~1.4 KB against 872 B of usable padding; a bank cross costs 32,768 B and the documented silent boot hang | All new per-slot state lives in the external `LoopPlaybackRuntime` (6,344 B extmem) or the `LoopPool`; per-track state is externally held or out-of-line `TRACK_COLD_MEM`; `static_assert` size pins. |

### 1. The interaction latch and the switch rule

```
PendingInteractionRequest {          // one per live interval; extern-held beside captureStopJobs_
  kind          : Stop | Switch | Start
  destination   : Playing | Stopped  // recorded at acceptance, re-derived at drive
  slot          : interval's slot    // the interval's identity, not activeLoopIndex
  switchTarget  : slot index         // set only by kind = Switch; never overwrites `slot`
  interval      : captureInterval_
  phaseTick     : capturePhaseTick(requestedTick)
  transportEpoch: monotonic
  valid         : bool
}
```

- One predicate `hasAcceptedUnexecutedInteraction()` is added beside `hasPendingDeferredOverdubStop()`; the two
  are distinct authorities for distinct facts (accepted vs admitted) and both are consulted where "a stop is in
  flight" today.
- Execution precedence is **stop > switch > start**; distinct continuations survive in manager-owned carriers (handoff §5); a `(kind, slot, switchTarget, interval)` match
  makes a repeat a no-op. A `Switch` record never modifies `slot`, so the interval's identity and its
  canonical-output guard stay intact while the switch waits.
- Clear the matching stop obligation only after admission; pending switch/start carriers survive it.
  Start reset cannot erase an unadmitted predecessor stop. Transport Stop retargets the live stop to
  Stopped and explicitly supersedes unexecuted starts (handoff §5).
- The latch is diagnostic-visible through counters and stage names only; it is not a stored interval and takes
  no ring capacity.
- **Switching**: `TrackManager::finalizeCaptureAndSelectSlot` is rewritten to the acceptance contract — the
  switch is accepted; the stop is latched against the interval's slot; the interactive/display role may transfer
  immediately, but `activeLoopIndex` does not move until the stop is admitted and bound to that slot.

### 2. Cardinality and liveness (supersedes Revision 9/13)

| Lease | Holder identity | Cardinality today | Survives a switch? |
|---|---|---|---|
| `PlaybackWindowLease` | sounding slot | N derived; 1 per track | released when the slot stops sounding |
| `LiveCaptureLease` | live capture interval | **1 per track** | tied to the interval |
| `DeferredFinalizationLease` | **interval** `(track, slot, interval generation)` | 1 per interval, bounded by that track+slot's own resources | **yes** — pinned across `activeLoopIndex` movement and across track/slot switching |
| `InteractiveLease` | selected/active track+slot | 1 | transferred by the switch protocol |
| `TransferOverlap` | switching track | inbound + outbound occupancy | transient |

`noLiveOwnerForSlot(track, slot)` is the predicate for **release**, **load admission**, and the per-slot form of
persistence: no ring entry with `job.slot == slot` in any state, no deferred stop commit, no prepared handover,
no latch targeting the slot, no job-owned undo root. Boot/load is separate: a committed-but-inactive slot holds
descriptors only and reserves no canonical-copy capacity.

### 3. Extracted stage

**S2.9 is extracted** to a sibling change, `windowed-playback-generation`, carrying its own DEC and reassessment
for the interval publication unit, per-window currency, the five gather call sites, LSV1 root authority and undo
root pinning. The sibling is **planned but not created**. This change keeps S4c as contract-only work, not publication/root implementation; **S2.3's N stays provisional** and may not be
claimed as the memory law until that sibling lands.

### 4. New invariants (32–36, binding)

| # | Invariant | Enforced by |
|---|---|---|
| 32 | **Acceptance covers every interaction, including a switch**; no interaction is refused, and `activeLoopIndex` does not move while an interval is live unless its stop was admitted and bound to that slot | spec "every user interaction is accepted"; S4f, S4b.5 |
| 33 | **Finalization is per interval** and survives an active-slot change; liveness is **per slot** where the resource is per slot | spec "release waits for a deferred owner"; S4, S4.5 |
| 34 | **Boot/load is an admission boundary**: a committed-but-inactive slot reserves no canonical-copy capacity | spec "library growth does not grow working memory"; S2b |
| 35 | **A missing runtime retains its job and retries bounded regeneration; no silent permanent stall or ownerless extent** | spec "failed admission is non-destructive"; S4.5 |
| 36 | **New per-slot state is external or FLASH-constructed**, under the 872 B usable RAM1 padding | § 5 Placement; S3.0, S4g |

### 5. Placement rules (RAM1/DTCM)

- Per-slot state → the external `LoopPlaybackRuntime` (6,344 B, `extmem_malloc`) or the `LoopPool` `Loop`
  (536 B each, 64 total).
- Per-track state → an externally held container (`std::unique_ptr`, as `captureStopJobs_` is) or out-of-line
  `TRACK_COLD_MEM` constructors/destructors. A new by-value non-trivial array inside `Track` emits implicit
  ctors/dtors into ITCM and has already cost one 32 KB FlexRAM bank (the `CaptureStopJobs` precedent).
- `static_assert` pins (`sizeof(LoopPlaybackRuntime)`, `sizeof(Loop)`, `sizeof(TrackPlaybackRuntime)`) land in
  S3.0 before any field is added.
- Usable new-ITCM/DTCM budget for this change: **872 B**. Crossing the next bank costs 32,768 B and drops the
  stack into the documented silent boot hang.

### 6. Caps and eviction (a new owner, not a reuse)

No existing cap exists for the envelope or standby: `kMaxDetailedWindowBars` is a render policy,
`overviewCaptureBandMask_` is a DisplayManager scratch mask, `kBarsPerSlice` is idle granularity, and
`VisualCache::notes` is uncapped. Each new tier therefore declares its own byte cap, its own eviction point, and
its store's fixed external capacity, with a fill-to-cap no-growth test. The envelope and standby remain
**deliberate, bounded exceptions** to "inactive slots retain descriptors only".

### 7. Cold undo (supersedes Revision 4's "no slab ownership" claim)

- A cold descriptor must **not** register pass ids into `SlotPassReferences` unless reconstruction genuinely
  needs the canonical row; if it does, the pinned canonical bytes are charged to the cold budget, because the pin
  is what keeps the chunk resident (`LoopCapture.cpp:160,167`, `LoopEventStore.cpp:350`).
- The budget therefore charges **descriptor bytes + pinned canonical bytes**, with a new accounting tag.
- **ClearSlot is excluded from the cold tier**: its before-state is a whole-slot snapshot
  (`TrackUndo.cpp:1197`, `LoopEditPasses.cpp:68`) that no pass-id descriptor reproduces. ClearSlot roots either
  stay hot or are dropped on demotion, with the loss recorded.
- A missing canonical body **defers** (`undo_resource_denied`), it never consumes the descriptor
  (`TrackUndo.cpp:986-989` is the defect to remove).

### 8. Open items this revision does not close

The window/interval publication unit (sibling change); transfer-overlap depth; the canonical-residency policy;
whether a second finalization lease is ever warranted; and the measured acceptance-to-execution latency per
interaction class (S4f.6). The cold-undo descriptor field set is **no longer open** — §9 item 2 / S5.2a fix it
(ids + per-pass metadata, charged as pinned canonical bytes vs descriptor bytes).

### 9. Rerun corrections (2026-09-19, cold-undo / liveness probe)

The first re-attack of Revision 18 confirmed the intent but named five places where an *outcome* was stated
without an implementing **owner or operation**. They are binding corrections, not notes.

1. **Cold descriptors always pin, and the budget always charges.**
   `PassReferenceSet` is a **veto**, not a refcount: the builder is `collectReferencedPasses`
   (`src/PassReclaim.cpp:26-65`), the consumer is `TrackManagerMemoryPressure.cpp:80-83`, and erasure destroys
   the row *and* its ids in one step (`reclaimDisabledCapturePass`, `src/Loop/LoopCapture.cpp:131-155` →
   `LoopEventStore::releaseChunkRefs`). "Do not pin" therefore means "do not scan", i.e. the entry is not a cold
   undo descriptor at all and its pass body is reclaimed permanently. **There is one live branch: keep the pin
   and charge the pinned canonical bytes.** The writer that must change is the demoter — the object that inserts
   the cold entry into `GlobalUndoStack::entries` and keeps a `LoopPasses`-shaped id set that
   `pinPassesFromLoopPasses` (`src/PassReclaim.cpp:10-24`) can read.
2. **A cold descriptor needs per-pass metadata, not only ids.** Because erasure destroys the row, reconstruction
   also needs the fields the row carried (tick span, note count, capture-pass state). Descriptors that retain the
   row (pin it) pay canonical bytes; descriptors that keep only metadata pay descriptor bytes. S5.2a must state
   both field sets and which budget each is charged to. **Superseded by §22.1: the row carries neither a tick span
   (a chunk property) nor a note count, and reconstruction resolves ids against the live rows, so the descriptor
   is a whole-row-set `ColdPassDescriptor`, not an id + metadata pair.**
3. **ClearSlot's retained hot cost is capped, not merely excluded.** The exclusion is consistent, but "stays hot"
   is bounded today only by `ABSOLUTE_MAX_UNDO_ENTRIES = 512` with a reactive trimmer
   (`src/PassReclaim.cpp:67-119`), while a ClearSlot entry holds a whole-`LoopPasses` deep clone
   (`src/TrackUndo.cpp:1197`, `src/Loop/LoopEditPasses.cpp:58-70`). The spec's cold-undo requirement gains an
   explicit **count/byte cap and eviction point** for ClearSlot hot roots, independent of memory pressure.
4. **Deferred cold-undo reconstruction needs its own owner.** `undoForLoop`
   (`src/TrackUndo.cpp:922-995`) applies `entries[cursor - 1]` and, on failure, **erases** it (`:988-991`) then
   skips to the next entry; `cursor` means "entries below are done". So "keep the descriptor and retry" requires a
   new `TrackUndo::stepDeferredReconstruction()` (state `{track, slot, entry id, phase}`, declared grains,
   `undo_resource_denied`), driven beside `Track::processDeferredIdleMaintenance`; `undoForLoop` returns
   *accepted/deferred* instead of falling through to erase, may not reach older entries while the head descriptor
   is deferred, and the deferred descriptor is read-only for display (never consumed).
5. **A missing slot runtime needs a named regeneration operation, and re-entry is a content-loss hazard.**
   `beginDeferredOverdubStop` freezes and rolls the role (`src/Track/TrackOverdubLifecycle.cpp:402-410`, `:437`)
   and seeds reservations from *live* staging — re-entering it after a runtime loss would re-freeze an empty
   generation and publish an empty pass. Required: `Track::regenerateFrozenCaptureStaging(CaptureStopJob&)` —
   (a) `trySlot(job.slot)` + `prewarmCaptureStaging()`; (b) refill the frozen role from the job's own sealed
   extent, `job.rawRange` read via `CaptureRange::read(...)` (`src/Loop/LoopEventStore.h`), which is the
   authoritative copy; (c) restore `captureGeneration`/`frozenGeneration`/reservation count **without**
   `freeze()`/`exchangeCaptureStagingRoles()`; (d) rebind `RuntimeCanonicalPassInputs` /
   `RuntimeCompanionPasses` consumers; (e) if capacity is absent, retain the same live job and sealed extent,
   retry bounded regeneration through the existing stop worker, and retire only after normal resource
   consumption/release. No re-admission or ownerless range; see handoff §3 (DEC-079).
6. **The per-slot liveness predicate is scoped to the live ring window.** A retired entry keeps its `slot`
   (`:1195-1206` clears `owner`/`loopId` only; `slot` defaults to 0 in `include/Track.h:541`), so a full-array
   scan yields a **stale positive** that blocks release and load admission forever. `noLiveOwnerForSlot` is
   defined as: for `i` in `[head, head + count)` (mod `kCapacity`), `entries[i].slot == slot` → owner; plus any
   non-pending entry in the window is still an owner. The track-scoped guard it replaces
   (`Track::tryReleasePlaybackMergedMidiEventsMemory`, `src/Track/TrackPlaybackWindowBuild.cpp:786-800`) loops
   every slot, so the per-slot test belongs **inside** that loop, not as a pre-loop early-out.

### 10. Rerun corrections (2026-09-19, acceptance probe)

1. **The accepted switch needs a carrier and a commit point.** Removing the refusal at
   `TrackManagerCaptureQueue.cpp:329-332` is not sufficient: nothing carries the target forward after the stop is
   admitted, and `TrackManagerTransportTick.cpp:52` unconditionally calls
   `slotStateMachine.clearPendingSlotSwitch(i)`. The switch is therefore represented in the latch itself
   (`PendingInteractionRequest{kind = Switch, switchTarget = target}`) and dispatched after the interval's stop is
   admitted and bound to its own slot; the interactive/display role may transfer immediately, but the sounding
   index may not. The commit point and the `clearPendingSlotSwitch` interaction are part of the contract, with a
   fixture asserting the target is never lost (queued switch + undo + stop admission in one window).
2. **Every `getActiveLoopIndex()`-derived persistence and resolution site is re-audited for the latch window.**
   `MidPassChunkPersist.cpp:34,54` still resolves the active slot as the primary answer;
   `TrackCaptureStopCommit.cpp:139,263` and `TrackStopTelemetryColdHelpers.cpp:161,166` mark the active slot
   dirty on the stop-commit path. They must resolve the interval's slot (latch or job), not the sounding index.
   S4f.5's list is a seed, not the complete set.
3. **`DeferredValidatePolicy`** (`include/Utils/DeferredValidatePolicy.h:13-27`) must consult
   `hasAcceptedUnexecutedInteraction()` — it is a pure bool-parameter function with no `Track` in scope, so the
   **caller** (`TrackDeferredMaintenance.cpp:1269-1272`) passes the predicate — or the standing "no full validate
   on the stop path" contract can be violated when the interval's live-ness ends before the latch drives.
4. **Spec gaps:** the spec gains a scenario for the switch rule (no `activeLoopIndex` move while an interval is
   live unless its stop was admitted and bound to that slot), and the latch identity in the acceptance scenarios
   includes `slot` and `transportEpoch`, not only destination/tick/interval.

### 11. Rerun corrections (2026-09-19, boot/placement and binding/transfer probes)

1. **The stop-side DEC-067 guard chain is slot-parameterised too.** `beginDeferredOverdubStopForSlot`'s parameter
   must reach `canonicalCaptureOutputAdmitted()` and the owner checks (`TrackOverdubLifecycle.cpp:264,284,295`,
   `:1469,1473,1495`). Binding `job.slot` from the parameter is not enough: those guards resolve
   `activeLoopIndex`, and under DEC-078 the finalization occupancy survives an active-slot change, so they would
   test the **wrong** slot's canonical output.
2. **The cold boundary must actually provision.** `requestSlotSwitch`, `setSelectedSlotIndex` and
   `loadTransportSlotIndices` only queue/refresh; the sole provisioner is `Track::prewarmPlaybackForSlot`
   (boot + capture admission). S4b.6 therefore needs **both** an occupancy predicate in the commit branch and a
   **named off-clock allocation point** (extend provisioning into the S4f.3 switch path). Switching to
   `slotIfAllocated` alone yields silence or a permanently pending switch. `ensureLoopsAllocated()` can spin
   `while(1) delay(1)` if the pool is uninitialised.
3. **The existing release APIs must be converted, not supplemented.** `resetAll`, `clearAllLedgers`,
   `Track::releasePlaybackMergedMidiEventsMemory` and `TrackManager::releaseBackgroundPlaybackMergedMidiEventsMemory`
   are track-global, predicate-free, and discard `reset()`'s refusal. A successor stop on the same slot while a
   predecessor is pinned reaches `clearAllLedgers()` and erases the predecessor's ledger. They must be retired or
   converted to the `(slot, generation)` release gated by `noLiveOwnerForSlot`.
4. **`captureStopOwner` needs a generation.** It is a raw `PendingStopAdmission*` cleared by address identity
   (`TrackPreparedStopCommit.cpp:205`) while ring entries are reused at the same addresses. Carry
   `(slot, runtime generation)` and check it at set **and** clear, and bump the slot runtime generation on every
   release.
5. **Cancelling a queued switch is three calls**, not one (`clearPendingSlotSwitch` +
   `setPendingEnabledSetReplacement(false)` + `cancelSlotSelectionHold`), and `TrackManager::setActiveLoopIndex`
   must cancel/requeue rather than move. Direct `Track::setActiveLoopIndex` writes are **three**
   (`TrackManagerSlotMatrix.cpp:240`, `TrackManagerCaptureQueue.cpp:281,335`); the last two route through the
   manager.
6. **Byte correction:** the per-slot boot reservation is **40,976 B** (8 × 256 × 20 B + 8 × 2 B directory), not
   41,056 B; 60 slots = 480/512 chunks.
7. **Open-items correction:** the cold-undo descriptor field set is no longer open — §9 item 2 / S5.2a fix it
   (ids + per-pass metadata; pinned canonical bytes vs descriptor bytes).

## Revision 24 — S4f.4 is live timing re-derivation plus identity validation, not a stored epoch

**Architect decision 2026-09-23.** Revision 21.4 required the latch's epoch to be "a **new
TrackManager-level counter** bumped by transport stop, `resetToLoopStart`/`setCurrentTick`, load and MIDI
Start — **not** `transportCommandEpoch`". That requirement is **withdrawn as redundant state**:

- The drive already re-derives the **destination** from live transport state
  (`clockManager.isTransportRunning()`) and the **boundary tick** from the live clock
  (`clockManager.getCurrentTick()`); it never trusts the stored `phaseTick`. That is the property the
  epoch existed to protect ("an absolute tick is diagnostic only").
- An epoch that is never *compared* adds no protection, and adding a counter "for evidence only" would
  not improve correctness while costing RAM1 in a tree that is 840 B from the next FlexRAM bank.
- Transport **history** is covered by **consumption plus identity validation** instead: an accepted stop
  is consumed by the admission that binds it, by the transport-stop entry that retargets or completes it,
  or by a synchronous completion; the drive validates `(kind, slot, interval)` before acting. A stop
  accepted before a transport stop therefore cannot later be driven with a PLAYING destination.

The latch's `transportEpoch` field is therefore **removed** (latch 40 → 32 bytes; `CaptureStopJobs`
2,736 → 2,728), and both pins were updated and re-verified.

**S4f.4 — transport timing validity closes only on paused-worker transition tests**, because live state
alone cannot distinguish "running continuously" from "stopped and restarted":

1. **Transport Stop → Start** — the earlier stop cannot regain a PLAYING destination or resurrect a
   canceled start. *(native, green: B03 transition 1)*
2. **Seek / reset / MIDI Start** — execution recomputes the boundary in the new tick domain.
   *(no native entry: the `ClockManager` seek/reset/MIDI-Start entries are harness stubs)*
3. **Load / replacement** — the old request cannot bind to replacement content even when the slot number
   is unchanged. *(needs the load harness)*
4. **Repeated retries** — exactly one stop commits against the intended interval.
   *(native, green: B03 transition 4)*

If a test reveals missing history detection, implement a **consumed generation check for that specific
gap** — not a general epoch. Transitions 2 and 3 stay open, so **S4f.4 is not closed**.

## Revision 19 — the two named operations (2026-09-19, focused rerun input)

The second rerun closed the design gaps but left two outcomes without an operation. Both are extensions of
existing owners (`Track`, `TrackManager`); this revision adds no owner, no lease and no state machine.

### 1. `Track::parkAcceptedStopForCanonicalOutput()` — the stop-side half of B7

When an accepted stop's drive finds `canonicalCaptureOutputAdmitted()` false **for the interval's slot**, the
drive must neither hold the ring nor drop the request. It parks the accepted request in this named deferred step
(declared grains, no allocation outside the deferred turn), which:

- (a) re-runs the slot's canonical reservation through the same admission used at capture admission
  (`LoopPlaybackRuntime::prewarmCanonicalCaptureOutput`), never by re-entering `beginDeferredOverdubStop`;
- (b) re-evaluates the **full** DEC-067 guard chain against the interval's slot (S4f.0's parameter), including
  `canonicalCaptureOutputAdmitted()`;
- (c) executes the accepted stop exactly once when admitted, and otherwise reports `canonical_output_busy` as a
  **wait** attributed to the holding owner and retries on the next deferred turn.

It never freezes, exchanges staging roles or re-seals a range (that is `regenerateFrozenCaptureStaging`, §9 item
5). Invariant 31 governs: the request stays accepted; the user is never asked to press again. This is also the
fallback that keeps S2b.2's lazy admission honest when the reservation cannot be granted at the stop edge.

**The park is the canonical-output admission point, and START stays ungated.** DEC-067 deliberately moved the
canonical-output check *away* from `startOverdubbing`; gating start on it again would re-create the START denial
and need its own start-side park. So: the **record arm** keeps its existing admission
(`prewarmPlaybackForSlot`), the **accepted stop's park** acquires the reservation in a deferred turn (legal to
allocate there, exactly as `replenishCanonicalCaptureChunk` already does), and `Track::startOverdubbing` is
explicitly **not** gated on `canonicalCaptureOutputAdmitted()`. The succession case (a new interval on the same
slot while the predecessor's job still holds the output store non-empty) is handled as a stop-side
`canonical_output_busy` wait, not a start denial.

### 2. `TrackManager::provisionSlotForSwitch(trackIndex, slotIndex)` — the off-clock allocation point of B5

S4b.6 requires the inbound occupancy to exist before the boundary but names no function. This is it:

- **both call sites are MIDI-input/action edges** (`MidiButtonActions.cpp:62`, `:287`), so inline allocation is
  **not legal** there. The primary form is therefore a **deferred provisioning owner**: `requestSlotSwitch` marks
  the pending switch as "needs provisioning" and the provisioning runs in an idle owner turn (the existing
  deferred idle lane, or a child of it) before the boundary;
- **calling shape**: `TrackManager::provisionSlotForSwitch(trackIndex, slotIndex)` performs the
  `prewarmPlaybackForSlot`-style provisioning of the inbound occupancy (runtime slot, window/merged events, and
  the roles the switch needs), reported as `#MEM_DENIED` with a switch operation if the pool and heap refuse. It
  is called by the deferred owner (and by any genuinely cold caller, if one is ever added — which must be
  stated per call site);
- **what the commit does**: no allocation. `TrackManagerTransportTick`'s commit branch keeps the switch pending
  when `slotIfAllocated(targetSlot)` is null instead of falling into the allocating
  `ensurePlaybackMergedEventsForSlot`, and never leaves a silent dead switch. `ensureLoopsAllocated()` cannot
  spin `while(1) delay(1)` on this path because provisioning happens before it.

**Boundary legality is part of the contract:** the deferred owner is the allocation site, so the switch is
provisioned off the MIDI edge and the commit path stays allocation-free. Until provisioning succeeds the switch
stays pending and is reported as a wait, not dropped.

## Revision 20 — focused-pass corrections to the two operations (2026-09-19)

1. **The park's wait is scoped, and it has a terminal.** `canonicalCaptureOutputAdmitted()` is false for three
   distinct reasons — chunk-credit deficit, a **non-empty output store**, or a stale pool `lifetime_` — while
   `prewarmCanonicalCaptureOutput()` requires `store.empty()` and therefore remedies **only the first**. The park
   must:
   - state what it waits for (`chunk_credit | store_drain | pool_lifetime`) and act only on `chunk_credit` by
     reserving;
   - carry the invariant **"a retired job leaves the interval's canonical output store empty"**: the retire path
     (`TrackOverdubLifecycle.cpp:1197-1205`) drains the store (commit) or clears it, and if it can do neither the
     park routes to `regenerateFrozenCaptureStaging` (§9 item 5) instead of retrying;
   - **bound the wait** with a park-age counter and a **named terminal**, so an accepted stop can never hold the
     latch forever (`accepted_stop_finalizer_wait` counts the wait; the terminal is separate);
   - report `canonical_output_busy` **once per wait episode** (the `deferredStopStallNoted_` pattern,
     `TrackOverdubLifecycle.cpp:773-777`), not once per turn.
2. **"Ring busy" and "canonical not admitted" are mutually exclusive at the drive.** The ring entry *is* the
   owner of the canonical copy (`captureStopOwner` blocks a second admission for the slot), and
   `processDeferredIdleMaintenance` spends the whole turn in the stop grain while the ring is non-empty. S4f.2 and
   S2b.6 state this so the park does not model an impossible state, pinned by a native assert
   (`hasPendingDeferredOverdubStop() == false` on park entry).
3. **The provisioning owner is named.** `MidiServiceDrain::DeferredIdleOwner` gains a **`SlotProvision` turn
   kind** — a work kind under the existing drain, not a new owner, Manager or scheduling class — selected when a
   pending switch needs provisioning, with one declared grain and a row in `ExecutionBounds.md`. A pending switch
   makes its track eligible for a turn. `requestSlotSwitch` (both call sites are MIDI-input edges) only **marks**
   the switch; `finalizeCaptureAndSelectSlot`'s MIDI-edge callers do the same. Only the `SlotProvision` turn
   allocates.
4. **The long-loop pending hold is paired with that owner.** `TrackManagerTransportTick.cpp:213-217` holds a
   long-loop switch pending waiting for a build whose only schedulers S2b.1 removes; the hold is satisfied by the
   `SlotProvision` turn, and the pending switch carries an age/bound so it cannot pend forever silently.
5. **The `ensureLoopsAllocated()` claim is restated.** Provisioning *is* `prewarmPlaybackForSlot`, which calls
   `ensureLoopsAllocated()` first, so "provisioning happens before it" is circular. The requirement is: the pool is
   initialised before either path is reachable (boot does this today) and a provisioning refusal is a named
   `#MEM_DENIED`, never a spin.
6. **Bounds:** the park's per-turn bound plus its maximum wait/terminal are added to the deferred-owner row
   (ExecutionBounds row 19 / S4f.2's row).

### 7. Pass-4 verification corrections (2026-09-19)

- **The drain is the seal, not the retire.** `canonicalCaptureOutput.store` is emptied at the stop's
  `PrepareTransfer` seal (`TrackOverdubLifecycle.cpp:941-947` → `LoopEventStore::detachChunksToCommittedChunkIds`,
  `LoopEventStore.cpp:707-753`), not on the retire path (`:1197-1205`, which only releases `pendingPass` and
  clears `owner`/`loopId`). The only `store.clear()` today is the runtime destructor
  (`TrackPlaybackRuntime.cpp:398`), so the "drain or clear" fallback lives on the **retire** route, which is the
  only place a cleared store cannot disturb a live copy: it runs past `DeferredCleanup` and reports
  `canonical_output_drained_at_retire`. The **park** must *not* clear: a busy owner's populated store is exactly
  the mid-copy state S2b.4 refuses to release, so the park routes nothing and only reports the stall as a **new
  named terminal (`accepted_stop_park_terminal`)** in the
  `include/Utils/DebugSessionCapture.h` family, alongside `canonicalFinalizationCapacityReleased` (`:233`).
  (Corrected 2026-09-23: an earlier revision of this paragraph had the park clearing the store, which would have
  destroyed an in-flight canonical copy and executed the stop before the owner freed -- see ARCHITECTURE-REVIEW.md
  § P1 — S2b.6.)
  `accepted_stop_finalizer_wait` is prose-only today — the counter is new.
- **The park is keyed on `job.slot`, not the active slot.** The ring check, `canonicalCaptureOutputAdmitted()`
  and the native assert are all scoped to the interval's slot, mirroring the guard chain at
  `TrackOverdubLifecycle.cpp:1469-1475` — the **live edge**, which keeps `activeLoopIndex` — and matching S2b.4;
  otherwise a pending job on X and a non-admitted output on Y can coexist.
- **`SlotProvision` must also build the target's merged events** (`ensurePlaybackMergedEventsForSlot(targetSlot)`),
  or the long-loop hold at `TrackManagerTransportTick.cpp:213-215` can never be satisfied; the turn is exempt
  from the all-stopped guard (`main.cpp:463-469`) while being admitted for pending-switch tracks. The dispatcher
  edits are mandatory: enum member, `kDeferredIdleOwnerCount` 5→6, `selectDeferredIdleOwner` cursor expectations,
  and `test/test_midi_service_drain` (exhaustive switch and 5-owner cycle assertions).
- **`ensureLoopsAllocated()`:** `LoopPool` exposes only `void ensureInitialized()` / `bool initialized()`
  (`LoopPool.h:35-36`) with no denial-returning variant, so the spin cannot
  become a named denial without new surface; the requirement stays "return before it is reached", and pool init
  is independent of the removed boot prewarm (`TrackManager.cpp:53-57,67`).
- **Citation fixes:** §10.3's header is `include/Utils/DeferredValidatePolicy.h`; §11 item 5's `:84` is
  `Loop& loop = getActiveLoop()`, and the `getActiveLoopIndex()` reads are `:123,:139,:191,:206,:263`.

## Revision 21 — focused-pass corrections to the latch, the epoch, and the erasure surface (2026-09-19)

1. **One field cannot be both the interval identity and the switch target (contract contradiction).** §1 defines
   `slot : interval's slot`, while §10.1/S4f.3 use `kind = Switch, slot = target`. A Switch latch would overwrite
   the interval-slot binding and reproduce the wrong-slot canonical-output bug §11.1 exists to prevent. Split the
   record: `slot` (the interval's slot, immutable for the interval) and `switchTarget` (only for
   `kind = Switch`). Precedence becomes three-way: **stop > switch > start**; a `(kind, slot, switchTarget,
   interval)` match makes a repeat a no-op.
2. **The stop admission's `Loop`-derived half is parameterised too.** S4f.0 names only runtime sites; the
   guards and bindings `loop.hasActiveOverdubTransferAdmission()`, `loop.canHandoffSessionExitSnapshot()`, the
   staging block, `job.owner = &loop` and `job.loopId` all come from `getActiveLoop()`
   (`TrackOverdubLifecycle.cpp:263,290,300,316-323,426,451`) and must use `getLoop(slot)`. The sites at
   `:1469,:1473,:1495` are inside `Track::stopOverdubbing` — the **live edge** — so they keep `activeLoopIndex`
   and are not part of the deferred parameterisation.
3. **The write authority needs a non-re-entrant commit point.** Routing `TrackManagerCaptureQueue.cpp:335`
   through `TrackManager::setActiveLoopIndex` recurses (set → `finalizeCaptureAndSelectSlot` → set); keep a private
   commit-point write (or split `applyActiveLoopIndex`), and add `hasAcceptedUnexecutedInteraction()` to the
   manager guard at `TrackManagerSlotMatrix.cpp:23`, which today sees only `hasPendingDeferredOverdubStop()` and
   therefore lets a latched stop fall through to the raw write at `:27`. Put the three-call cancel in one named
   helper.
4. **A real transport epoch.** `transportCommandEpoch` is a per-admission 0→1 retarget flag
   (`TrackPlaybackRuntime.h:99`; set from a hardcoded 0 at `TrackOverdubLifecycle.cpp:403`; incremented only by
   `PendingStopAdmission::requestTransportStop`, `TrackPlaybackRuntime.cpp:235`), so it is neither monotonic nor
   available before admission. The latch's epoch is a **new TrackManager-level counter** bumped by transport stop,
   `ClockManager::resetToLoopStart`/`setCurrentTick`, load, and MIDI Start; it is declared under invariant 36 and
   pinned by S3.0. It is explicitly **not** `transportCommandEpoch`.
5. **The erasure surface is wider than the four named APIs.** `Track::sendAllNotesOff()`
   (`TrackPlaybackHotPath.cpp:608`) calls `clearAllLedgers()` at `:618` **predicate-free**, and transport stop
   invokes it for every track (`TrackManagerTransportTick.cpp:61,73,77,84`) — inside the latch window. It joins
   the S4.1/S4f.5 conversion list. `finalizeCommitSideEffects` (`TrackCaptureStopCommit.cpp:81`) resolves
   `Loop& loop = getActiveLoop()` at `:84` and reads `getActiveLoopIndex()` at `:123,:139,:191,:206,:263`
   (undo-entry pushes), so `slot` must be threaded through
   `finalizeCommitSideEffects`/`commitCaptureForStop`. `DeferredValidatePolicy` is a bool-parameter pure function
   (`include/Utils/DeferredValidatePolicy.h:13-27`) with no `Track` in scope: the **caller**
   (`TrackDeferredMaintenance.cpp:1271`) passes the predicate. And `MidPassChunkPersist.cpp:34` runs *before* the
   job fallback at `:46`, shadowing it — `Track::captureStopJobHead()` is the accessor to use.
6. **`(slot, generation)` needs state that does not exist yet.** `PendingStopAdmission`
   (`TrackPlaybackRuntime.h:90-184`) has no `slot`, and `completeDeferredCleanup` has no slot parameter, so S4.2's
   generation-qualified clear needs a field/signature change; and there is no per-slot runtime generation field
   (only `canonicalReleaseMarkerArmed`). Both are added under S3.0's invariant-36 pin.
7. **Load deferral must be pre-check-before-pop.** Re-queueing through the existing API is lossy:
   `queueDeferredLoopSlotRestore` early-returns unless `needsSlotLoad`, which is false once
   `isLoopSlotRestoreAttempted` is set or the slot has committed passes — exactly the finalizing-predecessor
   state. S2b.5 specifies the pre-check (leave the entry queued; predicate via `captureStopJobHead()`), and names
   the predicate over `job.owner`/`job.slot`.

## Revision 22 — focused-pass corrections to cold undo and the start side (2026-09-19)

1. **A cold descriptor is a row set, not an id + metadata pair.** The row carries only
   `id/committedChunkIds/state/sealedAtTick/mergeSequence/overdubSessionIndex` (`include/LoopPasses.h:57-71`);
   "tick span" is a chunk property and "note count" is stored nowhere; and reconstruction resolves ids against the
   **live** row set (`TrackUndo.cpp:331,354` via `disableCapturePass`; `:362,385` via `setEditPassState`), so an
   id+metadata descriptor fails once the row is erased. `LoopSnapshotRef` (`shared_ptr<PersistedLoopSnapshot>`
   carrying a deep-cloned whole `LoopPasses`) is the ClearSlot carrier, **superseded by §6 below: it is not
   reused** (its deep clone keeps internal-heap `EditPassVec`/`MidiEventVec`, and the spec forbids a `LoopSnapshot`
   reference or slab in a cold descriptor). A cold descriptor is therefore a **new, externally allocated
   `ColdPassDescriptor` = whole row set + edit-pass ids**. Do **not** add `PassIdList`/`EditPassIdList`
   vectors: their `InternalHeapFirstAllocator` is RAM1 and violates invariant 36.
2. **ClearSlot eviction drops the whole entry, not the root.** A null `beforeSnapshot` makes
   `restoreLoopSnapshot` call `resetPassTimeline()` while `applyUndoEntry` still returns true and consumes the
   entry — a silent slot wipe. Eviction removes the **entry**, repairs the cursor (the `TrackUndo.cpp:194,203-227`
   precedent), preserves the protected reserve, resets `nextEntryId` when empty, and reports the loss as **entry
   loss**. The spec's "redo invalidated only by a new committed edit or capture" gains the cap as a *reported*
   eviction (or the cap is scoped per slot so redo pairing is untouched).
3. **Deferred reconstruction needs a drive point and a reachability predicate.** `hasRealtimeIdleWork`
   (`main.cpp:40-42`) is false for a stopped, non-audible track while another track plays — exactly the demoted
   cold-undo case — and `DeferredIdleOwner` has no reconstruction value. Add a turn kind (or extend the existing
   lane) and `hasPendingDeferredReconstruction()` in the eligibility and in the in-flight predicate. Deferral must
   also be reflected in `pushUndoEntry` (which sets `cursor = entries.size()`, marking the unexecuted head done),
   `dropRedoBranch`, trim/erase/reposition, `noLiveOwnerForSlot`, and load admission; the wait resolves by entry
   id (or index + generation). **All-stopped admission:** the reconstruction turn must not be swallowed by the
   all-stopped guard (`main.cpp:463-468`, which today returns unless the owner is `VisualCacheWrapAppend`, or
   `Snapshot` with stopped-clock work) — exempt it there or route it through the focus-track lane
   (`:470-504`), exactly as `SlotProvision` is exempted in §20.3.
4. **The start side is best-effort and non-denying.** `startOverdubbing` returns `void` and its callers log "ok"
   unconditionally, so a start-side canonical reservation could silently drop a start and would reinstate the
   DEC-067 START gate. Revision 19 already resolved the gate: START is **not** gated on the canonical output; the
   park (S2b.6) is the admission point. What remains is observability — where a start cannot be admitted at all,
   the latch carries `kind = Start` (S4f.1) and the caller reports the acceptance, so the user never re-presses
   and the caller never logs an admission that did not happen.
5. **Load deferral is peek-before-pop.** `popNextDeferredLoopSlotRestore` removes the entry before the call and
   treats `hasCommittedPasses()` as done; re-queueing is inert because `needsSlotLoad` is false once the slot has
   committed passes or the attempted bit is set. Use `peekFirstPendingLoopSlotRestore` (or a forced enqueue) with
   the `job.owner`/`job.slot` predicate, so the request survives the deferral.

### 6. Pass-4C verification corrections (2026-09-19)

- **Citations.** The row set is `include/LoopPasses.h:57-71` (`RecordPass` `:57-62`, `OverdubPass` `:64-71`;
  container `LoopPasses` `:106-110`), and `src/TrackUndo.cpp:362,385` resolve through `setEditPassState`
  (`:266-292`), not `setCapturePassState`.
- **Descriptor type decision.** The cold descriptor is a **new, externally allocated `ColdPassDescriptor`**
  (row set + edit-pass ids). `LoopSnapshotRef` is **not** reused: it is the ClearSlot carrier the cold tier
  exists to avoid, its deep clone keeps internal-heap `EditPassVec`/`MidiEventVec` (`sharePassesSnapshot` uses a
  plain `std::make_shared`, `LoopEditPasses.cpp:59`), and the spec forbids a `LoopSnapshot` reference or slab in a
  cold descriptor. The byte budget charges the descriptor's **full** footprint including nested vectors, which
  must be external or flattened. `UndoEntry` already carries `passIds`/`editPassIds`, so the new struct adds only
  the row set.
- **Lookup helper.** No lookup-by-id helper exists (`TrackUndo.cpp:126-132` exposes only the tip). Name its owner
  (`TrackUndo`/`GlobalUndoStack`) and the enforcement point that makes "head deferred ⇒ older entries
  unreachable" hold under `pushUndoEntry`, trim, `dropRedoBranch` and reposition
  (`GlobalUndoStack.h:201`, `TrackUndo.cpp:1262,1278`).
- **Load admission** joins the deferred-reconstruction predicate (`noLiveOwnerForSlot`), not only release.
- **Citation fix:** `main.cpp:803` gates the visual-cache wrap-append scan, not maintenance; the maintenance gate
  is `:591-598`.
- **Start-side report.** The unconditional "ok" is the session-capture label at
  `TrackManagerCaptureQueue.cpp:221-222,236-237`; S4f.6 still needs a named acceptance report (a diagnostic name
  or counter read from the latch), so a start that cannot be admitted is never logged as admitted.

## Revision 23 — owner corrections at approval (2026-09-19)

Approved with two specification corrections and three clarifications. No structural change.

**Owner approval recorded 2026-09-19** (review of revision (4)): approved with two corrections — both landed below
(items 1–2) — plus the transaction-aware eviction rule (item 3), the implementation order (item 4) and the
mandatory continuous device session (item 6). Design revisions stop here per item 5.

1. **The wait counter is renamed: `finalization_lease_stop_denied` → `accepted_stop_finalizer_wait`** (DEC-078
   item 10). The old name described the architecture this change removes; a future trace must not read "denied"
   and conclude DEC-078 was violated. The pre-fix counterfactual is unchanged:
   `successor_stop_denied_capacity` + `reentryDecision("deny", …)`. Telemetry now reads
   `accepted_stop_finalizer_wait` / `accepted_stop_park_terminal`.
2. **`accepted_stop_park_terminal` is a fault/escalation state of the execution mechanism — never cancellation.**
   The accepted interaction remains authoritative until it is (a) executed, (b) explicitly superseded by a later
   interaction under the defined supersession rules, or (c) the session is destroyed/reset. The park does not
   abandon the stop and the interval is not left recording in place of the accepted stop. Invariant 31 is the
   authority; a timeout that cancels an accepted interaction would be a new DEC, not an implementation detail.
3. **Cold-undo eviction is transaction-aware.** The eviction unit is the **oldest prefix that can be removed while
   leaving the oldest retained descriptor independently reconstructable from canonical state** — not merely the
   oldest entry, because a retained descriptor may depend on a predecessor state that trimming would make
   unreconstructible. A descriptor names a reconstructable logical state, not a delta against a runtime root; the
   per-slot stack is the unit and the device-wide budget selects which slots' prefixes to trim.
4. **Implementation order (owner):** S0/S1 measurement and the red proof → **S2b** boot/load admission (it fixes
   today's arm/record regression directly) → S3/S4 ownership identity and generation-qualified release → S4f
   DEC-078 accepted-interaction latch (including STOP→START→STOP) → S5 cold undo → then the playback-window/root
   decoupling that raises N. **The 32/64 envelope must not block fixing today's residency defect.**
5. **No further broad design revisions.** Revisions stop here unless implementation uncovers evidence that
   contradicts the contract; such findings return to the architect as targeted corrections, not another pass.
6. **Mandatory device acceptance** (S7): one continuous session — load ~60 recorded slots, play several, arm and
   record a long empty slot, STOP it, switch immediately, START overdub B, STOP B once while A still finalizes,
   continue transport, B stops exactly once, switch to C and record — requiring **0 user-facing denied
   interactions, 0 second presses, 0 lost capture, 0 wrong-slot publication, 0 stale-generation release, 0
   playback interruption, bounded MIDI gap, pool residency returning after owners retire, and steady-state RAM
   independent of the number of cold recorded slots**.

## Heap-correctness prerequisite

The S1.6 execution packet in [ARCHITECTURE-REVIEW.md](ARCHITECTURE-REVIEW.md) defines the accounting repair, failure attribution and subsequent owner-fix gate. Capacity sweeps pause until allocation failure is attributed and corrected; accounting repair alone is not a capacity fix.

### S1.6d — stopped heap diagnostics

DEC-084 extends MemoryMonitor with explicit session-capture `!HEAP_STATS`. Reject transport, external clock, track work and deferred storage/prewarm before reading allocator metadata. The command performs bounded, non-mutating scans of pinned newlib free lists and smalloc gaps, reports completeness/duration and distinguishes free chunks, sbrk tail, payload and allocator occupancy. No live scan, allocation probe or malloc pointer ledger. This is diagnostic evidence for S1.6b attribution; it neither changes allocator admission nor closes S1.6c owner correction. See the execution packet in ARCHITECTURE-REVIEW.md and the internal/external memory guide for units and limitations.

### S1.6c.1 — known owner lifetime fixes

RuntimePlaybackView releases origin and rank arrays alongside its event storage at the existing whole-take release boundary. The selected-bridge refusal remains unchanged. DisplayManager borrows its previous pending-paint allocation as synchronous compose scratch and returns the old compose allocation before painting transforms; a scope guard returns borrowed storage on early exits. This eliminates the fresh candidate allocation without adding a persistent scratch or merging compose/paint authority. Slide-window filtering preserves note identity, geometry and order in place. No shortened display horizon or admission/routing change. Aggregate capacity revalidation remains a separate oracle.

Pending-paint growth explicitly retires obsolete backing before copy assignment when the composed source exceeds capacity. This avoids target libstdc++'s allocate-copy-free overlap; compose remains intact and capacity-fitting frames retain reuse. No record/display rows are dropped to fit memory.

### S1.6c.2 — snapshot construction reuse

DEC-085 retains the existing note/event apply workspaces for one construction. Their mutable pitch, bar-page and membership directories identify candidate-private nodes; subsequent changes reuse those nodes, updating chain tails and page counts on append. Active-work flags separate cursor completion from workspace lifetime. Published parents and sibling constructions remain immutable, and completed constructions cannot resume mutation. No allocator replacement, published-node refcount heuristic, membership-capacity change or playback-policy change. Lookup work remains proportional to affected private edits; each membership copy/remove/sort grain retains its existing page bound.

Role-specific first-record reservations are not removed by this change: current capture admission also guarantees deferred-stop and immediate-overdub resources. A replacement role transition/admission contract must preserve those guarantees before the 500 KiB whole-take reservation can be omitted safely.

## Automatic runtime metadata repair at boot

DEC-089 — boot metadata repair follows the approved source-preserving boot sequence.
[Storage architecture](../../../docs/Authority/Architecture/Storage.md#automatic-runtime-metadata-repair-at-boot)
is normative for prerequisites, candidate/backup publication, replacement settings,
write blocking and cold-setup bounds. The execution packet and host evidence are in
[ARCHITECTURE-REVIEW.md](ARCHITECTURE-REVIEW.md#automatic-boot-repair--execution-packet-2026-09-26).
Device installation and recovery acceptance remain separate gates under the incident hold.
