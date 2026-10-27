# Production contracts — the five readiness blockers

**Status: all five contracts ratified and all four production packets specified; nothing implemented.**
[P-A](packets/P-A-CUSTODY.md), [P-B](packets/P-B-SOURCE.md), [P-C](packets/P-C-FINALIZATION.md) and
[P-D](packets/P-D-VISUAL.md) exist, each with its named RED demonstrated. **DEC-160** ratifies the
undo/lifetime semantics C4 needed. This file itself generates no code.

This file resolves the five contract-level unknowns that [readiness](evidence/READINESS.md) left
BLOCKED — not by asserting capacity, timing or a new capability, but by fixing the owner, fields,
transition set, denial tokens, work unit, cancellation rule and independent oracle of each required
change. Measured terms stay UNKNOWN and are named, never zero. Task 2.2 — production packet
generation stays unchecked.

Authority: [design](design.md) decisions1–5; **DEC-159 — immediate first-overdub sealed custody**
(direction only); **DEC-142 — Record presentation lifetime split**;
[execution bounds](../../../docs/Authority/Architecture/ExecutionBounds.md) bound classes, large-work
rules and owner-inventory rows5, 8, 10, 27, 28, 31;
[Naming](../../../docs/Authority/NAMING.md) for `pass`, `Capture`, `editPass`, slot, overdub.
Baseline `faa089b8` plus the existing dirty source on `feature/unified-rolling-playback-path`.

Boundary of this file: it may cite and refine an existing owner, it may not delete a guard, alias a
sealed range, add a second reader, promote an unbounded row, or claim a timing envelope. Where a
clause needs a number, the clause says which command produces it and what happens while it is absent.

## Reading rule

Every clause is one of:

| Tag | Meaning |
|---|---|
| **VERIFIED** | Observed in the current source or in the P1 evidence files. |
| **CONTRACT** | Fixed here; production must implement exactly this. |
| **BLOCKED** | Needs a target measurement or a ratified decision before it can be implemented; the measurement procedure is named. |

Clause identifiers are `C<n>.<m>` so a packet, test, diagnostic or review can cite one clause.

---

## C1 — independent sealed custody and early Stop

**Owner:** `Track` (interval identity, sealed custody) · `TrackManager` (admission) ·
`LoopPlaybackRuntime` (the single canonical worker/output). No new Manager, no SD schema.

**Why the trace stops here today (VERIFIED).** `hasPendingDeferredOverdubStop()` is
`captureStopJobs_ && count != 0` (`src/Track/TrackStateFacade.cpp:145`): a *per-track* "any pending
job". `beginDeferredOverdubStopForSlot` therefore denies while A finalizes — at the per-track
predicate, then `finalizer_busy` (`runtime->captureStopOwner != nullptr`), then
`canonical_output_busy`. Even if those passed, the seal destination is
`captureStopJobs_->presentations[slot]` (`src/Track/TrackOverdubLifecycle.cpp:1169`), one record per
slot, still armed by A. The four-entry FIFO depth is not custody: there is one extent per slot.

**C1.1 — the custody record is interval-qualified, not slot-qualified (CONTRACT).** A new
externally allocated per-Track array holds one record per *sealed interval*, in the same ring object
as `entries`/`presentations`, so its address is stable for the Loop's borrow:

```cpp
enum class CaptureCustodyState : uint8_t { Free, Sealed, Retiring };

struct CaptureCustodyObligation : uint8_t {  // bit set, one acquirer per bit
  kFinalizer   = 1u << 0,  // closure → canonical order → staging → pass transfer → publication
  kReader      = 1u << 1,  // frozen/playback reader (DEC-142)
  kPersistence = 1u << 2,  // persist admission/drain of this interval's material
  kUndo        = 1u << 3,  // undo entry built from this interval
  kDisplay     = 1u << 4,  // derived/visual reference
};

struct CaptureCustodyReceipt {
  LoopEventStore::CaptureRange rawRange;      // sealed extent; reserved-empty when Free
  Loop* owner = nullptr;
  LoopId loopId = kInvalidLoopId;
  uint64_t interval = 0;                      // interval identity — the ABA guard
  uint64_t frozenCaptureContentGeneration = 0; // E2 capture content identity at the cut
  uint32_t generation = 0;                    // == presentationGenerationFor(interval), one derivation
  uint32_t loopLengthTicks = 0;
  CaptureCustodyState state = CaptureCustodyState::Free;
  uint8_t obligations = 0;                    // CaptureCustodyObligation mask
  uint8_t custodyIndex = 0;
  uint8_t slot = 0;
  bool ownsExtent = false;
};
```

