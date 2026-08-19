# Occupy leftover identity after rematerialize

**Status:** Native **PASS** 1371/1371. RAM1 **425932** / locals **4768**. HITL gate open.  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent (duplicate Off apply shipped, leftover FAIL):** [`overdub_occupy_clock_duplicate_off_ledger_bugfix.md`](overdub_occupy_clock_duplicate_off_ledger_bugfix.md)  
**Pin:** [`104654`](../../captures/session_20260819_104654.log) — 19× `n=1 a=0` pitch 12 `lid=5701` `lst=96`  
**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTickInLoop` / `nextEventIndex` from USB; Off stamping; FIFO; option B (ledger consults derived spans); folding capture into `mergedMidiEvents`; duplicate `noteOn` no-op push; clock duplicate Off ledger apply; `isPlaybackCatchUpWindow` equal-tick contract; clearing the whole ledger on rematerialize

---

## Invariant (one sentence)

An open ledger identity is retained iff its NoteOn identity exists in a **complete** rebuilt committed playback stream.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — writer stays `applyPlaybackLedgerEvent` from clock. Occupy stays a reader. Rematerialize still owns `runtime.mergedMidiEvents`. |
| **State transition change?** | NO — wrap still commits at session start 312. `shouldApply` skip bound unchanged. `runtime.reset(true)` still preserves legitimate open notes. |
| **Reuse** | YES — extend the existing rematerialize site (`ensurePlaybackMergedMidiEventsBuilt` / wrap catch-up after rebuild) and `ActiveNoteLedger` (already has `eraseAt`). No new owner. Do not consult `overdubSourceViewNotes`. |

## Debugging boundary

```
… → DEC-042 open-NoteOn ledger ← trust nested [`095902`]
 → duplicate identity catch-up then clock ← trust [`103234`] led == n
 → clock duplicate same-phase Off/On skips ledger ← trust apply; HITL leftover not this FAIL [`104654`]
 → leftover Entry whose On is gone from merged ← this RC
 → nested same-start n=2 a=2 / n=3 a=2 pair ← not this FAIL
