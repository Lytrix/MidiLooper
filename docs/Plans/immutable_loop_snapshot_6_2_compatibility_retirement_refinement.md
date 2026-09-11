# OpenSpec 6.2 — Compatibility retirement (owner-by-owner)

**Status:** **6.2.3 SHIPPED** (2026-09-12) — B2/B5/B6 narrow C1-a / stale-root; **6.2.4 next**  
**OpenSpec:** `immutable-loop-snapshot-passes` task **6.2**  
**Depends on:** [6.1 plan](immutable_loop_snapshot_6_1_compatibility_consumer_inventory_refinement.md) (**APPROVED**), [5.6 plan](immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md) (SHIPPED)  
**Regression baseline:** HITL `base` PASS [`231439`](../../captures/session_20260911_231439.log) (C1-a slimmed re-ship). Prior persist-bounds baseline [`203803`](../../captures/session_20260911_203803.log).

## Planning checkpoint (approved direction)

**6.2 removes B1–B8 compatibility reachability owner-by-owner, while preserving the compatibility machinery itself until its final consumers are gone. Any unexpected snapshot miss becomes evidence to investigate, not permission to revive LCR/pass replay.**

6.2 is **not** a broad “delete old code” exercise. It clears **normal-consumer fallback** to `gatherCommittedEvents*`, `materializeEditViewFromPasses`, and `loop.midiEvents()` at named sites. Construction machinery (`materializeToEventVector` in capture/LCR) stays until 6.3+ when no normal consumer depends on its output.

## North star (task 6.2)

Completeness authority lives on **`LoopSnapshot`**. Each blocker owner migrates to snapshot-first reads with an explicit policy for **absence** vs **stale/incomplete**. Compatibility pass replay remains available only where 6.2 explicitly retains it (construction, parity oracle, rollback gates) — not as a silent consumer fallback.

## Architecture gate (task 6.2 — parent)

| Question | Answer |
|---|---|
| **Owner module** | Per-blocker existing owner (`TrackPlaybackWindowBuild`, `LoopVisualCache`, …) |
| **Primary invariant** | Normal consumers do not reach compat gather/materialize when a published root should satisfy the request |
| **Ownership change?** | NO — extend existing owners only |
| **State transition change?** | NO — no new capture/edit transitions |
| **Execution bound change?** | NO — no new interval-path scans; removal only |
| **Behavior-preserving?** | YES for playback/display/edit output contracts at HITL baseline |
| **Reuse** | Extend `tryGatherPublishedSnapshotEventsInWindow` + existing freshness keys |
| **Phase scope** | One blocker slice per commit/stage |

### Architecture checkpoint (parent)

1. **Ownership?** NO  
2. **State transitions?** NO  
3. **Unbounded interval owner?** NO

## Snapshot gather taxonomy (mechanical — do not conflate)

All 6.2 slices must name which condition applies before removing a fallback.

### G0 — `tryGatherPublishedSnapshotEventsInWindow` contract (today)

Source: `Loop::tryGatherPublishedSnapshotEventsInWindow` in [`LoopSnapshotPlayback.cpp`](../../src/Loop/LoopSnapshotPlayback.cpp); enum in [`Loop.h`](../../include/Loop.h).

| Return | `SnapshotWindowGatherPath` | Meaning |
|---|---|---|
| **false** | `None` | **No published root** (`!publishedSnapshot()`) |
| **true** | `Snapshot` | Published root exists and `publishedSnapshotIsCurrent()` — membership from current root only |
| **true** | `Overlay` | Published root exists but **not current** — stale-root membership plus unpublished pass/companion overlay |

Window membership is always clamped to `[0, loopLengthTicks)` inside `gatherPublishedSnapshotMembership`. An empty `out` with **true** can still be valid (empty bar range or legitimately empty window).

### Conditions for 6.2 policy (consumer-facing)