`presentations[slot]` is retained as the slot's *current served obligation* and becomes a handle
(`custodyIndex` + `interval`) into this array, not the extent's storage. A DESTINATION for a new
seal is chosen by `state == Free`, never by slot. `presentationGenerationFor(interval)` stays the
**one** derivation of `generation` (`include/Track.h:1670`); no second convention is introduced.

**C1.2 — validation compares the 64-bit interval, not only the 32-bit generation (CONTRACT).** A
consumer holds `(custodyIndex, interval)`; acceptance requires `state != Free`,
`receipt.interval == interval`, `receipt.owner == &loop`, `receipt.loopId == loop.loopId`, and
`receipt.generation == presentationGenerationFor(interval)`.
`captureInterval_` is monotonic (`++captureInterval_`, `src/Track/TrackStateFacade.cpp:55`), so
truncation in `generation` can only alias intervals `2^32` apart; the 64-bit comparison is what
rejects that, and a stale index is refused rather than reinterpreted.

**C1.3 — Stop cuts input at its command boundary (CONTRACT).** Acceptance of the stop command ends
this interval's input in the accepting turn, as bounded, allocation-free work: latch the interval's
append endpoint, advance `captureInterval_`, and open the successor window. Sealing, freezing
staging, closure and canonical work stay deferred. A row that arrives after the cut either belongs
to the successor interval or is refused with a named token — it never grows the stopped interval.
Today the endpoint is only requested: evidence [handoff](evidence/HANDOFF.md) §1.2 shows producer
`2 → 3` in the same interval after the requested Stop. That observation is the RED for this clause.

**C1.4 — admission is per interval; the canonical writer stays single (CONTRACT).** Replace the
per-track admission predicate with an interval-qualified one — `hasPendingStopForInterval(interval)`
— and keep the per-track form only where it means "this Track has deferred service due", never where
it means "no other interval may be admitted". Call sites to reclassify (VERIFIED inventory):

| Site | Today | Meaning after C1 |
|---|---|---|
| `src/Track/TrackOverdubLifecycle.cpp:1008` | per-track gate | interval-qualified admission |
| `src/Track/TrackOverdubWrap.cpp:32,46,121` | per-track predecessor gate | interval-qualified: only the *same* interval's unfinished work may block a wrap |
| `src/Track/TrackPlaybackHotPath.cpp:600,613` | per-track emission gate | interval-qualified on the *emitting* interval's admission |
| `src/Track/TrackOverdubLifecycle.cpp:122` (`hasStopInFlight`), `:320-324` (`serviceAcceptedInteractionTurn`) | state query / deferred service | stays per-track |
| `src/Track/TrackOverdubLifecycle.cpp:450` | guards `prewarmPreparedCompanionHistory`, which is Loop-scoped and single | **stays per-track**, and C2 must price a second prepared companion history before this predicate is relaxed at all |
| `src/Track/TrackStateFacade.cpp:145` (`hasRealtimeDeferredIdleWork`), `src/Track/TrackDeferredMaintenance.cpp:904` | service predicates | stay per-track |

One serialized canonical worker and one canonical output remain: B may be *admitted and sealed*
while A finalizes; B may not run canonical order concurrently with A. `runtime->captureStopOwner`
therefore becomes an admission receipt per interval while the *writer* binding keeps its single
occupancy. Concretely the row-28 owner in the bounds inventory —
`Track::stepOverdubStopAdmission` — is instantiated as the one-grain-per-turn service that advances
whichever admitted interval is furthest behind, so several admitted stops are progress, not parallel
writers.

