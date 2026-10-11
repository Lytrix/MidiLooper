# S7.5a — holder, reserve and competitor budget: recorded active-set choice, incomplete proof

**Status: representative active set specifically approved 2026-10-11; remaining budget lines unproved.**
The owner recorded **8 sounding + 1 capture + 1 finalizer + 1 switch**, as preserved in
[`DECLARED-LIMITS.md`](DECLARED-LIMITS.md) §6/§7. This is approval of the selected workload, **not** a fresh
whole-memory or timing guarantee, approval of every inventory line, or reduced Cmax/Dmax/Omax.
The historical source-derived **5,471 KiB / margin 2,721 KiB** calculation is provisional/incomplete.
Current timing limits and oracle are in **DECLARED-LIMITS.md §2–§3**; the proposal companion is historical,
superseded and unapproved as a version. This table answers `DECLARED_ENVELOPE.md` §6 and `tasks.md`'s
S7.5a — continuity-envelope budget obligation; other proofs remain owed.

It changes no firmware, test, bound class or checkbox, and **S7.5a stays unchecked**. `DEC-100` still governs: the
envelope is reported, never enforced at admission.

## 1. What the spec requires of this table

The continuity requirement asks the envelope to declare, in one place: *"simultaneous sounding/capture/finalization/
interaction holders, loop lengths, effective event density and same-tick bursts aggregated per physical route,
sustained input/output rates, window span, tempo and clock-source/advancement assumptions, resource headroom and
capture replenishment, and competing persistence/display/load work"*, covering *"the intended supported topology
rather than silently reducing concurrency to obtain a pass"*, with unsupported combinations and unproven targets
named.

Three rules decide the shape of this document, all quoted from that requirement and from the per-lease-class budget
requirement beside it:

1. **Per lease class, not per device.** *"each per-holder cost SHALL be multiplied by its maximum simultaneous holder
   count"*, and *"live capture and finalization SHALL NOT be budgeted as device-wide singletons"*.
2. **Shared backing is counted once**, and *"an envelope that exceeds capacity is reported as unproved rather than
   silently priced as one holder"*.
3. **A row may only be reserved as a timing guarantee if its class permits it.** From
   [`ARCHITECTURE_RULES.md`](../../../../../docs/Authority/ARCHITECTURE_RULES.md) § Execution bound rules: *"Only
   **structurally bounded** units, or **empirically bounded** units with an explicit overrun guard and failure policy,
   may be treated as timing guarantees."* A line whose owner is **Unbounded** cannot be reserved — it must be named
   as a competitor the envelope tolerates, not one it schedules.

Rules 1–2 are already derived elsewhere and are **cited, not re-derived**: `S2C1-PLAYBACK-WINDOW-LEASE.md` derives
the window-lease class `P`, and [`S2C23-CLASS-BUDGET.md`](../../S2C23-CLASS-BUDGET.md) charges every class against the
pool with source-derived maxima. What is new here is doing the same for the **continuity** envelope: naming each line,
its unit, its source and whether it may be reserved.

## 2. Holders — the residency lines

Values are the device-ABI KiB figures already recorded in `S2C23-CLASS-BUDGET.md` §3–§5; the per-holder derivation
lives in `S2C1-PLAYBACK-WINDOW-LEASE.md`. **Nothing is re-measured here.**

| # | Holder class | Symbol / source of the maximum | Max simultaneous | Per holder | Line total | May be reserved? |
|---|---|---|---|---|---|---|
| H1 | sounding playback window lease `P` | `LoopPlaybackRuntime` window; `N` = 8 today (8 tracks x 1 sounding slot) | **N = 8** | **331 KiB** windowed (`831 KiB` whole-take) | **2,648 KiB** | yes — but the *claim* is provisional (§6) |
| H2 | live capture `C` | `Track::hasLiveCaptureInterval`; one live capture per track | **Cmax = 8** = `Config::NUM_TRACKS` | **383 KiB** | **3,065 KiB** | yes |
| H3 | deferred finalization `D` | `Track::hasPendingDeferredOverdubStop`; one admitted stop per track | **Dmax = 8** = `Config::NUM_TRACKS` | **308 KiB** | **2,464 KiB** | yes |
| H4 | interactive lease `I` | the selected interactive holder | **Imax = 1** | byte figure **owed** (`IMPLEMENTATION_CONTRACT.md` §2 names it as an input) | — | yes, once priced |
| H5 | inbound switch preparation `O` | `SlotStateMachine::hasPendingSlotSwitch`; at most one inbound preparation per track, and a replaced target retires its preparation first | **Omax = 8** | **331 KiB** | **2,648 KiB** | yes |