| ID | Condition | Legitimate compat fallback in 6.2? | 6.2 action |
|---|---|---|---|
| **C1** | **No published root** — gather returns **false** / `None` | **Maybe — slice-specific.** Empty loop, pre-first-publish, or legacy-only hydration may still require a documented hold or compat path until 6.3. | Each owner documents allowed C1 behavior; shrink deliberately. |
| **C2** | **Root exists; request outside normal consumer window** — e.g. visual idle slice uses padded bar window; playback long-loop uses 2-bar playhead window. Empty gather inside clamped window is not automatically C2. | **No** for playback B1 when root is current and window is the established contract window. **Case-by-case** for display idle slices. | Prove window contract; use snapshot notes/event membership for that window; do not flatten whole loop. |
| **C3** | **Root exists and should be complete; gather succeeds (`Snapshot`) but output is unexpectedly empty/incomplete vs invariant** | **Never** — not permission for `gatherCommittedEvents*` | **Diagnostic / assert / invariant failure** (capture-serial evidence). Fix snapshot construction or consumer contract — do not fall back. |
| **C4** | **Stale root (`Overlay`)** — gather returns **true** / `Overlay` | **Not equivalent to C1.** Overlay is unpublished-session content on a stale root. | **Per-owner policy:** B1 currently **accepts Overlay** as snapshot path. B3/B4 **reject Overlay** and fall back today — 6.2 must replace that fallback with hold, prepared path, or explicit stale policy — not silent whole-loop compat gather. |

**Rule:** Only **C1** (and explicitly documented hold paths) may retain compatibility reachability during early 6.2 slices. **C3** is always investigate. **C4** must be spelled out per owner — do not treat Overlay rejection as C1.

## Blocker slices (execution order)

| Stage | Blockers | Owner | Remove | Retain / defer |
|---|---|---|---|---|
| **6.2.1** | **B1** | `TrackPlaybackWindowBuild` | `gatherCommittedEventsInWindow` / `gatherCommittedEventsForDerivedView` on gather **false** only after C1 policy is explicit | Overlay path stays until overdub/publication stale policy is unified |
| **6.2.2** | **B3**, **B4** | `LoopVisualCache`, `DisplayManager` gather | Compat fallback after snapshot miss / overlay reject | Distinguish C1 vs C4 vs C2; `holdUntilCurrentSnapshot` pattern from display paint |
| **6.2.3** | **B2**, **B5**, **B6** | `CommittedMidiScratch`, `TrackDeferredMaintenance`, `NoteEditSessionLifecycle` | `materializeEditViewFromPasses` + `loop.midiEvents()` consumer reachability | Do not change NOTE_EDIT source semantics; parity oracle only |
| **6.2.4** | **B7**, **B8** | `LoopContentResolution`, `LoopCapture` | **Normal-consumer dependency** on construction materialize output | **`materializeToEventVector()` API remains** construction machinery until 6.3 |

Host gate: after each shipped slice, shrink `_6_2_BLOCKER_SITES` in [`scripts/test_compatibility_consumer_inventory.py`](../../scripts/test_compatibility_consumer_inventory.py) and re-run **7 tests**.

## Stage 6.2.1 — B1 `TrackPlaybackWindowBuild` (next implementation)

### Scope

File: [`TrackPlaybackWindowBuild.cpp`](../../src/Track/TrackPlaybackWindowBuild.cpp) — `ensurePlaybackWindowBuilt` merged-events rebuild.

Today:

- `tryGatherPublishedSnapshotEventsInWindow` → on **false**, `gatherCommittedEventsInWindow` (long loop) or `gatherCommittedEventsForDerivedView` (short loop).
- On **true** (`Snapshot` or `Overlay`), no compat call.

### B1 invariants to preserve

- Playback merged-events output contract unchanged at HITL `base` baseline [`203803`](../../captures/session_20260911_203803.log).
- `RuntimeTimingTelemetry::PlaybackRebuildPath::CompatibilityPasses` must not appear for paths where a published root exists and gather succeeds.
- Long-loop 2-bar window and short-loop full-loop window contracts unchanged.

### B1 migration steps

