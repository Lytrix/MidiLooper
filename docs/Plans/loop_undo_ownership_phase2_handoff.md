# Handoff — Loop undo ownership Phase 2 (DEC-024)

**Date:** 2026-07-14  
**Branch:** `dev` @ `ec7b4a5`  
**Sequencing:** Start **after** `feature/persistence-work-queue` (B1–B5) merges to `dev`  
**Decision:** [DEC-024](../DECISION_LOG.md#dec-024--loop-owned-undo-ownership-direction)  
**Phase 1:** Shipped `b1d3259` — `TrackUndo::*ForLoop` filter layer on track-wide stack  
**Phase 2:** Planning complete; **implementation not started**

---

## Status summary

| Item | Status |
|------|--------|
| Task 0 — `UndoEntry` ownership audit | **Done** — [loop_undo_ownership_phase2_audit_refinement.md](loop_undo_ownership_phase2_audit_refinement.md) |
| Prerequisite — Loop-owned record baseline geometry | **Not started** — **first firmware work** |
| OpenSpec change `loop-owned-undo-phase2` | **Not created** — recommend `/opsx:propose` + `ARCHITECTURE-REVIEW.md` before Stage 1 |
| Persistence wire format decision | **Pending** design session |
| Stage 1 — per-loop stacks (Track stack dormant) | **Not started** |
| Stage 2 — remove Track stack + Phase 1 helpers | **Not started** |
| Native / HITL | **Not started** |

**Not in [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md)** — user-directed Phase 2 scope; confirm with user before displacing active bugfix work.

---

## One-line outcome

Move **`GlobalUndoStack`** from **`Track`** to each **`Loop`** so undo history belongs to the musical object; **`TrackUndo`** keeps execute/apply; **`TrackManager`** keeps selection resolution; **1:1 Slot → Loop unchanged**.

---

## Locked decisions (do not re-litigate without design session)

### Ownership model

**Before (Phase 1 today):**

```
Track
 ├── Passes          (via Loop pool)
 ├── Undo            (GlobalUndoStack on Track; filtered per slot)
 └── Loops
```

**After (Phase 2):**

```
Track
 └── Coordinates Loops

Loop
 ├── Passes
 ├── Capture
 ├── Undo
 ├── Geometry
 └── Playback state
```

### Invariants

| Invariant | Rule |
|-----------|------|
| **Undo history** | Belongs to **Loop** |
| **Slot assignment** | Must **never** modify or invalidate Loop undo history |
| **Undo execution** | Stays on **`TrackUndo`** — do not move apply into `Loop` |
| **Selection** | **`TrackManager`** resolves selected `Loop&` — unchanged |
| **`loopId`** | Persistent musical identity — keep on wire |
| **`slotIndex`** | Compatibility field during migration — keep on wire |

### Two-stage migration

1. **Stage 1:** Add per-loop stacks; migrate call sites; leave `Track::undoStack` **dormant**; verify native + HITL.
2. **Stage 2:** Remove Track stack, Phase 1 slot-filter helpers, migration-only code.

### Persistence-first

Finalize undo **wire format** (per-loop in payload/footer + legacy split-on-load) **before** or in parallel with Stage 1 call-site migration — runtime should follow persistence model, not adapt silently to legacy track-wide footer.

---

## Non-goals (Phase 2)

- Loop library, slot reassignment, multi-slot → one Loop
- Loop archival, workspace loop catalog
- UI selection on Track/Loop
- SlotAssignment runtime routing (keep both `slotIndex` + `loopId` for future)
- Note edit **session** undo (`NoteEditSessionUndoStack`)

See [slot_loop_identity_decoupling_refinement.md](slot_loop_identity_decoupling_refinement.md) for long-term Slot/Loop decoupling (future).

---

## Implementation order (strict)

| Step | Work | Gate |
|------|------|------|
| **0** | Task 0 audit | **Done** |
| **1** | **Prerequisite:** Loop-owned record capture baseline | `pio test -e native`; small standalone commit |
| **2** | Design session: persistence format + R2–R5 sign-off; optional OpenSpec | Architecture gate posted in chat |
| **3** | Stage 1: loop stacks + persistence + `TrackUndo` + `PassReclaim` + tests | `pio test -e native` |
| **4** | Stage 1 HITL | User — multi-slot undo, clear restore, boot |
| **5** | Stage 2 cleanup + docs/spec | `pio test -e native`; DEC-024 completion note |

---

## Step 1 — Prerequisite (start here)

**Only push-time Track dependency** from Task 0 audit.

| Today | Target |
|-------|--------|
| `Track::startRecording` sets `Track::recordCaptureBaselineGeometry_` | Set baseline on recording **`Loop`** at record start |
| `TrackUndo::pushRecordPassAdded` reads Track fields | Read **`loop`** only |
| Track baseline members cleared after push | Remove from `Track` |

**Why record-start, not commit-only:** `startRecording` captures geometry **before** loop mutations (empty-slot length reset, `loopStartTick = 0`).

**Suggested Loop fields:**

```cpp
UndoLoopGeometry recordCaptureBaselineGeometry_{};
bool hasRecordCaptureBaselineGeometry_ = false;
```

**Primary files:**

| File | Change |
|------|--------|
| [`include/Track.h`](../../include/Track.h) | Remove baseline members |
| [`include/Loop.h`](../../include/Loop.h) | Add baseline members |
| [`src/Track.cpp`](../../src/Track.cpp) | `startRecording` → loop baseline |
| [`src/TrackUndo.cpp`](../../src/TrackUndo.cpp) | `pushRecordPassAdded` → read loop |

**Test:** Extend [`test/test_global_undo_slot_scope`](../../test/test_global_undo_slot_scope/test_global_undo_slot_scope.cpp) or add record-undo geometry fixture.

**Architecture checkpoint (bugfix):** ownership **yes** (Track → Loop baseline); transitions **no** — proceed as minimal prerequisite patch.

---

## Step 2 — Design session + persistence gate

Decide before Stage 1 firmware:

| Topic | Options |
|-------|---------|
| Undo on wire | Per-loop in loop slot payload vs 8 stacks/track in footer vs hybrid |
| Legacy load | Split track-wide stack by `slotIndex` into per-loop stacks |
| OpenSpec | `/opsx:propose loop-owned-undo-phase2` with `ARCHITECTURE-REVIEW.md` |

**R2–R5** (from audit — sign-off required):

- **R2:** `collectReferencedPasses` scans **every loop stack** per track
- **R3:** Lazy redo fields (`afterSnapshot`, etc.) — persistence serializes post-undo state
- **R4:** ClearSlot pins `afterSnapshot` only after first undo
- **R5:** Keep `slotIndex` + `loopId`; no new `slotIndex == loopId` assumptions

---

## Step 3 — Stage 1 firmware (after prerequisite + persistence decision)

### Architecture gate (post in chat before first edit)

Copy from [OpenSpec-Phase-Gate.mdc](../../.cursor/rules/OpenSpec-Phase-Gate.mdc) + audit checklist:

- Owner: `Loop` history, `TrackUndo` execution
- Ownership change: **YES** (approved Phase 2)
- State transition change: **NO**
- Record baseline Loop-owned: **YES**
- Persistence format: **decided**
- Phase scope: Stage 1 only

### Data model

- Add `GlobalUndoStack` + `getUndoStack()` to [`Loop`](../../include/Loop.h)
- Keep dormant `Track::undoStack` until Stage 2

### `TrackUndo` ([`src/TrackUndo.cpp`](../../src/TrackUndo.cpp))

- `pushUndoEntry` → `loop.getUndoStack()`
- Drop stack-tip `slotIndex` gating in `undoForLoop` / `redoForLoop`
- `undoDepthForLoop` → loop-local cursor + `isPassUndoEntryKind`
- Re-audit all `getGlobalUndoStack()` call sites (grep)

### `PassReclaim` ([`src/TrackManager.cpp`](../../src/TrackManager.cpp))

Today:

```cpp
collectReferencedPasses(track.getGlobalUndoStack(), refs);
```

Target: per-loop `collectReferencedPasses(loop.getUndoStack(), slotRefs)`.

### Persistence

| File | Role |
|------|------|
| [`RuntimeBundleFooter.cpp`](../../src/StorageManager/RuntimeBundleFooter.cpp) | Read/write per-loop stacks |
| [`WorkspaceSave.cpp`](../../src/StorageManager/WorkspaceSave.cpp) | `DeferredSaveStage::UndoStacks` FSM |
| [`StorageManager.cpp`](../../src/StorageManager.cpp) | Boot hydrate + `processDeferredUndoSnapshots` |

---

## Step 4 — Stage 2 cleanup

- Remove `Track::undoStack`, `getGlobalUndoStack()`
- Remove `countApplied*ForSlot` runtime use, stack-tip slot gating
- Update [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md), OpenSpec deltas, DEC-024 completion in [`DECISION_LOG.md`](../DECISION_LOG.md)

---

## Tests

| Suite | When |
|-------|------|
| `pio test -e native` | After every step |
| `test_global_undo_slot_scope` | Prerequisite + Stage 1 — per-loop stacks, ClearSlot, pass depth |
| `test_runtime_bundle_boot_load` | Stage 1 — footer + legacy split |
| `test_pool_budget` | Stage 1 — per-loop trim/reclaim |
| `test_redo_functionality` | Stage 1 — **teensy41** env only |
| HITL baseline | Stage 1 — user; see [HITL-Test-Flow.mdc](../../.cursor/rules/HITL-Test-Flow.mdc) |

---

## HITL checklist (Stage 1)

- Record/overdub slot A → switch slot B → **U:** shows B only; undo on B does not affect A
- Clear + double-undo restore ([slot_clear_undo_restore_bugfix.md](slot_clear_undo_restore_bugfix.md))
- Overdub undo/redo (baseline preset; recent fix `ca40a1e` on redo depth)
- Cold boot with undo on multiple slots same track
- Deferred save footer completes (if persistence active on branch)

---

## Task 0 audit — quick reference

| `UndoEntry` area | Owner after push | Risk |
|------------------|------------------|------|
| ClearSlot snapshots | Entry-owned deep clone (`sharePassesSnapshot`) | Low |
| `passId` / `editPassIds` | Live Loop pass rows — reclaim must pin | Medium — R2 |
| Record `beforeGeometry` | Value copy — **was** Track push dependency | **Prerequisite** |
| Playback caches | Derived on apply — not in entry | Low |

Full table + lifetime diagram: [loop_undo_ownership_phase2_audit_refinement.md](loop_undo_ownership_phase2_audit_refinement.md).

---

## Related artifacts

| Doc | Role |
|-----|------|
| [loop_undo_ownership_refinement.md](loop_undo_ownership_refinement.md) | DEC-024 direction Phase 1/2 |
| [loop_undo_ownership_phase2_audit_refinement.md](loop_undo_ownership_phase2_audit_refinement.md) | Task 0 audit + prerequisite detail |
| [slot_loop_identity_decoupling_refinement.md](slot_loop_identity_decoupling_refinement.md) | Future Slot/Loop (out of scope) |
| [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) | Undo routing constraints |
| `.cursor/plans/phase_2_loop_undo_stack_4293f71b.plan.md` | Cursor plan + todos |

---

## Suggested first message for next chat

```
Implement DEC-024 Phase 2 per docs/Plans/loop_undo_ownership_phase2_handoff.md.

Start with Step 1 only: Loop-owned record capture baseline geometry
(prerequisite). Post architecture gate, run pio test -e native, then stop
for review before Stage 1.
```

Or, if user wants full Stage 1 in one session:

```
/opsx:propose loop-owned-undo-phase2 then apply handoff Steps 1–4 per
docs/Plans/loop_undo_ownership_phase2_handoff.md
```

---

## Pre-implementation review (for implementer)

### Ready

- Task 0 ownership table complete
- Before/after ownership model locked
- Non-goals, invariants, selection unchanged locked
- Two-stage migration strategy agreed
- Prerequisite scoped and file list identified

### Open (pin at session start if skipping design session)

1. **Persistence wire layout** — footer 8 stacks vs per-slot payload (user decision)
2. **OpenSpec** — create change folder or implement from plan docs only
3. **CURRENT_WORK** — confirm Phase 2 start vs other branch priorities

### Proceed?

**YES** for Step 1 (prerequisite) without persistence decision.  
**Stage 1** requires persistence format + architecture gate in chat.
