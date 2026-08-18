# Loop-head playback ledger after wrap

**Status:** Native **PASS** 1348/1348. HITL open (same 1-bar overdub as [`185831`](../../captures/session_20260818_185831.log)).  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_present_at_tick_jit_architecture.md`](overdub_present_at_tick_jit_architecture.md)  
**Investigation:** [`overdub_loop_head_playback_ledger_investigation.md`](overdub_loop_head_playback_ledger_investigation.md)  
**Does not reopen:** [`overdub_wrap_committed_pass_playback_bugfix.md`](overdub_wrap_committed_pass_playback_bugfix.md) (HITL PASS), occupy fallback, `collectOverdubNoteOnParticipantIds`, wrap-S `(prev, S]`

---

## Invariant (one sentence)

Overdub wrap seal does not remove completed pairs shorter than `noteMinLengthTicks`. Those events stay in `lastCommittedPassId()` so the existing loop-head `atLoopStart` walk can apply phase-0 NoteOns to `ActiveNoteLedger` before occupy.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Writer stays `playbackCursorAdvanceSend` → `applyPlaybackLedgerEvent`. |
| **State transition change?** | NO — wrap still commits via `commitOverdubWrapAtSessionStart` / `commitCapturePass(OverdubWrap)`. Q16 min-length stays hot-stop only. |
| **Reuse** | YES — extend `Loop::sealCapture` with `CommitReason`. Do not add a loop-head catch-up owner. |

## Pin

[`185831`](../../captures/session_20260818_185831.log) wrap 1: occupy 60 @ storage **0** `n=0 a=1 b=1` (`54243271`). Same pitch @ **64** `n=1`. Wrap-S 71 @ 704 stays `n=1`.

Named missing write: wrap-committed 60 On@0 Off@8. Isolated 0-clock cursor math **PASS** once that pair is in the stream (`test_wrap_pass_events_at_loop_head_occupy_without_merged_rebuild`). Production stream lacked On@0 because `sealCapture` ran `removePairsShorterThanNoteMinLength` on OverdubWrap (span 8 < 12).

Q16 is **hot stop** ([`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md); [`capture_pass_note_min_length_refinement.md`](capture_pass_note_min_length_refinement.md)). Wrap is not stop.

## Fix

In `Loop::sealCapture`: skip `removePairsShorterThanNoteMinLength` when `reason == CommitReason::OverdubWrap`. Record/overdub stop still remove short pairs.

Do **not** change wrap-S `(prev, S]`, apply On@0 at wrap S, or teach occupy about loop heads.

## Tests

- `test_wrap_pass_events_at_loop_head_occupy_without_merged_rebuild` (185831: On@0 Off@8, reanchor at 696, `atLoopStart` `(760, 0]`, occupy 60). Default min-length enabled.
- `test_overdub_stop_still_removes_pairs_shorter_than_min_length` — OverdubStop still drops On@0 Off@8 and keeps On@64 Off@416.
- `pio test -e native` 1348/1348.

## HITL

Same 1-bar overdub as [`185831`](../../captures/session_20260818_185831.log). Gate: wrap-S 71 @ 704 still `n=1`; first loop-head 60 @ 0 `n=1 a=1 b=1`; 60 @ 64 still `n=1`.