```

Do not make occupy catch-up run when `occupyPhase == lastTick`. Do not stamp Off identities. Do not clear the ledger on rematerialize (that is `n=0 a=1` for notes still open in merged). Do not change `isPlaybackCatchUpWindow(88,88)==true` in this RC.

---

## Pin facts ([`104654`](../../captures/session_20260819_104654.log))

72 `DIAG,lcr,part`. **41** mismatches, all `cu=0`. `led == n` **41/41**. `n=0 a=1` **0**. `eq=1` on 72/72. One `RING,overflow` before the occupy window (L2601). Pin two-Off dumps **0**.

| n,a | 103234 | 104654 |
|-----|------:|-------:|
| **0,1** | **0** | **0** |
| **1,0** | **18** | **19 FAIL** |
| **2,1** | **20** | **10** |
| **2,0** | 0 | 4 |
| **3,1** | 0 | 6 |
| **3,2** | 1 | 2 |

**All 19 `n=1 a=0` are the same Entry:** pitch 12, `lid=5701`, `lst=96`. Includes occupy at the start tick (`hs=96`, `a=0`) and wrap-head `hs=0`. Not exclusive-end at an Off tick as the occupy phase.

`id=5701` never appears as `mmspan`. `mmevt` never has `k=on,id=5701`. Occupy mismatch `mmevt` logs events within one bar of hold (`occupyMismatchTickDistance` ≤ `TICKS_PER_BAR`); at `hs=240` an On@96 would be distance 144 and would log immediately after Off@96. First leftover L2668: **Off@96 present, On@96 absent**.

L2668 covering is wrap-head **5772 `0–96`** (exclusive end 96) plus later spans. Off@96 is untagged (`id=0`).

L3779 occupy **at `hs=96`**: covering 0; dump has On@0 / Off@48 / On@144 — no On@96, no Off@96. Ghost already on the ledger.

L4580 occupy **at `hs=96`**: covering **5841 `96–288`**, `mmevt` **On@96 id=5841**, `lid=5841` `lst=96`, `n=3 a=1`. Leftover 5701 still underneath (plus a third identity). The On@96 that exists later is not 5701.

Second leftover class: `n=2 a=0` L3179 `lid=5786` `lst=192`. No On@192 in that dump. Same class, not only 5701.

First pitch-12 occupy in this log (L2621 `hs=480` `n=2 a=1`) already has an extra identity. 5701 is older than nearby ids (5755–5772). Opening is **before** this occupy window; RING overflow at L2601.

Overdub wrap S is **312** (`1080, 1848, …` all `% 768 == 312`).

Remaining `n=3 a=2` L3910 is nested On@240 pair (5799, 5805) plus leftover — not two Off@71.

---

## Root cause

Two cooperating facts. Creation is not in this capture; persistence is.

### 1. Preserve-ledger rematerialize (creation condition)

`LoopPlaybackRuntime::reset(true)` clears merged events and **keeps** `ActiveNoteLedger`. Wrap commit (`commitCapturePass` / `commitPendingCapturePass`) increments `playbackRevision` and `notifyCommittedContentChanged`; `catchUpOverdubWrapPlaybackLedger` then rebuilds merged. Edit-pass hide/shorten bake does the same revision bump. The Entry for 5701 survives. Reconstruct and merged no longer contain On@96 id=5701.

`noteOn` stores `evt.tick` as `Entry.startTick`. `lst=96` means some writer applied a NoteOn with `noteId=5701` and `tick=96` (`applyPlaybackLedgerEvent` → `applyPlaybackEventBody`). Writers are clock `advancePlaybackCursor`, wrap-pass `applyCommittedOverdubPassPlaybackInterval`, and occupy catch-up. Capture echo does not write the ledger.

This log does not contain that apply. Native fixture must start from the post-drop state the occupy window actually shows.

Do **not** treat equal-tick catch-up replay of `[0, S]` as this pin’s creation path. After wrap, `rebuildPlaybackOrder` + `reanchorPlaybackIndex` run before `syncRevision`. A later `isStale` path zeros `nextEventIndex` then `ensurePlaybackMergedMidiEventsBuilt` sets `playbackOrderDirty` (merged was cleared) and reanchors when `lastTickInLoop` is known. `test_playback_catch_up_window_differs_from_at_loop_start` requires `isPlaybackCatchUpWindow(88,88)==true`; this RC does not change that.

### 2. Untagged Off LIFO cannot retire the ghost (persistence, in the pin)

Off@96 is untagged. `ActiveNoteLedger::noteOff` LIFO-pops the **newest** On on the lane. Wrap-head 5772 (On at 672/0) is newer than 5701. Off@96 closes 5772. 5701 stays. Later Ons stack on top; later untagged Offs never reach 5701.

One Off in merged cannot close two Ons. Duplicate-Off apply does not apply here (zero consecutive `k=off,k=off` in this capture).

Occupy at exclusive-end `hs=96` is `a=0` because source view has no span present at 96 (`displayNotePresentAtHold` exclusive end). Ledger still holds 5701 → `n=1 a=0`.

---

## Fix

**Identity set only.** Retain an open Entry iff its `noteId` has a NoteOn in the rebuilt committed event stream. Do not compare start ticks, `(channel, pitch, startTick)`, LCR, or reconstructed spans. Untagged Entries (`kInvalidNoteId`) stay — they have no identity to invalidate. Keep Entries whose On is still in the stream even if their Off is not (open state at reconcile). Do not `ledger.clear()`. Do not walk `overdubSourceViewNotes` (option B).

**Full-loop precondition (architectural).** Only a complete reconstruction of committed playback content may invalidate ledger identities. A partial window cannot make that claim. The call site must use `isFullLoopMergedPlaybackWindow` — do not inline `windowStartTick == 0 && windowLengthTicks == loopLengthTicks` as the gate.

```text
if (isFullLoopMergedPlaybackWindow(runtime.mergedMidiEvents, loop.loopLengthTicks)) {
  runtime.ledger.eraseOpenNotesMissingFromCommittedNoteOns(
      runtime.mergedMidiEvents.mergedEvents.data(),
      runtime.mergedMidiEvents.mergedEvents.size());
}
```

Windowed long-loop gather must **not** run this. HITL pin loop is 768 ticks (full gather). Occupy does not call it.

ITCM: `playCommittedLoopMidi` / other hot rebuild sites call FLASHMEM `reconcilePlaybackLedgerAfterFullLoopRebuild`, which gates on `isFullLoopMergedPlaybackWindow`. Wrap catch-up keeps the predicate at the call site (already COLD).

**Wrap order.** Reconciliation runs after the new full-loop merged stream exists and before wrap-pass playback can write more legitimate ledger state:

```text
commit
→ complete merged rebuild
→ if full-loop window: reconcile ledger against committed NoteOn identities
→ wrap-pass interval apply / cursor catch-up
```

Do not reconcile against old or partial merged content.

API: `ActiveNoteLedger::eraseOpenNotesMissingFromCommittedNoteOns` — reuse `eraseAt`. The ledger does not name `mergedMidiEvents`; the call site supplies the committed NoteOn stream.

Observability: one `DIAG,ledger,erase,id=` per dropped identity so a later leftover is “still in committed stream” vs “erase did not run”.

---

## Tests

[`test_playback_midi_output.cpp`](../../test/test_playback_midi_output/test_playback_midi_output.cpp):

- `test_erase_open_notes_missing_from_committed_note_ons_keeps_present_ids` — ledger `[A, B, C]`, committed NoteOns `{B, C}` → ledger `[B, C]`. Open identity whose On is present survives even if its Off is in the stream. Untagged Entry survives. `isFullLoopMergedPlaybackWindow` true only when start 0 and length equals loop length.

[`test_pending_note_change.cpp`](../../test/test_pending_note_change/test_pending_note_change.cpp):

- `test_occupy_leftover_off_at_exclusive_end_closes_wrap_head_not_ghost_104654` — LIFO persistence without reconcile: 5701 stays after Off@96 closes 5772.
- `test_erase_open_notes_missing_from_committed_note_ons_drops_5701_keeps_5772` — after reconcile, 5701 gone, 5772 still open; Off@96 then empties the lane.

Keep `test_occupy_clock_duplicate_off_closes_both_pre_abut_103234`. Do not change `test_playback_catch_up_window_differs_from_at_loop_start`.

---

## HITL

Want `n=1 a=0` down vs 19 on a new capture. Leftover `lid=5701` gone at `hs=96` and at interiors. Keep `led == n` and `n=0 a=1` = 0.

Do not FAIL this RC on nested `n=2 a=2` / `n=3 a=2` same-start pairs (L3910 On@240). Do not FAIL on parked span-start `n=0 a=1` or prepared `eq=0`/`b=0`.

---

## Pre-implementation review

### Ready
- `playCommittedLoopMidi` stale `reset(true)`, wrap `catchUpOverdubWrapPlaybackLedger`, `applyPlaybackEventBody`, mismatch `mmevt` filter traced.
- Pin leftover dumps read (L2668, L3779, L3179, L4580, L3910).
- Duplicate-Off apply not this FAIL (zero two-Off dumps).

### Resolved (user / code)
| Topic | Decision |
|-------|----------|
| Occupy | Still a reader; skip bound unchanged |
| Whole ledger clear | No — `n=0 a=1` |
| Option B / Off stamp / FIFO | No |
| `isPlaybackCatchUpWindow` | Unchanged this RC |
| Windowed gather | Do not erase against a partial window |
| Full-loop gate | `isFullLoopMergedPlaybackWindow` at every call site |
| Identity set | Retain iff NoteOn identity is in the rebuilt committed stream |
| Method name | `eraseOpenNotesMissingFromCommittedNoteOns` |
| Wrap order | rebuild → reconcile → wrap-pass apply |
| Reconcile on reuse | No — only after `ensurePlaybackMergedMidiEventsBuilt` actually rebuilt |

### Open before coding
None.

### Proceed?
YES.
