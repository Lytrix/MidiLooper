# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-07-09 (slot boot + split focus)

---

## Now implementing

### Slot boot + split focus (`slot-performance-interaction` 4b/4c) — **Landed (uncommitted)**

**DEC-021 amendment + DEC-025:** exhaustive boot restore queue; `slotHasLoopContent`; playing/preview/pending split; `LoopEnd` performance launch; LED phase on playing slot; flashing preview playhead.

| Gate | Status |
|------|--------|
| Native | `pio test -e native` — 537/537 (`test_slot_focus_policy` + loop-end commit fix) |
| HITL | Preview slot switch: piano roll notes + metadata immediate; loop-end commit for MIDI/LEDs |

OpenSpec: [`slot-performance-interaction`](../../openspec/changes/slot-performance-interaction/) tasks 4b, 4c checked off.

---

### Playhead-after-undo fix (record stop + ARMED guard)

**Landed (uncommitted):** Fixes A–D from `session_20260708_233241` replay:

| Fix | Behavior |
|-----|----------|
| **A** | `RecordPassAdded` undo restores `beforeGeometry`; empty slot → `resetLoopSlotAfterEmptyCapture` |
| **B** | Record stop length = `min(transport-quantized, content-based)` via `computeRecordStopLengthTicks` |
| **C** | Record start clears stale `loopLengthTicks` when slot has no published events |
| **D** | `TRACK_ARMED` blocked when selected slot `hasPublishedEvents()`; reconcile cancels stale arm |

**RAM1:** `TrackUndo` → `TRACK_COLD_MEM`; capture baseline on `Track` (not per-`Loop`). Native: `test_record_stop_length`, updated `test_track_display_state`. **HITL:** replay `session_20260708_233241` scenario after flash.

**Playhead bar-skip (SD load + transport):** `startPlaying` now anchors `projectionCycleStartTick` to `loop.loopStartTick` (display origin from SD), fixes signed projection math, and detects loop wrap on display phase (not raw storage phase). Re-anchor on external MIDI Start when already playing.

---

### Loop-scoped undo display (Phase 1)

**Landed:** `TrackUndo::*ForLoop` filters track-wide `GlobalUndoStack` by selected loop; sidebar `U:` uses `undoDepthForLoop`; `handleUndo` / `handleRedo` gate on stack tip matching selected loop. Native: `test_global_undo_slot_scope` (5 tests). Plan: [`docs/plans/loop_undo_ownership_refinement.md`](../plans/loop_undo_ownership_refinement.md).

---

### Slot clear + arm state fix (multi-slot transport)

**Landed:** `Track::hasAnySlotData()` + `reconcileTransportStateAfterSlotMutation()`; slot clear no longer forces `TRACK_EMPTY` when sibling slots have data; `cancelPendingRecordArm` / empty record stop / transport-stop armed branch use any-slot semantics; layer-hold commit blocked during capture; `restoreAudiblePlaybackAfterSlotClear` sets `STOPPED` before `startPlaying`. Native: `test_slot_clear_state` (7 tests). **HITL:** reproduce `session_20260708_203611` clear slot 3 → arm → switch slot → record without reboot.

---

### Boot load — USB Host defer + slot manifest scan hardening

**Landed:** defer `usbHost.begin()` until after `loadState`; suppress boot LED updates; close bundle before 8×8 manifest scan; `SD.exists`-only boot probe; light `resetLoopSlotForBootManifest`; scan off FLASHMEM; `beginBootOled` before load / `finishBootSetup` after.

**HITL gate (manual):** DROID attached, cold boot ×5 — serial must show `BOOT,scan,start` → `BOOT,scan,t0`…`t7` → `BOOT,scan,done` → `BOOT,load,ok` → `#CAP,HDR,v1` on first power cycle. Then 191659 slot-switch scenario.

---

### Boot/display fix — slot-aware track state (DEC-020 load path)

**Landed:** empty workspace load maps `TRACK_STOPPED` → `TRACK_EMPTY` when no restorable loop payloads; OLED track column uses selected-slot display state via `resolveDisplayTrackState` / `TrackManager::getTrackState`. Native: `test_track_display_state`.

**Verify on hardware:** boot empty workspace → all track rows `-`; select empty slot on track with data elsewhere → `-`.

---

### OpenSpec: [`unified-capture-commit-owner`](../../openspec/changes/unified-capture-commit-owner/) (DEC-023)

**Recovery on branch `dec-023-recovery`:** slice **1+2 FAILED** boot at workspace load (`session_20260708_173016`). Firmware at **`37f6b00`** = baseline Track/Loop + Storage quarantine/recovery helpers.

**Before retrying slice 1:** quarantine SD workspace (clearing a loop ≠ removing runtime bundle). Then confirm boot reaches `Boot recovery chain exhausted` or `loaded successfully`.

| Step | Commit | Contents | Status |
|------|--------|----------|--------|
| **0** | `40db4df` | `e40f26c` baseline | **Passed** (`session_20260708_172531`) |
| **1+2** | `83a954b`+`4f75fdc` | Track + Loop | **FAILED** boot load — reverted |
| **4s** | `37f6b00` | Storage quarantine/recovery only | **Landed** — test boot after quarantine |
| **1** | `83a954b` | Track deferred commit | Retry after empty boot OK |
| **2** | `4f75fdc` | Loop helpers | After slice 1 |
| **3** | `50dbd05` | DisplayManager | Pending |
| **4** | `75e5176` | TrackManager + tests | After slice 1 |

**Verify:** manual record+overdub only (HITL parked) · `pio test -e native` after each slice

---

### OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/) (DEC-020)

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** |
| **1** Chunk lifecycle | **Complete** |
| **2** Persistence queue | **Complete** |
| **3** Cooperative scheduler | **Complete** — overdub-stop HITL passed (`f0ee520`) |
| **4** Mid-pass persistence | **Shipped** (native) — seal journal writer + failure policy; HITL gate pending |
| **5** Recovery | **Next** |
| **6** Full 64+64 HITL | Pending |

**M5 boot load** (DEC-022) — cold boot passes.

| Doc | Role |
|-----|------|
| Agent map | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| OpenSpec | [continuous-runtime-persistence](../../openspec/changes/continuous-runtime-persistence/) |

**Verify:** `pio test -e native` (498/498) · Phase 4 HITL: 64-bar record with `#CAP,PERS,mid_pass` slices during capture; `freeChunkCount` above reserve

### Prior track (wind-down)

**OpenSpec: [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)** — M5 adopt-on-load shipped; lazy load + 64+64 HITL gate remain.

---

## Paused — blocked by UIP

**Derived note overlap logic** — EditSessionAction geometry pipeline:

- **OpenSpec:** [`edit-session-action-geometry`](../../openspec/changes/edit-session-action-geometry/) — **blocked** until UIP Phases 1–5 + HITL
- Handoff: [derived_note_overlap_logic_handoff.md](../plans/derived_note_overlap_logic_handoff.md)

## Explicitly NOT implementing

- D13 arrangement jam capture — future roadmap only
- Bisect / save-bypass HITL gates — parked (DEC-017)
- Workspace quarantine as substitute for load fix — valid data must load (DEC-019)
- Note-edit continuous persistence — out of scope v1 (DEC-020)
- Phase 5 journal prefix recovery — not started until Phase 4 HITL passes