1. **Inventory C1 occurrences** — native fixtures + capture grep: when does playback rebuild see `gather false` during normal record/overdub/play HITL?
2. **Policy pin** — document allowed C1 (if any) vs hold-empty vs hard diagnostic.
3. **Remove fallback** — delete compat branches only where policy says snapshot root must exist; replace C3 with assert/`DIAG` (SESSION_CAPTURE).
4. **Tests** — extend native playback-window fixtures; host gate B1 site removed from inventory when done.
5. **Verify** — `pio test -e native`; HITL `base` PASS.

### B1 pre-implementation review (before first edit)

| Topic | Decision |
|---|---|
| Overlay at B1 | Keep accepting Overlay in 6.2.1 unless capture proves incorrect playback vs compat |
| C1-a session-exit gap | **Confirmed** on [`190525`](../../captures/session_20260911_190525.log): first PLAYING after record stop runs **~759 ms** with `snapshot_idle root=0x0`; **131/163 MO** ticks in that window correlate to C1. B1 compat fallback is **required today** unless stop path publishes before `startPlaying` or playback **holds empty** until root exists. |
| C1-b legacy pass-only | Out of HITL `base` happy path when hydration publishes; keep for SD/boot until 6.2.2+ |
| C1-c empty loop | Hold empty merged window; no compat flatten |
| C3 detection | Parity hook: compare event count vs `publishedSnapshot` membership for same window in native only |
| C4 at B1 | Overlay returns **true** — not C1; B1 does **not** compat-fallback on Overlay today |

**Proceed?** YES — **Option 3 approved** (narrow C1-a). **6.2.1b SHIPPED.**

## 6.2.1b — B1 narrow C1-a compatibility (SHIPPED)

**Decision:** Option 3 — retain `gatherCommittedEvents*` **only** during the measured session-exit publication gap; all other gather-miss paths hold empty with `#CAP` evidence.

### Architecture gate (6.2.1b)

| Question | Answer |
|---|---|
| **Owner** | `Loop::isPlaybackSessionExitPublicationGap` + `TrackPlaybackWindowBuild` |
| **Invariant** | Generic `if (!snapshot) gather…` retired; compat only when session-exit publish is queued and no root exists |
| **Ownership change?** | NO |
| **Transition change?** | NO |
| **Execution bound change?** | NO |
| **Behavior-preserving?** | YES for HITL-class record→play (C1-a bridge retained) |

### Mechanical predicate

`Loop::isPlaybackSessionExitPublicationGap()`:

```text
!publishedSnapshot() && pendingSnapshotSessionExitQueued() && hasPendingSnapshotPublish()
```

### B1 policy table (implemented)

| Condition | B1 behavior |
|---|---|
| Published root exists | Snapshot / Overlay path |
| No root, **not** C1-a | Hold empty + `#CAP,DIAG,playback_build,hold_empty,...` |
| No root, **C1-a session-exit gap** | `gatherCommittedEvents*` + `#CAP,DIAG,playback_build,session_exit_compat,...` |
| Root exists, gather `Snapshot`, wrong content | No compat (C3 — native parity elsewhere) |
| `Overlay` | Snapshot path (`StaleRootOverlay` telemetry) |

Telemetry: `PlaybackRebuildPath::SessionExitPublicationGap` → `session_exit_compat`; `GatherMissHoldEmpty` → `hold_empty`. Generic `CompatibilityPasses` **removed from B1**.

### Verification (6.2.1b)

| Gate | Status |
|---|---|
| Native `test_playback_session_exit_publication_gap_predicate` | **PASS** |
| Host `scripts/test_b1_narrow_session_exit_compat_gate.py` (5 tests) | **PASS** |
| Host `scripts/test_compatibility_consumer_inventory.py` (B1 → C1-a exception) | **PASS** |
| Full `pio test -e native` | **1824/1829 PASS** |
| RAM1 `teensy41-capture-serial` (clean) | **6880** locals (PASS ≥ 4000). Incremental 2930bce7 report of **2784** was a section-shift artifact, not a new 4 KB C1-a symbol |
| HITL `base` record-start | **PASS** [`231439`](../../captures/session_20260911_231439.log) — `STOPPED→ARMED→RECORDING` with **no** `#CAPTURE_RECONNECT` |
| HITL C1-a post-record PLAYING | **PASS** — `#CAP,42270719,DIAG,playback_build,session_exit_compat,615,0,2304,1,4,0,6` immediately after `STOPPED_RECORDING→PLAYING` |

