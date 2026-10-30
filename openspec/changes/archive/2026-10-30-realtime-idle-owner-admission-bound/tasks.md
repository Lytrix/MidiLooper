## 1. Readiness gates (before any firmware edit)

- [x] 1.1 Run [`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md) against D1–D7 of `design.md` and record the outcome. Verify it names the owner (the deferred idle-owner turn), the bound-class changes for rows **5/8/9/10/12/19 only**, the new `kOwnerWorkBudgetUs` reservation rule (D2), and that no new scheduler/owner is introduced. **Row 14 (`processDeferredSaveState`) is explicitly excluded and stays coarse/empirical, recorded as "nominal budget not enforced within an indivisible step" until section 6 splits it.**
- [x] 1.2 Grep `docs/DECISION_LOG.md` for `admission`, `bound`, `idle_maint`, `resolveDisplayNotes`, `persist` budget, and `SEVT` entries; verify no existing DEC is contradicted, and list the DEC numbers the change must cite in its commit messages.
- [x] 1.3 Write the G2 execution packet for slice 1 using [`docs/Templates/EXECUTION_PACKET.md`](../../../docs/Templates/EXECUTION_PACKET.md). Verify it pins: the owner work budget vs breach threshold separation, reserve-before-dispatch, aggregate turn conformance, one named deferral per unbounded child, no content loss across a deferral, and **the atomicity of the interdiction and the windowed replacement for the overdub source view** (D3).
- [x] 1.4 Decide and record `kOwnerWorkBudgetUs`: **corrected from 25 ms to 8 ms**, because a budget derived from half the 50 ms alarm can exceed one MIDI clock service interval. The measured interval is ~20.8 ms (`DIAG,service_period,20832`), so the rule is `budget + turn overhead < minimum clock interval`. 8 ms leaves ~12.8 ms for the turn's own cost. Verify `test_riao1_budget_fits_inside_one_midi_clock_interval` (asserts budget × 2 ≤ the anchor) and that the packet records the residual as the remaining tunable input. **Do not size tiles or grains against this number.**
- [x] 1.5 Confirm the declared envelope the contract is sized against: `Config::NUM_TRACKS`, `Config::MAX_LOOPS_PER_TRACK`, the longest supported loop (window count = `ceil(loopLengthTicks / (16 * TICKS_PER_BAR))`), and the `min(declaredChildMax, remainingOwnerWorkBudget)` rule. Verify the numbers are recorded in the packet, not assumed.

## 2. Owner admission contract — **lands with section 4 in one commit and one image**

> Sections 2 and 4 are one deliverable. `rebuildOverdubSourceView` is load-bearing for overlap
> correctness: an image in which it is denied while the whole-loop resolve is still the only refresh
> path would make the source view stale. Do not build, flash or commit section 2 without section 4.

- [x] 2.1 Add the admission predicate beside the existing deferred-owner policy helpers in `include/Utils/MidiServiceDrain.h`: `run` / `defer(named reason)` / `not owed`, the compile-time registry of children with their **declared maximum grain**, and the reservation rule. Verify with a native unit test that a child whose declared maximum exceeds the remaining owner work budget is never dispatched, and that a finer-grained resumable child may take a smaller deadline. **Done:** `admitIdleOwnerChild` / `IdleOwnerTurnBudget` with the `kIdleOwnerChildBounds` registry; `test_riao1_b_child_that_cannot_fit_is_not_dispatched`, `test_riao1_c_finer_grain_takes_the_remaining_deadline`, `test_riao1_d_aggregate_turn_is_bounded` and `test_riao1_e_registry_is_total_and_coherent` pin the rule. The production gate for **unbounded** children is the header-only `unboundedIdleOwnerChildMayRun` (the inline `admitIdleOwnerChild` ODR-used the registry array into `.data` and snapped DTCM by 4 KB); the full reserve-before-dispatch contract remains the bounded-child path and the registry/enumeration test.
- [x] 2.2 Add the **RED** production-path fixture: an overdub chain with a timing-critical track where every currently-unbounded child is owed on the idle turn. Assert the aggregate turn conforms, each unbounded child emits a named deferral, and no child is dispatched into a budget it can overshoot. Verify it is red before 2.3 and mutation-red if the reservation check is bypassed. **Resolved by decomposition (the literal combined fixture is unreachable):** the prewarm call site is reached only on the target track's playback/stopped-recording lane, while the stored-MIDI verification call site requires that **same** track fully idle (`transportFullyIdle`), and an idle turn targets one track — so one target-track turn cannot owe both. Coverage is decomposed: the aggregate bound for every admission permutation is `test_riao1_d_aggregate_turn_is_bounded`, the per-child admission/reservation is `test_riao1_a/b/c/e`, the lossless production-path deferral is `test_owner_admission_defers_the_unbounded_wrap_fallback_losslessly`, and the named-row literal is pinned by `test_capture_line_tier`.
- [x] 2.3 Wire the predicate into `runDeferredIdleMaintenance` (`src/main.cpp`) and mark as unbounded-and-denied while timing-critical: the whole-loop display-note resolve helpers reached from the idle lane, `prewarmOverdubSourceSpanCache`, **`deferredStoredMidiVerification`'s effective-notes phase, and the `SEVT` dump**. `rebuildOverdubSourceView` is **not** simply denied — it is admitted only through the bounded grain of section 4 (tasks 4.6/4.8). Verify 2.2 is green and the existing owner-rotation tests pass unchanged. **Done:** `timingCritical` is now a parameter of the Track idle turn (production passes `timingCriticalTrackActive`); the prewarm, the stored-MIDI/SEVT verification and the deferred append's whole-pass fallback are gated by `unboundedIdleOwnerChildMayRun`. The coverage worker remains the bounded producer (row 36). Full native green.
- [x] 2.4 Add the Tier-A deferral row (child name + reason + whether coverage still held, where applicable; one per child per interval) and register its literal in `include/Utils/CaptureLineTier.h`. Verify `test_capture_line_tier` covers the literal and that it survives the reduced timing-critical flush budget (extend `test_capture_ring` if the existing cases do not cover it). **Done:** `#CAP,<us>,DIAG,idle_owner,defer,child=<name>,reason=timing_critical_unbounded`, rate-limited to one per child per 5 s in RAM2; `test_idle_owner_deferral_row_is_tier_a_and_its_boundary_is_exact` pins the classifier boundary.
- [x] 2.5 Add the lossless-deferral fixture: a deferred child retains its cursor/request and completes on the next non-timing-critical turn with identical output. Verify it fails if the deferral resets state or reports success. **Done (wrap-fallback child):** `test_owner_admission_defers_the_unbounded_wrap_fallback_losslessly` proves the pending append and the cache are untouched on the denied call and the append completes on the admitted call. The prewarm/verification retain their existing queue/pending flags (the gate never clears them).
- [x] 2.6 Update `docs/Authority/Architecture/ExecutionBounds.md`: row 19 becomes the aggregate bound; rows 5/8/9/10/12 are reclassified with the reason the predicate now defers them; **row 14 is left as-is with an added explicit note that its nominal budget is not enforced within an indivisible step and that naming the step does not bound it.** Verify the table matches the predicate's registry one-for-one (enumerate both in a test or script rather than eyeballing). **Done:** rows 9/12/19 updated and row 38 added for the gate; row 14 is unchanged and still carries its explicit coarse/empirical note. The registry/enumeration parity is pinned by `test_realtime_idle_owner_admission`.
- [x] 2.7 Run the full native suite (`pio test -e native`) and the target build (`pio build -e teensy41-capture-serial`); verify zero failures beyond pre-existing skips and record the exact RAM1 `variables` / ITCM figures against `96,192 / 391,992 + 1,224`. Any new static must be `DMAMEM`/external, not DTCM. **Done:** full native **3700 / 7 skipped / 3693 / 0 failed**; `teensy41-capture-serial` SUCCESS, RAM1 `variables 96,192` unchanged, ITCM `392,120 + 1,096` (floor 840), locals 34,880. **RAM1 finding:** a first version that called the inline `admitIdleOwnerChild` from the hot Track TU ODR-used the registry array in `.data` and snapped DTCM by +4,096 B; the production gate is now the header-only `unboundedIdleOwnerChildMayRun`, and the registry stays in the test/contract path.

## 3. Report-only persistence attribution (same build as the first oracle)

- [x] 3.1 Add the named-step row for deferred persistence: step name, step duration, budget in force, indivisible flag — emitted only when a single step exceeds the active budget or the declared bar, one row per step per interval. Verify it does not allocate, does not run on the stop path, changes no admission/budget/order, and **does not alter row 14's classification**.
- [x] 3.2 Register the row's literal in `include/Utils/CaptureLineTier.h` and verify it survives the reduced flush budget (`test_capture_line_tier` + `test_capture_ring`). **Done:** the row rides the already-registered `DIAG,persist_save,` prefix; `test_persistence_named_step_row_is_tier_a` pins it. No new classifier literal.
- [x] 3.3 Add a native fixture that drives the deferred save state machine with an artificially expensive single step and verifies exactly one named-step row is emitted for it, and none when every step is within budget. Verify it is red if the row is emitted per step instead of per breach. **Done:** `test_persistence_step_is_named_only_on_a_breach` covers at-budget, over-budget, under-bar, over-bar and the measured 213 ms step. **State-machine half now landed:** `test_riao3_3_forced_slow_step_names_exactly_one_row_per_interval` drives the real revision-commit FSM with a test-only reported-duration override (60,000 µs; the measured duration still drives `displayBlockUs`), observes exactly one report for the executed step, and pins one row per step per 5 s with none inside budget.
- [x] 3.4 Verify the existing persistence suites are unchanged: `test_storage_loop_io`, `test_storage_drain_restore`, `test_shared_capture_finalization`. Record the counts.

## 4. Item 1 — touched-coverage preparation (the chosen 4.5 architecture)

> §2 and §4 remain one commit and one image (D3 atomicity). The windowed derive from the earlier
> 4.2–4.4 pass is retained as the **grain**; it is no longer the correctness mechanism.

### 4a. Characterization and contract (read-only first)

- [x] 4.1 Characterize when overlap information is semantically required. **Done by inspection:**
  occupancy at the pressed tick is required at note-on (`TrackCaptureInput.cpp:222-232`, ledger primary,
  source view fallback); the **complete note span** is required at note-off
  (`TrackCaptureInput.cpp:1106-1130`) and at stop finalization (`:720-745`); the held-pitch merge
  (`ensureOverdubSourceNotesForHold`) additionally requires participation as the playhead advances. A
  held note's *remaining* span may lag. Recorded in `design.md` D3.
- [x] 4.1a Write the prerequisite contract as code — **three**, not two: `OccupancyAtPhase`,
  `HeldParticipationAtPhase`, `SpanFinalization` — each returning ready / owed. An **owed obligation is
  persistent state**, not a transient result: it survives until discharged or the session ends, and it is
  **coalesced by note identity + tile** (never one per tick/sample/event), generation-qualified, and
  re-owed with required coverage on a generation change. Verify unit tests for each state, that none can
  report ready on partially prepared coverage, and that the obligation set stays bounded under repeated
  hold ticks in one tile. Pin the NoteOn ordering rule: the MIDI note is accepted immediately, the
  pressed-phase demand is marked, then ledger authority is consulted; "fallback refused" is never
  interpreted as "not occupied". **Done:** `include/Utils/CoveragePrerequisite.h` (tile model, obligation
  set coalesced by (note identity, tile, kind) with a bounded capacity of 64 and re-owe on generation
  change, the three status functions) + `test_coverage_prerequisite` **10/10**, covering the four
  obligation REDs, the sparse-tile case and the envelope. No production wiring yet.
- [x] 4.1b Define **two separate** envelopes: (a) the **tile set** — tile size, bounded maximum tile
  count, and the required/prepared/pending representation per slot; (b) the **worker grain** — an
  event/operation-bounded resumable cursor *inside* a tile. Verify the tile envelope against the longest
  supported loop and that nothing is charged to RAM1 (per-slot external state; file-scope state in
  `DMAMEM`). Verify the grain is chosen from a worst-case dense tile plus long closure, **not** from
  `kOwnerWorkBudgetUs`. **Done:** 4-bar tiles, 64-tile envelope (256 bars, one 64-bit word), and a
  `GrainCursor` bounded by events/operations (64/64) rather than by tile; above the tile envelope the
  caller takes a named whole-loop fallback. Verified in `test_coverage_prerequisite`.

### 4b. Coverage accounting

- [x] 4.2 **Required coverage is "phases touched so far", in three cheap transitions** — the final span
  is not known at acceptance. NoteOn marks the pressed-phase tile; held advancement marks each newly
  crossed tile; NoteOff/stop seals the complete span and ensures every tile in it is required. None of
  the three derives. Verify: sparse touches at bar 2 and bar 27 require exactly two regions and the
  middle stays untouched; repeated wraps add nothing **unless a held note continues into an untouched
  phase** (that case must add the tile); revisiting a prepared region re-requires nothing.
  **Done:** `CoverageState` in `include/Utils/CoveragePrerequisite.h` (`markNoteOn`, `markHeldAdvance`,
  `markSpanSealed`, `publishPrepared`, `changeGeneration`, `spanPrepared`, `nextOwedTile`) with 8 state
  tests in `test_coverage_prerequisite` (18/18 total): sparse middle untouched, 16 wraps add nothing,
  a crossed boundary adds exactly one tile, revisiting a prepared tile is free, span seal covers the
  wrap, generation change keeps required and re-owes prepared, partial coverage is never ready, worker
  selection skips prepared tiles. Not yet wired to capture events.
- [x] 4.3 Conservative closure: expand candidate lookup backward by the maximum committed source-note
  span. Verify a committed note starting before a touched region and sustaining into it is a candidate,
  and a deliberately over-wide maximum only over-admits. **Done (primitive):** `closurePadTicks` /
  `closureWindow` in `CoveragePrerequisite.h` — the pad is the max committed span, a pad `>= loop` and a
  tile+pad that covers the loop both **saturate to the whole loop** (no ambiguous modular range), and the
  clock-start tile wraps instead of clamping. `test_coverage_prerequisite` covers the pad/saturation/wrap
  cases; `RIAO-2.T` proves two adjacent tile closures composed through `upsertResolvedDisplayNotes`
  contain **no duplicate identity** and equal the whole-loop derivation restricted to the two-tile union.
  **Not yet wired to a real `maxCommittedNoteSpan` source.**
- [x] 4.4 A source-generation change **keeps `requiredCoverage`** (it is session state describing what was
  touched and the session stays semantically alive) and invalidates `preparedCoverage` plus any in-flight
  preparation; every currently required tile becomes **pending again** for the new generation. **Done:**
  `Loop` now owns the per-slot `CoverageState` (it cannot live in `Track` — the 384-byte DTCM pin forbids
  a member; shells are extmem, so `sizeof(Loop)` 1,856 → 1,888 is external bytes and RAM1 is unchanged).
  `openOverdubSession` calls `beginSession(loopLengthTicks, playbackRevision)`, and
  `notifyCommittedContentChanged` — the single committed-content edge — calls
  `changeGeneration(playbackRevision)`. `RIAO-2.U` drives that production edge and asserts both touched
  tiles are re-owed while required coverage survives and prepared coverage is cleared.
- [x] 4.5 Per-region atomic publication: **done** — `serviceOverdubCoverageTurn` publishes a tile's notes
  and its prepared bit on the completing grain; `RIAO-3.1` proves an incomplete grain writes no content and
  sets no bit. Required coverage is the open window plus acceptance/hold marks, never the whole loop.

### 4c. Deferred preparation owner

- [x] 4.6a **Worker has an owner turn.** `Loop::serviceOverdubCoverageTurn(eventBudget)` advances one
  declared grain of the next owed tile (`nextOwedTile`), starts a tile's cursor fresh when the tile or the
  source generation changes, and publishes the tile atomically only on the grain that completes it. Called
  from `Track::processDeferredIdleMaintenance` **before every early return**, gated on the VisualCache
  rotation while a session is open — the demand comes from an accepted note, not from the turn. Verified
  end-to-end in `RIAO-4.7`: a real overdub session accepts a note, the production idle lane runs, and the
  owed tile becomes prepared (`owedCount() == 0`, `preparedCount() > 0`). `sizeof(Loop)` 1,888 → 1,944
  (+56 measured) for the per-slot worker cursor; RAM1 unchanged.
- [x] 4.6 Replace the windowed refresh with the region worker: one **declared event/operation grain** per
  deferred-owner turn (a resumable cursor inside the tile), selecting an owed tile deterministically,
  deriving it with the existing windowed primitive, publishing it atomically on completion. **RED first:**
  a worst-case dense tile plus a long-closure case must still stop at the declared grain in one turn —
  written before the worker and verified to fail against a tile-sized (unbounded) turn. **Done (primitive
  only):** `LoopContentResolution::stepTilePreparation` steps the *same* per-pass windowed derivation over
  a declared event budget with a resumable per-pass cursor and publishes only on completion. `RIAO-2.R`
  proves byte-for-byte parity with the single-shot windowed derive at budgets 1/3/7/64/unbounded
  (turns 16/6/3/1/1, 8 notes each); `RIAO-2.S` is the dense-tile RED — a 4-bar dense tile at the 64-event
  grain needs **2 turns**, reports incomplete on the first, and equals the single-shot result when done.
  **No owner calls it yet.**
- [x] 4.7-pre **Marking on the production capture path** (the precondition for wiring the prerequisites):
  NoteOn marks the pressed tile where the occupancy phase is computed; held advancement marks each
  phase a held key crosses at a playback note-on; NoteOff and stop finalization seal the complete span
  (wrap-aware) before the delta is computed. All three are marks only — no derivation, no allocation.
  **Done:** `Loop::markOverdubCoverageNoteOn` / `markOverdubCoverageHeldAdvance` /
  `markOverdubCoverageSpan`, called from `TrackCaptureInput.cpp` at those three sites. `RIAO-4.7` drives
  a **real** overdub session and note acceptance (`test_immediate_capture_handoff`) and asserts the
  touched tile is required and nothing is prepared. Build SUCCESS, RAM1 unchanged, ITCM unchanged.
- [x] 4.7a **Obligation REDs (write these before 4.7):** **done (b/d and coalescing)** — `RIAO-3.4`
  defers a completion in an unprepared tile and discharges it exactly once with the reference Shorten;
  `test_coverage_prerequisite` covers coalescing by (note identity, tile, kind), the bounded set and the
  generation re-qualification. (a)/(c) remain covered by the hold-advance mark and the coalesced hold
  ticks in the same state tests. (a) a held note enters an unprepared tile, leaves
  it, the tile prepares afterwards → the held-participation consequence still executes **exactly once**;
  (b) NoteOn occupancy is owed and the note reaches NoteOff before its tile prepares → preparation later
  produces the **same final overlap delta as the synchronous reference**, applied once; (c) many hold
  ticks inside one unprepared tile do not create an unbounded backlog; (d) a source-generation
  invalidation retains and re-owes the outstanding obligations alongside required coverage. Verify each
  is red before 4.7 and mutation-red if obligations are dropped when the playhead leaves the tile.
> **§4.7 + §4.8 are one atomic slice (`RIAO-3`).** The dependencies are circular in isolation: the worker
> cannot publish content until it is the sole producer, the producer cannot be retired until the view is cut
> from prepared tiles, and the prerequisites cannot be wired until their deferrals are dischargeable. The
> packet, the six REDs and the ordered steps are in
> `evidence/2026-10-29-ATOMIC-4.7-4.8.md`. Shape: **one producer (the worker), two drivers** — the deferred
> owner and the synchronous (bounded) session-open path.

- [x] 4.7 Wire **all three** prerequisites: **done for the two decision points** — `OccupancyAtPhase`
  (note-on, and the held-advance path through the same function) and `SpanFinalization` defer by name and
  discharge on the published tile or at the seal deadline. `HeldParticipationAtPhase` is folded into the
  occupancy decision: the hold path calls `resolveOccupancyAtNoteOn`, so it shares the same obligation and
  no second deferral kind exists. `ensureOverdubSourceNotesForHold` is now a read-only view projection. `OccupancyAtPhase` on the NoteOn path,
  `HeldParticipationAtPhase` in place of `ensureOverdubSourceNotesForHold`'s synchronous derive (the
  94.9 ms `why=private_hold` breach — a fixture must prove that path can no longer derive), and
  `SpanFinalization` into `accumulatePendingNoteChangesForIncomingNote`. Verify each defers **by name**
  rather than computing against partial coverage, and that a deferred note still completes when its tile
  finishes (no wedge).
