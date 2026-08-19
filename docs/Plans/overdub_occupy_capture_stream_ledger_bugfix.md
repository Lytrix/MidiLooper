# Occupy capture-stream ledger last-writer

**Status:** FROZEN — emit-only capture walk shipped; remaining writer was capture folded into committed-only `mergedMidiEvents`. Successor: [`overdub_occupy_merged_capture_ledger_bugfix.md`](overdub_occupy_merged_capture_ledger_bugfix.md).  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_present_at_tick_jit_enhancement.md`](overdub_present_at_tick_jit_enhancement.md)  
**Pin:** [`221334`](../../captures/session_20260818_221334.log)  
**HITL FAIL:** [`224719`](../../captures/session_20260818_224719.log)  
**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; USB occupy `playMidiEvents` catch-up (reverted [`214856`](../../captures/session_20260818_214856.log)); occupy fallback; DisplayManager consume

**Misattribution guard:** A later `n=1 a=0` after this freeze is not a regression of `playbackCursorAdvanceSendCapture`. Investigate whether live capture was folded into `mergedMidiEvents` ([`overdub_occupy_merged_capture_ledger_bugfix.md`](overdub_occupy_merged_capture_ledger_bugfix.md)). A later `n=0 a=1` with committed-only gather is USB occupy vs clock interval ([`overdub_occupy_on_tick_clock_catchup_bugfix.md`](overdub_occupy_on_tick_clock_catchup_bugfix.md)).

---

## Invariant (one sentence)

Occupy reads `ActiveNoteLedger` Entries written from **committed** playback only. Live capture echo must not `applyPlaybackLedgerEvent`.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Writer stays `playCommittedLoopMidi` → `applyPlaybackLedgerEvent` on merged / wrap-pass / loop-head. Capture already owns MIDI echo via `sendMidiEvent`. |
| **State transition change?** | NO — no wrap/commit on USB NoteOn. Occupy stays `snapshotOverlapHoldCandidates` only. |
| **Reuse** | YES — `playbackCursorAdvanceSendCapture` next to `playbackCursorAdvanceSend` in [`TrackPlaybackHotPath.cpp`](../../src/Track/TrackPlaybackHotPath.cpp). No occupy fallback. No DisplayManager patch. |

## Root cause

Occupy reads `ActiveNoteLedger` at USB NoteOn. Source-view `a=` is committed geometry at that hold. Those disagree in [`221334`](../../captures/session_20260818_221334.log) because `playCommittedLoopMidi` applied the live capture stream through `playbackCursorAdvanceSend` (ledger then emit).

Capture Off of pitch P **clears** the committed `Entry` for P (`n=0 a=1`). Capture On of P **leaves** an `Entry` when source-view has no P (`n=1 a=0`).

151 occupy lines. Closed gates hold (wrap-S every 768 at storage 240; loop-head @ 0 `n=1 a=1`; 12 `as=96–192` `n=1 a=1`).

| Kind | Count | Meaning |
|------|------:|---------|
| `n=1 a=1` | 97 | match |
| `n=0 a=0` | 38 | empty lane |
| **`n=0 a=1`** | **10** | ledger empty, committed span present |
| **`n=1 a=0`** | **5** | ledger occupied, no source-view span (`as=0 ae=0`) |

This is **not** wrap-S `(prev, S]`, not loop-head Q16, not On@96 after S.

## Fix

In `playCommittedLoopMidi`, the `ActiveCaptureOverdub` walk emits with `playbackCursorAdvanceSendCapture`:

- `sendMidiEvent` (live echo and `collectOverlapHoldPlaybackNoteOn`)
- no `applyPlaybackLedgerEvent`

Merged / wrap-pass / loop-head walks keep `playbackCursorAdvanceSend` (ledger then emit).

Orphan capture Off still emits (the matching capture On was already sent). Duplicate Off on a later committed Off is harmless.

Do **not**: teach `collectOverdubNoteOnParticipantIds` about wrap or source-view; apply pending Hide at NoteOn; two ledgers; `playMidiEvents` from occupy.

Occupy CAP adds `hs=` (phased `pending.startNoteTick`). Keep `as=` / `ae=`.

## Tests

- `test_capture_off_does_not_clear_committed_occupy` — committed On@384 Off@480 pitch 23; capture Off@400; occupy at 420 `n=1`. Applying capture Off would be `n=0`.
- `test_capture_on_does_not_occupy_empty_source_view` — source-view empty at hold 45; capture On must not occupy. Applying capture On would be `n=1 a=0`.
- `pio test -e native` 1352/1352.

## HITL

**FAIL** [`224719`](../../captures/session_20260818_224719.log): 1-bar 768 OVERDUBBING; wrap storage **256** (not 221334’s 240). Occupy CAP `hs=` on all **109** lines; `hs=` equals COORD `storage` on every occupy that has a COORD (50/50). `RING,overflow` after the last occupy.

| Kind | Count | Gate |
|------|------:|------|
| `n=1 a=1` | 62 | match |
| `n=0 a=0` | 28 | empty lane |
| **`n=0 a=1`** | **8** | **FAIL** (want 0) |
| **`n=1 a=0`** | **6** | **FAIL** (want 0) |
| `n=1 a=2` | 5 | one ledger slot vs two source-view ids (not this gate) |

Closed pins this run: occupy 12 @ `hs=0` `n=1 a=1` (`9919`, `10020`). Occupy 12 @ `hs=96` is `n=0 a=0` (empty lane; 221334’s `as=96–192` is not in this take). Occupy 12 @ `hs=240` `n=1 a=1` (`9843`, `10378`).

`n=0 a=1` interiors (not On@96 after S): 24 `as=264–359` `hs=288`; 24 `120–312` `hs=264`; 30 `672–767` `hs=720`; 30 `48–144` `hs=48`; 30 `336–432` `hs=336`; 12 `336–431` `hs=352`; 24 `552–744` `hs=576`; 24 `264–359` `hs=336`.

`n=1 a=0` leftovers all `as=0 ae=0`: 24 @ `hs=72` twice (`9784`, `11967`); 12 @ `720` / `48` / `720` / `144`.

Capture emit-only is on this firmware (`hs=` is the same occupy CAP as this commit). Device still disagrees `n` vs `a`. Do not reopen wrap-S `(prev, S]` or USB `playMidiEvents`. Do not treat `n=1 a=2` as this FAIL.
