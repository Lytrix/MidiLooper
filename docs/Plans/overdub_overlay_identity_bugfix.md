# Overdub overlay identity bugfix

**Status:** host in tree; HITL recapture open  
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
(1 skipped). HITL recapture of [`195409`](../../captures/session_20260907_195409.log)
is open: `missed_preview=0` for repeated same-tick A/D# rows, and all
valid overlay IDs remain in the frame.

## Out of scope

- RC-A recover prefix cut (`c60c8fac`)
- RC-B unmatched NoteOff admission
- Global keep-all rule for invalid IDs
- `DisplayNote` provenance fields
- Playback event merge or NoteUtils pairing
- `upsertDisplayNoteAtPitchStart` used by `retainCapturePreviewAtSeal`