**C1.4a — the staging prerequisite (2026-10-25; option A approved).** The two-role baseline
(`RuntimePlaybackViewSet::captureStagings`: one live, one frozen) cannot admit another Stop while
the predecessor owns its frozen role. Owner explicitly chose **A — add independent staging roles**,
preserving the existing note-reservation/closure accounting; no transfer of reservations into sealed
ranges. At least four roles (three frozen + one live) are needed for A/B/C, at62,004 target bytes each:
minimum increment124,008 requested external bytes. This minimum does not prove the next-Stop or K5
envelope; independently unfinished intervals retain distinct roles until their finalizers release them.
Provision the admitted role capacity before capture, identity-qualify release, retain one canonical
writer/output, and price actual role cardinality before checkoff. **Implemented** at six roles
(five frozen + one live:62,004 each, +248,016 while the A/B/C/next-Stop overlap is live, released back
to the two-role baseline) with+10,448 inline metadata per allocated runtime; choice recorded by
[DEC-161 — additive frozen staging roles](../../../docs/DECISION_LOG.md#dec-161--additive-frozen-staging-roles-preserve-closure-accounting).
Native/build/review gates passed; device PSRAM, stack, allocator and timing remain NOT RUN.
[Execution](evidence/PA-STAGING-LEASE-EXECUTION.md). [Original finding](evidence/PA-STEP2-STAGING.md).

**C1.5 — independent release per obligation (CONTRACT).** `rawRange.releaseGrain(1)` may begin only
when `obligations == 0`. `releaseCustodyObligation(receipt, bit)` clears one bit; each acquirer has
exactly one acquire edge and one release edge:

| Obligation | Acquired at | Released at |
|---|---|---|
| `kFinalizer` | admission (`beginDeferredOverdubStopForSlot` / Record route) | `clearCaptureStopOwner` in `stepDeferredRecordRetirement` / `publishRecordPresentation` |
| `kReader` | `borrowSealedCaptureRange` | `runtime->clearPresentationOwner` |
| `kPersistence` | persist admission for the interval | persist drain completion |
| `kUndo` | `TrackUndo::pushRecordPassAdded` entry build | stack erase/adopt of that entry |
| `kDisplay` | derived/visual adoption of the sealed rows | retirement of the derived frame |

A sealed interval owns its actual bodies and does **not** owe an extra spare tail (VERIFIED,
[custody](evidence/CUSTODY.md) §"Directory, tail and credit accounting"); the free successor range
owns the spare. Retirement is bounded: one release grain and one `reserveTail()` per turn per
receipt, with the existing `deny_at_retire` diagnostic preserved
(`serviceCaptureStopDescriptorReservations`).

**C1.6 — denial taxonomy is closed and named (CONTRACT).** Reuse the existing
`SC_ODUB_STOP_STAGE` tokens — `stale_interval`, `queue_full`, `finalizer_busy`, `transfer_denied`,
`canonical_output_busy`, `session_exit_handoff_full`, `preparation_service_unavailable`,
`raw_range_unavailable`, `freeze_or_contract_denied`, `raw_seal_unexpected`,
`descriptor_rearm:deny` — and add exactly one: `custody_cap_denied` (C2). A denial performs no seal
and no transfer; a second, differently-named token for the same condition is a defect.

**C1.7 — oracle.** `test_early_stop_ends_B_without_reusing_A_custody` (RED today) becomes GREEN by
reaching the whole trace: A private → B accepts → B Stop cuts input → C starts and stops → A
completes → exact A/B/C content, geometry, undo and save order against the sequential canonical
oracle, with `A`'s tuple inventory unchanged at every step. Compile/link failure is not evidence.

**C1 status:** CONTRACTED. Both inputs it needed are delivered: the C2 receipt price
([120 target bytes](evidence/CONTRACT-PRICE.md)) and the C2 cap constant
(`kCaptureCustodyCapacity = 9`, [`R_rec` = 9 chunks](evidence/CAP-DERIVATION.md)); owner capacity bump 2026-10-25 (FIFO 8 / K 9 / session-exit ring 8).

---

## C2 — total reserves and pre-admitted cap

**Owner:** `Track` active-role storage (externally allocated) · `LoopPlaybackRuntime` (single
canonical reservation) · `MemoryMonitor`/pool (global condition). No new pool, no schema.

**C2.1 — every reserve term has one owner and one charge site (CONTRACT).** The ledger in
[custody](evidence/CUSTODY.md) §"Candidate price and whole-setup ledger" is adopted as the closed
list; this clause only fixes the accounting rule and the cap.

| Term | Charge site | Value |
|---|---|---|
| Custody receipt metadata | per Track, inside the single external `CaptureStopJobs` object | `sizeof(CaptureCustodyReceipt)` = **120 target bytes measured** ([price](evidence/CONTRACT-PRICE.md)); supersedes the 128-byte candidate box |
| Armed-range directory | per free/sealed receipt | 1,024 requested bytes (512 `uint16` prewarm) |
| Spare tail | free armed range only | 5,208 target bytes (1 real chunk) |
| Raw bodies | the interval that owns them | actual chunks; charged once |
| Canonical output | one, project-wide per admitted transfer | `ChunkReservation` 1,040 + credits; never multiplied by interval count |
| Canonical credits | unconsumed output capacity | disjoint from output chunks |
| Frozen staging roles | one per sealed-and-unfinished interval plus one live role, provisioned by active Capture admission | **62,004 target bytes each** (2,612 metadata +59,392 dynamic event storage). Ten roles (nine frozen + one live) at K=9: **+496,032** requested external while the envelope is live, released back to the two-role baseline when the Track owes nothing. See C1.4a and [staging finding](evidence/PA-STEP2-STAGING.md). |
| Roots / undo | `UndoEntry`(+C4 field), root refs, snapshot pages | own owners; not priced by `sizeof` wrapper |
| Visual | retained frame + continuation | C5; retained frame is not a new buffer |
| Persistence | queue/checkpoint buffers | global, once |
| Recovery reserve | `R_rec` | **9 chunks = 46,872 target bytes** ([derivation](evidence/CAP-DERIVATION.md)); one re-arm grain plus one retirement grain per admitted receipt |

The global condition that admission must satisfy is unchanged from custody §"Directory, tail and
credit accounting", and it is a *global* condition, not per Track:

```text
unique committed/retained chunks
+ sum_tracks(distinct N/R raw chunks + live chunks + F spare tails
             + canonical output chunks + unconsumed canonical credits)
+ other pool owners + R_rec <= 512
```

**C2.2 — the cap is derived, then frozen as one constant (CONTRACT).** `kCaptureCustodyCapacity` is
a single compile-time constant, produced by, not invented alongside, this computation:

```text
K_trace  = 8   // FIFO depth: eight concurrently admitted successors (owner bump)
K_reader = 9   // + one receipt still retiring while the next interval is admitted
K_max    = min( floor(A_custody_dirs / 1024),          // binding: directory/tail arithmetic
                floor(A_custody_meta / 120),            // measured: not binding at 8 Tracks
                maxChunksPerBudget(global condition, R_rec) )
kCaptureCustodyCapacity = min(K_max, K_product)
```

`A_custody_dirs` and `A_custody_meta` are the active-role bytes the packet actually reserves; the
metadata divisor is 120, the measured receipt size, not a placeholder. `K_product` is the product trace
the owner requires and is never below `K_trace`. **Resolved:** [the cap derivation](evidence/CAP-DERIVATION.md)
fixes `kCaptureCustodyCapacity = 5` because the metadata and directory tiers clear `K = 5` by a wide
margin while the chunk tier is a runtime admission, not a compile-time reservation. Admission above the cap denies
`custody_cap_denied` and retains accepted content; the *trace* is never narrowed to fit a cap found too
small — a `K_max < K_trace` result is a measured conflict returned to the owner, exactly as
[design](design.md) decision1 requires. `K_trace` is a required trace, not an admitted capacity:
A+B+C being reached does not prepay the fourth Stop.

**C2.3 — exhaustion and recovery are named (CONTRACT).** On `Pressure`/denial the interval keeps its
already-accepted content and its custody receipt; nothing is published partially, no fallback
re-labels private rows as committed, and each receipt releases independently
(C1.5). A failed reservation growth preserves existing credits and rolls back new acquisitions
(VERIFIED primitive behaviour). `R_rec` must cover one full re-arm grain plus one retirement grain
for every admitted receipt.

**C2.4 — pre-flight measurement, not a cap blocker.** The metadata tier is measured in
[the price annex](evidence/CONTRACT-PRICE.md): receipt 120 target bytes — identical to the
`CaptureStopPresentation` it supersedes, superseding the 128-byte candidate box — slab 488 at K4 and
608 at K5, charged to the single external `CaptureStopJobs` object per Track (+4,864 bytes for 8
tracks at K5) with no `.bss`/RAM1 growth, but external-first with an internal-heap fallback that a
cap may not assume away. The cap value is derived in [the cap derivation](evidence/CAP-DERIVATION.md);
what remains is a **pre-flight check on the first slice that declares the slab**, and none of it
changes `K`: target allocator overhead/fragmentation for the larger ring object; `RAM1` stack/call-frame
high-water for the retirement lane (the 720-byte `CaptureStopJob` built on the stack); target allocator
*execution* (the probe compiles and links ARM but runs natively only); roots/undo/visual/persistence
graph occupancy beyond wrapper `sizeof`. Procedure: declare the slab in the shipping image and take the
`teensy41-capture-serial` link/map figures plus a registered stack high-water. **Response to a
pre-flight overrun is to reduce how many Tracks prewarm custody — never to narrow the A/B/C/next-Stop
trace and never to reduce the `K = 5` overlap.**

**C2.5 — oracle.** The extended probe asserts the receipt price and the ledger arithmetic
reproducibly; a native admission test asserts `custody_cap_denied` at `K+1` with accepted content
retained and every prior receipt still releasable; the shipping build reports RAM1 and pool figures at
unchanged pins.

**C2 status:** CONTRACTED. Metadata tier MEASURED ([price annex](evidence/CONTRACT-PRICE.md): receipt 120
target bytes, slab 488 at K4 / 608 at K5, 8 tracks +4,864 external, no static growth). Cap value DERIVED
([cap derivation](evidence/CAP-DERIVATION.md)): `kCaptureCustodyCapacity = 5`, `R_rec = 5 chunks` =
26,040 target bytes, with body admission left dynamic on the 512-chunk pool. The pre-flight measurements
listed in C2.4 remain and none of them changes `K`.
---

## C3 — private-source preparation and deadline

**Owner:** `Loop` source view/span cache (`establishOverdubSourceView`,
`rebuildOverdubSourceSpanCache`) with the bounded cursor pattern already proved by
`RecordSourceStaging::Cursor`; service from the existing `Track::processDeferredIdleMaintenance`
lane. No new reader, no whole-take derived store.

**Why it fails today (VERIFIED).** Every qualified origin is a *committed* origin:
`publishedSnapshotIsCurrent()`, `LoopContentResolution::preparedWindowReady(playbackRevision)`,
`overdubSourceSpanCacheReusable()`; the non-committed origins are `prior`, `visual` and `passes`
(pass-store reconstruction). A private A appears in none of them, and
`committedPlaybackNoteOnIdentityValid` refuses its rows, so the same-pitch participants are zero
([handoff](evidence/HANDOFF.md) §1.5). The private extent *is* readable through
`borrowSealedCaptureRange`, but as sealed raw input, not as committed currency.

**C3.1 — a distinct origin, never a relabel (CONTRACT).** The private predecessor enters as its own
qualified origin — `SourceOrigin::PrivatePredecessor` — carrying
`PlaybackViewSourceIdentity{origin, captureContentGeneration = receipt.frozenCaptureContentGeneration}`.
Reads served from it must be visibly non-committed: they may satisfy the overlap/hold query, and must
never satisfy committed currency (root/`pass`, save, or a `Snapshot`/`Prepared` attribution). The
existing `"snapshot"|"prior"|"visual"|"passes"` strings gain exactly one member and no existing member
changes meaning.

**C3.2 — fixed work units, one cursor per interval (CONTRACT).** Preparation is a resumable cursor
held in the custody receipt (so it is interval-qualified and cancelled with its interval), shaped
after `RecordSourceStaging::Cursor` (`begin` / `step(quantum)` / `discard`, `Step{Pending, Complete,
Empty, Pressure, Denied}`, `DenyReason{None, SourceUnavailable, OutputUnavailable, ZeroQuantum}`).
One step admits at most `kDeferredStopPreparationQuantum` (32) units, and one unit is exactly one of:

1. one sealed-range row read (source row consumed or left unconsumed on `Pressure`);
2. one pass-local pair decision against the causal base (no body copy);
3. one pending-change materialization: one `Add`/`Shorten`/`Hide` upsert into the interval's own
   `pendingChanges`.

The cursor's state is fixed storage; it never materializes a whole-take vector, and it does not
write `passes`, roots or `playbackRevision`.

**C3.3 — bounded progress at a reachable maintenance wakeup (CONTRACT, amended by owner ruling
2026-10-25).** Private-predecessor preparation receives **at most one bounded grain per eligible owner
turn** at the reachable maintenance wakeup of `Track::processDeferredIdleMaintenance` while preparation
is owed. No catch-up loop and no synchronous completion are permitted. The originally named branch is
unreachable in exactly the state this clause exists for (maintenance returns from the stop lane while
`hasPendingDeferredOverdubStop()` holds, i.e. the whole time the predecessor is private), so the
eligible wakeup is the reachable maintenance turn: the pending-stop lane's own grain site and, when that
lane is not taken, the derived-view branch. The two sites are mutually exclusive within a turn, so the
producer still runs once per turn on one lane. The device-gate producer is **not** a valid wakeup: it is
gated on `transportFullyIdle` and compiled only under `SESSION_CAPTURE` (C5.3). No second scheduler and
no priority grant.

**C3.4 — materialisation deadline (CONTRACT, amended by owner ruling 2026-10-25).** Note-on/off must
not wait for the built source: they record the causal pair (own identity, start tick,
private-predecessor provenance) and defer only the materialisation. **Retained predecessor-dependent
overlap intents must be materialised, or denied by name, before the pending-change set leaves the
Loop's current ownership domain.** The concrete deadline is:

- the seal edge (`Loop::sealPendingNoteChangesToEditPasses`) for the **synchronous** route — overdub
  stop (`src/Track/TrackCaptureStopCommit.cpp:200`) or overdub wrap
  (`src/Track/TrackOverdubWrap.cpp:90`); and
- the **stop-admission pending-set swap** (`loop.swapPendingNoteChanges(job.pendingChanges)`) for the
  **deferred** route, which never reaches the seal helper because its pending set is fed to the
  canonical tail instead.

Failure policy at either edge: retain intent, deny the dependent transform with a named diagnostic, and
do **not** apply stale, empty or cache-only content. Materialising the retained intents at the deadline
is bounded by the declared hold capacity; it is not a synchronous completion of the *scan*, which stays
one bounded grain per turn under C3.3. Losing an overlap decision is a defect; applying a wrong one is
worse.

**C3.5 — supersession (CONTRACT).** A publishing mid-resolution invalidates the cursor by identity,
exactly as `fallbackGatherSessionMatches(windowStart, windowLength, playbackRevision,
includeActiveEditRows)` does for the gather session: A's publication bumps `playbackRevision` and the
committed root, so the private-origin partial is discarded and the committed origin is used. Mixing a
private prefix with a committed suffix is forbidden. Clear, slot switch, load, transport stop and
record re-entry cancel the cursor with the interval.

**C3.6 — oracle.** `test_private_record_is_the_same_pitch_overlap_source` (RED today) becomes GREEN;
the differential must compare B and C overlap decisions against an independent sequential canonical
run over the same pitch set, including a publication that lands *mid-resolution*, and must assert the
per-step unit count against `kDeferredStopPreparationQuantum`. The existing canonical control (same
query after real publication) stays GREEN.

**C3 status:** CONTRACTED. The numeric command→ingress and input-service-gap envelope is BLOCKED on
device measurement and is *not* inferred from this contract.

---

## C4 — successor-safe undo and empty cleanup

**Owner:** `Track` deferred finalization (`stepDeferredRecordEmptyResult`,
`stepDeferredRecordPublication`), `TrackUndo`, `Loop` pending-change/session owners,
`StorageManager` (persistence). This clause changes undo/lifetime semantics: it requires one ratified
decision before production (C4.6).

**Why it fails today (VERIFIED).** `stepDeferredRecordEmptyResult` computes
`resetGeometry = loop.activeCapturePassCount() == 0` and then calls `Loop::resetPassTimeline()` plus
five geometry writes. `resetPassTimeline` clears passes, `clearPublishedSnapshot()`,
`playbackRevision = 0`, `visualCache`, `clearOverdubSourceView()`,
`clearCommittedPlaybackNoteOnIdentities()`, `clearPendingNoteChanges()` and `closeOverdubSession()` —
it destroys a live successor's session state, which is why accepted B's rows survive while geometry
goes `768 → 0`. Separately, `TrackUndo::pushRecordPassAdded` calls
`eraseUndoEntriesForSlot(stack, slotIndex, false)` (`src/TrackUndo.cpp:691`), erasing *every* prior
entry for the slot, including a successor's.

**C4.1 — cleanup is predecessor-local (CONTRACT).** Empty-result cleanup runs only when

```text
loop.activeCapturePassCount() == 0
&& !track.hasLiveOrSealedSuccessorForSlot(job.slot, job.loopId, job.interval)
```

where the successor predicate is: a custody receipt (C1) or a live Capture for the same
slot/LoopId with `interval > job.interval`. When denied, the empty result performs **only** release
of its own custody obligation; it must not call `resetPassTimeline`, must not write
`loopLengthTicks`, `loopStartTick`, `startLoopTick`, `nextEventIndex` or `lastTickInLoop`, and
must not reset transport. Geometry remains what the successor needs.

**C4.2 — pending changes are interval-owned (CONTRACT).** A predecessor's finalization operates on
its own `job.pendingChanges`, never on the shared session: the admission swap
(`loop.swapPendingNoteChanges(job.pendingChanges)`) is what makes that true today. `C4.1` is what
keeps `resetPassTimeline`'s global `clearPendingNoteChanges()` and `closeOverdubSession()` away from a
successor. No new pending-change store is introduced.

**C4.3 — undo erasure is causally scoped (CONTRACT).** `UndoEntry` gains `uint64_t interval`, and
the predecessor prunes only entries whose `interval <= job.interval` for that slot/LoopId.
Successor entries survive and keep stack order as causal order. Effect on the ABI, measured
([price annex](evidence/CONTRACT-PRICE.md)): `132 → 144` target bytes (`+12`, because the appended
`uint64_t` makes the struct alignment 8). `UndoEntry` graph occupancy stays a C2.4 unknown.

**C4.4 — publication is per receipt and idempotent (CONTRACT).** Each sealed interval advances
`RecordPassReady → RecordPublished` on its own receipt; exactly-once is guaranteed by the phase
machine plus `job.ownsPendingPass = false`, never by the persistence dirty flag. Persistence stays
slot-scoped and idempotent: `requestLoopSlotPersistAndSaveState` marks the slot dirty and
`PersistenceWorkQueue::admitWork` dedupes per `(type, key)` while queued. No per-interval admit count
or receipt is added, and no private intermediate state is persisted or advertised as complete.

**C4.5 — independent sealed chronology (CONTRACT).** The acceptance trace must show A, B and C each
completing its own publication/empty/retirement without touching a successor's content, geometry,
pending changes, undo entries or session. Publication order may differ from stop order; content may
not.

**C4.6 — ratification (SATISFIED, 2026-10-25).** C4.1–C4.3 change undo and lifetime semantics, so
`docs/DECISION_LOG.md` required one entry recording: the predecessor-local successor predicate, the
`UndoEntry::interval` field and its RAM2/link impact, the scoped erasure rule, and the explicit
statement that an empty predecessor cannot reset a successor's geometry. The owner ratified all four;
they are recorded as **DEC-160 — a predecessor's finalization is local to its own interval**. These
clauses are authorised for implementation; the decision is ratified, not implemented.

**C4.7 — oracle.** `test_filtered_empty_record_does_not_clear_live_B` (RED today) becomes GREEN with
the desired geometry assertion — B's accepted rows *and* its geometry survive — plus a new case that
a successor's undo entry survives a predecessor's publication, and a new case that both orders
(A-then-B, B-then-A publication) yield the same final content and undo chronology.

**C4 status:** CONTRACTED and RATIFIED by **DEC-160**; fixture `P-C-1` demonstrated RED; not implemented.

---

## C5 — reachable bounded visual continuation

**Owner:** `Loop::rebuildVisualCacheIdleSlice` (visual cache) · `DisplayManager` (presentation) ·
`Track::processDeferredIdleMaintenance` (service) · `LoopContentResolution` prepared-index session
(the bounded continuation). No second reader, no new buffer owner.

**Why it fails today (VERIFIED).** On a source miss the slice calls
`LoopContentResolution::resolveDisplayNotes(passes, loopLengthTicks, paired)` at
`src/Loop/LoopVisualCache.cpp:445` — every active `pass`, complete chunk-list copies, edit
application, pass-local pairing, sort and append — *before* filtering to the window at `:451`. The
`fallbackGatherEventBudget` parameter (`:294`) is never read. The only bounded traversal,
`Loop::advanceFallbackGatherSession`, has no call site, and inventory row5 is explicitly "structurally
bounded, but NOT on the live path". Rows 6, 7, 8, 9 and 10 (sort, edit projection, reconstruct,
wrap-pair append, overlap removal) are unbounded.

