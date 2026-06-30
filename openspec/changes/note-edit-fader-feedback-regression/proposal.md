## Why

Commit `d49e4c8` (2026-06-29) added deferred DROID fader feedback on NOTE_EDIT entry to prevent stale loop-edit motor positions from pulling the select bracket. That change regressed outbound sync: **fader1 may not reach the bracket motor**, **fader2 coarse often does not update on entry or note select** while **fader3 does**, and **NOTELEN length edit** is unusable when fader2 still reflects a prior loop-edit position. Inbound fader2 still moves notes — the failure is feedback scheduling, not edit store logic.

This blocks trust in NOTE_EDIT on hardware and is orthogonal to active persistence/overlay work ([CURRENT_WORK.md](../../docs/runtime/CURRENT_WORK.md)).

## What Changes

- **Single outbound coordinator** in `NoteEditManager`: cancel duplicate `sendSelectnoteFaderUpdate` when session fader sync is active; always send fader2 coarse and fader3 fine together.
- **Session entry sync:** Reliable fader1 bracket + fader2/3 note-start feedback within bounded latency; narrow input blocking so fader1 is usable after its feedback ignore period.
- **Note select sync:** `performSelectnoteFaderUpdate` and session sync step 2 SHALL refresh coarse and fine together when `!lengthEditingMode`.
- **NOTELEN:** Coarse motor feedback to note end before accepting length inbound; serial log anchor tick + pitchbend for HITL gates.
- **Coarse feedback consistency:** All coarse outbound paths set `lastSentTime` (via `sendFaderUpdate` or equivalent).
- **Regression tests:** Native helpers for length coarse round-trip; HITL serial gates for fader2 sync after select and NOTELEN range.

## Capabilities

### New Capabilities

- `note-edit-fader-feedback`: Outbound DROID fader sync on NOTE_EDIT session entry, fader1 note select, and NOTELEN enable; input gating during sync.

### Modified Capabilities

- *(none)* — no requirement changes to archived `note-edit-modification-session` or `note-edit-session-state` storage contracts.

## Impact

- **Firmware:** [`NoteEditManager.cpp`](../../src/NoteEditManager.cpp), [`NoteEditManager.h`](../../include/NoteEditManager.h), [`EditManager.cpp`](../../src/EditManager.cpp) (`syncNoteEditSessionStateToUi` guard).
- **Tests:** New native suite `test_note_edit_fader_feedback` (mapping + sync invariants); HITL edit baseline serial gates.
- **Brownfield:** [BUG.md](./BUG.md); related archived `edit-record-display-length-mode` (D3 length lifecycle — do not regress).
- **Docs:** Update [PROJECT_STATE.md](../../docs/runtime/PROJECT_STATE.md) when shipped.

## Non-Goals

- ~~Full fader state-machine rewrite or migration to `MidiFaderProcessor` for NOTE_EDIT path.~~ *(Phase 3: selection-driven outbound state machine in `NoteEditManager` — not `MidiFaderProcessor` migration.)*
- Unifying encoder `NoteEditKind::Length` with `lengthEditingMode` (NOTELEN path only for this bug).
- Jam / multi-loop / persistence overlay changes.

## Open Decisions (TBD)

- **Sync latency budget:** Resolved in Phase 3 — frame-stepped coordinator, 400 ms user-classified quiet before dependent refresh.
- **HITL gate strictness:** Fail edit baseline on missing fader2 sync vs warn-only until firmware fix lands.

---

## Phase 2 — Deferred refresh pipeline (2026-06-30) — superseded

**Status:** Partially implemented then reverted on hardware; diagnostic immediate+settle path replaced Phase 2 coordinator. See **Phase 3**.

### Why (Phase 2)

Phase 1 shipped the frame-stepped outbound coordinator (`NoteEditFaderOutboundPlan`, `processFaderOutbound`) but retained preempt-on-request: every `requestFaderOutbound()` cancels in-flight ch15 sequences. During fader1 note-select movement, dependent faders (F2–F4) update once then starve — see [BUG.md RC5](./BUG.md).

### What Changes (Phase 2)

- **Deferred dependent refresh:** After fader1 note select, F2–F4 outbound waits until fader1 stable for ≥1000 ms; each new select resets the timer.
- **Non-preemptive execution:** Active ch15 pipeline runs to `Done`; pending refresh coalesces (latest wins) and runs immediately after completion.
- **Fader1 immediate, dependents deferred:** Fader1 bracket feedback on note select remains immediate; only F2–F4 use the deferred scheduler.
- **Centralized scheduling:** All note-select dependent refresh routes through `scheduleDependentFaderRefresh()` instead of immediate `requestFaderOutbound(NoteSelect)`.
- **Instrumentation:** Temporary `#DBG outbound_step` serial lines for HITL diagnosis.
- **HITL:** Rapid fader1 multi-select scenario verifying one complete F2/F3/F4 sequence after quiet period.

### Phase 2 non-goals (unchanged)

- No `MidiFaderProcessor` migration.
- No persistence/overlay scope.

---

## Phase 3 — Selection-driven outbound state machine (2026-06-30)

### Why (Phase 3)

Diagnostic path (immediate F2–F4 + PC re-arm + millis settle + pitch deadband) improved motors intermittently but remained erratic: fast-then-slow gestures left selection lagging pitch; settle timers fought motor echo; multi-minute stalls were policy conflicts, not MIDI TX queue buildup — see [BUG.md RC7–RC10](./BUG.md) and capture `session_20260630_113422.log`.

### What Changes (Phase 3)

- **Slot-index navigation:** User-classified fader1 input applies selection immediately on mapped slot-index change (no pitch deadband for navigation).
- **Outbound state machine:** `processFaderOutbound()` frame-stepped pipeline; `FaderSelectPhase` tracks user motion vs quiet; dependent F2–F4 refresh after **400 ms** user-classified quiet (`NoteEditFaderOutboundPlan.h`).
- **Non-preemptive coalesce:** NoteSelect dependent refresh coalesces while ch15 pipeline active; SessionOpen / LengthModeEnter / Exit may preempt.
- **Single outbound path:** Remove settle timer and duplicate immediate burst path; keep DROID PC re-arm before ch15 burst.
- **Instrumentation:** `#DBG outbound_step` + `#DBG select_slot` under `SESSION_CAPTURE`.
- **Tests:** Native policy tests on `NoteEditFaderOutboundPlan`; HITL capture gates for fast-then-slow and fader1-min scenarios.

### Phase 3 non-goals

- No `MidiFaderProcessor` migration.
- No persistence/overlay scope.
