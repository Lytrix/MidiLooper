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

## Revision 1 — the envelope, and why windowing is a prerequisite

The requirement, pinned:

| Envelope | Value |
|---|---|
| Tracks | **16** (`NUM_TRACKS` is 8 today) |
| Slots sounding per track | **2** |
| Slots sounding simultaneously | **up to 32** (2 x 16), any loop length |
| Slots holding the complex role (record / overdub / passes / undo / redo / display, resumable small-packet logic inside MIDI boundaries) | **exactly 1**, transferable |
| Transport | **never stops** -- switching, record/overdub start and undo/redo happen while playing |

So the resident set is **up to 32 playing leases + 1 complex lease** plus transfer overlap, and at today's
cost that is 32 x 845 KB ~= **27 MB**, or 32 x 1.47 MB ~= **47 MB**, against ~8 MB. **Windowing is the only
way the envelope fits**, and it must also make the cost length-independent: a 64-bar slot costs what a
4-bar slot costs, with playback crossing window boundaries seamlessly. Consequently:

| Quantity | Value | Backed by |
|---|---|---|
| Library residency | ~60+ recorded slots | canonical descriptors + persisted content |
| Logical active slots | product envelope, e.g. 16 x 2 | role tiers (not all necessarily resident) |
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
stated simultaneous-playback bound (`k` slots per track, `k < 2`) rather than a claim that the envelope is
met.

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

## Revision 4 — undo history at cold needs an explicit policy

Undo/redo roots can keep snapshot residency alive after the runtime is released, so

```
60 cold slots x multiple undo snapshot roots
```

can defeat the law even with the playback runtime released. Candidate policies:

| # | Policy | Consequence |
|---|---|---|
| A | Cold slots retain undo; undo roots count against a **separately bounded global history budget** | Undo fidelity preserved; needs its own budget and admission |
| B | Demotion **compacts undo to lightweight pass identities** | Cheapest memory; loses inter-pass undo granularity while cold |
| C | Demotion **seals/drops runtime undo history** | Simple and predictable; cold slots have no undo until reload |
| D | Undo history is **evictable and reconstructible** | Best of both, most work; needs a reconstruction path |

Note the existing persistence contract already yields zero undo depth after load, so C or B may be
acceptable -- but that is a **product-behaviour decision**, and the plan must not claim flat cold-slot
memory until it is pinned (open decision 1). The owning files are `src/TrackUndo.cpp` and
`src/PassReclaim.cpp`; `GlobalUndoStack` retention is already the subject of RC-MEM-3.

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

A single "exactly one complex role" is incompatible with what is already landed: DEC-067 supports a
**deferred/finalizing predecessor (A) alongside an independently live recording successor (B)**
simultaneously, so the runtime cannot have one indivisible record/overdub/passes/undo/redo/display role.
The architecture already distinguishes live capture ownership from deferred finalization ownership and
allows both concurrently; the new model must not design that away.

| Role class | Cardinality | Notes |
|---|---|---|
| `PlaybackWindowLease` | up to **32** (2 per track, 16 tracks) | must be small and length-independent |
| `LiveCaptureLease` | **1** | record/overdub ingress, staging, capture stop jobs |
| `DeferredFinalizationLease` | **1** | successor-reentry predecessor, concurrent with a live successor |
| `InteractiveLease` | **1** | edit / undo / redo / display, unless evidence requires more |
| `TransferOverlap` | measured | explicitly budgeted, not slack |

Classes are **composable**: a slot may hold several at once, and each owns only the resources its operation
needs. Assignment is generation-qualified per class, so releasing a playback lease cannot disturb a live
capture or deferred finalization lease on the same slot.

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