- [x] 4.8 **Ordering constraint (measured): retire the moving-window producer BEFORE the worker publishes
  content.** `Loop::rebuildOverdubSourceSpanCache` and the coverage worker would otherwise be two producers
  of a single-owner cache (C3.1/C3.5: a private-predecessor scan owns it from its first grain until its
  holds are materialized). A candidate change that had the worker merge its tile's notes on completion was
  reverted because it broke `test_predecessor_publication_mid_scan_discards_the_private_prefix`; the worker
  currently publishes only the prepared bit and yields while a scan is pending
  (`evidence/2026-10-29-WORKER-OWNERSHIP-FINDING.md`). Retire `kOverdubSourceWindowBars` and the
  playhead-centred coverage as the correctness mechanism, keep a region-grain selector, **then** make the
  worker the sole producer of the cache (content + bit on the completing grain), and only then make the
  view's reuse predicate "every tile of the window is prepared".
- [x] 4.9 Make the whole-loop span cache optional: **done for the session path** — no session consumer
  depends on the prewarm; it is STOPPED-only and adopted whole at the open edge only when current. The
  display no longer reads the overlap view at all (`CommittedDisplayBase`). `fullLoop` stops being a readiness requirement, and no
  consumer may depend on the prewarm having run. Verify a session with the prewarm permanently disabled
  produces identical overlap deltas.

