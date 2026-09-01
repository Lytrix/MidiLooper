# Overdub paint — closed occupy preview stays out of Hide

**Status:** Device PASS [`005439`](../../captures/session_20260902_005439.log). Occupy frame grows `63→82` (`hidden` 0–2). No beats 3–4 `new,0,0,0` wipe.  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`004351`](../../captures/session_20260902_004351.log) 62.162–62.577s  
**Does not replace:** [`overdub_paint_sealed_prefix_hide_bugfix.md`](overdub_paint_sealed_prefix_hide_bugfix.md)  
**Does not authorize:** RC2b tails, Stage 4, reconstruct, 150650 release predicate

---

## Debugging boundary

```
sealed after committedDisplayEnd
    ← trust (004351 hid=1 while sealed held)
open-hold tails after that mark
    ← trust
closed capturePreview upserts the full set (prefix)
    ← this slice
```

## Problem

Overdub occupies the record descending scale on beats 3–4 (`73@528`, `72@576`, `71@624`, `70@672`, `69@720`). COORD matches those starts. DRAW_SRC:

| Time | hidden | frame | diffs |
|------|--------|-------|-------|
| 62.162 | 2 | 62 | `73@528` / `72@576` → `new,0,0,0`; `71@624` → `624` (zero) |
| 62.577 | 5 | 59 | ids 12–16 all `new,0,0,0` |

`missed_preview` stays 0 because live preview rows have `noteId == kInvalidNoteId` and the logger skips them. `new,0,0,0` means the frame has no row at that pitch+start.

Closed preview `upsertDisplayNoteAtPitchStart` replaces the record row **in the prefix**. Occupy Hide then runs on that prefix and removes the replacement.

## Invariant

```
closed capturePreview upserts only after committedDisplayEnd
prefix Hide removes the record sibling
the closed occupy row stays in the suffix
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** |

Reuse: `upsertDisplayNoteAtPitchStartFrom` already used for sealed.

## Tests

Host: occupy `60@100→200` with closed preview `100→400`; after prefix Hide of the record, `60@100→400` remains and sits at `index >= committedEnd`.
