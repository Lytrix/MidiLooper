# Overdub wrap — sealed Shorten must keep the live paint end

**Status:** Host shipped (`233150` pin). Device recapture required.  
**Date:** 2026-09-05  
**Kind:** bugfix (sealed-companion draw-source paint)  
**Evidence:** [`233150`](../../captures/session_20260905_233150.log) (same pattern [`232926`](../../captures/session_20260905_232926.log))  
**Does not authorize:** occupy / wrap seal persist changes; VisualCache mutation on wrap; pending across wrap; Stage 1 wrap-held head occupy (`225200` device FAIL)

---

## Debugging boundary

```text
resolveOccupancyAtNoteOn (linear 63)             ← trust
pending paint copy (painted_pending_shorten)     ← trust while pending exists
commitOverdubWrap + sealPendingNoteChangesToEditPasses  ← trust persist
recoverMissingPendingShortenSources              ← pending-only; empty after wrap
gatherCommittedDrawSourceBars (snap=0 → VisualCache)  ← originals
applyPendingNoteChangesToDisplayNotes (sealed)   ← this slice
```

## Root cause

Two gaps after wrap seal:

1. **Suffix skip:** `applyPendingHideAndShortenToNotes` skipped sealed overlay
   when `i >= liveCaptureBegin` (fixed: only open holds skip).
2. **Missing compose row:** draw-source gather prefers published snapshot
   while visual cache is dirty (`234535`). Overdub NoteIds sealed at wrap are
   absent from compose; sealed overlay matches by NoteId only. Pending recovery
   is empty after seal — `recoverMissingSealedShortenSources` reuses the same
   authoritative lookup as `223435`.

Pin owners: sealed apply + sealed Length compose recovery.

## Invariant

```text
pending Shorten painted → wrap seal
    → first OVERDUBBING paint of that NoteId still has holdStart-1
compose / VisualCache may still hold the original end
stop effective already keeps the short end — seal persist unchanged
```

`liveCaptureBegin` remains the sibling-drop / Hide-by-overlap prefix
boundary only. Open capture holds (`capturePreviewNoteIsOpenHold`) are
the sole transform skip on the paint copy.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — extend `applyPendingHideAndShortenToNotes` |
| State transition change? | **NO** — pending still clears on wrap seal |
| Unbounded interval owner? | **NO** |
| Reuse | **YES** — sealed overlay path unchanged |

## Host pins

- `test_wrap_seal_keeps_painted_shorten_233150` — occupy `63@2304–2880`
  id 71 `hs=2736` → seal → suffix compose row + missing-compose recovery
  must paint `2304–2735`
- Focused: `test_pending_note_change`, `test_overdub_source_view`,
  `test_display_window_utils` **PASS**
- `teensy41-capture-serial` RAM1 locals **6848**
- `pio test -e native` **1670/1671** — pre-existing
  `test_same_pass_playhead_tails_keep_closed_bar_four_end`

## Device recapture

After wrap, a note that already painted Shorten must still show
`holdStart-1` on the first `DISP` before the next `BAR`. New post-wrap
occupies may still emit `painted_pending_shorten`; already-sealed ones
will not.

Separate: Stage 1 wrap-held head occupy device FAIL (`69@hs=48`
`n=1,a=0`) — [`overdub_wrap_head_occupy_shorten_bugfix.md`](overdub_wrap_head_occupy_shorten_bugfix.md).