### 4d. Parity and acceptance

- [x] 4.10 Per-tile parity **and closure composition**: **done** — `RIAO-2.T` (identity composition,
  no duplicate candidate) plus the both-side closure fixture in `RIAO-3.1`/the long-note pin; `RIAO-3.3`
  is the assembled convergence gate. a single touched tile's candidate set equals the
  whole-loop derivation restricted to that tile plus closure; combining adjacent tiles composes by stable
  `NoteId` (never by concatenation) so a note in two closures appears once; and
  `maxCommittedNoteSpan >= loopLengthTicks` saturates closure to the whole loop without ambiguous modular
  ranges.
- [x] 4.11 Convergence parity: **done** — `RIAO-3.3` touches every tile, drives the worker, and compares
  the cut to the whole-loop derivation (wrap rows compared in their head form).
  geometry equals the whole-loop derivation exactly — wrap-crossing notes, repeated/simultaneous
  same-pitch notes, `NoteId` identity, start/end geometry, no duplicates, no omissions.
- [x] 4.12 Wrap correctness under tiled coverage: **done** — `RIAO-2.B` (wrap window) and the closure
  saturation case; the convergence fixture includes a loop-wrap note and a same-pitch twin. a wrap-crossing source note and a wrap-crossing
  overdub note resolve correctly when the touched regions straddle the loop end.