**Architectural label:** B1 **normal-consumer generic fallback retired**; **C1-a session-exit bridge retained** as explicit bounded exception.

### Record-start RC (2026-09-11)

[`224714`](../../captures/session_20260911_224714.log) reset after `STOPPED→ARMED` (USB drop → `#CAPTURE_RECONNECT`). No `playback_b1` / `session_exit_compat` lines — B1 gather-miss did not run on the empty armed slot (`ensurePlaybackMergedEventsForSlot` requires committed passes + length).

Control: uncommitted revert of 2930bce7 ran HITL [`225352`](../../captures/session_20260911_225352.log) through RECORDING (no reconnect). Clean rebuild of slimmed C1-a recovered RAM1 locals **6880**. Removed synchronous `playbackB1GatherMissPolicy`; policy stays on existing deferred `#CAP,DIAG,playback_build,<path>`. Re-ship HITL [`231439`](../../captures/session_20260911_231439.log) **PASS**.

## 6.2.2 — B3/B4 visual/display (SHIPPED)

**Decision:** Mirror B1 Option 3 — retain `gatherCommittedEvents*` only during
`isPlaybackSessionExitPublicationGap()`; all other gather-miss / Overlay-reject
paths use LCR `resolveDisplayNotes(passes)` (`PassesResolve`) or existing hold
gates (`holdUntilCurrentSnapshot` on B4 paint).

### Architecture gate (6.2.2)

| Question | Answer |
|---|---|
| **Owner** | `LoopVisualCache`, `DisplayNoteResolveCommitted`, `DisplayColdHelpers` |
| **Invariant** | Generic B3/B4 compat retired; C1-a session-exit bridge only |
| **Ownership change?** | NO |
| **Transition change?** | NO |
| **Execution bound change?** | NO |
| **Behavior-preserving?** | YES — native overdub/visual-cache fixtures |

### B3/B4 policy table (implemented)

| Condition | B3 idle | B3 full rebuild | B4 paint | B4 window gather |
|---|---|---|---|---|
| Current / Snapshot gather | Snapshot / SnapshotNotes | `resolveDisplayNotes(*snapshot)` | incremental snapshot path | `tryCopyCurrentSnapshot…` |
| Overlay (C4) | Reject → Prepared or `PassesResolve` | Reject → `PassesResolve` | Reject → hold or `PassesResolve` | Reject → `PassesResolve` |
| No root, C1-a | `gatherCommittedEventsInWindow` (budgeted) | `gatherCommittedEvents` | `gatherCommittedEvents*` | `gatherCommittedEventsInWindow*` |
| No root, not C1-a | `PassesResolve` | `PassesResolve` | `PassesResolve` / hold | `PassesResolve` |

`VisualCacheIdleGatherSource::PassesResolve` added for telemetry. Idle slice does
not stamp `builtFromRevision` on passes-resolve; full rebuild does when not
overlay-rejected.

### Verification (6.2.2)

| Gate | Status |
|---|---|
| Host `scripts/test_b3_b4_narrow_session_exit_compat_gate.py` (6 tests) | **PASS** |
| Host `scripts/test_compatibility_consumer_inventory.py` (10 tests; B3/B4 → C1-a) | **PASS** |
| Native `test_overdub_source_view` + `test_loop_take_survival` | **PASS** |
| Full `pio test -e native` | **1823/1829** (linker flake on 4 unrelated suites in one run; re-run clean) |
| RAM1 `teensy41-capture-serial` | **6880** locals |
| HITL `base` core baseline | **PASS** [`000210`](../../captures/session_20260912_000210.log) — transitions, notes, undo/redo, DISP |
| HITL persist verifier | **PASS** on re-run [`001343`](../../captures/session_20260912_001343.log); flake [`000210`](../../captures/session_20260912_000210.log) → OpenSpec **5.7** |

