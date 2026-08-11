# Long overdub post-stop display handoff

**Status:** Implemented — native/build PASS; combined HITL pending  
**Branch:** `bugfix/long-overdub-display-freeze`  
**Evidence:** [`session_20260811_013056.log`](../../captures/session_20260811_013056.log), [`session_20260811_024505.log`](../../captures/session_20260811_024505.log)  
**Parent:** [`long_overdub_display_freeze_bugfix.md`](long_overdub_display_freeze_bugfix.md)

## Problem

After overdub stop, the first display snapshot contained 2883 notes:

```text
1872 visualCache notes + 1011 capturePreview notes = 2883 frame notes
```

`refreshViewportAfterRecordStop()` preserved the live composed vector after its cache boundaries
are cleared. That vector can include playhead-mutated tails and temporary wrap-head rows.
`resolveDisplayNotesCommitted()` returned it while deferred save work was active
(`shouldDeferHeavyDisplayRebuild()` before the long-loop window path).

`024505` also showed empty PLAYING after long record until overdub began — overdub’s live
compose rebuilds the committed layer, while post-record PLAYING depended on the broken handoff.

## Invariant

The first post-stop long-loop frame must come from bounded canonical committed data. Temporary live
tail geometry must not become committed display authority.

## Architecture checkpoint

- **Owner:** `DisplayManager`.
- **Ownership change:** NO.
- **State-transition change:** NO.
- **Stop path:** No synchronous representation rebuild in `Track`/`Loop` commit; overdub stop
  calls the same `refreshViewportAfterRecordStop` handoff as record stop (invalidate/trim/recenter
  only). Window rebuild stays in `resolveDisplayNotesCommitted` / `resolveWindowedDisplayNotes`.

## Implementation

1. `resolveDisplayNotesCommitted` — long-loop window reconstruction runs **before** deferred-save
   stale `liveDisplayNotes` fallback.
2. `refreshViewportAfterRecordStop` — clamp preserved notes with
   `DisplayWindowUtils::clampPreservedDisplayNoteCount` (committed prefix only).
3. `resolveDisplayNotesLiveCapture` — bind `livePlaybackDisplaySlot_` / track so post-stop
   context checks do not discard a valid preserved committed prefix.
4. Overdub stop (including in-edit fold) — call `refreshViewportAfterRecordStop` before the
   display snapshot emit.

## Acceptance

- [x] Long-loop committed resolve prefers window rebuild over deferred-save live fallback.
- [x] Post-stop preserve drops rows beyond the committed compose prefix (0 after live record).
- [x] Native: `test_display_window_utils` clamp fixture + `pio test -e native` (981/981).
- [x] `pio run -e teensy41-capture-serial`: SUCCESS.
- [ ] Combined long-record/overdub HITL: first post-stop `DISP` is window-bounded; PLAYING shows
      notes without requiring overdub entry; no inherited full capture-suffix count.

## Out of scope

Capture-preview lifecycle parity (RC1), USB Host button input (RC3), and Stage 5 storage pressure.