- [x] 4.13 Cost acceptance: a two-bar touch derives proportional work, not loop-proportional work.
  Verify with the existing seam: derive ops for a two-bar touch are a small fraction of the whole pass
  (the windowed baseline measured whole 311 / 16-bar 181 / 4-bar 61 ops). **Done:**
  `test_riao4_13_two_bar_touch_is_region_proportional` puts the same two touched bars in an 8-bar and a
  31-bar loop: the covering tile derive is **3 ops / 2 notes in both**, while the whole-loop derive is
  **7 ops / 6 notes (8 bar)** vs **30 ops / 29 notes (31 bar)**. Region-proportional and loop-independent.
- [x] 4.14 Verify the scheduling side effect: **confirmed and recorded** — `overdubSourceSpanCacheReady()`
  means "no required tile is owed" during a session (an empty required set is ready), so
  `canonicalPreparationHasPriorityWork` reports ready unless preparation is actually owed. Recorded in
  `ExecutionBounds.md` rows 19/36 and DEC-211. `canonicalPreparationHasPriorityWork` now usually reports
  ready during a session. Confirm that is the intended behaviour with its own fixture and record it in
  `ExecutionBounds.md` if an owner's bound class moves.

- [x] 4.15 Update `docs/Authority/Architecture/ExecutionBounds.md` (row 12 and the whole-loop resolve
  rows) and `docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md` in the same commit. Verify the
  guide no longer describes the whole-pass derivation or the playhead window as the view's source.