## 6.2.3 — B2/B5/B6 edit/idle (SHIPPED)

**Decision:** Mirror B1/B3 Option 3 — retain `materializeEditViewFromPasses` only during
`isPlaybackSessionExitPublicationGap()` (C1-a) or stale-root overlay (C4:
`publishedSnapshot() && !publishedSnapshotIsCurrent()`). All other B2 misses return
`RefreshKind::HoldEmpty` (clear scratch, stamp revision). B5 idle maintenance materializes
only on C1-a. B6 `openNoteEditSession` rematerialize uses `copyEffectiveCommittedEvents`
instead of `loop.midiEvents()`.

### Architecture gate (6.2.3)

| Question | Answer |
|---|---|
| **Owner** | `CommittedMidiScratch`, `TrackDeferredMaintenance`, `NoteEditSessionLifecycle` |
| **Invariant** | Generic B2/B5 pass materialize and B6 `midiEvents()` consumer retired; C1-a / C4 only |
| **Ownership change?** | NO |
| **Transition change?** | NO |
| **Execution bound change?** | NO |
| **Behavior-preserving?** | YES — NOTE_EDIT source semantics unchanged; parity oracle only |

### B2/B5/B6 policy table (implemented)

| Condition | B2 `CommittedMidiScratch::refresh` | B5 idle maintenance | B6 note-edit open rematerialize |
|---|---|---|---|
| Current snapshot copy | `FromSnapshot` | N/A (freshness guards) | `copyEffectiveCommittedEvents` |
| C1-a session-exit gap | `FromPasses` | `materializeEditViewFromPasses` | via `copyEffectiveCommittedEvents` path |
| C4 stale root | `FromPasses` | hold (no materialize) | via `copyEffectiveCommittedEvents` path |
| No root, not C1-a / C4 | `HoldEmpty` | hold | `copyEffectiveCommittedEvents` (may hold empty) |

### Verification (6.2.3)

| Gate | Status |
|---|---|
| Host `scripts/test_b2_b5_b6_narrow_session_exit_compat_gate.py` (4 tests) | **PASS** |
| Host `scripts/test_compatibility_consumer_inventory.py` (13 tests; B2/B5 → C1-a, B6 no `midiEvents`) | **PASS** |
| Native `test_committed_scratch_hold_empty_without_snapshot_or_c1_a` + full suite | **1825/1830** |
| RAM1 `teensy41-capture-serial` | **6880** locals |
| HITL `base` core baseline | **PASS** [`002259`](../../captures/session_20260912_002259.log) — transitions, notes, undo/redo, DISP |
| HITL persist verifier | **open** — `already_pending` wedge (OpenSpec **5.7**); same class as [`000210`](../../captures/session_20260912_000210.log) |

## Stage table

| Stage | Scope | Status |
|---|---|---|
| **6.2.1** | C1 inventory | **SHIPPED** |
| **6.2.1b** | B1 narrow C1-a firmware + gates | **SHIPPED** |
| **6.2.2** | B3/B4 visual/display | **SHIPPED** |
| **6.2.3** | B2/B5/B6 edit/idle | **SHIPPED** |
| **6.2.4** | B7/B8 construction | **next** |

## C1 inventory (6.2.1 — SHIPPED read-only)

Tool: [`scripts/analyze_b1_c1_gather_miss_inventory.py`](../../scripts/analyze_b1_c1_gather_miss_inventory.py)

```bash
.venv/bin/python scripts/analyze_b1_c1_gather_miss_inventory.py captures/session_20260911_190525.log
.venv/bin/python scripts/analyze_b1_c1_gather_miss_inventory.py --test
```

### Mechanical C1 subclasses (B1)

