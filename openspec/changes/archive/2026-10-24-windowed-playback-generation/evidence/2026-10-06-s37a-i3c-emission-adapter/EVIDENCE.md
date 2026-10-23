# Task 3.7a (I3c-a) — the paged **emission adapter**: built, measured, and its own assumption falsified

Packet (written **before** the edit): [`EXECUTION.md`](EXECUTION.md). Authority: `AGENTS.md`,
`docs/Authority/IMPLEMENTATION_READINESS.md` (G2), `tasks.md` **3.7a**/**3.12**, **DEC-108 §13(a)/§14(c)**,
`ExecutionBounds.md` **row 27** and its `CoverageServiceContract` row ("if not provable, **reject active windowed
selection**").

**Outcome in one line.** The emission adapter exists, reads the ring by **stream ordinal** (no staging copy), never
produces or waits, and **refuses its own delivery by name** because the measurement this slice performed shows the
producer cannot lead a wrap with a three-page ring: a cyclic pass loses the events the first frames of the next pass
cross. The gate therefore stays closed, the merged fallback keeps playing the slot, and I3c-b's job is now a *measured*
requirement rather than an assumption.

## 1. What landed

| File | Change |
|---|---|
| `include/WindowPagedStream.h` | `WindowEventPage::baseOrdinal` (a page's first **stream ordinal**), `producedOrdinal()` (monotone across passes; reset only by `reset`), `eventAtOrdinal(ordinal)` (read-only, ≤ `kWindowPageCount` scan), `retireThroughOrdinal(next)` (**retirement by emission position**), `rearmForNextPass()` (restarts the traversal, **keeps** the pages and the ordinal space -- the additive pass re-arm I3b named as its blocker). |
| `include/TrackPlaybackRuntime.h` | `PagedEmissionState`: the walk's consumed ordinal, the pass base, pass/dry/unstraddled accounting, `emitting`, and the unreachable-path fallback event (in the runtime's **external** memory, so it costs no RAM1 `variables` -- a file-level static measured +32 B and was refused by the guard). |
| `include/TrackInternal.h` | `PagedEmissionStreamCtx` + `makePagedEmissionStream` (the adapter's only context is the slot's runtime; the position has one owner). |
| `src/Track/TrackPlaybackWindowBuild.cpp` | The adapter (`size`/`isValid`/`eventAt`/`consume`, all `TRACK_COLD_MEM`), `notePassBoundary`'s judgement, the producer's re-arm for an **emitting** delivery, and `Track::pagedEmissionDeliveryStateForSlot` -- the named predicate a gate reads: `NoDelivery`, `StaleRevision`, `CannotStraddleTheWrap`, `Unproven`, `Admissible`. |
| `include/Track.h` | The predicate's declaration and its five names. |

**The design points that matter.**

- **`passBaseOrdinal + cursor` is the mapping.** The walk's index restarts every pass (`DEC-108 §13(a)`: a pass is one
  traversal of the interval), the ring's ordinal does not. The pass base is set at a **boundary the projection signals**
  (`notePassBoundary`), never inferred -- only the projection knows the playhead wrapped.
- **No staging copy.** An earlier design staged events into a per-slot buffer (16 events: a turn could then serve only
  16 events while a frame demands ~104). It measured +296 B host/+192 B target per slot and *could not serve a frame's
  demand*; it was replaced by the ring's ordinal view, which costs +40/+48 B per slot and no per-turn copy bound.
- **Retirement by emission position** (`retireThroughOrdinal`): a page returns to the ring when the walk has passed its
  last event, not when a copy-out call empties it.
- **Never produces, never waits**: a ring with nothing ahead of the walk is a counted dry, and the deferred lane's next
  turn continues the pass. The clock path calls no producer, allocates nothing, and blocks on nothing.

## 2. The measurement that falsified the increment's own assumption

Fixture **W-37a-19/20** drives two consecutive passes through the *production* walk (`advancePlaybackCursor`) with the
lane producing one page per turn behind it, at the contract's declared advance (64 ticks/turn):

| Observation | Measured |
|---|---|
| Pass one | **exact**: all 2,500 interval events, in order, once each -- the emission-side proof the adapter exists for |
| Pass two | **2,244 of 2,500** -- 256 events (two pages) lost, skipped as behind the frame |
| `passesUnstraddled` | **2** (both wraps arrived with **no** next-pass head published) |
| `passesWithDrys` | 0 (the loss is a *skip*, not a dry read) |
| Predicate | **`Unproven`** -- no gate may select this delivery |

**Why, in the mechanism's own terms.** The producer completes its traversal exactly when the consumer reaches the pass's
end, so the ring is *empty* of the next pass's head at the wrap: the producer cannot produce pass *N+1*'s first page
until a page frees, and by then the playhead has already crossed the early events of the new pass. A three-page ring
cannot straddle a wrap at this density with one page produced per turn — which is precisely the "page base mapping" /
"lead across the wrap" work I3b named as I3c's blockers, now **measured** rather than argued.

**Consequences, stated rather than smoothed over.**

1. The **gate stays closed** (`Admissible` is unreachable in production today: the boundary judges the straddle, and a
   delivery whose wrap was not straddled is never proof).
2. **I3c-b's requirement is now measured**: give the producer a lead across the wrap (deeper ring, pass-ahead
   production, or a lead proportional to the interval's density), then re-run this fixture -- it is the acceptance test,
   and it is written to fail today.
3. The packet's red-proof set (**MR-R/S/T/U**) was **not run** in this increment. Recorded as owed: the mutations that
   guard the *shipped* behaviour (the ordinal view, the boundary judgement, the predicate's refusals) belong with the
   wiring that makes them load-bearing, which is I3c-b. This is a deviation from the packet, and it is named here
   rather than presented as a pass.
4. The fixture's expectations were **corrected down to the measurement** (the second pass's shortfall, and
   `passesUnstraddled == 2`) instead of being left as the hoped-for 5,000 events.

## 3. Verification

| Check | Result |
|---|---|
| Focused `-f test_runtime_working_set_ownership` | **157/157** (154 + the three new cases), 0 failed |
| Focused `-f test_window_paged_legacy` | **3/3** (3.12b's ring fixtures, undisturbed by the additive methods) |
| Full native `pio test -e native` | **2,832 cases / 7 skipped / 2,825 succeeded / 0 failed** (baseline 2,829 + 3) |
| Device build `pio run -e teensy41-capture-serial` | **SUCCESS**, guard **passes**: `variables 96,192` (unchanged), `code 392,280 + padding 936`, **96 B above the floor** (I3c-a's own **+64 B** of ITCM, after the adapter functions were moved to flash) |
| `LoopPlaybackRuntime` pins | Compiler-measured: target **7,312 → 7,384 (+72 B)**, host **9,448 → 9,528 (+80 B)** -- the state plus the in-runtime guard event (a staging design measured +192/+296 B and was removed) |
| Strict validation / `git diff --check` | valid / clean |
| Device run / upload | **NOT RUN** |

## 4. Not claimed

- **No production selection** (the hot path's source choice is unchanged; no gate is open), no timing claim, no
  `ExecutionBounds.md` row change, no cross-layer ordering claim, no device run, no fallback removal (3.7b).
- **No red-proof run** (see §2.3).
- The delivery is prepared, addressable by ordinal, walkable and **refused by name for selection** -- that is the whole
  claim of this increment.


---

## 5. I3c-b — the lead across the wrap, the seam, and the second defect the acceptance test found (same increment, 2026-10-06)

**What landed.** (a) **A lead across the wrap**: an *emitting* delivery keeps one page free
(`pagesPublished() + 1 < kWindowPageCount`), so the producer hands the next pass's head over while this pass's tail is
still queued -- and that flipped the acceptance measurement, `passesUnstraddled` **2 → 0** with **both passes exact**
(the second pass's 2,500 events, in order, once each). (b) **The judgement is walk-independent**: the straddle is judged
at a boundary against `passContentOrdinal` (the ring's ordinal when the producer last re-armed), and a boundary with no
completed traversal counts as unstraddled by definition -- so admissibility no longer depends on who is emitting.
(c) **The seam**: `playCommittedLoopMidi` selects the paged stream for a slot with no served window, no bridge and no
prepared pair when the lane has admitted the delivery (`runtime.pagedEmission.admittedForEmission()` -- one cached
boolean), skips the unbounded merged build in that case, and signals the pass boundary where the projection detects it
(the interval's own start-phase crossing, or the loop wrap when the interval starts at the loop's start).

**The defect the acceptance test found, recorded rather than smoothed over.** With the seam in place the fixture's
element-wise comparison failed at **index 5,000**: the *second* pass's sequence drifts out of alignment past the first
pass's events (tick 0 expected, 64 emitted) -- the walk's per-pass index and the ring's ordinal do not re-align exactly
at a boundary. Emitting wrong content is worse than not emitting from pages, so **selection is closed by construction**:
`PagedEmissionState::kPagedEmissionSelectionEnabled = false` (a compile-time constant, so the seam also costs no ITCM).
The merged fallback plays such a slot exactly as before; the delivery's *measurement* (`Admissible`) and its *selection*
(the switch) are now separate answers, and the fixture asserts pass one element-wise, the delivery's state, and that
selection stays closed. **This is I3c-c's first work: fix the boundary mapping, then flip the constant.**

**Also recorded:** the dry-based withdrawal written during this increment was **removed** -- a read that finds nothing
ahead of the walk is legitimate at a pass's event-free tail (the fixture measured 64 of them across two healthy passes),
and the *harmful* case (a late page whose events the walk has already passed) is invisible to the adapter, so the
straddle judgement is the only sound one and `dryTurns` stays as named accounting, not as a rule.

| Check | Result |
|---|---|
| Focused RWS / ring | **157/157** / 3/3 |
| Full native | **2,832 / 7 skipped / 2,825 succeeded / 0 failed** |
| Device build | **SUCCESS**, guard passes: `variables 96,192`; `code 392,152 + padding 1,064`; **224 B above the floor** (the closed seam is *cheaper* than I3c-a's: `code` fell by 128 B) |
| Selection | **closed by construction**; the merged fallback serves |
| Device run / upload | **NOT RUN** |


---

## 6. I3c-c — the boundary mapping fixed, and selection enabled (same increment, 2026-10-06)

**The defect, diagnosed.** The I3c-b fixture drifted past index 5,000 because the adapter served **everything
published** -- and the producer deliberately runs ahead into the *next* pass (that is the lead across the wrap), whose
events occupy the **same tick space** as the pass being emitted. The walk cannot tell pass *N*'s tick-800 event from
pass *N+1*'s, so the next pass's head leaked into the current pass's window.

**The fix (one clamp, and it is self-correcting).** The adapter bounds the walk by the **emitting pass's own content
end**: `passEnd = passContentOrdinal > passBaseOrdinal ? passContentOrdinal : published`. `passContentOrdinal` is the
producer's last re-arm ordinal -- the end of the traversal it completed -- so while pass *N* is being emitted it *is*
pass *N*'s end; at the boundary the base moves to that end, the two become equal, and the bound opens again for the pass
that now follows. No queue of ends is needed and no pass index is stored.

**Why the residual case cannot arise on this path**: a producer completing a *further* pass inside one window would need
the next pass to fit the ring's three-page depth. This path exists for content **over `L`** (2,048 events, over sixteen
pages) -- the ceiling switch and the admission switch both require it -- so no pass fits three pages, and the clamp's
boundary case is unreachable. Recorded as the reason, not assumed silently.

**Selection enabled:** `kPagedEmissionSelectionEnabled = true`. A slot with no served window, no bridge and no prepared
pair whose delivery has been admitted (one cached boolean, maintained off-clock) now emits from the ring instead of the
merged store, and the unbounded merged build is not made for it.

**Acceptance, and its red-proof.**

| Check | Result |
|---|---|
| Both passes exact, element-wise, **exactly 5,000 emissions** | **passes** (the assertion I3c-b failed) |
| **MR-I3c-c-1** (the content bound removed) | **RED** -- *Expected 5000 Was 4872*: next-pass content leaks in and the pass loses events; restored byte-identically |
| Focused RWS / ring | **157/157** / 3/3 |
| Full native | **2,832 / 7 skipped / 2,825 succeeded / 0 failed** |
| Device build | **SUCCESS**, guard passes: `variables 96,192`; `code 392,216 + padding 1,000`; **160 B above the floor** |
| Strict validation / `git diff --check` | valid / clean |
| Device run / upload | **NOT RUN** |

**Still owed (named, not smoothed over).** The red-proofs the I3c-a packet asked for (**MR-R/S/T/U**) remain unrun; the
one this increment identified as load-bearing (the content bound) *is* red-proved above. I3c's own end-to-end assertion
-- emission from the ring on a real transport tick -- belongs to **I4/4.3's** differential parity, where an
emitted-stream comparison is the oracle; the fixtures here drive the production walk directly.


---

## 7. I3c-a's owed red-proofs, delivered (2026-10-06)

The packet's mutations were written against the **staging** design that this increment replaced with the ring's ordinal
view, so two were re-expressed and one is inapplicable. Every run is against the **final revision**
(`src/Track/TrackPlaybackWindowBuild.cpp` sha256 `fbe1d6b7f8265ca201ffca0b68e8c64ea6fbe7fe6a61a0d45fbeea916ffe3684`,
`include/TrackPlaybackRuntime.h` `7a324cb9…`, `include/WindowPagedStream.h` `95b3d11a…`), and every restore was verified
byte-identically on all three files.

| Packet ID | Mutation run | Result (measured) |
|---|---|---|
| **MR-R** | **MR-R-baserebase** -- the pass base does not move at a boundary (the walk's per-pass index then addresses the previous pass) | **RED** -- the acceptance fixture: *Expected 48 Was 0*. The boundary's base movement is load-bearing. |
| **MR-S** | **MR-S-mapping** -- the per-pass mapping not applied (`size` served as an absolute ring ordinal) | **NOT RED** (157/157). **Recorded as a non-red-proof**, with the mechanism: the walk's frame break (`evTick > tickInLoop`) bounds the damage before the over-open bound can leak content. The *load-bearing* bound is the emitting pass's content clamp, and that one **is** red-proved (**MR-I3c-c-1**, *Expected 5000 Was 4872*). |
| **MR-T** | **MR-T-predicate** -- the delivery-state predicate accepts unconditionally | **RED** -- the dry-pass fixture: *Expected 3 Was 4* (`Unproven` where `Admissible` is required). |
| **MR-U** | **MR-U-rearm** -- the pass re-arm releases the ring instead of keeping the pages the walk has not reached | **RED** -- the acceptance fixture: *Expected 2 Was 1. the boundary moved once per wrap*. |
| **MR-S (as written)** | the refill moved out of `size` into a per-event path | **INAPPLICABLE** -- there is no refill: the staging buffer was measured and removed in favour of the ordinal view (§1). Named rather than quietly dropped. |
| *(added)* **MR-V** | selection unconditional (`admittedForEmission()` true) | **RED by abort** -- the run stops at **17 cases (16 succeeded)**: an unbound stream is selected, `isCursorValid` fails, and the order-dirty path crashes an unrelated fixture. The failure mode is named: the admission is what keeps an unbound stream off the clock path. |

**Process incident, recorded.** The first closeout attempt's harness used a shell variable for its restore paths that
resolved to a non-existent directory, so its `cp` restores silently failed and left the window-build TU mutated. The file
was restored from git (tree clean, hash `fbe1d6b7…`) before the corrected run; the corrected harness restores with
`git checkout --` and verifies all three sha256s after every mutation. The results above are the corrected run's.


---

## 8. I3c — the production-path assertion, and the circular gate it found (same increment, 2026-10-06)

**A real defect, found by driving the production entry.** The pass-boundary signal in `playCommittedLoopMidi` was
gated on the *selection* flag (`pagedEmission`), while admission -- which the selection tests -- is the boundary's own
output. The delivery could therefore never be admitted: `boundaries` stayed 0 through a whole pass of production ticks.
The signal is now gated on the delivery being **bound** (`runtime.pagedEmission.bound`), which is the correct input: the
judgement runs whether or not anything is selected, and selection remains a separate answer.

**The production-path fixture, scoped to what this suite can drive**
(`test_3_7a_i3c_the_production_tick_path_selects_nothing_before_its_boundary`): with `Track::playMidiEventsForSlot` as
the driver, a prepared-but-not-yet-admitted paged delivery has

- `admittedForEmission() == false` and `PagedEmissionDeliveryState::Unproven` (nothing may be selected yet),
- `consumedOrdinal == 0` (nothing is consumed from the ring),
- the merged store **empty** -- the heavy fallback build is refused on the clock path by design, so a rootless dense
  slot is **silent** here, which is the condition the paged delivery exists to change.

**Named, not implied:** the *emitting* half of the production path (a boundary judged by a real wrap, the ring consumed
by real ticks) needs the slot's own **playing** state -- the `LayeredSlot` route refuses a slot the slot state machine
does not consider playing -- which this suite's accessors do not provide. That belongs to **I4/4.3's** harness, where the
differential-parity oracle lives; until then the substitutes are the walk-level fixtures (both passes exact) and the
seam's own red-proof (**MR-V**, red by abort).

**RAM1, and the rule applied.** Inlining the decoupled boundary block pushed ITCM to `code 392,408 + padding 808`, i.e.
**32 B under the floor**: the guard failed it. The block now lives in flash (`Track::servicePagedPassBoundaryForSlot`,
declared with the rest of the paged entries) and the clock path decides only *that* a delivery is bound. The guard
passes again: `variables 96,192`; `code 392,296 + padding 920`; **80 B above the floor** (112 B recovered).

| Check | Result |
|---|---|
| Focused RWS | **158/158** |
| Full native | **2,833 / 7 skipped / 2,826 succeeded / 0 failed** |
| Device build | **SUCCESS**, guard passes (`variables 96,192`; `code 392,296 + padding 920`) |
| Strict validation / `git diff --check` | valid / clean |
| Device run / upload | **NOT RUN** |


---

## 9. I4 (first slice) — the timing-admission measurement, and the parity harness this suite lacks (2026-10-06, test-only)

**The timing-admission clause of 3.7a, measured.** The paged delivery's service requirement is now a *number*, not an
assumption:

| | |
|---|---|
| Declared contract | one page (128 events) per deferred turn, permitted advance **64 ticks** per turn (`kPlaybackViewPermittedAdvancePerTurn`) |
| A dense over-`L` interval at that advance | ~**104 events per 64-tick frame** |
| At the declared rate | the producer's 128 events/page keeps up; the ring's three-page depth absorbs jitter; passes are served **exactly** (the walk-level and production fixtures above) |
| **At half the declared rate** (one turn per two frames = 64 events per frame) | **the producer falls behind within the pass, and the breach is named** -- a dry pass or an unstraddled wrap -- rather than served as content |

The fixture (`test_3_7a_i4_the_paged_delivery_names_a_halved_service_rate`) asserts the property in both directions --
"either the halved rate is absorbed (the pass is served exactly) or the breach is named" -- and then pins the measured
outcome: the breach *is* named. That is the first evidence row for `ExecutionBounds.md` row 27's "caps declared, bounds
unmeasured" on the paged path; a device envelope remains owed.

**The parity harness this suite does not have, measured rather than assumed.** The *emitting* half of the production
path (real transport ticks, the ring consumed by the clock path, the emitted stream compared) **cannot be driven from
this suite**: with the transport started through its own owner (`Track::startPlaying`, which clears
`transportStopOutputBarrier_`), the projection re-anchored (`reanchorPlaybackCycle`) and the state asserted `PLAYING`,
the clock path still recorded no tick through a whole pass (`loop.lastTickInLoop` stayed 0). The fixture is therefore
scoped to what it verifies -- a prepared-but-unadmitted delivery consumes nothing, is not selected, and leaves the
merged store **empty** (a rootless dense slot is silent on the clock path: the condition paging exists to change) -- and
the emitting half is **named** as I4/4.3's harness work, together with the parity oracle it needs: this suite has **no
MIDI sink**, so velocity/tick-exact content parity and **4.3's same-tick order** gate require one.

| Check | Result |
|---|---|
| Focused RWS | **159/159** |
| Production sources | **unchanged by this slice** (test-only) -- the device/guard figures of §8 stand |
| Strict validation / `git diff --check` | valid / clean |
| Device run / upload | **NOT RUN** |


---

## 10. I4 (second slice) — the emitting-half prerequisite narrowed to a line (2026-10-06, test-only)

Having established (§9) that the emitting drive does not run in this suite, this slice narrowed *where* it refuses rather
than leaving the prerequisite vague:

- **The entry gates are all satisfied**: `transportStopOutputBarrier_` cleared by `Track::startPlaying`, no pending
  deferred overdub stop, not stopped-recording, `hasAudiblePlaybackSource(slot)` true (committed passes), and the slot's
  runtime present (the first refusal in `playCommittedLoopMidi` is `runtimePtr == nullptr`, which does not apply).
- **So the refusal is inside `playCommittedLoopMidi`'s body, before it records the tick** (`loop.lastTickInLoop =
  tickInLoop`, which stayed 0 through a whole pass).
- **The nearest candidate is the stale-runtime path**: `runtime.isStale(loop.playbackRevision, playbackGeneration)`
  (line 247) → `if (!runtime.reset(true)) return;` (line 257) — a **silent** return, and exactly what a *fresh* fixture
  slot would trip on its first tick, because this suite never performs the production boot/load admission that
  leaves the runtime synchronised. That is a **candidate, not a verified cause**: confirming it needs the same
  transport/load harness the emitting half needs.

**Consequence, and the honest state.** The emitting half (real ticks → the ring consumed → the emitted stream compared)
remains I4/4.3's harness item, now with a concrete starting point instead of a shrug. Recorded in the fixture's own
comment as well, so a future harness meets it where it matters.


---

## 11. I4 (third slice) — the parity matrix, stated where each property is proven (2026-10-06, test-only)

The parity work has been deferred item by item; this slice states the matrix once, so nothing is claimed by silence.

| Property | Status | Where |
|---|---|---|
| **Amount and content of a paged pass** | **proven** | the walk-level fixture, element-wise against `Loop::copyEffectiveCommittedEventsInRange`, over two whole passes (5,000 events) |
| **Same-tick order (4.3), single-pass interval** | **proven, and now asserted as *exercised***: the dense span packs several events per tick, so a slip inside a tick would break the element-wise comparison -- and the fixture now *requires* that such ticks exist, so the property cannot be vacuous | the walk-level fixture (this slice added the coverage assertion) |
| Emission through the **production send path** (ledger + `sendMidiEvent`) | **not proven** | needs a MIDI sink: this suite has none, and `playbackCursorAdvanceSend` is file-local to the hot-path TU |
| **Same-tick order across layers** (record pass + overdubs in one window) | **not proven** -- and deliberately not claimed: the paged path emits in traversal order, and multi-layer ordering is I1's hand-sort for *windows*, not a property of pages | named since I3b |
| **Real transport ticks** (the clock path consuming the ring) | **not proven**; the prerequisite is narrowed to a line (IS10): the refusal is inside `playCommittedLoopMidi` before it records the tick, nearest candidate the stale-runtime reset path | IS10 |
| **Timing admission** | **measured**: one page per turn at a 64-tick advance keeps up; half that rate breaches within the pass and the breach is named | IS9 |
| Device envelope / `ExecutionBounds.md` row 27 | **owed** (device) | - |

| Check | Result |
|---|---|
| Focused RWS | **159/159** |
| Production sources | unchanged (test-only): the device/guard figures above stand |
| Strict validation / `git diff --check` | valid / clean |


---

## 12. The harness attempt: the §10 candidate falsified, and what the harness still needs (2026-10-06, test-only)

**Measured:** after a whole pass of clock-path ticks, the slot's paged delivery is **still bound** (`pagedActive` true).
`LoopPlaybackRuntime::reset` *drops* the ring (its own comment: "the page ring is the same kind of work"), so the
stale-runtime path IS10 named as the nearest candidate **did not run** -- the candidate is **falsified**, and the tick
path's silence comes from elsewhere in `playCommittedLoopMidi`'s body (the pair/merge section between its entry and the
tick record is the next place to look).

**A probe that had to be withdrawn, recorded rather than hidden:** calling `isStale(loop.playbackRevision,
playbackGeneration)` directly from the fixture **crashed the run** (one case, none succeeded). Reading the runtime's
staleness therefore needs a production-safe route, which is itself part of what the harness must provide.

**What the harness still needs (named, with what exists):**

| Piece | State |
|---|---|
| A **MIDI sink** at the walk level | **exists**: the suite's `pagedEmissionSinkSend` captures the emitted stream, and the acceptance fixture compares it element-wise against the effective-content resolver |
| A MIDI sink at the **production send path** (`applyPlaybackLedgerEvent` + `sendMidiEvent`) | **not implemented**: `playbackCursorAdvanceSend` is file-local to the hot-path TU, and this suite has no handler-level capture |
| **Transport/load** state that lets the clock path run | **not implemented**: the drive is silent, the stale-reset candidate is now falsified, and the next bisection step is the pair/merge section |

---

## 13. The bisection closed: the refusing return is identified, and the MIDI edge now records what it emits (2026-10-06; firmware + tests, native-only seam)

**The bisection (measured; the probe was withdrawn before the commit).** Sections 10-12 narrowed the tick path's silence
to "somewhere inside `playCommittedLoopMidi` before it records the tick", with the stale-runtime path as the nearest
candidate. A temporary probe -- a file-writing marker at each of the **five** returns between the function's entry and
`loop.lastTickInLoop = tickInLoop`, written to a file rather than stdout because the test runner's output capture hides
`printf` -- ran the whole focused suite. The probe's own output is kept beside this document
([`bisect-probe.txt`](bisect-probe.txt): the tag distribution over the whole run, and the I4 drive's own tail):

| Return in `playCommittedLoopMidi` before the tick record | Hits over the whole suite run |
|---|---|
| **the zero-content gate** (`eventCount == 0 && fixedBridge == nullptr && rolling.phase == Idle`) | **5,273, including 24/24 of the I4 fixture's ticks** |
| `runtime == nullptr` | 0 |
| `!runtime.reset(true)` (the stale-runtime path, section 12's candidate) | 0 |
| the bridge-stream refusal (`bridgeStream == nullptr`) | 0 |
| the `UINT32_MAX` catch-up guard | 0 |

So the fixture's drive takes the **zero-content gate** on every tick, and the other four returns never fire anywhere in
the suite. Why that gate on this shape: the **pair** path does not apply to the loop by its own gate
(`shouldAvoidFullVisualRebuild`, 512 bars over the rebuild threshold); the **rolling** section *excludes* the slot
(`rolling.phase == Idle` -> `resolveRollingPlaybackWindow` answers `Excluded`), which is also the gate's third clause --
without it an empty stream would be walked and the tick *would* be recorded; and the **merged** fallback is empty
because its build (for a long loop) gathers only a **published** window, and a rootless demoted slot has none. The
silence is the gate's own content test -- exactly the condition paging exists to change -- not a reset, not a bridge,
not the catch-up guard.

**The door is asserted, not merely narrated.** The fixture now asserts the six facts this conclusion depends on
(`lastTickInLoop` is a real phase, not the fresh-transport `UINT32_MAX`; the pair gate excludes the loop;
`rolling.phase == Idle`; no bridge was ever selected and no window is published for the slot; the merged store is empty
before the drive; the paged delivery is not admissible), then asserts the outcome at **both** edges: no tick recorded
through the whole pass, **and nothing committed at the MIDI edge with the drive's output enabled** -- so the silence is
a production refusal rather than a muted drive.

**The sink, and the correction it carries.** `MidiSendCapture`'s own doc comment promises "every event that reaches the
wire"; the playback route *does* reach it (`MidiHandler::sendRealtimePlaybackEvent` funnels into
`MidiHandler::sendMidiEvent`, which records), so section 12's row -- "a MIDI sink at the production send path (...) not
implemented: `playbackCursorAdvanceSend` is file-local to the hot-path TU, and this suite has no handler-level capture"
-- was **wrong as stated**: the ownership suite already used `MidiSendCapture` elsewhere. What was genuinely missing is
the **Track-level record with the slot pairing**, and `Track::sendMidiEvent` now has one: a native-only capture of the
pair it hands the handler, at the point of the send, after its own gates and the playback channel remap, bounded at
8,192 entries with a **counted** drop, read through `sentPlaybackMidiCountForTest()`, `sentPlaybackMidiAtForTest()`,
`sentPlaybackMidiDroppedForTest()` and `clearSentPlaybackMidiForTest()`.

**The new fixture (`test_3_7a_i4_the_midi_edge_records_what_the_clock_path_emits`), teeth in both directions on one pass
of one slot.** A two-bar slot (under the rebuild threshold, so the merged fallback serves it) with a committed
bar-spaced pass and a **current published root** -- the only source the merged gather reads. *Control:* the pass walked
with the output disabled: events are due, the tick record advances, and **neither** edge records anything. *Measured
drive:* the same pass, re-anchored and walked with the output enabled -- **exactly four** events (ticks 0/48/768/816,
velocities 100/0, channel 1, slot 0), with the wire boundary's own record equal to the sink's **element-wise** (type,
channel, tick, note, velocity), which is what makes the sink the production edge rather than a parallel one.

**Red-proofs (both restored byte-identically).**

| Mutation | Result |
|---|---|
| **RP-1**: the zero-content gate's `return` removed | **RED** -- the I4 fixture fails at *"Expected 0 Was 1536. no tick was recorded through a whole pass: the zero-content gate refused every one..."*, which is the proof that the identified return is what silences this drive. One unrelated fixture also failed (`test_s4b_1_a_queued_playback_switch_waits_for_the_predecessor_and_then_commits`, *Expected 0 Was 1*): the gate is load-bearing beyond this fixture, recorded rather than tidied away |
| **RP-2**: the sink hoisted outside the emit gate (records unconditionally) | **RED** -- the new fixture's control fails at *"Expected 0 Was 4. nothing reached the MIDI edge with the output disabled, though events were due"*. The I4 fixture's own edge assertion still passes under RP-2, which is the honest distinction: that drive is refused *before* any send is attempted, so a sink that over-records cannot make it look like an emission |

**Verified.**

| Check | Result |
|---|---|
| Focused RWS | **160/160** (+1 fixture) |
| Focused shared capture finalization | **64/64** |
| Full native | **2,835 / 7 skipped / 2,828 succeeded / 0 failed** |
| `teensy41-capture-serial` | **SUCCESS**; RAM1 **identical with and without the change** (`variables 96,192 / ITCM 393,216 (code 392,296 + padding 920) / locals 34,880`; guard 80 B above the floor) -- measured A/B by stashing the change and building both ways |
| Strict validation / `git diff --check` | valid / clean |

**Not claimed:** no selection change (this shape's delivery is still `Unproven`, so the paged path is not selected and
the merged fallback stays the serving path by design), no timing claim, no `ExecutionBounds.md` row change, no device
run, no fallback removal (3.7b). The *emitting half*'s remaining piece is now named precisely: this fixture's slot has
**nothing to emit** until the delivery is admitted (a boundary must be straddled cleanly), which is the paged selection
work itself rather than a harness gap.

---

## 14. I5: the paged lane's production serving is a **closed cycle** -- measured, fixed, red-proved, and **landed** (funded by an ITCM budget slice)

**I3c-c's remaining item was "the end-to-end emission assertion on a real transport tick". Driving it found that the
assertion cannot be made: a bound paged delivery can never earn admission from the clock path.** Two shapes, one run
each (temporary probes, withdrawn; the numbers are the probes' own output):

| Shape | Result after a full drive |
|---|---|
| **Rootless** (the lane's reason to exist: no root, no window, fallback empty by construction) | `bound=1 boundaries=0 unstraddled=0 contentEnd=0 emitting=0 admitted=0 consumed=0 sink=0 wire=0 last=4294967295 state=Unproven` -- **no tick was ever recorded**: the delivery's own judgement never ran |
| **Root-backed** (the same delivery with a published root, so the fallback serves and the tick path runs) | `boundaries=4 unstraddled=4 contentEnd=0 emitting=0 admitted=0 consumed=0 sink=19/13/19 state=Unproven` -- every wrap judged, **every one a violation**, the producer never re-armed, nothing emitted from the ring |

**The mechanism is a closed cycle, in three links.** (1) The **zero-content gate** returns *above* the boundary
judgement (`servicePagedPassBoundaryForSlot` sits below it), so a slot whose merged store is empty by construction --
exactly the slot paging exists for -- never reaches the judgement that would admit it. (2) The straddle rule counts
`passContentOrdinal == 0` ("no completed traversal") as a violation, so the first boundary is always a violation, and a
single violation makes `admissible` false **forever** for that delivery. (3) `passContentOrdinal` is set only by the
producer's **re-arm**, which happens only while `pagedEmission.emitting`, which is set only by a **read of the adapter** --
and nothing reads it unless it is selected, which admission gates. The walk-level acceptance fixtures could verify the
adapter because *the fixture's own read* set `emitting`; the clock path never reads an unadmitted delivery, so the cycle
never opens. **What this means:** I3c-c's "selection is enabled" holds for a delivery that is already admissible, but
nothing on a device can make one admissible. F-8's purpose -- serving a demoted slot with no root -- is **not** delivered
by I3c alone, and only the walk-level half of I4's parity matrix describes production.

**The fix: three parts, plus one latent crash the fix would have hit.**

1. **The judgement comes first, in flash.** `servicePagedPassBoundaryForTurn` recomputes the turn's projection off-ITCM and
   judges the boundary **before** the selection that reads the admission (the I3c lesson one layer down: the judgement is
   the admission's input). Hoisting those three pure projections inline measured **+256 B** of ITCM, hence the flash call.
2. **The gate must not refuse an admitted delivery.** Its count is the merged store's, which is null on the paged branch;
   the exemption is one arm of that count (`pagedEmission ? 1 : 0`).
3. **The first boundary is judged against the walk's position, not against "no head by definition"**:
   `headRequest = passContentOrdinal != 0 ? passContentOrdinal : consumedOrdinal` -- the ring must hold content *ahead of
   the walk*, which is the walk-independent question before any traversal has completed.
4. **Measured while part 1+3 were in and part 2's guard was not: SIGSEGV.** `mergedEvents` is null on the paged branch, so
   `eventCount`'s `mergedEvents->size()` was a live null dereference the moment admission became reachable.

**The fix works -- measured, with the end-to-end assertions the task row asks for** (landed as
`test_3_7a_i5_the_boundary_judgement_precedes_the_selection_in_one_turn` and
`test_3_7a_i5_a_rootless_demoted_slot_is_served_from_the_ring_pass_exact`):

| Assertion | Measurement with the fix |
|---|---|
| The rootless 512-bar fixture, one frame | `passCount=1 unstraddled=0 admitted=1 emitting=1 consumed>0 lastTick=64` and the pass head **already on the wire** -- judgement, admission and walk land in the *same* frame |
| The same shape over a full interval | `sink=2500 wire=2500` for **one whole pass, exactly the dense span** (2,500 events); before the fix: 0, and after a partial fix that judged too late: 2,370 (the head frame lost) |
| A rootless 3-bar loop, **pass-exact** | pass 2 = **2,500 events, element-wise equal** to `copyEffectiveCommittedEventsInRange` (tick/note/velocity), with the wire boundary's record equal element-for-element; `passesUnstraddled == 0`, `passContentOrdinal > 0`, `state=Admissible` |

**Red-proved (all three restored byte-identically; the tree hash returned to the fixed state each time).**

| Mutation | Result |
|---|---|
| **RP-I5-hoist**: the judgement gated on the admission (the old ordering) | **RED** -- *"Expected 1 Was 0. the frame judged the delivery's own pass boundary"* and *"the drive must reach the second pass"* |
| **RP-I5-first-boundary**: `passContentOrdinal == 0` counted as a violation again | **RED** -- *"Expected 0 Was 1. and the judgement straddled"* and *"Expected 0 Was 3. every boundary arrived with the next pass's head already published"* |
| **RP-I5-gate**: the exemption removed | **RED** -- *"the same frame's walk took the delivery"* and *"Expected 0 to be greater than 0. the producer re-armed"* |
| **RP-I5-null** (implicit, measured during development) | **SIGSEGV** in the rootless fixture |

**Landed by funding it: the ITCM budget slice.** The guard refused the fix at every earlier form measured (floor:
`padding >= 840`; the approved line had **80 B** of headroom), so this slice *paid* for it instead of re-basing anything,
and two blocks of the same function moved off-ITCM (both measured, incremental):

| Variant | Post-link RAM1 |
|---|---|
| Baseline (before I5) | `code 392,296 + padding 920` -- 80 B of headroom |
| + the fix, unfunded: the gate exemption folded into the turn's count | `code 392,488 + padding 728` -- refused, +192 B |
| **+ the handoff seek moved to flash** (`seekBridgeWindowPastServicedInterval`) | `code 392,440 + padding 776` -- **-48 B**, still refused |
| **+ the fixed-source identity and its bridge/view resolved in flash** (`resolveFixedPlaybackSourceForTurn`) | **`code 392,360 + padding 856` -- the guard PASSES**, 16 B above the floor |

So the net cost of I5's fix is **+64 B** of ITCM, and the slice's own ITCM movement is **-128 B** of the same function: the
logarithmic handoff seek (a loop) and the identity/two-lookup resolution are calls now. Both were the function's largest
movable blocks without touching semantics: the seek runs once per handoff, never per event, and the resolved identity is
still the turn's own (written back, because five later decisions compare against it). **Remaining candidates for the next
slice's headroom, named rather than implied:** the `RUNTIME_TIMING_*` bookkeeping the capture build keeps on this edge,
the wrap block's bridge tail emission and index resets, and `makePlaybackContext`.

**Verified for this increment:** focused RWS **161/161** (the I4 fixture replaced by the two I5 fixtures), neighbouring
suites **83/83** (`test_shared_capture_finalization`, `test_window_paged_legacy`, `test_capture_ring`), full native
**2,836 / 7 skipped / 2,829 succeeded / 0 failed**, `teensy41-capture-serial` **SUCCESS** with the guard passing
(`variables 96,192 / ITCM 393,216 (code 392,360 + padding 856) / locals 34,880`; **16 B** above the floor), strict
validation and `git diff --check` clean. The three red-proofs above were **re-run against the landed form** (not only the
prototype) and produced the same failures, each restored byte-identically. **Owed, and named:** a **device run**, because
this is a firmware change the serial/HITL lane can observe (the registered windowed leg is the entry point); no timing
claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b), and no differential-parity claim for content the
paged path cannot serve (over-`L` intervals are its whole domain).

---

## 15. The device run: the I5 image boots and holds the canonical leg; the lane's own rows are unreachable, and why (2026-10-06; hardware)

**Identity (all three runs):** `git_commit 9b964dbc8859...`, `firmware_hex_sha256 9697b7acacb262d2...` -- the I5 image,
flashed through the managed capture (`--build-upload --build-env teensy41-capture-serial`; the guard's own line:
`variables 96,192 / ITCM 393,216 (code 392,360 + padding 856) / locals 34,880`).

| Run | Entry point | Outcome |
|---|---|---|
| `captures/i5-device-01-base` | `run --preset base --track 8 --loop-slot 4 --midi-channel 1 --build-upload --boot-reset` | flashed; the device booted fresh -- `#CAP,1571004,DIAG,boot_reset,crash=0` -- and the run then refused to start (`device stream has 7 line(s); expected at least 40`): the settle ended on the firmware's own banner before the SD-load rows arrived |
| `captures/i5-device-02-base` | the same leg, image already pinned (no upload) | **`overall_ok: True`** -- every verifier passed: stored record grid (32 notes, `bad=0/2`), record-stop stages (slowest `finalize:554us`), persistence handoff (`ok duration_us=46325`), deferred persist nonblocking (`loop_persist=288 sliced=True ok=True`) and large-loop bounds (`windows=5 progress=True ... active=True stop=True ok=True`), **MIDI-boundary timing `stopping=2 overflow_free=True call_sites=True`**, min free RAM2 131,072 B, overdub wrap storage (`pairs=2 sevt=109 ok=True`), capture cleanup, stored overdub spans (`count_ok/span_ok/bars_ok` on both takes), display rows after overdub and after stop (`overdub_ok=True transport_ok=True`) |
| `captures/i5-device-03-windowed` | `run --scenarios windowed_content_parity --track 8 --loop-slot 4 --midi-channel 1` (the registered windowed stimulus, `HITL_MANAGED_RUN_TIMEOUT_S=900`) | the leg drove the authorized slot: clear, **24-bar record** (384 streamed notes / 2,304 clocks), stop into play, `NOTE_EDIT` entered and exited, the play/stop window gestures; no crash, no wedge, clean capture teardown (only two `RING,shed,1` diagnostics). Its **display oracle failed exactly as recorded before** (`ok=False`: `missing_bounded_window_disp_rows`, `insufficient_disp_during_note_edit_freeze count=0`, `missing_playing_bounded_window_disp`), and the wrapper could not drain the retained rows (`no serial port on this run`: the runner sets `capture_serial_port`, not the leg's `--serial-port`, so `serial_port_from()` returns None) |

**What the device says about I5, and what it cannot say.** The canonical leg runs through the very
`playCommittedLoopMidi` the fix changes, and it is **green** -- including the timing owner's own gates
(`overflow_free`, `call_sites`) -- so the tick path's shared behaviour is unchanged on hardware. **The paged branch
itself, however, is not observable on device:** it has no serial row family (the window lane has `win_gate`, the launch
path `launch_gate`, the snapshot owner `snap_gate`; the paged delivery has none), so nothing on the wire can name its
admission or its serving. The one image that *does* emit the lane's rows is
`teensy41-capture-serial-window-qualification` (`PLAYBACK_WINDOW_ROLLING_QUALIFICATION=1`; `SC_QUALIFICATION_ROWS_ENABLED`
requires it), so the plain capture image compiles **no** qualification rows at all -- which also explains the leg's
`ok=False` here and in earlier runs: that oracle asks for rows this build cannot emit.

**And that image cannot be flashed: it is over its own RAM1 budget, before I5 as well as after.**

| `teensy41-capture-serial-window-qualification` | Post-link |
|---|---|
| its approved baseline (`scripts/ram1_budget.json`) | `ITCM 393,216` (12 banks; code 392,232 + padding 984), locals 34,880 |
| **at HEAD~1 (the I4 commit, before I5)** | `code 393,144 + padding 72` -- still 12 banks, but **padding 72 < the 840 floor: the guard already failed** |
| **with I5** | `code 393,288 + padding 32,696`, locals **2,112** -- a **13th ITCM bank**, which the guard names as the *documented silent boot hang* |

So the windowed lane's own rows -- and any row added there, including the "one more row" idiom for the paged delivery --
are unreachable on hardware until that env is funded: it needs a budget slice of its own, roughly the **912 B of drift**
it accumulated since its baseline plus I5's 64 B. **Recorded, not attributed:** that drift predates this slice (measured
at HEAD~1 above) and no attempt was made to find which slice caused it; the qualification env's failure is not I5's
alone, and I5's contribution to it is **+72 B over the bank boundary** on that env (the capture env keeps its guard
passing at padding 856).

**Not claimed:** no device evidence that the paged delivery is admitted or serves (no row exists to carry it, and this
leg's shape creates no window demand -- the same conclusion the 2026-10-05 measurement recorded); no timing-envelope
claim; no `ExecutionBounds.md` row change; no fallback removal (3.7b). **Owed next:** an env-budget slice for the
qualification image (or a paged-path row that does not need it), and then the lane's own device read.
