## Context

NOTE_EDIT DROID faders are owned by `NoteEditManager` (not `MidiFaderProcessor`). Outbound feedback uses deferred chains (`SELECTNOTE_UPDATE_DELAY` = 1600 ms) and layered ignore windows (`FEEDBACK_IGNORE_PERIOD` = 1500 ms, `selectFaderFeedbackIgnoreUntilMs_`, `sessionFaderSyncStep_`).

Commit `d49e4c8` introduced `deferSelectFaderSyncToBracket` → `processSessionFaderSync` (steps 1–3: fader1, fader2 coarse, fader3/4) to fix loop-edit → NOTE_EDIT bracket pull. The same commit documents fader2 coarse as unreliable. `syncNoteEditSessionStateToUi` still calls `sendSelectnoteFaderUpdate` on every select, creating a second 1600 ms scheduler that races with session sync.

Inbound edit geometry (`handleCoarseFaderInput`, `toggleLengthEditingMode`, `lengthEditCoarsePitchbendToLoopTick`) was fixed in `d576f85` — this bug is **outbound-first**.

Primary files: `src/NoteEditManager.cpp`, `include/NoteEditManager.h`, `src/EditManager.cpp`.

## Goals / Non-Goals

**Goals:**

- Fader1 motor reaches select bracket after NOTE_EDIT entry (within bounded latency).
- Fader2 and fader3 motors update **together** on session entry and fader1 note select when `!lengthEditingMode`.
- NOTELEN enable sends fader2 coarse to note end before length inbound is accepted.
- No fader3-only sync path without preceding fader2 coarse.
- Preserve `d49e4c8` intent: loop-edit stale motors must not pull fader1 bracket during ignore window.

**Non-Goals:**

- Rewriting `MidiFaderManager` / `MidiFaderProcessor`.
- Encoder `NoteEditKind::Length` ↔ `lengthEditingMode` unification.
- Changing note edit store, overlap, or undo paths.

## Decisions

### D1 — Single outbound coordinator (Option A, recommended)

**Decision:** `deferSelectFaderSyncToBracket` owns entry feedback; cancel pending `pendingSelectnoteFaderUpdate` when session sync starts. `syncNoteEditSessionStateToUi` skips `sendSelectnoteFaderUpdate` while `sessionFaderSyncStep_ != 0`.

**Rationale:** Eliminates duplicate 1600 ms fires at session open. Select-after-entry uses `sendSelectnoteFaderUpdate` only when sync is idle.

**Alternatives:**

- **Option B (immediate fader1):** Send fader1 synchronously on entry; defer ch15 only — shorter dead time but reintroduces bracket-pull risk if loop-edit motor echoes before ignore arms.
- **Option C (reduce delay):** Lower `SELECTNOTE_UPDATE_DELAY` alone — does not fix fader2/fader3 split or scheduler race.

### D2 — Coarse + fine always paired

**Decision:** Merge session sync steps 2 and 3: step 2 sends `sendStartNotePitchbend` (coarse + fine); step 3 sends fader4 note-value only. `performSelectnoteFaderUpdate` already pairs coarse+fine — keep that invariant.

**Rationale:** User report: fader3 updates without fader2 — caused by step 3 sending fine without guaranteed step 2 coarse.

### D3 — Narrow session-sync input block

**Decision:** While `sessionFaderSyncStep_` is 1 (fader1 pending/sent), block fader1 only via `selectFaderFeedbackIgnoreUntilMs_`. While step ≥ 2, block ch15 faders (coarse/fine/note-value) until step completes. Do **not** block fader1 after step 1 completes.

**Rationale:** Current `handleFaderInput` early-return blocks all faders for ~4.8 s — user cannot use fader1 after its ignore window.

### D4 — Coarse `lastSentTime` via `sendFaderUpdate`

**Decision:** Route `sendSessionFader2CoarseSync` and `sendStartNotePitchbend` coarse path through `sendFaderUpdate(FADER_COARSE)` or set `lastSentTime` + `armChannel15FaderFeedbackIgnore` consistently in `sendCoarseFaderPosition`.