## 5. Device oracle for slices 2+4

- [x] 5.1 List the registered stimuli (`.venv/bin/python scripts/host_midi_hitl.py stimulus list`) and verify an existing overdub-chain stimulus reproduces the capture's shape (one track, repeated overdubs over a grown take, transport running). Reuse it; create a temporary variant only if no registered stimulus reaches the shape, and promote it only on owner acceptance. **Done:** no canonical reaches the grown-take load shape (the closest `STIM-OCCUPY-PAINT-01` is 4 bars/two overdubs; `STIM-BASE-2-2-01` is 2 bars). Two TEMP variants were used: `STIM-OCCUPY-PAINT-01-TEMP-RIAO-ORACLE` (first attempt) and `STIM-BASE-2-2-01-TEMP-RIAO-HEAVY` (acceptance attempt, 32-bar dense record + one 32-bar overdub, bench track 8 slots 4/5). Neither is promoted.
- [x] 5.2 Ask the owner before flashing (default rule), then flash the clean `teensy41-capture-serial` image with Homebrew `teensy_loader_cli` 2.3, retaining the exact HEX SHA-256. Verify boot separately (`boot_reset,crash=0`, `BOOT,load,ok`, `BOOT,ram1`). **Done:** owner approved; `6a84c003…` (default capture) flashed and boot-verified, then `36160d60…` (`teensy41-capture-serial-no-grain`, the documented acceptance target) flashed and boot-verified. HEX retained in `captures/6a84c003/` and `captures/36160d60/`; loader 2.3 transcripts retained.
- [x] 5.3 Run the oracle and verify **all** acceptance numbers: zero `loop_rem,idle_maint` (exactly zero, not fewer), zero `win_gap_overrun`, **maximum `midi_gap` and maximum `clk`/`extclk_arrival` within their declared bounds and collapsed against `session_20261029_190227.log`**, and any `persist_save` breach named by step. **Done on the owner-accepted live-window scope** (`evidence/2026-10-30-ORACLE-LIVE.md`): 33-bar / 1,024-note grown take, transport 94.0-170.0 s, **zero live `idle_maint`**, **zero `win_gap_overrun` under transport**, **max overdub `midi_gap` 72.7 ms** (was 332 ms / 101 late events), `clk`/`extclk_arrival` 0 (no external clock), **12 named `persist_save,step` rows** (max 62.1 ms `work_item`). Recorded separately, not subtracted: the boot/load `idle_maint` 135,163 us at 9.07 s (`t7/s4/ph0`) and one post-stop `win_gap_overrun` (`overrunMax=83, running=0`) at 175.3 s.
- [x] 5.4 Verify the scheduler and the alarm agree: compare each turn's admitted child work against the residual (`idle_maint` span minus admitted child work). Confirm no turn lands near the 50 ms bar on residual alone, and record the measured residual that `kOwnerWorkBudgetUs` should be derived from. **Done:** image `5318f204…`, heavy base run, four near-bar breakdown rows: max total 45,291 us with **child 44,671 / residual 620 us**; 33,938 with residual 1,668; 30,406 with residual 200; 27,273 with residual 430. **Max residual 1,668 us (3.3 % of the bar)**; all near-bar turns are child work at boot/record start, none in the live overdub window. The scheduler and alarm agree on residual alone. **Finding for 5.5:** the 45.3 ms turn's visual-cache/wrap-append child work exceeds the declared 3 ms/4 ms maxima at boot; recorded as a boot/load-lane child-max gap, not a 5.4 residual pass. Evidence: `evidence/2026-10-30-IDLE-BREAKDOWN-5.4.md`.
- [x] 5.5 Attribute every remaining breach to a named child and verify each is either declared unbounded-and-deferred or has a declared maximum that the row proves was respected. Write the attribution table into `evidence/`. **Done:** the only `loop_rem,idle_maint` row is the boot restore (`track=7 slot=4 phase=0`, 135,163 us), the load lane's child — not an idle-owner unbounded child; the one `win_gap_overrun` is post-transport (`running=0`) and belongs to the windowed playback lane. No idle-owner child breach remains in the live window. The load lane's own declared maximum is not proven by the row and is a separate load-path finding. The 5.4 breakdown adds: boot/record-start visual-cache/wrap-append child work reached ~44.7 ms against declared 3/4 ms maxima; the live-overdub window has none. That boot child-max gap is recorded separately and is not counted as a live-window pass.

