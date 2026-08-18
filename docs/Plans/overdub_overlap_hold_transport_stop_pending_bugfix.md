# Overdub transport-stop pending close (RC9)

**Status:** Native in this commit; device gate open  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Parent:** [`overdub_overlap_hold_same_start_bugfix.md`](overdub_overlap_hold_same_start_bugfix.md)  
**Evidence:** [`122152`](../../captures/session_20260817_122152.log) — extra 60@64 length 104 (`SEVT F,168`); `Stop finalize pending: finalized=0 … phase=168`; `Seal wrap synth offs: count=1`

---

## Invariant

Overdub transport stop finalizes held notes (capture Off + overlap accumulate) before `sendAllNotesOff()` clears `pendingNotes`.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Track::finalizePendingNotes` / `stopOverdubbingToStopped` still own stop close. |
| **State transition change?** | NO. Transport stop is still OVERDUBBING → STOPPED. |

## Fix

Record already stops before `sendAllNotesOff()`. Overdub did the opposite (`handleTransportStop` and `stopOverdubbingToStopped`). `sendAllNotesOff()` clears `pendingNotes`, so a held 60 never got a performer Off. On a 1-bar loop the wrap window is the whole loop, so `finalizeCaptureWrapWindowAtStop` wrote a synth Off at the stop phase (168).

Order is now finalize/commit, then silence. `finalizePendingNotes` also calls `accumulatePendingNoteChangesForIncomingNote` after appending the Off, same as MIDI note-off.

## Device gate

1-bar occupied lane. Second overdub 60 at 64, transport stop while held: `finalized>0`, `wrap_synth=0` (or Off already in capture so wrap inserts 0). No stacked 60@64 length 104 plus 176. Consume Hide/Shorten of the earlier 64–240. [`005745`](../../captures/session_20260817_005745.log) 1-wrap stays green.
