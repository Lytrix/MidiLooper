# NOTE_EDIT fader outbound coordinator (trigger-stepped)

**Kind:** enhancement (replaces time-deferred sync from `note-edit-fader-feedback-regression`)  
**Status:** In progress (2026-06-29)

## Problem

Outbound DROID fader feedback uses stacked `millis()` deferrals (`SELECTNOTE_UPDATE_DELAY`, `sessionFaderSyncDueMs_`, `pendingFineFaderSync_`, `enableStartEditing` grace). This is structurally unsound:

- Competing schedulers cancel/reschedule each other (minutes-long delays).
- ch15 coarse (pitchbend) + fine (CC) + note-value sent in the same tick; DROID applies last message — fader3 moves, fader2 does not.
- Stale coarse inbound accepted after select (`startEditingEnabled` + smart feedback) moves note to old motor position.
- NOTELEN reads anchors before pending move geometry is committed → length range uses old tick.

## Design: trigger-stepped queue (no inter-step timers)

Single owner: `NoteEditManager::processFaderOutbound()` — one outbound step per `update()` frame.

### Triggers → plan

| Trigger | Fader1 | Coarse | Fine | Note value | Wait fader1 echo before ch15 |
|---------|--------|--------|------|------------|------------------------------|
| `SessionOpen` | yes | yes | yes | yes | yes (loop-edit bracket safety) |
| `NoteSelect` | yes | yes | yes | yes | no |
| `LengthModeEnter` | no | yes | yes | no | no |
| `LengthModeExit` | yes | yes | yes | yes | no |
| `Fader1Only` | yes | no | no | no | no |

New trigger preempts in-flight plan (cancel + restart).

**Phase 2 update (2026-06-30):** Preempt-on-request **removed** for NoteSelect dependent refresh. Active ch15 pipeline runs to `Done`; pending refresh coalesces and runs after completion. Fader1 bracket remains immediate on note select; F2–F4 defer until fader1 stable ≥1000 ms. See OpenSpec Phase 2 in `note-edit-fader-feedback-regression`.

### Phase 2 — deferred dependent refresh

| Component | Behavior |
|-----------|----------|
| `scheduleDependentFaderRefresh()` | Sets `pendingRefresh.executeAt = now + 1000 ms`; resets on each select |
| `processDeferredRefresh()` | When idle and `now >= executeAt`, starts ch15 pipeline (skip fader1) |
| `requestFaderOutbound(NoteSelect, skipFader1=true)` | If ch15 active → coalesce pending, do not cancel |
| `LengthModeEnter/Exit`, `SessionOpen` | May still preempt (cancel + restart) |

### Removed policy (Phase 2)

- Immediate `requestFaderOutbound(NoteSelect)` on every fader1 select tick for ch15 dependents
- Unconditional `cancelActiveFaderOutbound()` at start of every `requestFaderOutbound`

### Steps (advance on completion, not delay)

1. **Fader1Bracket** — send ch16 PC1 + bracket pitchbend; arm ignore.
2. **Wait** (sub-state of step 1 when `waitFader1Echo`) — advance when inbound ch16 within tolerance of sent **or** ignore window expired (safety cap only).
3. **Channel15Coarse** — PC2 + pitchbend + note-1 trigger; arm coarse blanket ignore.
4. **Channel15Fine** — CC2 + note-0 trigger (next frame).
5. **Channel15NoteValue** — CC3 (next frame).
6. **Idle** — `startEditingEnabled = true`.

### Inbound gating

- Coarse: blanket ignore until `FEEDBACK_IGNORE_PERIOD` after outbound coarse (no smart-match only).
- ch15 blocked while outbound queue is on ch15 steps.

### NOTELEN

- **Enter:** `commitAllPendingNoteEditActions` → `syncNoteEditFocusLastFromSessionStore` → set anchors from `liveEditDisplayNoteAtSelect` → `requestFaderOutbound(LengthModeEnter)`.
- **Exit:** commit → sync → bracket to start → `requestFaderOutbound(LengthModeExit)`.

### Removed (after migration)

- `sessionFaderSyncStep_` / `sessionFaderSyncDueMs_`
- `pendingSelectnoteUpdate` / `selectnoteUpdateTime`
- `pendingFineFaderSync_` / `fineFaderSyncDueMs_`
- `sendSelectnoteFaderUpdate` deferral for select path
- `sendImmediateNoteSelectFaderFeedback` (folded into coordinator)
- `enableStartEditing` outbound sends (inbound grace only)

## Verification

- Serial: one `MO,192,15,2` then `MO,224,15` then `MO,176,15,2` on **consecutive frames** per select.
- `#DBG` `outbound_step` lines with step index.
- Manual: fader1 bracket on display; fader2+3 on select; NOTELEN full range after move.

---

## Phase 3 — selection-driven state machine (2026-06-30)

OpenSpec: `note-edit-fader-feedback-regression` Phase 3 (D12–D16).

| Deliverable | Status |
|-------------|--------|
| `NoteEditFaderOutboundPlan.h` policy + triggers | Shipped |
| `processFaderOutbound()` single path | Shipped |
| Slot-index navigation (no pitch deadband for select) | Shipped |
| 400 ms user-classified quiet before F2–F4 | Shipped |
| Retired: settle timer, immediate burst on every select | Shipped |

**Retirement criteria for diagnostic path:** HITL §18–§19 pass on fresh capture.

**Verification:** `#DBG outbound_step`, `#DBG select_slot`; HITL `note_edit_fader_select_refresh` verify script.