**Rationale:** Inconsistent `lastSentTime` breaks smart feedback detection and skip logic in `performSelectnoteFaderUpdate`.

### D5 — NOTELEN coarse-first

**Decision:** After `toggleLengthEditingMode(true)`, call `sendFaderUpdate(FADER_COARSE)` then `sendFaderUpdate(FADER_FINE)` with `lengthFineAnchorEndTick` already set; log anchor tick at INFO for serial gates.

**Rationale:** Length inbound maps pitchbend absolutely — motor must reflect note end before user touch.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Bracket pull if fader1 sent too early | Keep 1600 ms delay before step 1; prime `selectFaderFeedbackIgnoreUntilMs_` at schedule time |
| Motor settle time insufficient | Retain `SELECTNOTE_UPDATE_DELAY` between steps; do not skip step 2 for step 3 |
| `performSelectnoteFaderUpdate` skip during protection window | After sync complete, force one coarse+fine pair if skipped — **shipped in follow-up:** always call `sendChannel15NotePositionFeedback` (no skip guards) |
| `sendFaderUpdate(FADER_COARSE)` skips program change when another ch15 fader was driver | **RC5 (post-capture):** use `sendChannel15NotePositionFeedback` for NOTELEN + `enableStartEditing` — always sends PC prog 2 before coarse pitchbend |
| Regression of loop-start bracket alignment (`d49e4c8`) | Manual verify: enter NOTE_EDIT from LOOP_EDIT with fader2 at loop end — bracket stays on note, not pulled |

## Migration Plan

1. Implement coordinator + paired sync in `NoteEditManager.cpp`.
2. Guard `EditManager::syncNoteEditSessionStateToUi`.
3. Add native tests; run `pio test -e native`.
4. HITL edit baseline with new serial gates on hardware.
5. Archive change; merge spec into `openspec/specs/note-edit-fader-feedback/`.

Rollback: revert `NoteEditManager` sync changes; pre-`d49e4c8` behavior restores immediate feedback but bracket-pull risk returns.

## Open Questions

- **TBD:** Final sync latency budget — is immediate fader1 (Option B) required for product feel, or is ≤2 s chain acceptable?
- **TBD:** HITL fail vs warn on missing `Session fader sync: sent fader 2 coarse` until firmware fix verified on device.

---

## Phase 2 — Deferred refresh pipeline (2026-06-30)

**Context:** Phase 1 replaced millis-deferred schedulers with a frame-stepped coordinator (`NoteEditFaderOutboundPlan`, `processFaderOutbound`). Bug persists because `requestFaderOutbound` still preempts in-flight ch15 sequences on every fader1 note select.

### Phase 2 goals

- Dependent faders (F2–F4) refresh reliably after fader1 note select when user settles.
- Active ch15 pipeline runs to `Done` without cancellation by subsequent refresh requests.
- Fader1 bracket feedback remains immediate on note select (user reports F1 works).

### D6 — Preemption policy

**Decision:** Never cancel a mid-pipeline ch15 sequence for NoteSelect refresh. If refresh is requested while `outboundStep_` is active on ch15 steps, set `pendingRefresh` with latest trigger; run one refresh immediately after current `Done`.

**Exception:** `LengthModeEnter` / `LengthModeExit` may still preempt — user is not moving fader1 during NOTELEN toggle.

### D7 — Request vs active

**Decision:** `pendingRefresh` (schedule: `pending`, `executeAt`, `trigger`) + `outboundStep_` (execution). No separate `outboundRequested` flag — `pendingRefresh.pending` suffices.

### D8 — Fader1 vs dependents timing

**Decision:** Fader1 bracket: immediate outbound on note select. F2–F4: defer until fader1 stable for ≥1000 ms (`DEPENDENT_REFRESH_STABILITY_MS`, aligned with `COARSE_STABILITY_TIME`); each new select resets `executeAt`.

### D9 — Coalescing

**Decision:** Single pending slot (latest wins). Multiple rapid selects → one full F2/F3/F4 pipeline for final selection geometry.

### D10 — Settle delay

