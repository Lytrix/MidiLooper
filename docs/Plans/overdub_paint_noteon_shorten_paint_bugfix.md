# Overdub paint — NoteOn Shorten on the first paint

**Status:** Host in tree. Device recapture open.  
**Date:** 2026-09-05  
**Kind:** bugfix (live paint handoff)  
**Evidence:** [`215658`](../../captures/session_20260905_215658.log) `lcr,part` `as=768,ae=1440,hs=1104` then next `draw_src` still matches VisualCache geometry  
**Does not authorize:** resolver/ledger/storage changes; VisualCache mutation on NoteOn; NoteOn sealing; a second recovery policy; Hide recovery

---

## Debugging boundary

```text
resolveOccupancyAtNoteOn / pendingNoteChanges_   ← trust
recoverMissingPendingShortenSources              ← single owner
live compose (draw-source or live-capture)       ← original geometry
paint copy + applyPendingHideAndShortenToNotes   ← this slice
VisualCache / snapshots / source-view caches     ← not mutated
```

## Invariant

```text
compose = unmodified authoritative source geometry
paint   = compose + pending transforms + playhead tails
```

A pending Shorten created at NoteOn must appear on the first paint frame
produced after that NoteOn. Compose keeps the original end. Authoritative
absence stays absent. Hide stays absent.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — live-capture invokes `recoverMissingPendingShortenSources` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — existing two-bar windowed gather per missing Shorten |
| Reuse | **YES** — draw-source recovery owner + `Loop::applyPendingHideAndShortenToNotes` |

## Host verification

- Focused: `test_display_window_utils`, `test_pending_note_change`, `test_overdub_source_view` **PASS**
- `teensy41-capture-serial` RAM1 locals **6848**
- `pio test -e native` **1667/1670** — pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end`

## Device recapture

Two overdub passes. `painted_pending_shorten` occurs at the incoming NoteOn,
before the next `BAR`, with NoteId and old/new geometry. The shortened row
stays present through the next bar update.

Parent: [`overdub_paint_shorten_source_recovery_bugfix.md`](overdub_paint_shorten_source_recovery_bugfix.md).
