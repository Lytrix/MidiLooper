# Overdub present-at-S JIT

**Status:** Architecture **pinned** 2026-08-18. Firmware **not authorized**. Native gold for the bounded miss path is the next implementation start gate.  
**Date:** 2026-08-18  
**Kind:** architecture  
**Decision:** [DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)  
**Parent:** [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md)  
**Related:** [`consumer_window_budget_ownership_architecture.md`](consumer_window_budget_ownership_architecture.md), [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype), [`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md)  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log) (16-bar USB `resolveWindow`), [`173842`](../../captures/session_20260815_173842.log) / [`185931`](../../captures/session_20260815_185931.log) (full-loop STOPPED gate 31–40 s), [`132806`](../../captures/session_20260818_132806.log) (Stage 1c held; 64-bar `from=span` missed because PLAYING started during `prep`)

**Does not authorize:** firmware; USB `resolveWindow` / cold `resolveState` on occupy; PLAYING full-history LCR (option B); wait-STOPPED-for-`lcr,mat` as occupy readiness; Stage 2 consume merge; `WindowManager` / `WindowRequest`; a new Session noun; shrinking `kOverdubSourceWindowBars` as production occupy policy; changing `notePresentAt`; deleting `overdubSourceView`.

---

## Stop

Do **not** continue Stage 1 `from=span` / wait-STOPPED-for-`lcr,mat` as product readiness. That is option **A** (STOPPED re-arm of the full-history device gate) — already **rejected** as always-ready ([`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md)). A 64-bar gate is 31–40 s STOPPED. Waiting for it cannot be how PresentNotes appear at `currentTick`.

[`132806`](../../captures/session_20260818_132806.log): Stage 1c **held** (64-bar indexed to `prep` during save). `from=span` missed because PLAYING started during `prep`. Unprepared enter `from=win` / occupy `from=miss` is honest. Do not patch occupy-set identity.

Park Stage 2 consume merge. Membership + length identity + dirty/save stall stay **shipped**. `from=span` is opportunistic source-view quality when a finished matching session exists.

---

## Goal

From the parent:

> Overdub needs to know which loop notes are **present** at the current loop tick.

Desired cost:

```text
USB note at S, pitch P
  → consume present-at-S if already prepared
  → else smallest canonical query for present(S, P)
  → RC8 participant ids
```

Not:

```text
wait for full-loop deviceGateComplete
  or resolveWindow(16 bars) on USB
  then reconstruct, then find participants
```

Legitimate cost is traversal to S + notes present at S + participant filter — **not** loop length × source window × reconstruct.

---

## Formal trigger

**Timing model:** when PresentNotes at `currentTick` become available. Today they exist only after STOPPED `deviceGateComplete` + matching length/revision (`preparedWindowReady`). Changing that is this pin, not a predicate patch on the full-loop gate.

**Owner:** `LoopContentResolution` (query) + `Track::processDeferredIdleMaintenance` (when slices run). No `WindowManager`. No new Session noun.

**6.0 unchanged:** `startOverdubbing` / `establishOverdubSourceView` consume only. Commit-site `publishPreparedOverdubPass` updates an already kept index. It does not create the first present-at-S set.

---

## Occupy contract

| Item | Pin |
|------|-----|
| Query | Notes of pitch **P** present at tick **S** (`currentTick` / hold start) |
| Membership | `OverlapNoteIdObservation::displayNotePresentAtHold` on `NoteSpan` start/end (Phase 0b / Phase 2 collect). Not emitted `PresentNote` (no `endTick`). Not `notePresentAt` (playback / checkpoint fill) |
| Prepared hit | `LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick` — walks prepared `NoteSpan`s. Cheap **if spans exist** |
| Prepared miss today | `Loop::collectOverdubSourceHoldParticipantIds` (source-view walk). Never `ensureOverdubSourceNotesForHold` / `resolveWindow` on note-on (Phase 3) |
| Must not mean | 16-bar all-pitch window; full-loop spans; MIDI send / `ActiveNoteLedger`; `from=span` |

`from=span` is source-view consume-when-ready. It is not the occupy contract.

### Occupy vs consume vs display

| Consumer | Required information | Must not mean |
|----------|----------------------|---------------|
| Occupy (note-on at S) | NoteIds of pitch P present at `currentTick` | 16-bar window; full-loop device gate |
| Consume (hold / wrap) | LinearSpan of occupied ids through [S, E), wrap | 16-bar reconstruct to rediscover ids (`merged=0` on [`213401`](../../captures/session_20260817_213401.log)) |
| Source view / display | Piano-roll / overlap lookup | Occupy readiness |

Consume / Hide stays on `overdubSourceView` until a later span neighborhood exists for occupied ids. Do not fold consume into occupy JIT.

---

## Why the full-loop gate is the wrong occupy prepare

Two questions were fused ([consumer window budget](consumer_window_budget_ownership_architecture.md)):

1. **How much** does occupy need? Present at S for pitch P.
2. **When** is it safe to resolve? Today: after a one-shot **full-loop** LCR gate (TickIndex + pair + reconstruct + spans + checkpoints at 8-bar stride).

DEC-037 already says `resolveState(tick)` is the fundamental query; cost after index is candidate events and affected state. The device gate still builds the **entire** loop before any tick query is allowed. Occupy then walks those spans. The stall is **building all spans**, not asking present(S, P).

