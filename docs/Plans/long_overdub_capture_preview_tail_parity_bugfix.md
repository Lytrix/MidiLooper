# Long overdub capture preview tail parity

**Status:** Active — RC1  
**Branch:** `bugfix/long-overdub-display-freeze`  
**Evidence:** [`session_20260811_013056.log`](../../captures/session_20260811_013056.log)  
**Parent:** [`long_overdub_display_freeze_bugfix.md`](long_overdub_display_freeze_bugfix.md)  
**OpenSpec:** [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)

## Problem

The Stage 1 preview sidecar distinguishes open notes through
`CapturePreviewNoteState::open`, but `applyCaptureEventToPreview()` still selects a normal
NoteOff target through `DisplayNote::endTick == startTick`.

A legitimately closed zero-duration note has the same geometry. A later orphan or duplicate
NoteOff can therefore mutate that closed row into a long span. Tick-zero rows make the defect
visible as a note stretching from loop start.

## Debugging boundary

```text
capture append
  → applyCaptureEventToPreview
  → CapturePreviewNoteState.open    ← RC1
  → DisplayManager composition      ← trust until RC2
  → post-stop handoff               ← RC2
```

Capture storage append exhaustion and stop canonicalization remain Stage 5 of the parent plan.

## Invariant

Only a preview row whose sidecar state is open may be closed by a normal NoteOff. Geometry is
display data, not lifecycle state.

## Architecture checkpoint

- **Owner:** `Loop` through `CapturePreview` and `applyCaptureEventToPreview`.
- **Ownership change:** NO.
- **State-transition change:** NO.
- **Behavior:** Preserve pitch-LIFO normal pairing and channel-aware wrap pairing.
- **Reuse:** Extend the existing sidecar; no new manager, store, or capture path.

## Scope

1. Make normal NoteOff selection require `CapturePreviewNoteState::open`.
2. Keep sidecar and note vectors aligned; reject inconsistent indices rather than falling back
   to geometry-only closure.
3. Rebuild complete open/wrap sidecar state in `rebuildCapturePreviewFromStore`.
4. Add native fixtures for:
   - closed zero-duration tick-zero row followed by orphan NoteOff;
   - repeated same-pitch tick-zero rows;
   - same-pitch channel/wrap behavior;
   - cold rebuild after wrap metadata exists;
   - synthetic loop-end and preferred wrap-head parity.

## Acceptance

- Closed zero-duration rows are never treated as open.
- Every `openNoteIndices` entry references an aligned sidecar row with `open == true`.
- Incremental and cold preview paths agree for covered wrap cases.
- Focused native suite and `pio test -e native` pass.

## Out of scope

- Post-stop display fallback (`long_overdub_post_stop_display_handoff_bugfix.md`).
- USB Host button delivery (`midi_button_usb_host_note_off_delivery_bugfix.md`).
- Capture append pool exhaustion and persistence.