**C5.1 — the slice resolves a window, not the pass set (CONTRACT).** A real-time source miss must not
select the whole-pass route. Order of attempts is fixed:

1. current published root (unchanged);
2. prepared window via `LoopContentResolution::tryResolvePreparedWindow(...)` (unchanged call site);
3. **C5.2's producer is admitted for this exact `(loop, playbackRevision, loopLengthTicks)`** → retain
   the previous complete frame and return with a named deferral; the producer is the progress;
4. neither → bounded window traversal only (below), never `resolveDisplayNotes(passes, ...)`.

Step 3 is a wait, and a wait is allowed *only* when a named producer is running for that exact
`(loop, playbackRevision, loopLengthTicks)` key. When no producer is admitted, this turn must advance
the window itself with the declared unit; when neither can run, the slice makes the denial visible
(`slice_deferred:<reason>`) instead of doing whole-pass work.

**C5.2 — the continuation is the existing prepared-index session (CONTRACT).** `DeviceGateSession`
already provides the complete required shape: phases
`Idle|IndexCommit|Window|RebuildPrepare|RebuildSpans|RebuildCheckpoints|State|Done`, per-phase
cursors (`indexPassCursor`, `indexChunkCursor`, `indexChunkEventCursor`, `indexEventCursor`,
`prep*`, `pair*`, `checkpoint*`, `rebuild*`, `recon*`), a stamped result
(`preparedIndexKept`, `preparedPlaybackRevision`) and the public gates
`preparedWindowReady(playbackRevision)` / `tryResolvePreparedWindow(...)`. The contract is to give it
a *reachable, production* wakeup:

