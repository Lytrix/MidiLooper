# RIAO-3 landed: one producer, touched-coverage prerequisites, and a committed display base split

**Date:** 2026-10-30
**Change:** `realtime-idle-owner-admission-bound`
**Status:** implemented; native and target build green; **device acceptance open**.

## What landed

1. **One producer, two drivers.** `Loop::serviceOverdubCoverageTurn` is the span cache's only writer while
   a session is active. One declared event grain (64) per turn inside one tile's closure-padded lookup
   window; the grain that completes the tile publishes its candidate notes **and** its prepared bit.
   Drivers: the deferred idle owner and the session-open edge.
2. **The overlap view is a read-only cut** of the published tiles, composed by `NoteId`, plus the private
   predecessor prefix while that scan owns the cache. No playhead window, no display role. Only pending
   Hide/Shorten are applied at the cut; a pending **Add** is not a committed candidate (merging it made a
   later consequence target the session's own live row).
3. **Both-side closure.** The lookup window is widened by the committed maximum note span in both
   directions, seeded once per session from a committed-event scan and grown from published rows. Without
   the forward pad a note whose Off lay beyond its tile (On@0/Off@5000 on a 6144-tick loop) was published
   by no tile.
4. **Prerequisites defer by name.** Occupancy at the pressed phase and span finalization are owed when
   their tiles are not prepared; the bounded obligation set coalesces per note, the worker discharges on
   the completing grain, and the seal deadline answers the remainder from a bounded window of the note's
   own span.
5. **CommittedDisplayBase.** `DisplayManager::buildCommittedDisplayBase` gathers the committed generation ×
   display window (paint window + margin, capped at 16 bars) on a miss and caches it. It is invalidated by
   a commit/publication, loop-length change, slot/loop identity change, or a window move outside its
   coverage — never by live overdub activity or overlap tile preparation. Frame composition is
   `CommittedDisplayBase + LiveCaptureOverlay`. The display no longer reads `overdubSourceViewNotes_`
   anywhere (`DisplayNoteResolveLiveCapture`, `DisplayNoteWindowGather`).

## REDs and evidence

- **Frame-level RED (pre-split):** with the view tile-scoped and the display using it as its committed
  base, `test_live_capture_display_boundary` failed four cases
  (`test_late_record_window_and_closed_overdub_progress_before_root_publication`,
  `test_dense_...`, `test_selected_gap_display_source_survives_competing_audible_track`,
  `test_retired_predecessor_notes_survive_third_overdub_start`) with an accepted NoteId missing from the
  frame. The parked attempt and the failure shape are recorded in `captures/riao3-attempt-20261029/`.
- **Abstraction pin:** `test_committed_display_base_is_independent_of_the_overlap_view` builds the base,
  shrinks the overlap view to one row, and requires the base to remain qualified and carry every committed
  record row; it also forces a base rebuild and requires the rows to come from the committed content.
  **Mutation-proven:** replacing `buildCommittedDisplayBase`'s window gather with an assignment of
  `overdubSourceViewNotes_` makes it fail (`Expected FALSE Was TRUE`, no committed rows). The display
  suite is **46/46** with it.
- **RIAO-3 contract set** in `test_overdub_source_window_parity` (15 existing + 4 new): the cut never
  derives and an incomplete grain publishes nothing (3.1), the open edge drives the same worker (3.2),
  convergence equals the whole-loop derivation (3.3), and an unprepared span defers then discharges exactly
  once with the reference Shorten (3.4). `RIAO-3.5` (scan ownership: the worker yields while a private scan
  owns the cache) is in `test_immediate_capture_handoff`.
- The long-note closure pin and the RIAO-2.T identity composition are in the same suite.

## Gates

| Gate | Result |
|---|---|
| Focused native (`overdub_source_view`, `pending_note_change`, `immediate_capture_handoff`, `overdub_source_window_parity`, `live_capture_display_boundary`, `coverage_prerequisite`, `shared_capture_finalization`, `continuous_capture`) | **602 / 602** |
| Full native `pio test -e native` | **3698 cases / 7 skipped / 3691 succeeded / 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS**; HEX `sha256 8f3ff2fc6281448977970a2e8ee6602a1dfed0e66fc5dc99a7b019d1c95c5cbb` |
| RAM1 / ITCM | `variables 96,192` (unchanged) / `code 392,120 + padding 1,096` (floor 840, 256 B headroom) |
| Struct pin | `sizeof(Loop)` 1,944 → **2,288** (+320 deferred overlap completions, +4 closure pad, alignment), external shells; re-based in `TrackPlaybackRuntime.cpp` with the reason |

## Limits

- Device acceptance is open: zero `loop_rem,idle_maint`, the MIDI/clock gap maxima, and the `persist_save`
  step remain the oracle.
- Closure saturates to the whole loop when a committed note spans the loop; such a slot pays a whole-loop
  window copy per tile (never a whole-pass resolve in one turn).
- A display-base miss gathers up to 16 bars; the first frame after a commit/window move pays that bounded
  gather. The visual cache remains the faster committed source when it covers.

## Section 2 — owner admission (same tree)

- `Track::processDeferredIdleMaintenance` now takes `timingCritical`; production passes
  `timingCriticalTrackActive` from `main.cpp`.
- The three unbounded children this slice can reach are named-deferred on a timing-critical turn by the
  header-only `MidiServiceDrain::unboundedIdleOwnerChildMayRun`:
  `overdub_span_prewarm`, `stored_midi_verification` (the SEVT/effective-notes dump), and
  `whole_loop_resolve` (the deferred wrap append's revision-moved whole-pass fallback, which keeps the
  pending append and the existing cache instead of rebuilding — lossless).
- Tier-A row `#CAP,<us>,DIAG,idle_owner,defer,child=<name>,reason=timing_critical_unbounded`, one per
  child per 5 s (RAM2 stamp), registered in `CaptureLineTier` and pinned by
  `test_idle_owner_deferral_row_is_tier_a_and_its_boundary_is_exact`.
- Lossless production fixture: `test_owner_admission_defers_the_unbounded_wrap_fallback_losslessly`
  (denied call retains the append/cache; admitted call completes).
- **RAM1 finding:** the first version called the inline `admitIdleOwnerChild` from the hot Track TU; the
  registry array was ODR-used into `.data` and DTCM snapped +4,096 B (`variables 96,192 -> 100,288`).
  The production gate is now `unboundedIdleOwnerChildMayRun`; RAM1 returned to `96,192` with ITCM
  `392,120 + 1,096`. The registry stays in the contract/test path.
- Gates after section 2: focused **322/322** (six suites); full native **3700 / 7 skipped / 3693 / 0
  failed**; `teensy41-capture-serial` SUCCESS, RAM1 `96,192`, ITCM `392,120 + 1,096`.
- **Task 2.2 resolved by decomposition:** the literal combined fixture is unreachable by construction. The
  prewarm call site is reached only on the target track's playback/stopped-recording lane; the stored-MIDI
  verification call site requires that same track fully idle (`transportFullyIdle`), and an idle turn
  targets one track. Coverage is decomposed: every admission permutation is bounded by
  `test_riao1_d_aggregate_turn_is_bounded`; the per-child rule is RIAO-1.A/B/C/E; the lossless
  production-path deferral is `test_owner_admission_defers_the_unbounded_wrap_fallback_losslessly`; the
  named-row literal is pinned by `test_capture_line_tier`.

## Section 3 — persistence named-step row (report-only)

- `PersistenceDiagnostics::persistenceStepBreachExceeded(stepUs, budgetUs, barUs = 50000)` is the pure
  decision: a step is named when it exceeds the active slice budget or the declared 50 ms bar.
- `DeferredSaveScheduler.cpp` times its four indivisible scheduler grains
  (`revision_commit`, `revision_load`, `mid_pass_chunk`, `work_item`) and calls
  `PersistenceDiagnostics::onPersistenceStep(...)`; a breach emits one row per step per 5 s:
  `#CAP,<us>,DIAG,persist_save,step,<name>,us=...,budget=...,bar=...,indivisible=1`.
  It rides the already-registered `DIAG,persist_save,` Tier-A prefix; no new classifier literal.
- Report-only: no admission/budget/order change, no allocation, never the stop path; row 14 stays
  coarse/empirical until section 6 splits the step (DEC-043 point 4).
- Tests: `test_persistence_step_is_named_only_on_a_breach` (decision), `test_persistence_named_step_row_is_tier_a`
  (classifier), and **`test_riao3_3_forced_slow_step_names_exactly_one_row_per_interval`** (state machine).
  The last one drives the real revision-commit FSM with the native-only
  `persistenceStepDurationOverrideUsForTest` seam (forced 60,000 us report; the measured duration still
  drives `displayBlockUs`), observes exactly one report for the executed step, and pins one row per step
  per 5 s with none inside budget. Persistence suites
  (`test_persistence_work_queue`, `test_storage_loop_io`, `test_storage_drain_restore`) **108/108**.
- **RAM1 finding:** the new literals in `PersistenceDiagnostics.cpp` snapped DTCM +4,096 B. A linker
  carve-out (`*PersistenceDiagnostics.cpp.o(.rodata.str* .rodata.*.str*)` in `.text.progmem`) restored
  `variables 96,192`; the new cold function costs 128 B ITCM (`392,248 + 968`, 128 B above the floor).
- Gates after section 3: full native **3704 / 7 skipped / 3697 / 0 failed**; target build SUCCESS at
  RAM1 `96,192`, ITCM `392,248 + 968` (393,216 total), HEX
  `sha256 6a84c00362a6b01bdae479860ed7fbc3c6822fcead64636af06617c42f858f01`.

## Task 4.13 — region-proportional cost

`test_riao4_13_two_bar_touch_is_region_proportional`: the same two touched bars in an 8-bar and a 31-bar
loop. The covering tile derive is **3 ops / 2 notes in both**; the whole-loop derive is **7 ops / 6 notes**
(8 bar) vs **30 ops / 29 notes** (31 bar). Full native after adding it: **3703 / 7 skipped / 3696 / 0**.

## Host closeout (before the section-5 oracle)

- Task 2.1 checked: the reservation contract and the compile-time registry are covered by
  `test_realtime_idle_owner_admission`; production unbounded children use the header-only
  `unboundedIdleOwnerChildMayRun` (the inline `admitIdleOwnerChild` snapped DTCM +4,096 B).
- Task 2.2 checked by decomposition (above).
- Task 3.3 state-machine fixture landed (above).
- Task 4.13 closed: the same two-bar touch on 8- and 31-bar loops derives the covering tile for 3 ops /
  2 notes in both; whole-loop 7/6 vs 30/29.
- DEC-212 (owner turn budget 8 ms vs 50 ms alarm), DEC-213 (parity is a convergence gate), DEC-214
  (persist step naming is report-only until the split).
- Final host gates: full native **3704 / 7 skipped / 3697 / 0 failed**; `teensy41-capture-serial`
  **SUCCESS**, RAM1 `96,192`, ITCM `392,248 + 968` (393,216 total), locals 34,880; HEX
  `sha256 6a84c003…`. The firmware code size is unchanged from the previous image; the HEX differs
  because the build is not byte-reproducible across runs.
- **Next is the device oracle (5.2+), which needs owner upload approval.** The nearest registered
  stimulus is `STIM-OCCUPY-PAINT-01@v1` (4-bar seed, two overdubs, undo/redo). It reaches "one track,
  repeated overdubs over a grown take, transport running" qualitatively but not the capture's 610-note
  31-bar scale; a heavier temporary variant is the load-bearing oracle, and promoting it needs owner
  acceptance (5.1).
