# Overdub paint — NoteOn pending handoff and stale-cache source guard

**Status:** Stage 1 and Stage 2 host shipped. Stage 3 Shorten source recovery is a sibling plan. Device recapture open.  
**Date:** 2026-09-05  
**Kind:** bugfix (display handoff / cache source identity)  
**Evidence:** [`202821`](../../captures/session_20260905_202821.log) Shorten only after stop; [`203406`](../../captures/session_20260905_203406.log) one-frame old passes / duplicate bars  
**Does not authorize:** resolver/ledger/storage changes; `invalidateVisualCache()` as the NoteOn fix; NoteOn sealing; new cache owner

---

## Debugging boundary

```text
resolveOccupancyAtNoteOn / pendingNoteChanges_   ← trust
ActiveNoteLedger                                 ← identity only
sealPendingNoteChangesToEditPasses               ← stop/wrap only
draw-source compose prefix + overlays            ← untransformed
next live paint copy                             ← Stage 1
visualCache append_resume source identity        ← Stage 2
```

## Stage 1 — pending-display-source handoff

**Invariant:**

```text
NoteOn-created pendingNoteChanges_ must advance the live-display
representation boundary for the next paint.

The next live paint must either:
  - consume the pending-painted representation, or
  - rebuild that live representation from the same authoritative
    capture + pending-change inputs.

It must not reuse a frame whose source predates the pending change.

This is a display handoff change only; it does not publish the
pending change into committed storage or VisualCache.
```

Owners: `resolveDisplayNotesByDrawSource`, overview density source selection in `drawPianoRoll`. Helpers: `shouldPaintPendingDrawSourceRepresentation`, `shouldUseCleanVisualCacheForOverview`.

## Stage 2 — incremental append only on proven current source

**Invariant:**

```text
source mismatch
    ↓
obtain authoritative current-source frame
    ↓
establish cache/source identity + revision
    ↓
only then permit incremental append
```

`applyDeferredVisualCacheWrapAppend` must not append an earlier pass onto a cache whose `builtFromRevision` / playback revision no longer matches the deferred source.

Owner: `Loop::applyDeferredVisualCacheWrapAppend`. Stores `deferredVisualCacheWrapAppendFromRevision_` when wrap-append is first deferred. Helpers: `shouldApplyDeferredVisualCacheWrapAppend`, `visualCacheIsCurrentSourceRevision`. Mismatch emits `VCACHE,append_skip` and rebuilds from current passes unless the cache already is the current-source revision.

## Stage 3 — pending Shorten source recovery

Sibling: [`overdub_paint_shorten_source_recovery_bugfix.md`](overdub_paint_shorten_source_recovery_bugfix.md). Recaptures after Stage 1+2 showed Hide can look correct from absence, while Shorten needs the committed source row. Do not reopen Stage 1 paint-copy or Stage 2 wrap-append.

## Host verification

- `teensy41-capture-serial` RAM1 locals **6848**
- Focused native: `test_display_window_utils`, `test_pending_note_change`, `test_overdub_source_view` PASS
- Full native **1660/1661** — pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end`

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |
| Reuse | **YES** — extend draw-source paint copy and existing wrap-append owner |
