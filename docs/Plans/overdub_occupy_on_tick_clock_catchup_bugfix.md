# Occupy on-tick before clock interval (ledger catch-up)

**Status:** Native **PASS** 1355/1355. `teensy41-capture-serial` links (RAM1 code 425852, locals 4768). HITL pending same 1-bar overdub as [`231038`](../../captures/session_20260818_231038.log).  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent (frozen leftover met):** [`overdub_occupy_merged_capture_ledger_bugfix.md`](overdub_occupy_merged_capture_ledger_bugfix.md)  
**Pin:** [`231038`](../../captures/session_20260818_231038.log)  
**Does not reopen:** wrap-S `(prev, S]` clock walk; loop-head Q16; `playMidiEvents` from occupy ([`214856`](../../captures/session_20260818_214856.log)); occupy fallback; DisplayManager consume; folding capture into `mergedMidiEvents`; `n=1 a=2` as this gate; mutating `nextEventIndex` / `lastTickInLoop` from USB

---

## Invariant (one sentence)

USB occupy may advance the committed `ActiveNoteLedger` to the USB phase; it must not advance playback (no send, cursor, `nextEventIndex`, `lastTickInLoop`, wrap, capture, or merged rebuild).

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId` after catch-up. Writer stays `applyPlaybackLedgerEvent` on committed-only `mergedMidiEvents`. |
| **State transition change?** | NO — do **not** call `playMidiEvents` / `maybeCommitOverdubWrap`. Clock owns cursor, `nextEventIndex`, and `lastTickInLoop`. |
| **Reuse** | YES — `catchUpCommittedPlaybackLedgerToPhase` in [`TrackCaptureInput.cpp`](../../src/Track/TrackCaptureInput.cpp) (`TRACK_COLD_MEM` / `noinline`, same RAM1 rule as wrap-tick catch-up). Interval `(lastTickInLoop, occupyPhase]` via `didPlaybackEventCross(false, lastTick, evPhase, occupyPhase)`. Do not put this in [`TrackPlaybackHotPath.cpp`](../../src/Track/TrackPlaybackHotPath.cpp) ITCM. No occupy fallback. No DisplayManager patch. |

## Debugging boundary

```
… → ensurePlaybackMergedMidiEventsBuilt committed-only gather ← trust
 → USB occupy ledger catch-up (lastTick, occupyPhase] ← current investigation
```

Do not fold live capture into `mergedMidiEvents` again. Do not call `playMidiEvents` from occupy. Do not rebuild merged from USB. Do not set `lastTickInLoop` or `nextEventIndex` from USB.

## Root cause

Occupy reads the ledger at USB NoteOn (`snapshotOverlapHoldCandidates`). Source-view `a=` uses `displayNotePresentAtHold` (`linearStart <= s && s < linearEnd`) — inclusive start. Clock playback applies events with `didPlaybackEventCross`: `(prevTickInLoop, tickInLoop]`.

MIDI Clock and NoteOn stay FIFO ([`MidiDispatchOrder`](../../include/Utils/MidiDispatchOrder.h)). `onMidiClockPulse` advances 8 ticks then `playMidiEvents`. USB NoteOn uses `clockManager.getCurrentTick()` after prior clocks in that batch, then occupy.

If NoteOn is dispatched **before** the clock pulse whose interval includes the committed On, occupy at span start sees `n=0` while `a=1`.

Proof this is order, not a missing On:

- Pitch 12 occupy at span start (`hs == as`): 12 match (`n=1 a=1`) and 5 miss (`n=0 a=1`). Same shape both ways.
- L5241 occupy 12 @ 192 (`as=192–280`) us `65168812`; playback `MO,144,4,12,100` is later at `65234756`.
- Five misses occupy at start (240, 432, 240, 192, 96). One interior (384 in 336–432) is the same class if `lastTickInLoop` is still below the On at 336.

[`231038`](../../captures/session_20260818_231038.log) leftover after committed-only gather:

| Kind | Count | Gate |
|------|------:|------|
| `n=1 a=1` | 30 | match |
| `n=0 a=0` | 49 | empty lane |
| **`n=0 a=1`** | **6** | **this RC** (all pitch 12, `b=1`) |
| **`n=1 a=0`** | **0** | parent leftover **met** |
| `n=1 a=2` | 1 | product; not this gate |

Closed pins still hold: occupy 12 @ `hs=0` `n=1 a=1`. `RING,overflow` is observability. Do not treat `hs` vs COORD mismatch as this FAIL (parser false positive).

## This is ledger catch-up, not playback catch-up

```text
clock cursor/state ─────── untouched
event emission ─────────── untouched
wrap/commit ────────────── untouched

ledger
  └── replay committed events only
```

Helper [`CommittedPlaybackLedgerCatchUp`](../../include/Utils/CommittedPlaybackLedgerCatchUp.h):

- Input: existing committed-only `mergedMidiEvents` (never rebuild), `lastTickInLoop`, `occupyPhase`
- Effect: mutate `ActiveNoteLedger` only
- Must NOT: send MIDI; mutate cursor / `nextEventIndex` / `lastTickInLoop`; commit wrap; modify capture; call `ensurePlaybackMergedMidiEventsBuilt` or `playMidiEvents`

Skip when: `lastTickInLoop == UINT32_MAX`, `occupyPhase <= lastTickInLoop` (exclusive lower bound), or `shouldCommitOverdubWrap(lastTickInLoop, occupyPhase)` (214856 — USB must not cross wrap).

`snapshotOverlapHoldCandidates` resolves occupy phase the same way CAP `hs=` does (`tickPhaseInLoop(pending.startNoteTick, 0, loopLength)`), then catch-up, then occupy lookup. Still no `playMidiEvents`.

## Tests

Native in [`test_pending_note_change.cpp`](../../test/test_pending_note_change/test_pending_note_change.cpp):

- `test_occupy_ledger_catchup_on_at_192_after_last_tick_184` — L5241: lastTick 184, On@192 Off@280 pitch 12, occupy 192. Without catch-up `n=0`; `(184, 192]` → `n=1`. `lastTickInLoop` and `nextEventIndex` unchanged.
- `test_occupy_ledger_catchup_exclusive_when_occupy_equals_last_tick` — lastTick 192, On@192, occupy 192 → do not reapply.
- `test_occupy_ledger_catchup_skips_wrap_crossing` — lastTick 760, occupy 10, wrap armed → `shouldApply` false, capture not sealed, cursor unchanged.

Keep existing wrap-S / loop-head / merged-capture gather fixtures.

## HITL

Same 1-bar overdub as 231038. Gates: `n=0 a=1` = 0 and `n=1 a=0` = 0. Keep occupy 12 @ `hs=0`. Do not treat `n=1 a=2` as this FAIL.
