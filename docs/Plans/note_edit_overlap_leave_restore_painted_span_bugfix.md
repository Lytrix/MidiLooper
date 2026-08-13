# NOTE_EDIT leave-restore uses rematerialize end, not painted span

**Status:** Active — RC0 native shipped; RC1 native shipped; device gate open  
**Date:** 2026-08-13  
**Kind:** bugfix  
**Parent:** [`note_edit_visual_cache_display_unification_refinement.md`](note_edit_visual_cache_display_unification_refinement.md) (Stages 8–9 shipped; do not reopen evaluate/select rematerialize)  
**Sibling (RC1 mover jump PASS):** [`note_edit_mover_wrap_length_jump_bugfix.md`](note_edit_mover_wrap_length_jump_bugfix.md)  
**Sibling (note 14 wrap-stub):** [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](note_edit_overlap_action_drop_and_wrap_stub_bugfix.md) — do not fold `ChangeLength` 14 `2256–2304` into this slice  
**Evidence:** [`session_20260813_200154.log`](../../captures/session_20260813_200154.log) @75.292 / 75.459 / 83.265; painted length from same loop [`session_20260813_193838.log`](../../captures/session_20260813_193838.log) @122.818  
**Overlap consume:** out of scope

Note **5** is never selected at open in 200154. First appearance is as an overlap target. Leave-restore writes `720–2255`. Later select paints `DNTE` length **1535**. Same loop, first select of note 5 in 193838 paints length **47** (`720–767`).

---

## Debugging boundary

```
Loop::passes / rematerializeEditView     ← keep at open
        ↓
visualCache + ensureVisibleRows          ← Stage 7/8; do not reopen rematerialize
        ↓
overlayUneditedBaselineMapFromDisplayNotes  ← Stage 8; skips when committed != current
        ↓
analyze (Shorten 5 720–1007)             ← analysis span overlapped 1008
        ↓
constrainedGeometryFromRestoreCandidate  ← Restore 720–2255 (this slice)
        ↓
select / DNTE 1535                       ← follows Restore
        ↓
note 14 ChangeLength 2256–2304           ← other plan
overdubSourceViewNotes_ / overlapNoteIds ← do not read
```

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. Same EditManager leave-restore / baseline overlay path. |
| **State-transition change?** | NO. NOTE_EDIT open / commit unchanged. |
| **Reuse** | YES — extend `constrainedGeometryFromRestoreCandidate` (and the Stage 8 display-span overlay it missed after Shorten). Do not add a new domain noun. Do not patch `moveNoteWithOverlapHandling`. |

---

## Device facts

Loop **2304** ticks. Mover **22** length **95**. Pitch 71 is the top lane in this gesture.

| Time / source | What the log writes |
|---------------|---------------------|
| 193838 @122.818 | First select of note 5: rebuild `noteId=5`, `DNTE,71,720,720,47` → painted **720–767** |
| 200154 @75.292 | Pitch 70→71. `ShortenNote` 5 `720–1007` pitch 71. Resolver `candidates=3 pairs=3 interactions=1 constrained=2` |
| 200154 @75.459 | Pitch 71→72 (leave). `RestoreNote` 5 `720–2255` pitch 71. `candidates=2 pairs=2 interactions=0 constrained=1` |
| 200154 @79.058 | Second `RestoreNote` 5 `720–2255` after `HideNote` 5 `720–767` |
| 200154 @83.265 | First select of note 5: `DNTE,71,720,720,1535` (`1535 = 2255 − 720`) |

115 Restore `48–863` and 87 Restore `864–1296` match their first-select `DNTE` lengths. Note 5 does not.

Stage 8 overlay replaces unedited `baselineMap` from visual cache. Visible shortened leave-restore in `constrainedGeometryFromRestoreCandidate` reads `participant.committedSpan`, not that overlay. After Shorten, `rowAllowsCommittedDisplaySpanOverlay` is false, so overlay does not rewrite `baselineMap[5]`.

`2255` is rematerialize pairing (`buildFromSessionStore` / `readLiveLinearSpan`). It is not the 193838 painted end **767**.

