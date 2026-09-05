# Overdub occupy — wrap-held head hold Shortens, not Hides

**Status:** Host in tree. Device recapture required.  
**Date:** 2026-09-05  
**Kind:** bugfix (occupancy classification)  
**Evidence:** [`225200`](../../captures/session_20260905_225200.log) wrap-held `69@4560→760` id 96 at `hs=48` → `lcr,part n=1,a=0`; consume `norowid,pitch=69,id=96,s=48,e=96`  
**Does not authorize:** `displayNotePresentAtHold` changes; linear Shorten/Hide (`startTick < holdStart`); VisualCache mutation; NoteOn sealing; reopen of [`overdub_paint_wrap_noteon_shorten_bugfix.md`](overdub_paint_wrap_noteon_shorten_bugfix.md) (`223435`); Stage 2 stop-hop frame

---

## Debugging boundary

```text
reconstruct / collapse / source-view copy   ← trust (pin Failure A: id 96 present at hs=48)
displayNotePresentAtHold                    ← trust (unwrap present-at-hold)
resolveOccupancyAtNoteOn classify           ← this slice
composedShortenDegradesToHide               ← wrap-held Shorten keeps end < start
classifyPendingShortenSource / paint        ← trust (223435; linear Shortens already paint)
```

## Root cause

Pin `test_wrap_held_record_row_occupies_head_225200` proved Failure A, then
Failure B. Reconstruct and collapse already produce wrap-held `4560→760` in
`overdubSourceViewNotes_`. `collectOverdubSourceHoldParticipantIds(48, 69)`
contains id 96.

`resolveOccupancyAtNoteOn` classified Shorten only when `startTick < holdStart`.
Wrap-held `start=4560`, `hold=48` fails that linear test, so occupancy wrote
**Hide**. Hide would delete the whole wrap-held note. The hold is interior to
the head fragment, so the transform is Shorten `4560–47`.

`composedShortenDegradesToHide` treated any `endTick <= startTick` as Hide, so
a wrap-held Shorten `4560–47` would have been rewritten to Hide on upsert.

Linear Shorten/Hide is unchanged. Hold at `0` of a wrap-held row stays Hide
(no `hold-1` underflow). Same-start occupy (`hold == startTick`) stays Hide.

## Invariant

```text
wrap-held row present at head hold (hold > 0, hold != start)
    → pending Shorten start…hold-1 (end < start is valid)
linear start < hold
    → Shorten (unchanged)
otherwise
    → Hide (unchanged)
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — extend `Loop::resolveOccupancyAtNoteOn` classification and `composedShortenDegradesToHide` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same per-selected-note loop |
| Reuse | **YES** — occupancy stays the single classify owner |

## Host pins

- `test_wrap_held_record_row_occupies_head_225200` — reconstruct/collapse keep `4560→760`; Failure A collect id 96 at `hs=48`; Failure B Shorten `4560–47`
- Focused: `test_pending_note_change`, `test_overdub_source_view` **PASS**
- `teensy41-capture-serial` RAM1 locals **6848**
- `pio test -e native` **1669/1672** — pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end`; one skipped

## Device recapture

Post-wrap occupy of wrap-held `69@4560→760` (or equivalent) at `hs=48` must be
`a>=1` and emit `painted_pending_shorten` before the next `BAR`. Do not treat
every wrap-head NoteOn as the gate. `63@hs=0` `n=0,a=0` remains correct.

Stage 2 (stop-hop foreign frame) is a separate commit. Parent Cursor plan:
`wrap_occupy_stop_hop_929650fd`.
