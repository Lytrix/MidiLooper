# Overdub wrap — sealed Shorten must keep the live paint end

**Status:** Host shipped (`002708` long occupy hides inner shortened notes). Device recapture required.  
**Date:** 2026-09-06  
**Kind:** bugfix (sealed-companion draw-source paint)  
**Evidence:** [`002708`](../../captures/session_20260906_002708.log) (prior [`002200`](../../captures/session_20260906_002200.log), [`001145`](../../captures/session_20260906_001145.log), [`000402`](../../captures/session_20260906_000402.log), [`235546`](../../captures/session_20260905_235546.log), [`233150`](../../captures/session_20260905_233150.log))  
**Does not authorize:** occupy / wrap seal persist changes; VisualCache mutation on wrap; pending across wrap; Stage 1 wrap-held head occupy (`225200` device FAIL)

---

## Debugging boundary

```text
resolveOccupancyAtNoteOn (linear 63)             ← trust
pending paint copy (painted_pending_shorten)     ← trust while pending exists
commitOverdubWrap + sealPendingNoteChangesToEditPasses  ← trust persist
recoverMissingPendingShortenSources              ← pending-only; empty after wrap
gatherCommittedDrawSourceBars (snap=0 → VisualCache)  ← originals
applyPendingNoteChangesToDisplayNotes (sealed then pending) + sibling drop  ← this slice
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
3. **Later Length skipped (`235546`):** lookup can find the first wrap-1
   NoteId and miss later ones (snapshot/source-view/visual). Recovery skipped
   those companions, so only the first sealed Shorten stayed. Fallback is the
   sealed Length row itself.
4. **Longer suffix remnant (`000402`):** later occupy Shortens already
   rewrite the target NoteId on the paint copy (`painted_pending_shorten`
   for id 73 / 77). `drawAllNotes` still draws a longer same-pitch+start
   sealed-preview remnant, so only the first shorten looks updated. Sibling
   drop now covers all closed rows at named Hide/Shorten pitch+starts;
   open holds stay.
5. **Same-start Hide remnant (`001145`):** after several Shortens, a later
   occupy at the previous Add start Hides that NoteId (`63@1152–1440`
   `hs=1152`). The new occupy is an open hold, so shortest-wins never
   dominates the closed remnant. Hide now drops closed rows whose end is
   at least the hidden source end; shorter keepers (`201956` 768–864) and
   open holds stay.
6. **Sealed Length overwrites later pending (`002200`):** after wrap 2,
   occupy of sealed `73@768–1247` at `hs=1056` writes pending `768–1055`.
   Applying pending then sealed restored `1247`. Stop effective already
   has `768–1055`. Sealed baseline now applies first.
7. **Long occupy leaves inner shortened notes (`002708`):** long
   `864–1344` covers `1056–1200` and `1296–1335`. Effective hid them;
   frame kept the inners because same-start Hide drop treated `1296–1335`
   as a shorter keeper. An earlier same-pitch covering occupy now drops
   those inners. Hide absorbs a later Length of the same NoteId.

Pin owners: sealed apply + sealed Length compose recovery + Length
fallback + closed same-start remnant drop.

## Invariant

```text
pending Shorten painted → wrap seal
    → first OVERDUBBING paint of that NoteId still has holdStart-1
compose / VisualCache may still hold the original end
stop effective already keeps the short end — seal persist unchanged
```

`liveCaptureBegin` remains Hide-by-overlap prefix only. Open capture
holds (`capturePreviewNoteIsOpenHold`) are the sole transform skip on
the paint copy. Same-start sibling drop now includes closed suffix
remnants at named Hide/Shorten pitch+starts (`000402`). Gather upsert
during capture is unchanged (`150650`).

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
- `test_wrap_seal_keeps_later_painted_shorten_235546` — id 74 and id 76
  both sealed; compose with only id 74 must recover id 76 from sealed
  Length and paint `1967` / `2735`
- `test_pending_shorten_drops_suffix_longer_remnant_000402` /
  `test_later_occupy_shorten_drops_suffix_remnant_000402` — later occupy
  NoteIds paint short; longer same-pitch+start suffix remnants drop;
  open holds stay
- `test_same_start_hide_drops_closed_remnant_keeps_open_hold_001145` /
  `test_same_start_hide_drops_remnant_when_hold_present_001145` — Hide of
  `63@1152–1440` drops the closed remnant when an open hold or shorter
  keeper is present; RC3 mismatched NoteId without occupy stays
- `test_pending_shorten_wins_over_older_sealed_length_002200` — sealed
  `73@768–1247` plus pending `768–1055` paints `1055`
- `test_long_occupy_hides_inner_shortened_notes_002708` — long
  `864–1344` plus Hide of `1296–1440` / `1056–1200` drops those inners;
  `768–863` stays
- Focused: `test_pending_note_change`, `test_overdub_source_view`,
  `test_display_window_utils` **PASS**
- `teensy41-capture-serial` RAM1 locals **6848**
- `pio test -e native` **1674/1675** — pre-existing
  `test_same_pass_playhead_tails_keep_closed_bar_four_end`

## Device recapture

Every occupy Shorten — not only the first — must show `holdStart-1` on
the next `DISP` before the next `BAR`, before and after wrap. Later
occupies already emit `painted_pending_shorten`; the remnant must not
remain beside that short row. Already-sealed NoteIds will not emit
pending paint.

Separate: Stage 1 wrap-held head occupy device FAIL (`69@hs=48`
`n=1,a=0`) — [`overdub_wrap_head_occupy_shorten_bugfix.md`](overdub_wrap_head_occupy_shorten_bugfix.md).