- **Admission unit:** one phase step per turn, `kDeviceGateEventsPerSlice` (8) index/span rows per
  slice where the phase appends rows — a declared bound, not µs.
- **Wakeup:** the same derived-view maintenance branch that calls the slice
  (`TrackDeferredMaintenance.cpp:1079-1087`), stepped before the slice so the slice can observe a
  prepared window. One grain per turn; the producer never runs concurrently with the slice for the
  same loop.
- **Qualification:** `preparedWindowReady` is already keyed by `playbackRevision`; the session is
  keyed by `(trackIndex, loopIndex)` via `deviceGateMatchesLoop`. The single global
  `sDeviceGateSession` stays single — one prepared index, one owner at a time. A request for another
  loop while active is not a second index: it is a qualified reset
  (`deviceGateReset()` → `deviceGateBegin(...)`).
- **Cancellation:** `playbackRevision`, `loopLengthTicks`, `loopId`, active slot and `playbackGeneration`
  changes all invalidate; the partial candidate is discarded and the previous complete frame stays
  authoritative. `deviceGateDirtyPolicy` gains the dirty-window case instead of forcing a reset.
- **Publication:** never partial. `++visualCache.revision` stays the single publication edge, and
  `builtFromRevision`/`builtFromSnapshotIdentity` stay gated by
  `visualCacheGatherCoversPlaybackRevision`.

