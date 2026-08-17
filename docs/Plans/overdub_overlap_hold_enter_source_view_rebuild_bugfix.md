# Overdub enter source-view rebuild (RC7)

**Status:** Native in this commit; device gate open  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Parent:** [`overdub_overlap_hold_same_start_bugfix.md`](overdub_overlap_hold_same_start_bugfix.md)  
**Evidence:** [`113236`](../../captures/session_20260817_113236.log) — enter copied 1-note visual cache; wrap rebuild was first complete source view

---

## Invariant

Overdub enter fills `overdubSourceViewNotes_` from prepared window (else per-pass `resolveWindow`), not from visual cache.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop` still owns the source view. |
| **State transition change?** | NO. Session start is still `beginCapture(Overdub)` → `establishOverdubSourceView`. |

## Fix

`establishOverdubSourceView` resets overlap-hold totals, calls `rebuildOverdubSourceView(..., "open")`, then clears pending. Same prepared / `resolveWindow(passes)` path as wrap. Not visual cache. Not `copyEffectiveCommittedEventsInRange`. Wrap `beginCapture` still keeps an already-established view (RC2). Consume unchanged.

## CAP tokens

| Token | Meaning |
|-------|---------|
| `DIAG,lcr,src,why=open,from=prep\|win` | Session-start rebuild |
| `DIAG,lcr,src,why=wrap,from=prep\|win` | Wrap rebuild (RC6) |
| `DIAG,lcr,vch` | Idle visual cache (display only) |

## Tests

- `test_overdub_enter_rebuilds_source_view_not_visual_cache` — 1-note clean cache, two committed notes; enter source view has both.
- Existing wrap rebuild fixtures stay green.

## Device gate

1-bar occupied lane. Enter overdub from PLAYING/STOPPED without a full visual cache. First notes: `looked_up > 0` and Hide or Shorten. `src,why=open,from=prep|win` present. Wrap `beginCapture` must not emit `why=open`. 1-wrap [`005745`](../../captures/session_20260817_005745.log) stays green.
