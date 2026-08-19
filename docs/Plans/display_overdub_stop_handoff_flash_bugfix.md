# Display overdub-stop handoff flash

**Status:** Implemented — native **1391/1391**; RAM1 code **425276** / locals **4768**; HITL **PASS** [`001925`](../../captures/session_20260820_001925.log)
**Date:** 2026-08-20  
**Kind:** bugfix  
**Evidence:** [`session_20260820_000553.log`](../../captures/session_20260820_000553.log)

**Does not authorize:** occupy/ledger/consume changes; reopening follow-readiness source-view authority.

---

## Debugging boundary

```text
refreshViewportAfterOverdubStop composed frame (RC5a)
    ← this slice
visualCache window filter while dirty (follow readiness)
    ← must not overwrite RC5a until slice_clean
overdubSourceViewNotes
    ← unchanged
```

---

## Problem

Overdub stop paints one frame without the just-overdubbed notes, then they return after idle `slice_clean`.

[`000553`](../../captures/session_20260820_000553.log) `OVERDUBBING -> PLAYING` at 83396171: last overdub `DISP` is `frame=655` `vis=2166`; first PLAYING is `frame=583` `vis=2166` (same cache size, 72 notes gone). After `slice_clean` (+426 ms) `vis=2220` `frame=622`.

Follow-readiness made incremental committed resolve filter a nonempty dirty `visualCache` before the RC5a handoff. Long-loop stop keeps the loop-wide cache (`refreshViewportAfterOverdubStop` does not `adopt_partial`), so that filter replaces the composed capture suffix with pre-commit cache notes.

## Invariant

While `visualCacheDirty` after overdub stop, the revision-matched composed frame stays paint authority. Follow re-filters that frame to the moving window. A clean cache may filter `visualCache.notes` again.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Reuse | YES — extend `resolveDisplayNotesCommitted` / `preferPreservedOverdubStopHandoff` |

## Implementation

- `preferPreservedOverdubStopHandoff` is true for incremental + preserved handoff + dirty cache.
- That path re-filters `liveDisplayNotes` with `filterNotesToFollowWindow` (does not read `visualCache`).
- Dirty-cache `visualCache` filter remains for frames with no handoff authority.

## Verification

- Native: `test_prefer_preserved_overdub_stop_handoff_while_cache_dirty`; `pio test -e native` **1391/1391**.
- Firmware: `teensy41-capture-serial` RAM1 code **425276** / locals **4768**.
- HITL: [`001925`](../../captures/session_20260820_001925.log) **PASS**. Six `OVERDUBBING -> PLAYING` stops: first PLAYING `frameNotes` stays at or above the last overdub frame while `visual` is unchanged (e.g. 864→874, 885→887, 891→894). No `000553`-style 72-note drop at same `vis` then recover after `slice_clean`. Follow-readiness still 0 stale holds.
