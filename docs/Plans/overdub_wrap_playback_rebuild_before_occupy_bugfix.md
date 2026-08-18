# Wrap rebuilds playback before occupy

**Status:** **FROZEN** — native PASS; HITL failed [`180844`](../../captures/session_20260818_180844.log) wrap 15. Wrong prerequisite: full-loop merged rebuild is not how wrap-tick ledger state is established. Superseded for this RC by [`overdub_wrap_committed_pass_playback_bugfix.md`](overdub_wrap_committed_pass_playback_bugfix.md).  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_present_at_tick_jit_enhancement.md`](overdub_present_at_tick_jit_enhancement.md)  
**Evidence:** [`152745`](../../captures/session_20260818_152745.log) wrap `n=0 a=1 b=1` pitch 71 @ 656  
**Does not start:** occupy fallback to prepared/`PresentNoteVec`; `length` on `Entry`; consume merge; wait-STOPPED-for-`lcr,mat`

---

## Invariant (one sentence)

After overdub wrap commit, `LoopPlaybackRuntime` merged playback and `ActiveNoteLedger` at `currentTick` include wrap-committed events at S **before** `snapshotOverlapHoldCandidates` can run.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy stays a reader of `Entry.noteId`. Wrap sealer stays `commitOverdubWrapAtSessionStart`. |
| **State transition change?** | YES — wrap rebuilds the merged playback stream on the same `playCommittedLoopMidi` call before USB occupy (user-approved). |

Reuse: YES — extend the wrap path in `Track::playCommittedLoopMidi`. `commitOverdubWrapAtSessionStart` stays the wrap sealer. No prepared/source-view fallback. Consume stays on `overdubSourceView`.

---

## What the log determines

[`session_20260818_152745.log`](../../captures/session_20260818_152745.log) pins the wrap `n=0 a=1 b=1` as **post-wrap content vs pre-wrap ledger**, not a random present-at-S mismatch.

Same session, same pitch **71**, same storage tick **656** (overdub S):

| When | CAP | Occupy |
|------|-----|--------|
| Third-session enter | `42834380` | `n=0 a=0 b=0 eq=1` — loop has no 71 at 656 |
| User 71 | MI on 656, off 720 | Capture span `656–720` (same-start included later) |
| Wrap | `44815415` `lcr,src,why=wrap,from=span,notes=5` | Source-view now has that 71 |
| Occupy 15.8 ms later | `44831234` | **`n=0 a=1 b=1 eq=1`** |
| Rebuild 3 ms after occupy | `44834150` `playback_build,2217,0,768,45` | First post-wrap merged build |

First wrap in the same capture is the opposite order and occupies: wrap `26345498` → `playback_build` `26377770` → occupy pitch 60 `n=1 a=1 b=1` `26442874`. That 60 was already in the **pre-wrap** loop at 528. The wrap-committed 71 at S was not.

Track 6 `midiChannel` is 7 (`TrackManager` `setMidiChannel(i + 1)`). This session has **no** NoteOn/NoteOff `MO` on ch 7 (only CC 123). Mute still writes the ledger; MO cannot be used as the ch-7 sounding trace.

---

## Root cause

In `playCommittedLoopMidi`: wrap ran **before** cursor advance, on the **old** `mergedEvents` reference. `commitCapturePass` does `++playbackRevision`. The new 71 NoteOn at 656 is not in that old stream.

Next tick: `prev=656`, `current=657`. `didPlaybackEventCross` is `prev < ev && ev <= current`, so **ev=656 is never crossed**. The wrap-committed same-start NoteOn is skipped until the next full pass through S.

USB occupy in that gap is exactly `from=ledger,n=0` with `a=1,b=1`.

Do **not** `reanchorPlaybackIndex` with `lastTickInLoop` already equal to S (that skips `evPhase <= S`, including the new NoteOn).

---

## Fix

`maybeCommitOverdubWrap` / `commitOverdubWrapAtSessionStart` return whether a pass was committed (`CommitResult::Committed`). Empty capture wrap is not a committed pass.

After a wrap that committed, on this same `playCommittedLoopMidi` call, **before** `lastTickInLoop = tick`:

1. `ensurePlaybackMergedMidiEventsBuilt` for the new `playbackRevision`
2. `rebuildPlaybackOrder` + `reanchorPlaybackIndex` while `lastTickInLoop` is still **prev**
3. Then set `lastTickInLoop` and `advancePlaybackCursor` on the **new** stream for `(prev, tick]` so the NoteOn at S is `applyPlaybackLedgerEvent` then `sendMidiEvent`

---

## Tests

- Native: wrap-commit a note that **starts at S** (71 @ 656–720, 1-bar 768 from [`152745`](../../captures/session_20260818_152745.log)), then occupy at S via `collectOverdubNoteOnParticipantIds` against the slot ledger → that `noteId` is present. Post-wrap `playback_build` is not required to wait a tick: the wrap tick applies `(prev, S]` on the new stream.
- `test_wrap_committed_note_at_s_occupies_ledger_same_tick` (`test_pending_note_change`)
- `test_wrap_committed_note_at_s_crosses_when_reanchored_at_prev` (`test_playback_midi_output`)
- `pio test -e native` before commit.

---

## HITL

After flash: 1-bar overdub, play a note that **starts at S**, wrap, play that pitch again at S. Expect `from=ledger,n=1,a=1,b=1` not [`44831234`](../../captures/session_20260818_152745.log) `n=0 a=1 b=1`.
