# Note edit overlap shorten — edit projection origin bugfix

**Kind:** bugfix  
**Capture:** [`captures/session_20260807_004127.log`](../../captures/session_20260807_004127.log)  
**Status:** Shipped (native)

## Symptom

On a 7-bar loop (`loopLengthTicks` 5376, `loopStartTick` 960), coarse move of causing note `noteId=3` into overlap target `noteId=14` (pitch 94) emitted:

```text
EditSessionAction: ShortenNote noteId=14 start=2881 end=2927
```

- `start=2881` — `OverlapNoteOn` head trim (`causingEnd + 1` at ghost boundary 2880)
- `end=2927` — `OverlapNoteOff` tail shorten (`causingStart - 1` at 2928)

User-visible: overlap target bar shortens from **note on** (+1 tick) instead of **note off** only.

## Root cause

`projectNoteBaselineForEditAnalysis` built `ProjectionContext::originTick` from **each span’s own** `baseline.startTick`. Wrapped storage spans near the loop boundary picked **different k-shift equivalent intervals**, misaligning causing note and overlap target in the linear analyze window → spurious `OverlapNoteOn` at `loopStartTick` offset (2880 vs 3840 storage).

Not selection bracket offset (that layer is shipped in [`unified_interval_projection_note_edit_selection_bugfix.md`](unified_interval_projection_note_edit_selection_bugfix.md)).

## Fix

| Area | Change |
|------|--------|
| `NoteGeometryResolver::resolve` | overlap **analyze + constrain** use storage `baselineMap` ticks (no k-shift projection in pipeline) |
| `projectNoteBaselineForEditAnalysis` | retained for projection API / unit tests; not used on geometry mutate path |

Shared `originTick` projection remains available for display/bracket layers; the mutate pipeline classifies on storage linear spans so local L→R overlap and wrapped tail-shorten both match storage truth (sessions `004127`, `011618`).

## Tests

- `test_shared_edit_projection_origin_tail_shorten_only_004127` in `test_edit_session_interaction`
- `pio test -e native` — 846 passed

## HITL gate (open)

7-bar loop, `loopStartTick` 960, coarse move on pitch 94 through overlap:

- Overlap target trims from note-off only
- No `ShortenNote` with `start == baseline.startTick + 1` from spurious head trim
- No `DISP` flicker on overlap target start edge

## Related

- [`note_edit_geometry_wrap_regression_bugfix.md`](note_edit_geometry_wrap_regression_bugfix.md) — storage vs display on mutate paths
- [`note_edit_overlap_restore_span_bugfix.md`](note_edit_overlap_restore_span_bugfix.md) — head-trim semantics when classification is correct