**C5.2a — amendment (2026-10-25): shape is not sufficient.** An implementation attempt enabled this
producer from the derived-view lane and was reverted before shipping. `DeviceGateSession` is *not*
live-lane-safe as it stands: eleven of its steps pass `indivisible = true` to `continueSlice`, and those
steps are content-sized — two whole-index sorts (`sortAndUniqueByNoteId`, `sortTickEventEntriesByTick`)
plus whole-vector copies (`win`, `ecopy`, dedup, channel index, `state`). `kDeviceGateSliceBudgetUs`
is never read by `runOneSlice`, so it is documentation, not a bound. Promoting it would put whole-pass
sorting on the MIDI lane, which C5.4 forbids. **The continuation may be the live-lane producer only after
its indivisible steps are bounded or replaced**, and the packet that bounds them is the one that may
enable it. Evidence: [producer bound](evidence/PD-PRODUCER-BOUND.md).

**C5.3 — the shipping gate is part of the contract (CONTRACT).** `maybeQueueContentResolutionDeviceGate`
and `processDeferredContentResolutionDeviceGate` are compiled under `SESSION_CAPTURE` and are further
restricted to the *selected* track, a clean `visualCache` and transport-idle lanes. In the shipping
image the prepared index is therefore never built, so C5.1 step 2 cannot hit and `:445` is the only
fallback. The packet must make the producer production-qualified: remove the capture-only gate from
the admission path, admit a non-selected owning track, and replace the clean-cache precondition with
the dirty-window case — each as a named predicate, not by deleting the checks.