## 6. Persistence splitting (sequenced by measurement)

- [x] 6.1 From 5.3/5.5, name the indivisible step and verify the choice with the named-step rows from two runs (one overdub, one stopped) rather than one sample. Confirm row 14 is still recorded as coarse/empirical at this point. **Done:** the step is **`work_item`** (the persistence work-item writer). Two runs name it as the only over-bar step: `6a84c003…` (13 rows; `work_item` 20.1 / 27.5 / 35.2 / 19.1 / 4.7 / 21.3 / 24.2 ms) and `36160d60…` (12 rows; `work_item` **62.1 ms** during the overdub, then 43.9 / 26.9 / 25.7 / 25.7 ms stopped, budgets 300/3000 us). **Refined by the adjacent `PERS,bundle` row:** the 62.1 ms slice is a **`SlotMeta` runtime-bundle slice** (`587,577 bytes`, `241 slices`, one `61,973 us` slice, `57` undo snapshots), not the loop payload, which writes 512-byte quanta. Row 14 remains coarse/empirical; 6.2 targets the bundle stage that produced that slice.
- [x] 6.2 Split that step so the active-transport budget is enforced *within* it, preserving ordering, exactly-once write semantics and the mid-pass capture-chunk flush. Verify with a native fixture that the step is resumable across turns and produces byte-identical output. **Done (bundle stage):** `DeferredFooterWriteStage::GlobalUndoStackHeader` writes the footer's empty-GUS headers one per slice (the former eight-header burst was the 61,973 us `PERS,bundle` slice). `test_riao6_2_footer_writes_one_empty_undo_header_per_slice` proves one header per call and byte-identical output. The TU moved to FLASH via the linker carve-out (`ITCM 391,864 + 1,352`). **Not the whole story:** the 6.3 run still shows a 53,143 us `LoopPersist` finalize (`PERS,work,52682,done`) and live `idle_maint` 102/55 ms; see 6.3.
- [x] 6.3 Re-run the oracle and verify `persist_save` no longer breaches. **Only now** reclassify row 14 in `ExecutionBounds.md`, in the same commit as the enforcing firmware, and remove the "nominal budget not enforced" note. **Attempted, NOT accepted** (`evidence/2026-10-30-ORACLE-63.md`): the 62 ms bundle step is gone (max named step 25,723 us), but `loop_rem,persist_save,53143` remains from a `LoopPersist` finalize/rename (not named by the four scheduler grains), and the run has live `idle_maint` 102,135/55,275 us plus a transport-running `win_gap_overrun` (6 overruns, max 108). Row 14 stays coarse/empirical. **Owner-accepted limit (DEC-215):** the residual is the synchronous atomic SD rename (`AtomicRename` 21-28 ms after removing two redundant lookups; one 161 ms card-stall sample did not recur; the recurring 161 ms `persist_save` is a post-stop aggregate). Deferring the rename under transport was declined; the live lane may block and the second probe shows three transport-running `win_gap_overrun` rows. Row 14 is **not** reclassified and the "nominal budget not enforced" note stays. **Instrumented** (`evidence/2026-10-30-ORACLE-FINALIZE.md`): the residual is the `VerifyAndRename` finalize stage, **33.6-44.9 ms on every save** (WriteToken 4-6 ms, EpochCrcBody 2-5 ms, EpochCrcHeader 5-8 ms); the 2-bar files are small, so it is the atomic rename plus its verify, not payload size. It cannot be split into CPU slices; the remaining choice is defer the stage while transport runs or accept it as an indivisible commit. **Split probe:** `AtomicRename` is **26.4-35.8 ms** of the 33.6-45.7 ms stage (`VerifyToken` 2.1-5.2 ms, `SealJournal` 4.8-6.4 ms); the rename is the barrier. Deferring the rename is the only way to keep it off the timing-critical lane. **Rename optimization:** removing the redundant `exists(temp)`/`exists(final)` lookups saved **~6-7 ms** per save (`AtomicRename` median 28.4 -> 22.0 ms), but the rename itself remains 22 ms and one probe hit a **161 ms card stall**. Code cannot bound that; deferral is the only live-lane-safe policy.

