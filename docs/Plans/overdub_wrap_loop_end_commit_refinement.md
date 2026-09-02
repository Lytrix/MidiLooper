# Overdub wrap — commit at loop end, not session start

**Status:** Stage 0 + Stage 1 host **shipped** (DEC-051). Device Stage 1 **PASS** [`201956`](../../captures/session_20260901_201956.log), [`204043`](../../captures/session_20260901_204043.log). Stage 3 / RC2b **parked** — reopen only on the recapture gates in [`overdub_wrap_companion_and_pairing_bugfix.md`](overdub_wrap_companion_and_pairing_bugfix.md).  
**Date:** 2026-09-01  
**Kind:** refinement  
**Parent:** [DEC-038](../DECISION_LOG.md#dec-038-overdub-wrap-commit-and-session-undo) (amend wrap persist grain only)  
**Evidence class:** RC2b wrap-held reconstruct ([`192435`](../../captures/session_20260901_192435.log), [`190847`](../../captures/session_20260901_190847.log), [`181116`](../../captures/session_20260901_181116.log)); bar-4 note-off symptom ([`193232`](../../captures/session_20260901_193232.log) — [`overdub_wrap_bar4_note_off_session_seal_bugfix.md`](overdub_wrap_bar4_note_off_session_seal_bugfix.md))  
**Does not start:** RC2b playhead-tails patch; OpenSpec `3.5`/`3.6` checkoff; occupy lookup changes; new wrap-origin noun

**Debugging boundary:**

```text
S = session start (playheadPhaseTick)
    ← stay: session identity, source-view open, occupy present-at-hold
wrap commit / journal / O: count
    ← move: seal when playhead crosses loop end (phase 0), not S
wrap-shaped consume
    ← retarget after seal move: musical [start, L) ∪ [0, E), not [S, L) ∪ [0, E)
```

---

## Problem

`Track::commitOverdubWrapAtSessionStart` seals when the playhead returns to **S**. When S is mid-loop (HITL delay, live punch-in), one capture pass spans `[S, L) ∪ [0, S)`.

That pass stores loop-crossing notes as wrap-held pairs (`endTick < startTick`) even when the note only crossed the **musical** loop, not a session-phase boundary. Reconstruct, source-view merge, occupy-at-0, and `applyCapturePlayheadTails` then have to interpret those pairs.

Pinned example (4-bar loop, `L = 3072`, S near 2296):

```text
Today — wrap at S
  pass holds On@2304 and Off@672 in the same store
      → wrap-held 2304→672
  closed 2304→2976 sits next to re-appended open On@2296
      → playhead tails / draw_src rewrite the closed NoteId
         (192435 host pin: 2296→2976 becomes 2296→2296;
          device draw_src end is 672)

Proposed — wrap at 0
  On@2304 Off@2976 seals at 0 as linear
  only a note still held at 0 is extracted into the next pass
  Off@672 belongs to the next pass, not a wrap-held pair invented
  by the session-S pass spanning tick 0
```

DEC-047 journals are already bar-cursors. Sealing at mid-bar S makes the first fragment a non-bar interval. Sealing at loop end makes every wrap journal a completed-bar set.

DEC-038 alternatives table rejected “Commit at loop tick 0 when S ≠ 0” as “Wrong wrap origin (6E.1b)”. 6E.1b proved **consume rotation** around S, not that the **seal clock** must be S. This plan splits those origins.

---

## Current vs proposed

```text
TODAY (DEC-038 wrap persist)
  start overdub at S
      openOverdubSession(S)
      source view @ S
      arm after leaving S
      seal when playhead crosses S
      extract held Ons → beginCapture → re-append
      O: += 1
      journal at S

  S = 2296, L = 3072
      first seal after a full return to 2296
      one pass contains [2296, 3072) ∪ [0, 2296)
      loop-crossing notes become wrap-held inside that pass

PROPOSED
  start overdub at S
      openOverdubSession(S)          unchanged
      source view @ S                unchanged
      occupy at note-on tick         unchanged
      arm after leaving S            unchanged (blocks immediate wrap if S == 0)
      seal when playhead crosses 0
      extract held Ons → beginCapture → re-append
      O: += 1
      journal at 0 (completed bars)

  S = 2296, L = 3072
      first seal at next 0  (partial: bars from S to L)
      later seals at every 0 (full loop, all bars)
      S == 0: same as today (arm after leaving 0, seal on next cross of 0)
```

---

## What stays at S vs what moves to 0

| Owner / contract | Stays at S | Moves to loop end (phase 0) |
|---|---|---|
| `Loop::playheadPhaseTick` / `hasOverdubSession` | session identity | — |
| `openOverdubSession` / `establishOverdubSourceView` | session-start sounding state | — |
| Occupy `collectOverdubNoteOnParticipantIds` | present-at-hold at the note-on tick | — |
| `armOverdubWrapAfterLeavingStart` | still arms after leaving S | — |
| `shouldCommitOverdubWrap` | — | `didPlayheadCrossPhase(..., 0, L)` |
| `commitCapturePass(OverdubWrap, sealedAtTick)` | — | `sealedAtTick = 0` |
| `queueOverdubWrapSnapshotJournal` | — | fires at loop end |
| `overdubSessionUndoDepth` / sidebar **O:** | — | increments at loop-end seal |
| `consumeSuppressedOverdubWrapCrossing` | — | suppress the next **0** crossing |
| Wrap-shaped consume `[S, L) ∪ [0, E)` | — | retarget to `[start, L) ∪ [0, E)` |
| `beginCapture(Overdub, …)` after wrap | keep passing **S** so source view is not re-opened at 0 | — |

`S == 0` is a no-op relative to today.

---

## Why this removes “adding wrapped notes”

`Track::commitOverdubWrapAtSessionStart` extracts preview-open Ons and re-appends them after `beginCapture`. That is correct **only** for notes still held at the seal.

When the seal is S and the pass already crossed 0:

1. A completed tail note (`2304→2976`) and a later Off in `[0, S)` share one store → reconstruct emits `end < start`.
2. A closed same-`NoteId` row and a re-appended open On at the same pitch+start collide in `applyCapturePlayheadTails` (RC2b red pin).
3. Occupy at `hs=0` then has to treat that wrap-held span as sounding, which fights RC2b (“closed same-pass Off cannot become wrap-held”).

When the seal is 0:

- Completed notes before 0 stay linear in the sealed pass.
- Extract runs only for notes actually held across the musical wrap.
- Re-append creates a wrap-held pair only when the Off arrives in the next pass. That pair is a true loop-crossing hold, not a session-S artifact.

Do **not** patch `applyCapturePlayheadTails`, `findOpenPassNote`, or gather union to chase 672. Those stay on the RC2b plan until this wrap-clock change is proven or rejected.

---

## Decision required (amends DEC-038)

Replace wrap-persist sentence 1:

| Was | Becomes |
|---|---|
| When the playhead returns to **S**, seal the current wrap | When the playhead crosses **loop end (phase 0)**, seal the current wrap |
| `sealedAtTick = S` | `sealedAtTick = 0` |
| First wrap after a full return to S | First wrap at the next loop end; later wraps every loop end |

Unchanged DEC-038 sentences: still an `OverdubPass`; no `finalizePendingNotes` at wrap; empty wrap is a no-op; undo while OVERDUBBING is session-gated **O:**; after stop one **U:** `OverdubPassAdded` for the session; crash mid-session out of scope.

6E.1b consume rotation is **not** reopened as wrap-origin proof. Consume for a wrap-shaped incoming note uses the musical loop, not S.

New DEC id: append **DEC-038 amendment** (or a short **DEC-051** that amends 038 wrap persist only). Do not add a second session-start noun. `playheadPhaseTick` remains S.

---

## Architecture checkpoint

| Question | Answer |
|---|---|
| **Ownership change?** | NO — same owners: `Loop::shouldCommitOverdubWrap` / `Track::maybeCommitOverdubWrap` / `Track::commitOverdubWrapAtSessionStart`. No new Manager. |
| **State transition change?** | YES — wrap seals at phase 0, not at S. First seal can happen earlier (S→0 instead of S→S). **O:** increments at that earlier seal. This plan is the design session. |
| **Unbounded interval owner?** | NO — same wrap commit, same journal caps, same extract of currently open Ons. |
| **Formal trigger?** | YES — [timing model](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) (when wrap seals) and an accepted-DEC amendment. |
| **Reuse** | YES — change the phase argument of `didPlayheadCrossPhase` from `playheadPhaseTick` to `0`. Rename `commitOverdubWrapAtSessionStart` → `commitOverdubWrap` (existing `shouldCommitOverdubWrap` pair). No new domain noun. |

Firmware is not authorized until the user accepts the transition change and the DEC amendment text.

---

## First wrap and undo (behavior that changes)

```text
S = 2296, stop at tick 1000 (crossed 0 once, never returned to S)

Today:  0 sealed wraps. Stop seals the whole S→1000 capture as the last wrap.
After:  1 sealed wrap (2296→0) plus a partial last wrap (0→1000).
        Session undo depth is 1 earlier. After stop still one U:.
```

Empty first remainder (start at S, no notes before 0): existing empty-wrap rule — no pass, no stack push, no revision bump.

Held note at 0: still do not call `finalizePendingNotes`. Extract and re-append only. Add stays NoteOff-owned.

---

## Wrap-shaped consume retarget

Shipped contract ([`overdub_wrap_crossing_hold_head_consume_bugfix.md`](overdub_wrap_crossing_hold_head_consume_bugfix.md)): incoming `end < start` consumes `[S, L) ∪ [0, E)` as one hold.

**`E` (consume):** head end tick of the incoming wrap-held note (exclusive end of the `[0, E)` head segment). This plan does not redefine `E`; child plan uses **`E_seal`** for the phase-0 commit endpoint separately.

After this change, a wrap-held incoming note is a musical loop-cross, not a session-S cross. Consume becomes:

```text
[start, L) ∪ [0, E)
```

`S` drops out of the **consume** window (not from session identity). Occupy-at-S holds that continue past 0 stay in the first pass until the 0 seal, then extract. The Off in the next pass uses the musical union.

Do not keep two consume origins. Stage 2 is this retarget, after Stage 1 native wrap-clock tests pass.

---

## Stages

### Stage 0 — DEC + native clock (no device) — **shipped**

1. DEC-051 amends DEC-038 wrap persist.
2. Native: `shouldCommitOverdubWrap` is true on `prev=L-1, curr=0` and false on `prev=S-1, curr=S` when `S ≠ 0`.
3. Native: `S == 0` still seals on the next cross of 0 after arm.
4. Native: undo suppress consumes the next **0** crossing, not S.
5. Renamed `commitOverdubWrapAtSessionStart` → `commitOverdubWrap`.

### Stage 1 — seal + extract at 0 — **shipped (host)**

1. `sealedAtTick = 0`.
2. After commit, `beginCapture(Overdub, S)` still (source view stays session-scoped).
3. Host fixture `test_first_wrap_seal_at_phase_0_keeps_tail_linear`: On@2304 Off@2880 in wrap 1; open On@2800 extracted; Off@200 in wrap 2; no Off@0 invented.
4. Device `2304→2976` / no `2304→672` remains Stage 3 regression, not host sole proof.
5. `test_overdub_source_view` wrap-crossing tests use phase 0 as the seal phase.

### Stage 2 — consume + journals

1. Consume already unions `[start, L) ∪ [0, E)` in `accumulatePendingNoteChangesForIncomingNote` — no firmware retarget in this change.
2. Confirm wrap journals enqueue at the 0 seal and bar cursors cover completed bars only — device Stage 3.
3. Overlap-resolve catalog wrap-while-open owner renamed to `commitOverdubWrap`.

### Stage 3 — device

HITL: start overdub with a non-zero delay (existing `--overdub-start-delay-beats 1`). Prove:

- first `Overdub wrap committed` is at loop end, not at the start phase
- closed tail notes stay linear through the first wrap (`2304→2976`, not `2304→672`)
- only notes held across 0 appear as wrap-held
- **O:** increments at that loop-end seal
- after stop, one **U:** still drops the whole session

Do not treat RC2b tails 672 as a pass criterion for Stage 1. That writer is DisplayManager-only until Stage 3 shows it is gone because the wrap-held pair is no longer stored.

---

## Tests (host first)

| Fixture | Assert |
|---|---|
| `shouldCommitOverdubWrap(L-1, 0)` e.g. 3071→0 | true after arm (primary trigger) |
| `shouldCommitOverdubWrap(0, 1)` | false (off-by-one past phase 0) |
| S≠0: `shouldCommitOverdubWrap(S-1, S)` | false (negative: cross S no longer seals) |
| S=640: `shouldCommitOverdubWrap(639, 640)` | false (193232 instance) |
| S=777, L=1920: `shouldCommitOverdubWrap(776, 777)` | false |
| S=0, arm after leave, cross 0 | true (parity with today) |
| Undo suppress | next 0 crossing consumed; later 0 crossing seals |
| Sealed pass geometry | First seal at phase 0 (not S); `[S,L)∪[0,E_seal)` provenance; per-pass pairing check only if fixture supports pass membership |
| 192435 device regression | sealed wrap 1 has `2304→2976` linear; no `2304→672` (not host Stage-1 sole proof) |
| Held across 0 | extract On; no synthetic Off at 0; Off in next pass |
| Empty S→0 | no pass, no **O:** increment |

Suites: `test_overdub_source_view`, `test_pending_note_change` wrap-crossing, `test_loop_take_survival` wrap-journal fixtures that pass `sealedAtTick=S`.

Then `pio run -e teensy41-capture-serial` (RAM1 locals ≥ 4000) and `pio test -e native`.

---

## Files (Stage 1)

| File | Change |
|---|---|
| `src/Loop/LoopOverdubSession.cpp` | `shouldCommitOverdubWrap` / suppress cross phase 0 |
| `src/Track/TrackOverdubWrap.cpp` | `sealedAtTick = 0`; rename commit function |
| `include/Track.h` | rename declaration |
| `include/Utils/CommittedPlaybackLedgerCatchUp.h` | call site follows rename only |
| `test/test_overdub_source_view/test_overdub_source_view.cpp` | seal phase 0 |
| `test/test_pending_note_change/test_pending_note_change.cpp` | consume / wrap clock |
| `test/test_loop_take_survival/test_loop_take_survival.cpp` | `sealedAtTick` 0 in wrap-like helper where the test is the device wrap |
| `docs/DECISION_LOG.md` | amendment |
| `docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md` | consume window (Stage 2) |
| `docs/Guides/NOTE_WRAPPING_LOGIC.md` | overdub wrap paragraph: seal at loop end |

---

## Explicit non-goals

- Do not change occupy present-at-S / present-at-hold.
- Do not rebuild source view at 0 on wrap `beginCapture` (keep session S).
- Do not invent an Off at 0 for held notes.
- Do not add a wrap-origin field; phase 0 is `didPlayheadCrossPhase`’s `phaseTick`.
- Do not patch RC2b tails / `draw_src` in the same commit.
- Do not check off OpenSpec `3.5` / `3.6`.

---

## Approval

Approved via child plan (2026-09-01). Firmware Stage 1 may start after:

1. DEC-038 wrap-persist amendment at Stage 0 (seal at 0, S remains session identity).
2. Earlier first-seal / **O:** increment when S ≠ 0 accepted.
3. Rename follows semantic change + tests (cleanup).