**Decision:** Optional `OUTBOUND_MOTOR_SETTLE_MS` (default 0). Insert wait sub-state between pitchbend/CC send and motor trigger if HITL shows motor miss. Validate with `#DBG outbound_step` before hardcoding 10–30 ms.

### D11 — SessionOpen

**Decision:** Keep `WaitFader1Echo` for loop-edit bracket safety on session entry. Dependent refresh still sequential after echo cap or safety timeout.

### Phase 2 state machine

```
Fader1 note select → mark pendingRefresh (executeAt = now + 1000 ms)
  → [fader1 quiet 1000 ms] → if outbound active, hold pending until Done
  → begin ch15 pipeline (F2 PB → trigger → F3 PB → trigger → F4 → trigger) → Done, clear pending
```

### Phase 2 risks

| Risk | Mitigation |
|------|------------|
| Dependents lag up to 1 s after last select | Acceptable — matches user desired behavior; coalesce rapid selects |
| SessionOpen still needs immediate full pipeline | SessionOpen bypasses deferred scheduler; uses existing coordinator |
| LengthModeEnter blocked by pending refresh | LengthModeEnter preempts (D6 exception) |

---

## Phase 3 — Selection-driven outbound state machine (2026-06-30)

**Context:** Diagnostic path (immediate F2–F4 + PC re-arm + millis settle + pitch deadband) partially restored motors but remained erratic on fast-then-slow gestures and multi-minute stalls. Capture `session_20260630_113422.log` shows 52 s F2 gaps and many `SEND_F1`-only outbound bursts — not MIDI TX queue buildup.

### Phase 3 goals

- Separate **input classification** (user vs feedback), **selection resolution** (slot-index navigation), and **outbound execution** (frame-stepped state machine).
- Dependent F2–F4 refresh after **user-classified** fader1 quiet (400 ms), not pitch deadband.
- Remove layered millis settle/significance gates that fight motor echo.

### D12 — Slot-index navigation

**Decision:** On user-classified fader1 input, any mapped **slot-index change** SHALL call `applyNoteSelectFromFader1Pitchbend` immediately without sending dependent feedback. `SELECT_MOVEMENT_THRESHOLD` applies only to feedback classification and stale-echo lockout — not navigation suppression.

### D13 — Outbound select phase + quiet gate

**Decision:** `FaderSelectPhase`: `Idle` → `UserMovingFader1` on user-classified input; after `kFader1QuietMs` (400 ms) without user-classified input, re-apply selection from final pitch and `requestFaderOutbound(NoteSelectDependent)`. Quiet timer resets **only** on user-classified fader1 samples.

### D14 — Frame-stepped coordinator (restored)

**Decision:** Single `processFaderOutbound()` in `NoteEditManager`; triggers in `NoteEditFaderOutboundPlan.h`. PC re-arm (`armNoteEditDroidMotorBank`) before ch15 burst. SessionOpen waits fader1 echo (D11).

### D15 — No dual paths

**Decision:** Remove `scheduleFader1SettleRefresh`, immediate dependent burst on every selection change, and `enableStartEditing` outbound sends.

### D16 — Instrumentation

**Decision:** `#DBG outbound_step` + `#DBG select_slot` under `SESSION_CAPTURE`.

### Phase 3 state machine

```mermaid
stateDiagram
    direction LR
    Idle --> UserMovingFader1: userClassifiedFader1
    UserMovingFader1 --> UserMovingFader1: userClassifiedFader1
    UserMovingFader1 --> PendingDependentRefresh: quiet400ms
    PendingDependentRefresh --> ExecutingDependent: processFaderOutbound
    ExecutingDependent --> Idle: Done
    ExecutingDependent --> ExecutingDependent: coalescePending
```

### Phase 3 risks

| Risk | Mitigation |
|------|------------|
| 400 ms lag after last user sample | Tunable `kFader1QuietMs`; slot change updates selection live |
| Motor echo classified as user | Smart feedback ignore; quiet only on classified user input |
| Hybrid diagnostic + coordinator | Single path only (D15) |
