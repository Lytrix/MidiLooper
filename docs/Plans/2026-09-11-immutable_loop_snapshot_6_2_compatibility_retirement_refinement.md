# OpenSpec 6.2 — Compatibility retirement (owner-by-owner)

**Status:** **APPROVED — planning checkpoint** (2026-09-11)  
**OpenSpec:** `immutable-loop-snapshot-passes` task **6.2**  
**Depends on:** [6.1 plan](immutable_loop_snapshot_6_1_compatibility_consumer_inventory_refinement.md) (**APPROVED**), [5.6 plan](immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md) (SHIPPED)  
**Regression baseline:** HITL `base` PASS [`203803`](../../captures/session_20260911_203803.log)

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
| Overlay at B1 | Keep accepting Overlay in 6.2.1 unless capture proves it causes incorrect playback vs compat |
| C1 on empty slot | TBD from fixture inventory — likely hold empty merged window, not compat flatten |
| C3 detection | Parity hook: compare event count vs `publishedSnapshot` membership for same window in native only |

**Proceed?** YES for investigation + policy pin; firmware edit blocked until C1 policy row is filled from fixtures.

## Verification matrix (6.2 parent)

| Requirement | Gate | Status |
|---|---|---|
| 6.1 inventory frozen | Host gate + plan | **PASS** |
| Snapshot taxonomy documented | This plan § taxonomy | **PASS** |
| Owner-by-owner commits | Git + shrinking `_6_2_BLOCKER_SITES` | **open** |
| HITL `base` after each slice | [`203803`](../../captures/session_20260911_203803.log) baseline | **required per slice** |
| `pio test -e native` | Full suite per slice | **required per slice** |

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
