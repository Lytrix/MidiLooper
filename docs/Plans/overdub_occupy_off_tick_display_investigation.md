# Occupy `n=0 a=1` off tick 0 — display vs ledger

**Status:** Investigation + native pin **PASS**. Device catch-up **FAIL** [`214856`](../../captures/session_20260818_214856.log) — production `playMidiEvents` from occupy **reverted**. `as=`/`ae=` stay.  
**Date:** 2026-08-18  
**Kind:** investigation  
**Parent:** [`overdub_present_at_tick_jit_architecture.md`](overdub_present_at_tick_jit_architecture.md)  
**Pin:** [`203948`](../../captures/session_20260818_203948.log) occupy `204277855` pitch **12** @ storage **96** `n=0 a=1 b=1`  
**Does not reopen:** wrap-S `(prev, S]` (HITL PASS [`185831`](../../captures/session_20260818_185831.log)); loop-head Q16 skip (HITL PASS 203948 @ 0). No occupy fallback. No `collectOverdubNoteOnParticipantIds` wrap logic. No DisplayManager consume patch.

---

## Question

When occupy is `n=0 a=1` **off** storage 0, is the piano-roll symptom a display-owner bug, or source-view paint of a note `ActiveNoteLedger` does not hold?

Occupy stays a reader of `Entry.noteId`. Do not patch paint to hide from empty occupy.

---

## Display owner (proven)

Paint during OVERDUBBING is not occupy and not `visualCache`.

```text
overdubSourceViewNotes  ──► resolveDisplayNotesLiveCapture ──► piano roll
capturePreview          ──►   (committed prefix + live suffix)
pending Hide/Shorten    ──►   paint copy at NoteOff only (RC10)

ActiveNoteLedger ──► collectOverdubNoteOnParticipantIds ──► overlapNoteIds
```

| Layer | Owner | 203948 wrap 2 |
|-------|--------|----------------|
| Painted committed notes | `DisplayManager::resolveDisplayNotesLiveCapture` assigns `overdubSourceViewNotes()` | `a=1` / `b=1` / `eq=1` — pitch 12 is in source-view and prepared at 96 |
| Live overdub note | `capturePreview` + playhead tails | USB On 12 @ 96 (`204277629`) |
| Hide/Shorten on roll | `Loop::applyPendingNoteChangesToDisplayNotes` on a paint copy, at **NoteOff** ([`overdub_overlap_hold_live_pending_display_bugfix.md`](overdub_overlap_hold_live_pending_display_bugfix.md) RC10) | Empty occupy still walks source-view (`emptySets`) |
| Occupy | `Loop::collectOverdubNoteOnParticipantIds` | `n=0` — ledger slot empty |
| `DISP` `visualNotes=12` vs `frameNotes=19` | `SC_DISP` logs `visualCache.notes.size()` vs painted `frameNotes` | Cache lag after wrap. Overdub paint uses source-view. RC-W1 already invalidates live cache on wrap |

**What the user can see:** source-view still draws the occupied-lane note (`a=1`) while occupy has no id (`n=0`). Until NoteOff, RC10 also leaves that note plus the new preview on the roll. Occupy is not a paint input.

**Do not fix in DisplayManager.** Hiding from source-view when ledger is empty would be a second occupy resolver.

203948 has no `DNTE` lines. Stop `overlap_hold` (`233304432`) is session totals (`empty_sets=11`); `add=4 hide=4` is the last pending list, not wrap-2.

---

## Wrap-2 clock (not tick 0, not wrap S)

Every overdub wrap in 203948 is at storage **64** (`Overdub wrap committed @ tick 8512` → `8512 % 768 = 64`). Occupy 12 @ **96** is **32 ticks after S**. `playback_build` (`204230461`) finished **before** occupy (`204277855`).

Later same-shape misses: 12 @ **384**, 23 @ **432**.

Occupy DIAG in 203948 has no DisplayNote `startTick`/`endTick`. Span is not named from that log.

---

## Observability

`Track::snapshotOverlapHoldCandidates` occupy line adds `as=` / `ae=` (first source-view note of that pitch present at hold, else 0) and `hs=` (phased `pending.startNoteTick`). Same `lcr,part` line. No new occupy owner.

---

## Native pin (named write)

Session start **64**, 1-bar 768, occupy at **96**, source-view present (`a=1`):

| Fixture | Stream | Walk before occupy | Occupy |
|---------|--------|--------------------|--------|
| On@96 Off@200 | wrap-pass chunks | reanchor at 64 only — **no** `(64, 96]` | `n=0` |
| On@96 Off@200 | wrap-pass chunks | reanchor at 64, then `advancePlaybackCursor` `(64, 96]` | `n=1` |
| On@0 Off@200 | wrap-pass chunks | `atLoopStart` `(760, 0]`, then wrap `(56, 64]` | `n=1` at 96 |

**Named missing write when occupy is 32 ticks after S and the source note is On@96:** `playCommittedLoopMidi` → `advancePlaybackCursor` **`(S, occupyTick]`** after wrap reanchor at S. Wrap-S `(prev, S]` does not apply On@96. Loop-head `(760, 0]` does not apply On@96.

Spanning On@0 at 96 is **not** this miss once the 0-clock walk has written the Entry (ledger preserved across wrap `reset(true)`).

Tests: `test_wrap_pass_on_at_96_occupies_after_s_interval`, `test_wrap_pass_spanning_on_at_0_occupies_at_96` in [`test_pending_note_change.cpp`](../../test/test_pending_note_change/test_pending_note_change.cpp).

---

## HITL FAIL [`214856`](../../captures/session_20260818_214856.log)

`as=`/`ae=` names the painted source-view span. Five occupies are `n=0 a=1` and **not** wrap-committed On@96 after S:

| CAP | Pitch | `as`–`ae` |
|-----|-------|-----------|
| `98573816` | 30 | 528–544 |
| `107573372` | 12 | 0–192 |
| `109577175` | 12 | 144–232 |
| `111112659` | 12 | 720–767 |
| `122149713` | 24 | 216–408 |

Wraps this run are at storage **8** (`1544 % 768`). Occupy totals: 82; `n=1 a=1` = 48; `n=0 a=0` = 22; `n=0 a=1` = 5; `n=1 a=0` = 5.

Calling `playMidiEvents` from occupy when `lastTickInLoop < occupyPhase` can cross session start and seal a wrap on USB NoteOn. Reverted. Writer stays clock `playCommittedLoopMidi`.

Successor: [`overdub_occupy_capture_stream_ledger_bugfix.md`](overdub_occupy_capture_stream_ledger_bugfix.md) (pin [`221334`](../../captures/session_20260818_221334.log)) — capture emit does not write occupy’s ledger.

---

## Hard don'ts

- Do not call `playMidiEvents` from `snapshotOverlapHoldCandidates`.
- Do not teach `collectOverdubNoteOnParticipantIds` about loop heads, wrap, or source-view.
- Do not apply pending Hide at NoteOn from occupy IDs.
- Do not treat full-loop `playback_build` as an occupy prerequisite.
- Do not treat `visualCache` notes=12 as the overdub piano-roll bug.
