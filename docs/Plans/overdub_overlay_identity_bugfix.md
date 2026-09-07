# Overdub overlay identity bugfix

**Status:** host in tree; HITL recapture **FAIL** [`203007`](../../captures/session_20260907_203007.log)  
**Date:** 2026-09-07  
**Kind:** bugfix  
**Evidence:** [`session_20260907_195409.log`](../../captures/session_20260907_195409.log)  
**Parent:** [`overdub_same_start_note_pairing_bugfix.md`](overdub_same_start_note_pairing_bugfix.md)

## Scope and boundary

Closed sealed and current `capturePreview` rows with distinct valid
`NoteId`s must coexist after `applyDrawSourceOverlays` even when they
share pitch+start, including equal end geometry. Same valid `NoteId`
updates the existing suffix row. Invalid-ID rows keep pitch+start
replacement.

```text
sealedCapturePreview rows
    → applyDrawSourceOverlays / upsertDisplayNoteAtPitchStartFrom  ← this slice
    → pending Hide/Shorten + Stage 3 remnant guard
    → painted frame
```

Do not reopen RC-A recover placement, Stage 1/2 pairing, unmatched
NoteOff admission, playback ledger, or currentTick paint tails.

## Root cause

`DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom` scanned the
suffix and replaced the first row matching only pitch+start. After
several overdub wraps, sealed already held prior-pass `69@48` /
`63@144` rows. Current closed preview ids `148`, `149`, and `150` were
emitted as `draw_src,diff new` and then overwritten before the painted
frame. `draw_src,layers` reported `missed_preview=3` and later `4`.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Owner module | `DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom`, called by `applyDrawSourceOverlays` |
| Primary invariant | Distinct valid `NoteId`s from sealed/current overlay coexist at the same pitch/start; the same `NoteId` is updated rather than duplicated |
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded MIDI interval owner? | NO |

Reuse: extend the existing overlay helper. No new Manager and no
provenance field on `DisplayNote`.

## Implemented

Match an existing suffix row by `NoteId` first when the incoming ID is
valid. If both IDs are valid and different, append. Invalid-ID rows
still replace by pitch+start. Stopped-path prefix skip (`154310`) is
unchanged.

Native:

- `test_overlay_keeps_distinct_valid_ids_at_same_pitch_start_195409`
- `test_overlay_same_id_replaces_suffix_row`
- `test_overlay_invalid_id_still_replaces_pitch_start`
- `test_overlay_repeated_sealed_ids_keep_equal_geometry_195409`
- `test_overlay_keeps_repeated_closed_ids_195409`

Focused native PASS. RAM1 locals **6848**. Full native **1722/1723**
(1 skipped).

## HITL recapture [`203007`](../../captures/session_20260907_203007.log)

Flashed immediately before this capture. Overlay-identity gate **FAIL**.

| Phase | Result |
|---|---|
| First overdub wrap (`sealed=9`) | `missed_preview=0` |
| After wrap 2 (`sealed=18`) | `id=126` `69@48–96` missing on every sampled frame (`missed_preview=1`, one frame `2`) |
| After wrap 3 (`sealed=25`) | `id=138` `69@48–96` missing (`missed_preview=1`) |
| Extra closed A | `id=128` `69@808–864` missing on one wrap-2 frame |

`hidden=0` on those frames. Diffs show `old,0,0,0` / `new,69,48,96`:
`visualCache` has no `69@48` row, and the closed current-preview
identity is not in the painted frame at that geometry.

Wrap-2 timeline: `preview=1` / `missed_preview=0` while A is still an
open hold at `hs=48`; the miss starts on the first layer row after
NoteOff@96 closes `id=126`. Same `69@48–96` miss as
[`195409`](../../captures/session_20260907_195409.log) (`id=124` then
`id=148`). This capture did not emit the `63@144–384` miss from that
session.

The helper change is not sufficient for the live painted frame after
the second wrap. Do not reopen RC-A recover cut from this log.

## Observability (next recapture)

`logDrawSourceComposeDifferential` now names the stage on each closed
preview miss. Gated by `SESSION_CAPTURE_DRAW_SRC`. Does not re-run
compose.

```text
draw_src,layers,...,missed_preview,N,compose_miss,M
draw_src,preview_miss,id,126,p,69,s,48,e,96,compose,C,CID,CEND,frame,F,FID,FEND,sealed,SID,SEND
draw_src,diff,...,old,<frame row or 0>,new,69,48,96
```

`compose` / `frame` match: `1` = same id+geometry, `2` = id present with
a different end, `0` = id absent (`CID`/`FID` then name the pitch+start
sibling). `compose_miss` counts closed preview rows that are already
absent from `liveDisplayNotes` (overlay + recover). `missed_preview` with
`compose_miss=0` means the row left after compose, during pending/tails.

## Out of scope

- RC-A recover prefix cut (`c60c8fac`)
- RC-B unmatched NoteOff admission
- Global keep-all rule for invalid IDs
- `DisplayNote` provenance fields
- Playback event merge or NoteUtils pairing
- `upsertDisplayNoteAtPitchStart` used by `retainCapturePreviewAtSeal`