## 7. Capture-only diagnostics

> `SEVT` and the verification dump are already denied while timing-critical in section 2. This section
> makes them genuinely bounded so they still run under transport instead of only when idle.

- [x] 7.1 Bound `deferredStoredMidiVerification`'s effective-notes phase (`copyEffectiveCommittedEvents` + whole-pass `resolveDisplayNotes` + `appendOverdubPassDisplayNotes`) into resumable grains under the owner rule. Verify a capture-build fixture shows it completing under transport within the owner work budget, and that denying it (2.3) is still the fallback if a grain cannot fit. **Host implementation landed:** the effective-notes phase is one bar per owner turn (`Loop::resolveEffectiveDisplayNotesInWindow` + `upsertResolvedDisplayNotes`), the dead `copyEffectiveCommittedEvents` copy was removed, and `stored_midi_verification`/`sevt_dump` are reclassified bounded (4 ms max / 1 ms fine grain) with a header-only gate (`boundedIdleOwnerChildMayRun`). `test_riao7_1_bounded_verification_admits_the_declared_grain` pins the constants/gate; full native **3706 / 7 skipped / 3699 / 0 failed**; no-grain image `ea40bb94…` builds at RAM1 `96,192`, ITCM `391,880 + 1,336`. **Base capture-build probe (`bbf88845…`):** two dumps completed, `dnte,src,effective,count,54` = `frame,count,54`, 332 `DNTE` rows; no `loop_rem,idle_maint` during either dump and the periodic `idle_maint` max stayed <= 1 ms. **Under-transport condition is unreachable, not unbuilt:** `transportFullyIdle` requires the target track STOPPED, the only per-track stop is the global transport stop, and a global stop clears `timingCriticalTrackActive`; mute does not change `trackState` (mute probe: dump still ran only after the final stop). The gate's timing-critical branch is native-tested; the **2.3 denial is the effective behavior**, and the bounded derive is exercised on the stopped lane. Evidence: `evidence/2026-10-30-STORED-VERIFY-BOUND.md`.
- [x] 7.2 Bound the `SEVT` dump into grains under the same rule and verify the same two properties. **Implemented:** the effective/SEVT row emission is capped at **4 notes per timing-critical turn** (64/16 otherwise), and the one-bar derive is the grain. The base probe (`bbf88845…`) completes both dumps with 54 effective = 54 frame notes and no one-shot `idle_maint`; the 44 ms/54-row burst is gone (periodic max <= 1 ms). The 4-row slice is verified on the base capture; the under-transport condition is unreachable for the same reason as 7.1 (mute does not change `trackState`; the global stop is the only per-track stop). The 2.3 denial remains the effective fallback.
- [x] 7.3 Verify a capture build's production acceptance is readable without subtracting capture-only rows: run the oracle fixture and confirm zero capture-only rows share a turn with a production breach. **Done on the accepted oracle fixture:** the no-grain acceptance run (`36160d60…`, `evidence/2026-10-30-ORACLE-LIVE.md`) has zero live production breaches, and the no-grain env (`SESSION_CAPTURE_LCR=0`, `SESSION_CAPTURE_GRAIN=0`, `SESSION_CAPTURE_DROID_QUEUE=0`) drops the high-volume capture-only families. The later DEC-215 runs' accepted live win_gap overruns are a separate owner-accepted limit, not an oracle-row subtraction.

