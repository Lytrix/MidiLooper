# Overdub paint — wrap NoteOn Shorten recovery without VisualCache identity

**Status:** Host in tree. Device recapture required.  
**Date:** 2026-09-05  
**Kind:** bugfix (draw-source pending-Shorten recovery)  
**Evidence:** [`223435`](../../captures/session_20260905_223435.log) post-wrap1 `lcr,part` `as=1536,ae=2208,hs=1824` / `hs=1872` with no `painted_pending_shorten`; later mid-lap `as=2304,ae=2976,hs=2592` paints id 72; stop `oh shorten=2`  
**Does not authorize:** resolver/ledger/storage changes; VisualCache mutation on NoteOn; NoteOn sealing; Hide recovery; pitch+start transform matching

---

## Debugging boundary

```text
resolveOccupancyAtNoteOn / pendingNoteChanges_   ← trust (`223435` a=1)
recoverMissingPendingShortenSources              ← this slice
classifyPendingShortenSource                     ← authoritative-first
VisualCache identity                             ← absence evidence only
paint copy + applyPendingHideAndShortenToNotes   ← NoteId match
```

## Root cause

Live paint applies pending Shorten by `NoteId`. After wrap, draw-source
compose can drop that identity while VisualCache is dirty and also lacks
it. `classifyPendingShortenSource` treated `!visualCacheHasSameIdentity`
as `NotNeeded`, so recovery never restored the source-view / snapshot
row. Pending existed; first NoteOn paint could not shorten until stop
seal/adopt.

## Invariant

```text
compose missing Shorten target + authoritative row exists
    → recover committed geometry (VisualCache optional)
compose missing + VisualCache has identity + authoritative absent
    → keep absent (authoritative_absence)
compose has target
    → NotNeeded
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — extend `classifyPendingShortenSource` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same per-pending-change recovery |
| Reuse | **YES** — `recoverMissingPendingShortenSources` stays the single owner |

## Host pins

- `test_pending_shorten_source_classify_and_recover_212700` — compose miss + visual miss + authoritative hit recovers
- `test_wrap_noteon_shorten_recovers_without_visual_identity_223435` — NoteOn at `hs=1824` paints `1536–1823` before seal
- Focused: `test_display_window_utils`, `test_pending_note_change`, `test_overdub_source_view` **PASS**
- `teensy41-capture-serial` RAM1 locals **6848**
- `pio test -e native` **1668/1671** — pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end`

## Device recapture

First post-wrap Shorten hold (`as≠hs`) must emit `painted_pending_shorten`
before the next `BAR`. Stop `oh shorten` is not the live-paint gate.

Parent: [`overdub_paint_noteon_shorten_paint_bugfix.md`](overdub_paint_noteon_shorten_paint_bugfix.md).
