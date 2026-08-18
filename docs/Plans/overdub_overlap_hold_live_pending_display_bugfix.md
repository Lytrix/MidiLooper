# Overdub live pending Hide/Shorten paint (RC10)

**Status:** Native in this commit; device gate open  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Parent:** [`overdub_overlap_hold_same_start_bugfix.md`](overdub_overlap_hold_same_start_bugfix.md)  
**Evidence:** [`122152`](../../captures/session_20260817_122152.log) — `overlap_hold hide=1` at first-session note-off; `DISP` stayed 7 until idle `slice_clean notes=6` after stop

---

## Invariant

Overdub piano-roll paint applies pending Hide/Shorten at note-off. Visual cache and the live-capture compose stay un-hidden so capture-layer index math is unchanged.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop` still owns pending. `DisplayManager::resolveDisplayNotesLiveCapture` still composes the frame. |
| **State transition change?** | NO. Pending still seals at wrap/stop. |

## Fix

`Loop::applyPendingNoteChangesToDisplayNotes` applies Hide/Shorten onto a paint copy. Not Add (capture preview already has the new note). `resolveDisplayNotesLiveCapture` copies `liveDisplayNotes` to `liveDisplayPendingPaintNotes_` and returns the copy. Does not mutate `visualCache` or `overdubSourceViewNotes_`.

## Tests

- `test_pending_hide_applies_to_display_notes_not_source_view`
- `test_pending_shorten_applies_to_display_notes`

## Device gate

1-bar occupied lane. Hold 60 through the record note; at MIDI note-off, `DISP` frame note count drops (Hide) or the record span shortens before stop. Not only after `slice_clean`.