Checkpoint tail (`tryResolvePreparedState`) matches the desired cost model **when prepared**. It requires `preparedWindowReady`, non-empty `spans`, `spanBoundaries`, and `presentAt` — products of `deviceGateComplete`. Building one checkpoint at S with no prior checkpoints is cold `resolveState(passes)` (full-loop gather + reconstruct). Parent §18.4: that is **not** the USB replacement.

---

## Bounded miss (pinned)

### USB today (6.0, no firmware change)

```text
tryCollectPreparedPresentNoteIdsAtTick
  hit  → occupy ids
  miss → source-view walk (may miss NOTE ONs outside the 16-bar view)
```

Do not block overdub. Do not wait tens of seconds STOPPED. Do not put `resolveWindow(16)` or cold `resolveState(passes)` on occupy.

### Next implementation — this-pitch present-at-S (not checkpoint tail)

**Preferred miss algorithm:** this-pitch events whose LinearSpan can contain S, then `displayNotePresentAtHold`. Owner: `LoopContentResolution` static next to `tryCollectPreparedPresentNoteIdsAtTick`. Not `ensureOverdubSourceNotesForHold`. Not a geometric 16-bar all-pitch window.

| Candidate | Why accepted / rejected as occupy miss |
|-----------|----------------------------------------|
| Checkpoint tail / `tryResolvePreparedState` | **Rejected for USB miss.** Requires the full-loop (or from-zero) span/checkpoint products. |
| 16-bar `resolveWindow` this-pitch merge (RC8 hold) | **Rejected for occupy.** [`213401`](../../captures/session_20260817_213401.log) paid 1177 events / 99 ms and could miss a NOTE ON before the window. Phase 3 native: 64-bar occupy from prepared spans when NOTE ON is outside the 16-bar view. |
| This-pitch pass events overlapping S | **Accepted.** Does not require `deviceGateFinished`. Cost is events of P, not all pitches × window. Membership is the same function occupy already uses on prepared spans. |
| Source-view walk | **Kept as USB fallback** until native gold of this-pitch present-at-S. Honest miss outside the view. |

**Native gold (start gate, no USB until pass):** ids equal `tryCollectPreparedPresentNoteIdsAtTick` on a finished matching session, and equal RC8 `collectOverdubSourceHoldParticipantIds` when those notes are in the source view. Additional fixture: 64-bar, NOTE ON outside the 16-bar source window, present at S — this-pitch query occupies that id; source-view walk does not. Existing `test_note_on_occupy_*` is that shape.

Do not wire the this-pitch query onto `handleMidiInput` until that gold passes. 6.0 still forbids construct/sort/checkpoint/`resolveWindow` on the button; a bounded this-pitch **Resolution** on occupy is a later architecture gate after native gold, not this pin’s firmware.

### Scheduled range (off USB) — opportunistic collect

Reuse the existing device gate as a **range**, not a new Session. Stage 9 already proved range index / pair / recon / proj / spans / prep match full (`test_stage9_range_*` in [`test_loop_content_resolution.cpp`](../../test/test_loop_content_resolution/test_loop_content_resolution.cpp)).

**Name (existing words):** `deviceGate` **range** around the playhead. Function shape when firmware is authorized: extend `deviceGateBegin` with an interval, or `deviceGateBeginRange` (`deviceGate` + `Range`). Not `PresentNeighborhoodSession`. Not `WindowManager`.

**Schedule:** PLAYING-safe **because the work is the range**, not full-history TickIndex+spans. Visual-cache idle already runs while PLAYING; full-loop LCR must not (option B stays rejected). Full-loop `deviceGateBegin(loopLengthTicks)` stays STOPPED-only and **opportunistic** for source-view `from=span`, not occupy readiness.

Once a playhead range has spans, USB `tryCollectPreparedPresentNoteIdsAtTick` can hit without `deviceGateFinished` for the whole loop — that requires a later stamp/ready predicate change (timing model; same DEC). Not this docs slice.

---

## JIT window (meaning)

JIT is **not** a smaller `kOverdubSourceWindowBars` clamp (Experiment 1 is evidence, not occupy policy). JIT is a consumer-sized resolution request:

```text
exact query: present(S, P)
  LCR resolves that tick (this-pitch overlapping events, or later range spans)
  not: 16 bars around S
  not: all 66 bars of TickIndex+spans
```

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO — `LoopContentResolution` + Track idle slices |
| State transition change? | NO for capture/overdub FSM. YES for **when** occupy present-at-S can succeed without full-loop `deviceGateComplete` — firmware not authorized until native gold |
| Reuse | YES — extend `tryCollectPreparedPresentNoteIdsAtTick` / device-gate range APIs already proven in Stage 9 |
| 6.0 | Unchanged — button consumes |

---

## Rejected as this goal

- Full-loop `lcr,mat` as HITL occupy / production gate (option A)
- PLAYING full-history LCR (option B)
- Cold LCR on `establishOverdubSourceView` (6.0)
- Experiment 1 1/2/4/8/16 as production occupy policy
- `WindowManager` / `WindowRequest`
- Tying occupy to `sendMidiEvent` / `ActiveNoteLedger` (withdrawn)
- Checkpoint tail as the USB miss path

---

## Next (not this slice)

1. Native this-pitch present-at-S vs prepared collect + RC8 + 64-bar outside-window fixture.
2. Architecture gate before any occupy USB call.
3. Device-gate range around playhead (PLAYING-admitted bounded slices).
4. Consume span neighborhood — separate information requirement.