---

## RC0 — Native fixture names the first writer

**Owner:** `test_leave_restore_note5_names_first_writer_200154` in [`test_resolve_constrained_geometry.cpp`](../../test/test_resolve_constrained_geometry/test_resolve_constrained_geometry.cpp). No firmware.

**Painted span in the fixture:** `720–767` (193838 `DNTE` length 47). Rematerialize: `720–2255`.

| Step | What to record |
|------|----------------|
| After `buildFromSessionStore` | note 5 `committedSpan.end` |
| After `ensureVisibleRowsForDisplayNotes` (cache `720–767`) | `committedSpan.end` |
| After `overlayUneditedBaselineMapFromDisplayNotes` (unedited) | `baselineMap[5].end`; analysis overlap vs mover `1008–1103` |
| After Shorten `720–1007` + overlay (edited row) | `baselineMap[5].end` |
| After pitch-leave `resolveAllConstrainedGeometry` | Restore constrained `endTick` |

**Decision table (filled):**

| committed after ensure | overlay unedited end | analysis overlap? | Restore end after Shorten | First writer |
|------------------------|----------------------|-------------------|---------------------------|--------------|
| **767** | **767** | **no** | **2255** | **`constrainedGeometryFromRestoreCandidate` uses rematerialize `committedSpan` after overlay skips the shortened row** |

Stage 8 evaluate is clean when the cache list has `720–767`: no overlap with mover `1008–1103`. After Shorten with rematerialize committed `2255`, overlay leaves `baselineMap[5]` at **2255** and leave-restore writes **2255**.

**Not this RC:** firmware, note 14, undo-warm, paint 59 vs 60.

---

## RC1 — Leave-restore uses the painted DisplayNote span — native shipped

**Owner:** `constrainedGeometryFromRestoreCandidate` in [`ResolveConstrainedGeometry.cpp`](../../src/EditManager/ResolveConstrainedGeometry.cpp). `NoteGeometryResolver::resolve` passes the Stage 8 `committedDisplayNotes` list.

**Invariant:** leave-restore of an Existing overlap target restores the painted DisplayNote span (`720–767` here), not rematerialize `720–2255`. Later select `DNTE` length is **47**, not **1535**.

**Change:** when `committedDisplayNotes` has a span for the target, Hidden and Visible-shortened leave-restore use that span instead of rematerialize `committedSpan`. Overlay still skips the shortened row; this path is the Stage 8 gap. 021407 without a display list still restores `committedSpan`.

**Test:** `test_leave_restore_note5_uses_painted_span_not_rematerialize_200154` — Restore constrained end **767**. RC0 fixture still names the writer when no display list is passed (**2255**).

**Not this RC:** note 14 `2256–2304`, cache rebuild pairing if the device cache itself is `2255`, undo-warm.

---

## Device gate (after RC1)

Same 34-note loop as 200154 / 193838:

1. Pitch a length-95 mover onto pitch 71 through note 5, then off. `RestoreNote` 5 is `720–767`, not `720–2255`.  
2. Select note 5: `DNTE` length **47**, not **1535**.  
3. 115 / 87 Restore spans stay matched to their first-select `DNTE`.

Env: `teensy41-capture-serial`. Ask before upload.

If Restore is still `2255` after RC1, the overlay list on device is not `720–767` — stop; do not patch a second owner in the same commit.

---

## Pre-implementation review

### Ready

- Stage 8 overlay owner and leave-restore owner are already named.  
- 193838 pins painted length 47 for note 5 on this loop.  
- 021407 leave-restore-to-`committedSpan` stays valid when `committedSpan` equals the painted span.

### Resolved

| Topic | Decision |
|-------|----------|
| Plan | Sibling bugfix; do not fold into wrap-stub or mover-jump |
| Painted end | `767` from 193838 `DNTE` 47, not grid-95 at other pitches |
| RC order | RC0 fixture, then RC1 on the named owner |

### Open before device flash

1. Device cache contents if RC1 native PASS and device Restore stays `2255`.

### Proceed?

YES — RC1 native shipped. Device after upload.
