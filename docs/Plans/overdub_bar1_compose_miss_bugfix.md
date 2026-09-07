# Overdub bar-1 compose miss bugfix

**Status:** RC-A host in tree; HITL recapture open  
**Date:** 2026-09-07  
**Kind:** bugfix  
**Evidence:** [`session_20260907_192322.log`](../../captures/session_20260907_192322.log)  
**Parent:** [`overdub_same_start_note_pairing_bugfix.md`](overdub_same_start_note_pairing_bugfix.md)

## Scope and boundary

RC-A only. Closed incoming `capturePreview` identities that overlay admits
must remain in the painted vector at their capture Off. Stage 3 sibling-drop
exemption is unchanged. RC-B unmatched NoteOff admission is out of scope.

```text
applyDrawSourceOverlays
    → recoverMissingPending/SealedShortenSources   ← this slice
    → applyPendingNoteChangesToDisplayNotes
    → painted frame
```

## Root cause

`recoverMissingPendingShortenSources` and
`recoverMissingSealedShortenSources` assigned `committedEnd = compose.size()`
after overlays had already appended closed preview rows. That moved a valid
closed `capturePreview` identity into the pending prefix. Overlap Hide could
then remove it before Stage 3 ran. The Stage 3 unit test never walked this
sequence.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Owner module | `recoverMissingPendingShortenSources` / `recoverMissingSealedShortenSources` via `DisplayWindowUtils::admitRecoveredCommittedShortenRow` |
| Primary invariant | Recovered Shorten rows join the committed prefix; overlay preview rows stay after `committedEnd` at their capture Off |
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded MIDI interval owner? | NO |

## Implemented

Recover inserts the authoritative committed row at the existing prefix cut.
`recoverMissingPendingShortenRows` accepts an optional `committedEnd` so
sealed recover uses the same cut. Existing callers that omit the cut still
append.

Native seam: `test_closed_preview_survives_draw_source_compose_192322`.

## Out of scope

- RC-B unmatched NoteOff admission
- Stage 3 predicate / `123236` / `000402`
- Overlay admission
- Adding a provenance field to `DisplayNote`
