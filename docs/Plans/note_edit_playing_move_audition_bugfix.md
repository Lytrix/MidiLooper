# Playing position-move playback audition — bugfix

**Status:** Shipped — HITL **PASS** (2026-08-08)  
**Capture anchor (pre-fix):** [`captures/session_20260808_113626.log`](../../captures/session_20260808_113626.log) @34–35s, @91–92s  
**Capture anchor (post-fix):** [`captures/session_20260808_115120.log`](../../captures/session_20260808_115120.log) @93s, @102–103s

## Symptom

While transport is **PLAYING**, coarse position (F2/F3) edits update the session store and display (`GEOM_APPLY,done,…`) but playback preview revision does not advance. `playMidiEvents` keeps emitting stale merged MIDI, so a note moved just ahead of the playhead is not heard on span crossing.

User expectation: Tier-2 session preview audition (note-on/off as playhead crosses the moved span), same model as pitch — not stopped-only direct `sendEditedNoteAuditionWhenTransportStopped` (pitch-only today).

## Debugging boundary

Trust session store and display projection. Fix **playback preview revision scheduling only** — do not revisit overlap geometry or projection ownership.

## Root cause

`moveNoteWithOverlapHandling` and `changeLengthWithOverlapHandling` passed hardcoded `false` to `NoteGeometryResolver::resolveForCausingNote` and `finalReconstructAndSelect`, so `Track::invalidateCaches(false)` bumped display revision only and never called `scheduleDeferredNoteEditDisplayRefresh()` while PLAYING.

Pitch path (`applyPlayingEditPitchGeometry` → `applyNoteEditChange` Pitch) already forwarded `refreshPlaybackPreview=true`.

## Fix

| Change | Location |
|--------|----------|
| Add `refreshPlaybackPreview` parameter (default `true`) to move/length helpers | `NoteEditGeometryApply.h` / `NoteEditGeometryApplyMutate.cpp` |
| Forward flag from `applyNoteEditChange` Move/Length cases | `NoteEditGeometryApplyMutate.cpp` |
| Replace hardcoded `false` in resolver + `finalReconstructAndSelect` calls | `NoteEditGeometryApplyMutate.cpp` |

Playing-transport defer path (`PlayingEditGeometryDefer::processPendingPlayingEditGeometry` → `moveNoteToPosition` / `changeNoteEndWithOverlapHandling`) uses `applyNoteEditChange` default `true`, matching pitch.

## Invariant

After a position or length geometry apply while PLAYING, `sessionPlaybackPreviewRevision` schedules via the existing 80 ms deferred flush (`kDeferredNoteEditPlaybackRefreshIdleMs`), so `ensurePlaybackMergedMidiEventsBuilt` reloads `editManager.sessionMidiEvents()` and playhead-crossing span audition works.

## Tests

| Test | Location |
|------|----------|
| Move/length export `refreshPlaybackPreview` parameter | `test_note_edit_fader_feedback` — `test_move_length_forward_refresh_playback_preview_parameter` |
| Full native gate | `pio test -e native` |

## HITL validation

**PASS** — user confirmed audible span-crossing audition OK ([`session_20260808_115120`](../../captures/session_20260808_115120.log)).

Log evidence @102–103s while transport running:

- `GEOM_APPLY,queue,1,3552,1,0` / `GEOM_APPLY,queue,1,3600,1,0` (kind=Move, transport=1)
- `moveNoteWithOverlapHandling` applies `EditSessionAction` Move to session store
- `GEOM_APPLY,done,1,<displayRev>` after each apply (display rev 674 → 677 → 680)

Reproduce workflow:

1. NOTE_EDIT, transport **PLAYING**, multi-bar loop.
2. Select long note; coarse-move start to just before playhead.
3. After ~80 ms fader settle, hear note-on as playhead crosses new start and note-off at end (or next wrap if placed behind playhead).

## Out of scope

- Direct F2/F3 monitor note while stopped (pitch-only)
- Reducing 80 ms defer interval
- `reanchorPlaybackIndex` redesign for behind-playhead placement
- Immediate flush when `|targetStart - playheadPhase| < N` ticks (follow-up if HITL still misses “just before playhead”)
