# Overdub hold-window just-in-time fill (RC8)

**Status:** Native in this commit; device gate open  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Parent:** [`overdub_overlap_hold_same_start_bugfix.md`](overdub_overlap_hold_same_start_bugfix.md)  
**Evidence:** [`120324`](../../captures/session_20260817_120324.log) — enter `src,why=open,from=win,notes=1`; hold 60 from 64 through 240; stop `empty_sets=6, looked_up=1, add=2, shorten=0, hide=0`. [`115622`](../../captures/session_20260817_115622.log) — same occupied-lane miss after undo left a 1-note idle slice.

The 1-bar HITL is a tiny repro. The same path must run on a **64-bar** loop without dumping the loop into the source view.

---

## Invariant

Hold fill merges **this pitch** from the **16-bar hold window** (`resolveWindow(passes)`), not the full loop and not a prepared idle slice. Empty hold IDs still do not scan the session view (Gate 3). Notes already in the enter window stay Add-only when IDs are empty.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop::ensureOverdubSourceNotesForHold` already owns D2 fill. |
| **State transition change?** | NO. Consume stays note-off → hold IDs → `resolveConstrainedGeometry` → Add/Shorten/Hide. |

## Root cause

1. Enter view is the 16-bar window around the playhead (prepared, else `resolveWindow`). On a 64-bar loop, enter near wrap does not contain 60@224.
2. `ensureOverdubSourceNotesForHold` returned immediately when the view was non-empty, so it never filled 224.
3. `snapshotOverlapHoldCandidates` only keeps notes sounding at hold start S. 60@224 is not sounding at 64.
4. Note-off `appendNotesForIds` only runs when hold IDs are non-empty (Gate 3). Empty set → Add. Original 224–288 stays.

## Fix

`ensureOverdubSourceNotesForHold` always resolves the 16-bar window around `holdPhaseTick` with `resolveWindow(passes)` (not prepared — a 1-note idle slice must not block 224), reconstructs, and merges **this pitch** only. Skips noteIds already in the view.

- **Note-on** (`snapshotOverlapHoldCandidates`): `soundingAtHoldOnly=true` so same-start / sounding notes outside the enter window are collected, without merging ahead notes into the view.
- **Note-off:** full pitch merge. Newly merged overlapping notes are unioned into `selected` even when hold IDs are empty. That is hold-window JIT, not a full-view scan.

Do **not** merge all pitches. Do **not** dump a 64-bar loop into `overdubSourceViewNotes_`.

## CAP tokens

| Token | Meaning |
|-------|---------|
| `DIAG,lcr,src,why=hold,from=win,pitch=,win=,ev=,merged=,notes=` | Hold-window JIT fill |
| `DIAG,lcr,src,why=open,from=prep\|win` | Session-start rebuild (RC7) |
| `DIAG,lcr,src,why=wrap,from=prep\|win` | Wrap rebuild (RC6) |

## Tests

- `test_empty_overlap_ids_add_only_when_source_overlaps` stays green (8-bar, note already in enter view, empty IDs → Add only).
- `test_empty_ids_resolve_jit_ahead_note_on_64_bar_loop` — 64-bar, enter near wrap; 60@224 not in enter view; sounding-only ensure does not merge 224; full ensure merges 1 pitch-60 note; view size stays far below 64.
- `test_empty_ids_shorten_jit_ahead_after_sounding_snapshot` — same 64-bar geometry after sounding-only snapshot; empty IDs still Shorten or Hide 60@224.

## Device gate

1-bar occupied lane, enter just before wrap, hold 60 from 64 through 240: `why=hold,from=win` with `merged>0`, Hide or Shorten of 60@224, not stacked `SEVT` 224–288 + Add. 64-bar must not hitch from thousands of source-view notes. 1-wrap [`005745`](../../captures/session_20260817_005745.log) stays green. Wrap `beginCapture` must not emit `why=open`.