| Subclass | Condition | HITL `base`-class evidence | B1 compat today? | 6.2.1 removal strategy |
|---|---|---|---|---|
| **C1-a** | `!publishedSnapshot()` after session-exit queue, **PLAYING/OVERDUBBING** | [`190525`](../../captures/session_20260911_190525.log): PLAYING#2 `C1_MOs=131/163`, gap **759 ms** | **Narrow exception only** — `isPlaybackSessionExitPublicationGap()` | **SHIPPED 6.2.1b** — `#CAP playback_build,session_exit_compat` [`231439`](../../captures/session_20260911_231439.log) |
| **C1-b** | Pass-only / no hydration root | Native `test_idle_slice_without_snapshot_uses_fallback` (display class) | Indirect via gather | Defer to 6.2.2 / load hydration |
| **C1-c** | Empty loop / `resetPassTimeline` | Native empty-loop gather false | N/A at B1 if no PLAYING | Hold empty |
| **C4** | Stale root — gather **true** / `Overlay` | [`190525`](../../captures/session_20260911_190525.log): PLAYING#2 `C4_MOs=158/1320`, roots non-zero | **No** at B1 — Overlay accepted | Preserve; do not conflate with C1 |
| **C3** | Root exists, gather **true** / `Snapshot`, output incomplete vs invariant | No capture evidence; native parity tests | Must **not** fallback | Assert / `#CAP` diagnostic |

### Native fixture anchors (tryGather false)

| Fixture | File | C1 meaning |
|---|---|---|
| Empty loop | `test_loop_take_survival` gather block | C1-c |
| `resetPassTimeline` | same | C1-c teardown |
| `test_live_record_starts_snapshot_before_session_exit` | same | C1-a construction |
| `test_overdub_idle_advances_in_flight_construction_only` | same | C1-a queued publish |
| `test_session_exit_handoff_when_live_record_notes_fit_sealed_length` | same | C1-a pre-drain |

**Gap:** no native test calls `ensurePlaybackMergedMidiEventsBuilt` — add a **6.2.1 fixture** from capture slice before deleting B1 fallback.

### Capture note on [`203803`](../../captures/session_20260911_203803.log)

203803 is a valid 5.6 persist-bounds sign-off capture but **not** a full record→play→overdub baseline (record stop → `STOPPED`, not `PLAYING`). Use **190525** for B1 C1-a timing evidence; keep **203803** as regression baseline after B1 ships.


## Verification matrix (6.2 parent)

| Requirement | Gate | Status |
|---|---|---|
| 6.1 inventory frozen | Host gate + plan | **PASS** |
| Snapshot taxonomy documented | This plan § taxonomy | **PASS** |
| Owner-by-owner commits | Git + shrinking `_6_2_BLOCKER_SITES` | **open** |
| HITL `base` after each slice | [`231439`](../../captures/session_20260911_231439.log) (6.2.1b); [`001343`](../../captures/session_20260912_001343.log) (6.2.2) | **required per slice** |
| `pio test -e native` | Full suite per slice | **required per slice** |

## Follow-up (outside 6.2 scope)

- **OpenSpec 5.7** — boot-save `PersistenceWorkQueue` wedge after `PERS,work,start` (flake on
  [`000210`](../../captures/session_20260912_000210.log); not a 6.2.2 regression). Plan:
  [`immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md`](immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md).

## Explicit non-goals (6.2 parent)

- Bulk removal of `LoopPasses::materializeToEventVector`, LCR history, or `passesMaterializedStore_` (6.3)
- Changing snapshot publication semantics (Phase 3–5 owners)
- OpenSpec archive / spec sync (6.5)

## Pre-implementation review (6.2 parent)

### Ready

- B1–B8 frozen in 6.1 host gate
- Hot-path forbid gate PASS
- Taxonomy C1–C4 aligned with `Loop::SnapshotWindowGatherPath`

### Open before B1 firmware

1. C1 fixture inventory for playback rebuild (`gather false` during HITL-class flows)
2. Confirm Overlay acceptance at B1 for 6.2.1 (default: keep)

### Proceed?

**YES** — plan approved; start **6.2.1** with C1 inventory (read-only + native fixtures) before removing B1 fallback.
