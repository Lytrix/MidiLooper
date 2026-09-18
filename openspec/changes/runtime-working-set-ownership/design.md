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

## Revision 1 — window sizing is a prerequisite, not a safeguard

Changing `recorded slots` to `active slots` still multiplies today's per-slot cost by the product
envelope (16 tracks x 2 active slots = 32):

```
32 x  845 KB  ~=  27 MB
32 x 1.47 MB  ~=  47 MB
```

against ~8 MB PSRAM. So the third quantity below must be designed down before the pool ceiling is
defined:

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

Taking ~8 MB total, minus the ~1 MB capture ring and ~1 MB backed chunk pool, minus a canonical
allowance and a transient reserve, leaves roughly **3-4 MB for leases**, i.e. **~100-125 KB per lease if
all 32 logical active slots are to sound simultaneously**. The present lease is ~845 KB. Either window
sizing reaches that order, or the plan states the *actual* simultaneous-playback cap as a capability
bound. This turns the active cap from a policy guess into an execution bound.

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
