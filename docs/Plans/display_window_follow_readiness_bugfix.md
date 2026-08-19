# Display window follow readiness

**Status:** Implemented — native **1390/1390**; firmware RAM1 code **425276** / locals **4768**; HITL gate open  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Evidence:** [`session_20260819_232337.log`](../../captures/session_20260819_232337.log)

**Does not authorize:** occupy/ledger/consume ownership changes; shrinking `kOverdubSourceWindowBars`; full-loop gather on each frame.

---

## Debugging boundary

```text
visualCache.notes window filter (display authority)
    ← this slice
overdubSourceViewNotes (consume / overlap only)
    ← unchanged
idle slice_clean (dirty-bar rebuild)
    ← still owns consume-region freshness
```

---

## Problem

On a 66-bar loop, after bar 16 the piano roll showed large gaps and lagged until idle `slice_clean`.

[`232337`](../../captures/session_20260819_232337.log): after each overdub stop, `DISP` `frameNotes` / `windowNotes` split (619/523, 667/552, 671/577) for 141–514 ms while `visualCacheDirty` was true. During overdub, `rebuildOverdubSourceView` kept a 16-bar source-view (`notes=649 bars=16`) and `resolveDisplayNotesLiveCapture` painted that list. Auto-follow then slid past it. `DisplayResolveLiveCapture` peaked at 9.3–10.5 ms against a 5 ms budget; `reuseLastValidFrame` froze the committed window.

## Invariant

Committed paint for a long-loop detailed window filters `visualCache.notes` for the paint window plus follow margin/lookahead. A globally dirty cache does not hold a 16-bar source-view frame as display authority. `overdubSourceViewNotes` stays consume ownership.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Reuse | YES — extend `resolveDisplayNotesCommitted`, `resolveDisplayNotesLiveCapture`, `resolveWindowedDisplayNotes`, `visualCacheCoversWindow` |

## Implementation

- `visualCacheCoversWindow` checks per-window clean bars and optional `kFollowReadyLookaheadBars`.
- Incremental committed resolve filters a non-empty `visualCache` (window + margin) even while dirty; handoff only when notes are empty.
- Overdub committed layer filters `visualCache.notes` first; source-view is fallback only.
- Over-budget compose still rebuilds the committed window (`committedWindowStale`); capture suffix may stay stale.

## Verification

- Native: `test_display_window_utils` neighborhood / lookahead / margin / newly-exposed-bar fixtures; `pio test -e native` **1390/1390**.
- Firmware: `teensy41-capture-serial` links (RAM1 code **425276**, locals **4768**).
- Capture check: `verify_follow_window_readiness` in `scripts/hitl/verify/display_window.py`. Evidence [`232337`](../../captures/session_20260819_232337.log) **fails** (holds 539 / 386 / 153 ms at window starts 5656, 10024, 12400). A post-fix 66-bar overdub-past-bar-16 capture must pass (no ≥120 ms stale hold).