**Named, not silently shrunk:** the descriptor ring's own capacity is **not** a finalizer count.
`CaptureStopJobs::kCapacity` is **4** descriptors per track (`include/Track.h`), i.e. 32 across eight tracks — four
record stop jobs per track, matching `DECLARED_ENVELOPE.md` §6. `Dmax = 8` counts *independent retained jobs on eight
tracks*; it is not evidence that 32 finalizers are provisioned, and this document does not price them as such.

## 3. Reserves — the lines that are held, not leased

| # | Reserve | Value | Source (verified in the tree) | What it protects | May be reserved? |
|---|---|---|---|---|---|
| R1 | window admission limit `L` | **2,048 events** per window on one route | `include/RuntimePlaybackView.h` `kEventsPerView`, `kWindowAdmissionLimit` (the window's storage capacity itself, no partition multiplier) | accepted content is never truncated to fit | yes — structural (fixed storage) |
| R2 | window closure reserve | **128** slots, counted **inside** `L` | `include/RuntimePlaybackView.h` `kWindowClosureReserve` (per-bar worst case of the two-bar comparison span) | the closure pass that must land inside the admitted window | yes — structural |
| R3 | capture ring | **1 MiB** | `src/Utils/DebugSessionCapture.cpp` `kCaptureRingBytes = 1024 * 1024` | the shed-free evidence window the acceptance surface reads | yes — but this is *evidence* headroom, not musical content |
| R4 | record reserve | **32 KiB** | `src/Utils/DebugSessionCapture.cpp` `kRecordReserveBytes = 32 * 1024` | Tier-A record rows (the identity edges the oracles read) | yes |
| R5 | canonical chunk admission | **1 chunk/turn** replenish (`CHUNK_CAPACITY` = 256 events, `POOL_CHUNK_COUNT` = 512) | `src/Utils/LoopEventStore.h`; `Track::replenishCanonicalCaptureChunk` | the accepted stop's own canonical copy (S2b.6 park) | yes — structural (one replenish per turn) |
| R6 | capture staging | **2 per track**, `kCaptureStagingEvents` = 2,048 events each | `include/RuntimePlaybackView.h`; `RuntimePlaybackView.cpp` staging accounting | live capture ingress while the stop finalizes | yes — structural |
| R7 | stop-job descriptors | **4 per track** (`CaptureStopJobs::kCapacity`), **32** across the supported topology | `include/Track.h` | bounded finalization admission; `queue_full` stays exceptional descriptor exhaustion | yes — structural |
| R8 | accepted-interaction latch | **32 B per live interval**, no descriptor | `Track::PendingInteractionLatch`, `Track::placementPins()` | one accepted unexecuted interaction survives a full owner | yes — structural and size-pinned (S4f.9) |
| R9 | undo protected horizon | **value owed** | `include/TrackUndo.h` (`undoDepthForLoop`) — no envelope line exists | undo residency for the protected entries | **not yet declarable** |
| R10 | `canonical` / `roots` / `visual` / `standby` / `reserve` terms | **owed** | `S2C23-CLASS-BUDGET.md` §7 | all resident event bodies until S6; whole-loop roots until phase F; display cache; standby; the pool margin | **not yet declarable** |

## 4. Competitors — the work that shares the interval path

Each row is the *declared contention* the envelope must tolerate, with its own bound class, because rule 3 decides
which may be reserved. Bound classes are cited from
[`ExecutionBounds.md`](../../../../../docs/Authority/Architecture/ExecutionBounds.md) by row.

| # | Competitor | Owner and entry | Declared quantum / guard | Bound class | May be reserved as a timing guarantee? |
|---|---|---|---|---|---|
| K1 | deferred persistence save | `StorageManager::processDeferredSaveState` via `DeferredSaveScheduler` | 300 µs / 3,000 µs between steps (row 14) | **Empirically bounded** | Conditional on explicit overrun guard/failure policy; **not a bound on indivisible SD write latency**, nor a hard 6,250 µs cycle guarantee |
| K2 | deferred load / restore frame | `DeferredJobScheduler::runFrame` -> `LoadLoopJob` (row 15) | 250 / 1,200 / 5,000 µs between steps | **Empirically bounded** | Conditional on explicit overrun guard/failure policy; **between-step checks do not bound indivisible SD operations** or guarantee the combined cycle |
| K3 | display resolve phase | `DisplayManager::update` -> `resolveDisplayNotesByDrawSource` (row 3) | 5,000 µs measurement + overrun reuses the prior frame | **Empirically bounded** | **yes** — the overrun policy *is* the guard |
| K4 | display draw + transfer | `DisplayManager::update` -> `_display.api.display()` (row 4) | fixed frame size | **Structurally bounded** (size), blocking | yes as a bound; contributes wall-clock blocking time |
| K5 | rolling playback lane service | `Track::stepPlaybackViewMaintenance` (row 27) | declared caps: one grain per admitted turn (snapshot freeze, ≤64 qualification/fill units, ≤48 order/rank units, ≤64 retirement units) | **Unbounded — implemented, not admitted** (caps declared, bounds unmeasured) | **no** — the envelope may not reserve the lane it is trying to qualify, which is why §6's claim stays provisional |
| K6 | per-track deferred maintenance | `Track::processDeferredIdleMaintenance` x 8 (row 19) | per-track only | **Unbounded in aggregate** | **no** as a device-wide line; the accepted-interaction child is separately bounded (row 31) |
| K7 | edge-effect drain | `TrackManager::drainEdgeEffects` (row 32) | ≤ 16 records per call, fixed per-record work | **Structurally bounded** | yes |
| K8 | accepted-interaction drive | `Track::serviceAcceptedInteractionTurn` -> `parkAcceptedStopForCanonicalOutput` (row 31) | one grain per turn: one admission attempt plus, on refusal, one replenish and one report; skipped while the ring is non-empty | **Structurally bounded** | yes; device latency for acceptance-to-execution remains open (that is `LIMITS-AND-ORACLE-PROPOSAL.md` L1.7/L1.8, not a residency line) |

**Class citations are not fresh elapsed-time proof.** K1/K2/K3 identify declared contention and existing empirical
classes/guards; architecture requires an explicit overrun guard and failure policy for any claimed guarantee.
A local budget checked between steps cannot bound an indivisible step. In particular unbounded SD latency and
unmeasured composed costs prevent deriving a hard **6,250 µs** cycle maximum from these citations.
K5/K6 remain **Unbounded**, not reservable guarantees. Size/grain bounds in the other rows are not by themselves
measured endpoint latency under the complete supported workload.

## 5. The arithmetic, as it stands

**Conservative case — every holder at its maximum at once (recorded in `S2C23-CLASS-BUDGET.md` §4):**

```
fixed       1,801
N*P         8 x 331 = 2,648
Cmax*C      8 x 383 = 3,065
Dmax*D      8 x 308 = 2,464
Omax*O      8 x 331 = 2,648
--------------------------------
sum                12,626 KiB  vs  8,192 KiB pool   ->  OVER by 4,434 KiB
```

**Representative concurrent states — historical source arithmetic, not complete fresh device qualification (same source, §5):**

| concurrent state | KiB | vs 8,192 KiB |
|---|---|---|
| N = 8 sounding, nothing else | 4,449 | fits, margin 3,743 |
| + 1 live capture | 4,832 | fits, margin 3,360 |
| + 1 capture + 1 finalizer | 5,140 | fits, margin 3,052 |
| + 1 capture + 1 finalizer + 1 in-flight switch | 5,471 | fits, margin 2,721 |

The last row is the historical **transient headroom** arithmetic. The cited S4 census settled at **6,505 KB**
at N = 8 with one take's runtime resident; neither that census nor the fitting source sum prices all required terms.
Interactive `I`, undo protected horizon and `canonical` / `roots` / `visual` / `standby` / `reserve` remain owed.

The conservative sum is over capacity at **any** `N`: the recorded C/D/O subtotal is **8,177 KiB** before
fixed/playback costs. All maxima remain **8**; no simultaneous count is silently reduced or class remeasured.
The conservative **12,626 KiB**, **4,434 KiB over** the pool, remains **not fitting and unproved**.

## 6. Recorded active-set choice and remaining proof scope

1. **Approved selected workload:** **8 sounding roles (8 tracks × 1 sounding slot) + 1 live capture + 1 deferred
   finalizer + 1 in-flight switch**. Historical arithmetic **5,471 KiB**, margin **2,721 KiB**, is preserved,
   not a complete capacity guarantee. Device validation of simultaneous multi-track work remains owed.
2. **Topology, not derived capacity:** `N = 8`; historical `N ≈ 13` arithmetic and `N × P` remain provisional
   until phase F. **16/32/48/64** are named targets, not measured claims.
3. **Unchargeable/resource gaps:** H4 `I`, R9 undo horizon and R10 resident terms remain named unproved lines;
   selected-workload approval does not waive them or qualify the all-at-maximum case.
4. **Contention:** the table retains class citations without upgrading them to a hard cycle guarantee.
   K1/K2 between-step checks do not bound SD latency; K5/K6 cannot be reserved.

## 7. What this document does not do

It records the specific active-set approval and cites historical costs; it does not approve every inventory line,
re-measure memory, change pool arithmetic, shrink counts, implement a guard, or claim a new device leg.
It does not price `I`, undo, `canonical`, `roots`, `visual`, `standby` or `reserve` (§3 R9/R10).
Current timing limits/oracle live in **DECLARED-LIMITS.md §2–§3**, not the superseded proposal.
Simultaneous multi-track validation and complete memory/timing qualification stay owed; no checkbox is closed.
