# Overdub paint — pending Shorten source recovery

**Status:** Host shipped. Device recapture superseded by [`215658`](../../captures/session_20260905_215658.log) / [`overdub_paint_noteon_shorten_paint_bugfix.md`](overdub_paint_noteon_shorten_paint_bugfix.md).  
**Date:** 2026-09-05  
**Kind:** bugfix (draw-source compose recovery)  
**Evidence:** [`212700`](../../captures/session_20260905_212700.log) `DIAG,draw_src,diff` `id,95,old,84,4560,4608,new,0,0,0` with `hidden,1`; [`212004`](../../captures/session_20260905_212004.log) pending `shorten>=1` before stop  
**Does not authorize:** resolver/ledger/storage changes; VisualCache mutation on NoteOn; NoteOn sealing; generic invalidate; new cache owner; copying VisualCache geometry into compose

---

## Debugging boundary

```text
resolveOccupancyAtNoteOn / pendingNoteChanges_   ← trust
LoopPendingNoteChange apply                     ← unchanged
draw-source compose prefix + overlays           ← may omit a Shorten target
prior VisualCache NoteId                        ← mismatch evidence only
authoritative committed row                     ← recover into compose
next live paint copy                            ← Stage 1 (already shipped)
```

## Invariant

```text
A pending Shorten target must be resolved against an authoritative
current-source row before the paint transform runs.

A missing Shorten row must not be treated as a completed Hide result.

Recover only when the prior VisualCache has the same source identity and the
selected snapshot cannot establish authoritative absence. If the authoritative
source also lacks the target, keep it absent and emit authoritative_absence.
Never resurrect a VisualCache row by default.
```

```text
pending Shorten missing from compose
    ↓
prior VisualCache has same NoteId?  --no--> leave absent (no mismatch evidence)
    ↓ yes
establish authoritative source
    ↓
row exists  → append committed geometry to compose, then paint copy + pending Shorten
row absent  → keep absent, diagnostic = authoritative_absence
```

Hide keeps the existing absence path. Do not recover for Hide.

Authoritative lookup (by `NoteId` only): published snapshot `findNote` → `overdubSourceViewNotes` → windowed `gatherCommittedEventsInWindow` around the VisualCache start-tick hint, then reconstruct. VisualCache start tick is a window hint, not copied geometry.

Owner: `resolveDisplayNotesByDrawSource`. Helpers: `classifyPendingShortenSource`, `recoverMissingPendingShortenRows`.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — extend `resolveDisplayNotesByDrawSource` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — existing two-bar windowed gather per missing Shorten |
| Reuse | **YES** — draw-source owner + existing gather helpers |

## Host verification

- Focused: `test_display_window_utils`, `test_pending_note_change`, `test_overdub_source_view` **PASS**
- `teensy41-capture-serial` RAM1 locals **6848**
- `pio test -e native` — known pre-existing fail `test_same_pass_playhead_tails_keep_closed_bar_four_end`

## Device recapture

Known `A.startTick < holdStart` Shorten. The row stays present with the shortened end across the next bar update until stop.
