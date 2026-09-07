# Overdub stop D# NoteOff pairing bugfix

**Status:** host in tree; HITL recapture open  
**Date:** 2026-09-07  
**Kind:** bugfix  
**Evidence:** [`session_20260907_211440.log`](../../captures/session_20260907_211440.log)  
**Parent:** [`overdub_same_start_note_pairing_bugfix.md`](overdub_same_start_note_pairing_bugfix.md)

## Scope and boundary

Stop-time synthetic closure of the final held D# is legitimate and out of
scope. This slice fixes only the deferred effective/display reconstruction
that still flattens passes before pairing:

```text
Track::finalizePendingNotes / stop adopt          ← trusted
    → deferred visual-cache / effective rebuild     ← this slice
    → pending Hide/Shorten on already-paired rows   ← keep existing owner
```

Do not add a second pairing authority. Playback `mergeSortedMidiVectors`
and `Track::noteOn` pending-slot ownership stay unchanged.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — `Loop` still owns visual cache; `LoopContentResolution::resolveDisplayNotes` already owns per-pass display pairing |
| State transition change? | NO |
| Unbounded MIDI interval owner? | NO — cold idle / last-resort display reconstruct |

## Root cause

`Loop::rebuildVisualCacheFromPasses`, fallback `rebuildVisualCacheIdleSlice`,
last-resort committed/window reconstruct, and the deferred `dnte,src,effective`
dump consume `gatherCommittedEvents` / `materializeToEventVector` and then
`NoteUtils::reconstructDisplayNotes` on the merged stream. Equal-tick merge
keeps chronology for playback, but one LIFO stack then lets a later pass's
Off close an earlier pass's On. `finishOpenNotes` (default true on some
3-argument reconstruct call sites) extends the leftover On to loop length
(`63@0` / `4608` in [`211440`](../../captures/session_20260907_211440.log)).

`LoopContentResolution::resolveDisplayNotes` already pairs each capture pass
before merging `DisplayNote` rows. Source-view fallback already uses it.
The deferred display/effective consumers did not.

## Implementation

Fallback and full visual-cache rebuild, last-resort committed/window display
rebuild when capture is inactive, and the deferred effective dump call
`LoopContentResolution::resolveDisplayNotes` and then apply the existing
pending Hide/Shorten owner. Idle fallback filters those paired rows to the
slice window so wrap-held append and dirty-bar isolation stay unchanged.
`appendOverdubPassWrapPairedNotes` also fills record wrap-held notes and
replaces a linear same-start remnant (`225200`). Snapshot/prepared event
gathers stay for telemetry and coverage; they are not re-paired through
the flat stream when the fallback/full path is the consumer.

Native wrap-held fixtures set `nextNoteId_` after `seedRecordPassFromStore`
so capture IDs do not collide with seeded record IDs.

Native pin:
`test_visual_cache_keeps_dsharp_committed_offs_after_rebuild_211440`.

## Validation

- Focused: `test_overdub_source_view`, `test_pending_note_change`,
  `test_loop_content_resolution`
- `pio run -e teensy41-capture-serial` — RAM1 locals >= 4000
- `pio test -e native`
- HITL: repeated D# overdub; stop frame keeps committed Offs
  `768→1440`, `1536→2208`, `2304→2976`; no orphan `63@0` / full-loop
  length unless actually held; one synthetic stop Off for the final held
  D#; deferred rebuild does not rewrite those endpoints
