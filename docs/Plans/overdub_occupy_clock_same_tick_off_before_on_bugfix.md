# Occupy clock same-tick Off before On

**Status:** Native **PASS** 1358/1358. `teensy41-capture-serial` links (RAM1 code 425852, locals 4768). HITL gate open.  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent (catch-up two-pass shipped, gate not met):** [`overdub_occupy_same_tick_off_before_on_bugfix.md`](overdub_occupy_same_tick_off_before_on_bugfix.md)  
**Pin:** [`235314`](../../captures/session_20260818_235314.log)  
**Does not reopen:** catch-up two-pass; occupy repairing clock when `occupyPhase == lastTick`; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`; `applyPlaybackLedgerEvent` matching Off by `noteId`; `n=1 a=2`

---

## Invariant (one sentence)

Clock playback at equal phase applies Off before On so abutting same-pitch replacement last-writes the new On.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — occupy still reads `Entry.noteId`. Writer stays `applyPlaybackEvent` on committed playback. Clock still owns cursor, `nextEventIndex`, `lastTickInLoop`, send. |
| **State transition change?** | NO — no wrap/commit on USB. Catch-up skip bound unchanged (`occupyPhase <= lastTick` still skips). |
| **Reuse** | YES — Off-before-On at equal phase in [`rebuildPlaybackOrder`](../../src/Track/TrackPlaybackWindowBuild.cpp) after phase+tick sort, same keys as `NoteUtils::sortMidiEventsChronologically` (Off=0, On=1, other=2). `TRACK_INTERNAL_MEM` + `noinline`. No new helper. No scratch. Do **not** change `applyPlaybackLedgerEvent` / `ActiveNoteLedger::applyPlaybackEvent`. |

## Debugging boundary

```
… → USB occupy ledger catch-up (lastTick, occupyPhase] ← trust two-pass when it runs
 → clock equal-phase Off before On ← current investigation
 → unmatched Off vs overlapping same-pitch / L4294 ← parked
```

Do not make occupy catch-up run when `occupyPhase == lastTick`. That was rejected as occupy repairing clock.

## Root cause

Occupy ticks in [`235314`](../../captures/session_20260818_235314.log) sit on the 8-tick MIDI-clock grid. Wall time between pitch-12 occupies matches tick delta at ~120 BPM (ratios 0.993–1.004). [`Track::playCommittedLoopMidi`](../../src/Track/TrackPlaybackHotPath.cpp) sets `lastTickInLoop = tickInLoop` before returning. USB NoteOn then reads that same tick, so [`CommittedPlaybackLedgerCatchUp::shouldApply`](../../include/Utils/CommittedPlaybackLedgerCatchUp.h) is false (`occupyPhase <= lastTick`). Catch-up two-pass **does not run**. Ledger is whatever clock last-wrote.

Clock walks [`rebuildPlaybackOrder`](../../src/Track/TrackPlaybackWindowBuild.cpp): `std::sort` was by phase then tick only (not stable). [`advancePlaybackCursor`](../../src/Utils/PlaybackCursorAdvance.cpp) applies that order. [`ActiveNoteLedger::applyPlaybackEvent`](../../include/ActiveNoteLedger.h) Off clears by `(channel, pitch)` with no `noteId`. On then Off at equal tick → `n=0`.

Contrast in the same capture at occupy 240:

| Line | Prev pitch-12 | `as`–`ae` | Result |
|------|----------------|-----------|--------|
| L4517 | L4514 empty @ 48 | 240–384 | **`n=1 a=1`** — no competing Off@240 |
| L4899 | L4897 `48–240` `n=1` | 240–336 | **`n=0 a=1`** — Off@240 of 48–240 plus On@240 of 240–336 |

L4811 is the same abut: L4139 `528–624` then occupy `624–720` at 624.

USB-before-clock (8-tick catch-up window) was 11→4. Remaining span-start fails are USB-after-clock.

## Residuals (not this RC)

- **L4750** interior `192–288` @ 240: Off@240 of `144–240` (L4369) clears the continuing 192–288 note. No replacement On@240 in source-view (`a=1` names 192–288 only). Clock Off-before-On still last-writes empty. Unmatched Off vs overlapping same-pitch — later RC; would change Off identity, not sort order.
- **L4294** interior `240–384` @ 352 after wrap 7: catch-up skipped; no known Off in (240, 352]; same span occupies at span-start later (L4517). Not proven as equal-tick order.

## Fix

In `rebuildPlaybackOrder` (`TRACK_INTERNAL_MEM` + `noinline`, window-build TU): sort by phase then tick as before, then reorder equal-phase+tick groups Off before On (same keys as `NoteUtils::sortMidiEventsChronologically`). Type keys stay out of the `std::sort` lambda so the ITCM sort instantiation does not grow. Catch-up skip bound unchanged.

## Tests

Native in [`test_playback_midi_output.cpp`](../../test/test_playback_midi_output/test_playback_midi_output.cpp):

- `test_equal_phase_off_before_on_last_writes_new_on` — seed On@240 before Off@240 in the merged vector; after the same keys as `rebuildPlaybackOrder`, equal-phase Off index precedes On; apply that order through 240 → occupy `noteId` is the new On.

Native cannot compile `rebuildPlaybackOrder` (`TrackInternal.h` → `Track.h` → `Arduino.h`). The fixture uses the same keys (phase, tick, Off before On). Catch-up two-pass tests stay unchanged.

## HITL

Same 1-bar overdub. Gates: `n=0 a=1` = 0 and `n=1 a=0` = 0. Keep occupy 12 @ `hs=0`. Do not treat `n=1 a=2` as this FAIL. If only span-start abuts clear and interior L4750/L4294 remain, freeze this RC and open unmatched-Off separately — do not widen `applyPlaybackEvent` in this commit.
