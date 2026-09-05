# Overdub paint — NoteOn Shorten on the first paint

**Status:** NoteOn paint gate met on [`221758`](../../captures/session_20260905_221758.log).
Wrap compose-miss recovery is a sibling:
[`overdub_paint_wrap_noteon_shorten_bugfix.md`](overdub_paint_wrap_noteon_shorten_bugfix.md)
([`223435`](../../captures/session_20260905_223435.log)).
**Not closed** — user reports a prior run's notes after overdub stop;
[`221758`](../../captures/session_20260905_221758.log) contains no undo.  
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

**NoteOn paint gate met; slice not closed.**
[`221758`](../../captures/session_20260905_221758.log). One
OVERDUBBING session (record → play → overdub → stop) with occupy Shortens
on later laps. Same-start Hides (`as==hs`) emit no
`painted_pending_shorten`.

First occupy Shorten: `lcr,part` `65928297` pitch 63
`as=2304,ae=2928,hs=2784` → `painted_pending_shorten` `65977618` id **114**
`old 2304–2928` → `new 2304–2783` (before `BAR,19200,25` at `66683377`).
id **114** still paints `2783` after that BAR and after `BAR,19968,26`.
Later Ons paint id **116** / **118** / **120** / wrap id **122**
(`as=0,ae=624,hs=288` → `0–287`) the same way. Compose old end matches
`ae`; paint new end is `holdStart-1`. No `shorten_src` /
`authoritative_absence` / `stale_visual_source` — compose already had the
source row. `draw_src,diff` in this window is only Hide `id,96`.

## Stored-verify reading (do not misread the phases)

`queueStoredMidiVerification` emits three `DNTE` blocks, each with a
`DIAG,dnte,src,...` header:

| Header | Source | Shows sealed Hide/Shorten? |
|---|---|---|
| `reconstruct,count,141` | `mergeActiveCapturePasses`, **no EditPass replay** | **No** — originals by design |
| `effective,count,137` | `gatherCommittedEvents` (companions applied) | Yes |
| `frame,count,137` | `emitStoppedFrameNoteDump` (painted frame) | Yes |

The `reconstruct` block legitimately carries `63,*,624` and the
pre-hide same-start `69` siblings. It is **not** the painted frame and
is not evidence of a paint defect.

On [`221758`](../../captures/session_20260905_221758.log) the painted
frame matches effective content on 136 of 137 rows. The one divergence:

```text
effective only:      69,4560,48
painted frame only:  69,3880,56
```

`69@4560–4608` is the record-pass row that `draw_src,diff` reports as
Hide `id,96` all session. `69@3880–3936` is the occupy target of
`lcr,part` `68806500` (`as=3880,ae=3936,hs=3888`). Both sit in the same
last-bar region. This single-row divergence is open and unexplained; it
is **not** yet tied to a user-visible symptom.

Parent: [`overdub_paint_shorten_source_recovery_bugfix.md`](overdub_paint_shorten_source_recovery_bugfix.md).
