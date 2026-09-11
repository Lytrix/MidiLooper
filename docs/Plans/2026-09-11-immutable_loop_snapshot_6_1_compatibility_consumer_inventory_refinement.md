# OpenSpec 6.1 — Compatibility consumer inventory

**Status:** **6.1 SHIPPED** (2026-09-11)  
**OpenSpec:** `immutable-loop-snapshot-passes` task **6.1**  
**Depends on:** [5.6 plan](immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md) (SHIPPED), [4.6 plan](immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md) (SHIPPED)  
**Kind:** verification / inventory (no compatibility removal — tasks **6.2–6.3**)

## North star (task 6.1)

Prove which **normal consumers** still reach maintained LCR history, `passesMaterializedStore_` / full pass replay, or whole-loop flatten — and which are **snapshot-first with documented compatibility fallback only**.

6.1 does **not** remove compatibility paths. It freezes the caller map that **6.2** must clear per owner.

## Architecture gate (Phase 5 / task 6.1)

| Question | Answer |
|---|---|
| **Owner module** | Inventory + host gate (`scripts/test_compatibility_consumer_inventory.py`); no firmware owner change |
| **Primary invariant** | Timing-critical paths do not call compatibility materialize/gather directly; every remaining fallback caller is named before 6.2 |
| **Ownership change?** | NO |
| **State transition change?** | NO |
| **Execution bound change?** | NO |
| **Behavior-preserving?** | YES |
| **Reuse** | Extend 4.6 consumer inventory + 5.x host gate pattern |
| **Phase scope** | Task **6.1** only |

### Architecture checkpoint

1. **Ownership?** NO  
2. **State transitions?** NO  
3. **Unbounded interval owner?** NO

## Consumer inventory (2026-09-11)

### Snapshot-first (normal path when published root exists)

| Consumer | Owner | Freshness | Compat fallback when no root |
|---|---|---|---|
| `PlaybackMergedMidiEvents` | `Track` / `LoopPlaybackRuntime` | revision + snapshot identity | `gatherCommittedEvents*` |
| `VisualCache` / idle slice | `Loop` | revision + snapshot identity | `gatherCommittedEvents` in `rebuildVisualCacheFromPasses` |
| `DrawSourceGatherKey` | `DisplayManager` | revision + snapshot identity | gather paths in display resolve |
| Overdub span cache | `Loop` | revision + snapshot identity | pass materialize in capture (construction) |
| NOTE_EDIT projection / focus materialize | `EditManager` | snapshot identity | `CommittedMidiScratch::FromPasses`, `loop.midiEvents()` at session open |
| Display committed gather | `DisplayManager` | snapshot window gather | `gatherCommittedEvents*` |

### Compatibility machinery (not normal consumers — stay until 6.2+)

| Source | Owner | Role |
|---|---|---|
| `passesMaterializedStore_` / `ensureEffectiveEventStoreCurrent` | `Loop` / `LoopMaterialization` | Effective-event flat cache |
| `LoopPasses::materializeToEventVector` | `LoopPasses` / `LoopMaterialization` | Full pass replay flatten |
| LCR `preparedPlaybackRevision` / tick history | `LoopContentResolution` | Construction / device-gate machinery |
| Capture / edit-pass materialize | `LoopCapture`, `LoopEditPasses` | Source-view and oracle construction |

### 6.2 removal blockers (fallback still reachable)

| Blocker | Call site owner | Compatibility API |
|---|---|---|
| B1 | `TrackPlaybackWindowBuild` | `gatherCommittedEvents*` when snapshot gather misses |
| B2 | `CommittedMidiScratch` | `materializeEditViewFromPasses` + `midiEvents()` |
| B3 | `LoopVisualCache` | `gatherCommittedEvents` in full rebuild fallback |
| B4 | `DisplayManager` gather resolve (`DisplayNoteResolveCommitted` anchor; siblings `DisplayColdHelpers`, `DisplayNoteWindowGather`) | `gatherCommittedEvents*` |
| B5 | `TrackDeferredMaintenance` | idle `materializeEditViewFromPasses` seed + oracle gather |
| B6 | `NoteEditSessionLifecycle` | `rebuildVisualCacheFromPasses`, `loop.midiEvents()` |
| B7 | `LoopContentResolution` | `materializeToEventVector` (LCR construction) |
| B8 | `LoopCapture` / overdub source | `materializeToEventVector` (construction) |

**Hot-path gate:** `TrackPlaybackHotPath`, `MidiHandler`, `ClockManager`, `TrackManagerTransportTick` — **zero** direct compatibility execute calls (**PASS**).

## Verification matrix

| Requirement | Gate | Status |
|---|---|---|
| Hot paths forbid compat materialize/gather | Host gate | **PASS** |
| Fallback callers frozen in allowlist + B1–B8 sites | Host gate `scripts/test_compatibility_consumer_inventory.py` | **PASS** (8 blockers) |
| `materializeToEventVector` only in machinery owners | Host gate | **PASS** |
| `midiEvents()` only in materialization owner + fallback sites | Host gate | **PASS** (`include/Track.h` wrapper + B2/B6) |
| HITL `base` still PASS after 5.x | [`203803`](../../captures/session_20260911_203803.log) | **PASS** (5.6 sign-off) |

## Stages

| Stage | Scope | Status |
|---|---|---|
| **6.1.1** | Plan + inventory table | **SHIPPED** |
| **6.1.2** | Host gate `test_compatibility_consumer_inventory.py` | **SHIPPED** |
| **6.1.3** | Close `tasks.md` 6.1 + runtime docs | **SHIPPED** |
| **6.2** | Remove blockers one owner at a time | **next** |

## Explicit non-goals (6.1)

- Removing `passesMaterializedStore_`, LCR history, or pass replay (6.2–6.3)
- Changing snapshot-first consumer behavior
- Archive / OpenSpec closeout (6.5)

## Pre-implementation review

### Proceed?

**YES** — inventory and host gates only; no firmware edits.
