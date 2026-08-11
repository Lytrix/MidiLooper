# Long-loop post-stop visualCache window paint

**Status:** Implemented — RC4d committed-window follow; HITL pending  

**Regression evidence:**
- RC4 sparse gaps after `c39eeb6` — RC4b (`3ea7b03`) fully-built-only filter.
- RC4c [`session_20260811_033336.log`](../../captures/session_20260811_033336.log): notes vanish for
  PLAYING→overdub, return after overdub stop. Overdub `windowCacheHit` resized committed layer
  to 0 because `liveDisplayCacheCommittedNoteCount_` stayed 0 after record-stop invalidate.
- RC4d user report / [`session_20260811_034230.log`](../../captures/session_20260811_034230.log)
  (boot-only CAP): PLAYING/overdub stuck on first ~18 bars; overdub window does not follow
  recorded pass until stop. `committedLayerChanged` ignored paint-window leave of gather range.  




## Architecture gate (session)

| Question | Answer |
|----------|--------|
| Owner module | `DisplayManager::resolveWindowedDisplayNotes` + `Loop::rebuildVisualCacheIdleSlice` |
| Primary invariant | Covered paint window filters `visualCache`; PLAYING idle slices stay near playhead |
| Ownership change? | NO |
| State transition change? | NO |
| Behavior-preserving? | NO for cost path only — same notes, cheaper source when covered |
| Reuse | YES — existing window filter + idle slice |
| Phase scope | RC4 only |

## Implementation result

- `visualCacheCoversWindow` in `VisualCache.h` — **fully built only** (`!visualCacheDirty`);
  partial dirtyBars neighborhoods must not authorize filter paint (RC4b).
- `resolveWindowedDisplayNotes` filters `visualCache` only when fully built; else gather.
- Overdub committed layer assigns full `visualCache` only when `!visualCacheDirty`; else
  window gather on long loops.
- PLAYING idle slices still limited to `kMaxDetailedWindowBars + 4` around playhead (tear
  mitigation without sparse-cache authority).
**Branch:** `bugfix/long-overdub-display-freeze`  
**Evidence:** [`session_20260811_030614.log`](../../captures/session_20260811_030614.log)  
**Parent:** [`long_overdub_display_freeze_bugfix.md`](long_overdub_display_freeze_bugfix.md)  
**Prior:** [`long_overdub_post_stop_display_handoff_bugfix.md`](long_overdub_post_stop_display_handoff_bugfix.md) (RC2)

## Problem

After a 195-bar record stop (`length=149760`), RC2 produced window-bounded frames (`frame=288`)
instead of the old capture-suffix supersize. OLED still tore/stuttered after stop.

Proven in `030614`:

- Post-record PLAYING for ~9 s with no mid-phase `#CAP` (`RING,overflow` at transport stop).
- Transport-stop `DISP`: `visual=3119`, `frame=288`, `wStart=0`, `wBars=16`, `wNotes=256`.
- Post-stop `DFRAME` ~12.2 ms at 288 notes.

Long-loop committed resolve always gathered/reconstructed the paint window even when idle
maintenance had already filled `visualCache`. PLAYING idle slices also wrapped the full loop
(`visual=3119`), competing with display.

## Invariant

When the paint window’s bars are already clean in `visualCache`, committed long-loop frames
must filter that cache (storage ticks) instead of gather+reconstruct. While PLAYING, idle
visual-cache slices stay in the playhead neighborhood; full-loop backfill waits until stopped.

## Architecture checkpoint

- **Owner:** `DisplayManager` (resolve) + `Loop` (`rebuildVisualCacheIdleSlice` budget).
- **Ownership change:** NO.
- **State-transition change:** NO.
- **Stop path:** No new sync rebuild in commit/stop.

## Scope

1. `visualCacheCoversWindow` helper + native fixture.
2. `resolveWindowedDisplayNotes` — when committed and window covered, filter `visualCache` by
   inclusion; gather only when uncovered/dirty or capture-active.
3. PLAYING idle slices — limit dirty-bar search to a neighborhood around `priorityBar`.
4. Docs / OpenSpec RC4 gate.

## Acceptance

- [x] Covered window filters `visualCache` before gather+reconstruct.
- [x] PLAYING idle slices stay within paint-window neighborhood.
- [x] Native: coverage fixture + `pio test -e native` (982/982).
- [x] `pio run -e teensy41-capture-serial`: SUCCESS.
- [ ] HITL: ~195-bar record stop — OLED paints without tear; `#CAP` can show mid-PLAYING frames.

## Out of scope

RC1/RC2/RC3, Stage 5 append pressure, capture-preview windowing.