**C5.4 — inventory rows are reclassified in the same commit (CONTRACT).** Per
[execution bounds](../../../docs/Authority/Architecture/ExecutionBounds.md) rule3: rows5 and 8 change
class with the enforcing code, row5 leaves "not on the live path", row8 loses the whole-pass fallback,
and the new producer gets its own row (declared grain, declared per-turn unit, cancellation,
retained-frame policy). Rows 6, 7, 9 and 10 keep their class unless this packet bounds them; a bound
transferred from a caller is not a bound.

**C5.5 — oracle.** `test_desired_bound_source_miss_must_not_resolve_whole_passes` (RED today) becomes
GREEN and gains eventual progress: under the named producer, a background 33-bar / 17-pass source with
the selected track Recording must reach the exact independent inventory (every identity once, exact
pitch/velocity/start/end and cardinality) with a coherent frame at all times, and must show a
declared unit count per turn. Native work counts remain counts, not device timing. Device
input-service gap and output lateness stay a separate measurement gate.

**C5 status:** CONTRACTED; device timing BLOCKED on a separate authorised stimulus.

---

## What this file does not do

- No production task packets. Task 2.2 — production packet generation stays unchecked; each `Cn`
  becomes exactly one packet when C2.4 is measured and C4.6 is ratified.
- No numeric capacity, warm-cycle threshold or timing envelope. Unset terms are not zero.
- No firmware, test, build, upload, device or SD change: this packet is documentation only.
- No guard removed, no sealed range aliased, no second reader, no queued-only behaviour relabelled.
- DEC-158 — post-Record retained intent remains the installed behaviour; DEC-159 — immediate
  first-overdub sealed custody remains direction, now with a contract and still without a packet.

## Status matrix

| Contract | Contract-level | Remaining blocker | Blocking the packet? |
|---|---|---|---|
| C1 — independent sealed custody and early Stop | CONTRACTED; receipt and cap constant delivered | none | no |
| C2 — total reserves and pre-admitted cap | CONTRACTED; `K = 5` and `R_rec = 5 chunks` derived ([derivation](evidence/CAP-DERIVATION.md)) | packet pre-flight only: RAM1 stack, allocator execution/fragmentation, graph occupancy — none changes `K` | no |
| C3 — private-source preparation and deadline | CONTRACTED | device service envelope (separate gate) | no |
| C4 — successor-safe undo and empty cleanup | CONTRACTED and RATIFIED by **DEC-160**; [P-C](packets/P-C-FINALIZATION.md) specified, `P-C-1` RED demonstrated | none | no |
| C5 — reachable bounded visual continuation | CONTRACTED | device timing (separate gate) | no |