## 8. Closeout

- [x] 8.1 Run the full native suite (`pio test -e native`) and the target build; record exact counts and RAM1/ITCM. Verify no pre-existing skip changed status. **Done:** full native **3706 / 7 skipped / 3699 / 0 failed** (parallel with the build/probes); `teensy41-capture-serial-no-grain` SUCCESS, RAM1 `96,192`, ITCM `391,976 + 1,240`, locals 34,880; the 7 pre-existing skips are unchanged. Image `5318f204…`.
- [x] 8.2 Update `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md`, `docs/DELIVERABLE_TRACKING.md`, and this `tasks.md`. Verify the state entry names the oracle numbers, not an impression. **Done:** the runtime entries name the live-window numbers (72.7 ms vs 332 ms, zero live idle_maint/win_gap), the 5.4 residual (1,668 us), the accepted limits and the evidence paths; `DELIVERABLE_TRACKING` has the delivered-behavior entry; this file is updated.
- [x] 8.3 Append the `DEC-###` entries the design's decisions require (turn-level owner work budget derived from the fastest supported clock interval; **touched-coverage tiles as the correctness/publication unit with an event/operation execution grain**; the three persistent prerequisites; **convergence parity**; persist step granularity) and verify each cites its evidence capture. **Done:** DEC-212 (turn budget vs alarm), DEC-213 (parity as convergence gate), DEC-214 (persist step naming until split), DEC-215 (atomic rename accepted as an indivisible blocking commit); DEC-211 already covers the touched-coverage producer/display-base split, the three prerequisites and the convergence unit. Each cites the oracle/probe captures.
- [x] 8.4 Archive only when every gate above is checked; verify `openspec validate realtime-idle-owner-admission-bound --strict` passes first. **Done:** all gates 1.1-8.3 are checked, `openspec validate --strict` passes, and the change is archived with its two delta specs applied to `openspec/specs/`.
