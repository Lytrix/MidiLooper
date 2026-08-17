# NOTE_EDIT / LOOP_EDIT display — undo, wrap, pitch-move ghosts

**Status:** Native PASS — RC-W1 / RC-U1 HITL PASS [`152627`](../../captures/session_20260817_152627.log); RC-N1 follow-up native PASS, device retest open  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Evidence:** [`144703`](../../captures/session_20260817_144703.log), [`144939`](../../captures/session_20260817_144939.log); original report [`142813`](../../captures/session_20260817_142813.log)  
**Architecture:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype), [DEC-038](../DECISION_LOG.md#dec-038-overdub-wrap-commit-and-session-undo), [`DerivedViews.md`](../Authority/Architecture/DerivedViews.md), [`overdub_lifecycle_representation_authority.md`](overdub_lifecycle_representation_authority.md), [`runtime_scheduler_lcr_consumer_grooming_refinement.md`](runtime_scheduler_lcr_consumer_grooming_refinement.md), [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md)

---

## Invariant (one sentence)

**Each display consumer reads one owner’s derived representation; wrap, pitch-commit, and undo must resync that consumer’s cache — they must not flatten a second oracle on the button/MIDI path.**

---

## Architecture checkpoint

| Question | RC-W1 | RC-N1 | RC-U1 |
|----------|-------|-------|-------|
| **Ownership change?** | NO — extend `Track::commitOverdubWrapAtSessionStart` + `DisplayManager::invalidateLiveDisplayCache` | NO — extend `commitAllPendingNoteEditActions` + `projectNoteEditDisplayNotes` | NO — extend undo/redo display refresh on `TrackUndo::applyUndoEntry` |
| **State transition change?** | NO | NO | NO — DEC-038.2 post-stop undo grain stays one `OverdubPassAdded` for the session |

---

## Executive summary

A prior patch called `Loop::rebuildVisualCacheFromPasses()` on undo and NOTE_EDIT commit. Both post-fix captures still fail. That patch refreshed **one** derivative (`visualCache`) while display is composed from several authorities:

```text
LoopPasses / effective store
        ├── visualCache.notes          (committed piano-roll)
        ├── capturePreview.notes       (live overdub suffix)
        ├── liveDisplayCacheCommittedNoteCount_  (DisplayManager composition)
        └── projectNoteEditDisplayNotes + NoteEditCurrentState  (NOTE_EDIT overlay)
```

Grooming stalker **C** (duplicate derivation): full flatten+append on the button path uses different rules than idle `rebuildVisualCacheIdleSlice` (prepared LCR, wrap-edge append only).

---

## Symptom 1 — LOOP_EDIT undo (`144939`)

| Anchor | Value |
|--------|-------|
| Before undo | `DISP,6,STOPPED,768,7,7,7,7,1` |
| Undo | `kind=1` (`OverdubPassAdded`) @ **165.792 s**; `Overdub undone` |
| Rebuild ran | `VCACHE,stale notes=7` → `VCACHE,full,ev=2,notes=1` |
| After undo | `DISP,6,STOPPED,768,1,1,1,1,1` |

Firmware **did** rebuild. The oracle collapsed to record-only (2 events / 1 note).

During this session overdub grew `DISP` from **1** to **7**. DEC-038.2: post-stop **U:** is one `OverdubPassAdded` for the **whole session** (`passIds` = wrap 1..N). **7 → 1 is the correct pass-state** when the record layer was one note.

The remaining display bug is **oracle mismatch**, not undo grain:

- Button path used `rebuildVisualCacheFromPasses` (`gatherCommittedEvents` + always `appendOverdubPassDisplayNotes`).
- PLAYING / idle uses `rebuildVisualCacheIdleSlice` (prepared LCR when stamped; append only on wrap-edge when unprepared).

Do **not** change DEC-038.2 session undo into per-wrap **U:** entries.

**RC-U1:** After pass-state change, `markDisplayCachesStale` and fill short loops via idle slices (same oracle as PLAYING). No `VCACHE,full` on undo.

---

## Symptom 2 / 4 — NOTE_EDIT pitch move ghosts (`144703`)

| Anchor | Value |
|--------|-------|
| Live move | `EditSessionAction: type=5` `noteId=358` @ `start=296`, pitches 76→79 |
| Stacking | `DNTE,76,296,296,120,3` … `DNTE,79,296,296,120,3` |
| Inventory | `2/3 notes at this position` → `3/3 notes at this position` |
| Commit @ 95.607 s | `take_only noteId=358 flatEvents=70` vs `replay_flat noteId=352 flatEvents=34` |
| Rebuild | `VCACHE,full,ev=34,notes=7` (was 6) → `DISP=7` |

`Track::invalidateCaches` during NOTE_EDIT **does not** invalidate committed `visualCache` (session overlay only). Paint is `projectNoteEditDisplayNotes(committedBase, session, focus, currentState)`.

Session identity **358** and persist identity **352** both occupy the same start tick. Projection keeps committed 352 (not the mover id) and overlays 358 at the new pitch. Each deselect commit then `rebuildVisualCacheFromPasses` **adds** a note (5→6, 6→7) instead of replacing home geometry.

**RC-N1:**

1. Live projection: suppress committed notes that match `focus.commitBaseline` pitch+start when `noteId != movingNoteId`.
2. After pitch commit rebuild: if settled pitch is present at that start, drop the previous home pitch at that start/end.
3. Do not treat `appendOverdubPassDisplayNotes` on the dirty NOTE_EDIT projection path as the fix (materialize already includes overdubs).

Persist-identity retarget (`reconcileMoverPersistIdentity`) stays DEC-039; this RC does not change `applyNoteEditPass` lookup.

---

## Symptom 3 — Overdub wrap display (`144703`, `144939`)

`144939` first wrap ~159 s:

| Phase | Log |
|-------|-----|
| Pre-wrap | `DISP,6,OVERDUBBING,768,7,1,7,7,1` |
| Wrap | `CLN,overdub,wrap_synth,0` |
| LCR | `DIAG,lcr,src,why=wrap,from=prep,notes=6` |
| Slice | `VCACHE,slice_clean,notes=6` |
| Brief sync | `DISP,…,6,6,6,6,1` |
| Drift | `DISP,…,7,6,7,7,1` |

Producer is correct (`rebuildOverdubSourceView`, `why=wrap,from=prep`). Live composition is not.

`resolveDisplayNotesLiveCapture` rebuilds the committed prefix when `cacheCold`, `playbackRevision` changes, or **source-view note count** changes. Wrap often keeps the same count (hide+add). `Loop::invalidateCaches` does **not** bump `playbackRevision`. `beginCapture` already clears `capturePreview`. The DisplayManager prefix (`liveDisplayCacheCommittedNoteCount_`) stays pre-wrap.

**RC-W1:** After a successful wrap seal + `beginCapture` + held-on re-append, call `displayManager.invalidateLiveDisplayCache()` so the next frame rebuilds the committed prefix from `overdubSourceViewNotes()` and reapplies the new preview. Do **not** call `rebuildVisualCacheFromPasses` on the MIDI wrap path.

---

## Authority table

| Representation | Owner | After fix |
|----------------|-------|-----------|
| Sealed overdub geometry | `LoopPasses` + companion `EditPass` | Unchanged (RC11/RC12 frozen) |
| Prepared analyze indexes | `LoopContentResolution` (idle) | Consume-only; wrap already publishes |
| Committed piano-roll | `Loop.visualCache` | Idle slice / short-loop drain after undo |
| Live overdub paint | `DisplayManager` + `capturePreview` | Invalidate live cache on wrap commit |
| NOTE_EDIT paint | `projectNoteEditDisplayNotes` + settled overlay | One geometry per mover home after pitch commit |

---

## Explicit out of scope

- Reopening RC11/RC12 consume/seal ([`overdub_overlap_hold_display_cache_bugfix.md`](overdub_overlap_hold_display_cache_bugfix.md) FROZEN)
- NOTE_EDIT hydrate Stages 1–5
- Grooming Slice 4e / 4f / 5
- Changing DEC-038.2 post-stop undo into N **U:** entries
- `tryResolvePreparedWindow` on MIDI, BAR LED, or overdub start/stop (DEC-037)
- Patching `applyNoteEditPass` to look up by pitch+start (DEC-039)

---

## Slices

| Slice | Owner | Invariant |
|-------|--------|-----------|
| **RC-W1** | `Track::commitOverdubWrapAtSessionStart` | After wrap publish, live committed-prefix tracks source view until the next capture append |
| **RC-N1** | `commitAllPendingNoteEditActions` / `projectNoteEditDisplayNotes` | After pitch-only deselect, one display note at the mover start tick (final pitch) |
| **RC-U1** | `TrackUndo::applyUndoEntry` | Undo/redo pass-state uses idle visual-cache oracle; no `VCACHE,full` on that path |

---

## Verification

| Slice | Native | HITL |
|-------|--------|------|
| RC-W1 | Wrap `beginCapture` clears preview; wrap source view rebuilt | After `slice_clean`, DISP committed/live prefix agree until a new note |
| RC-N1 | Projection hides persist twin; overdub pitch rebuild does not keep home pitch | `144703` class: no `3/3` inventory at unchanged tick after deselect |
| RC-U1 | Disable overdub pass + idle refresh matches materialize note count | `144939` class: undo whole overdub session → record-layer display, not `VCACHE,full` |

`pio test -e native` before each stage commit.

---

## Shipped firmware

| Slice | Commit | Native |
|-------|--------|--------|
| RCA | `9292368` | docs only |
| **RC-W1** | `f0b0e66` | wrap `beginCapture` clears preview; held-on re-append |
| **RC-N1** | `142b95b` | persist-twin projection hide + `retireSupersededPitchDisplayNote` |
| **RC-U1** | `324ffdd` | `refreshVisualCacheAfterPassStateChange` idle slices; no `VCACHE,full` on undo/redo |

HITL (after flash `teensy41-capture-serial`):

| Slice | Gate | [`152627`](../../captures/session_20260817_152627.log) |
|-------|------|------|
| RC-W1 | After wrap `slice_clean`, DISP committed/live prefix agree until the next capture note | **PASS** — second wrap `DISP 7,7,7,7` then new note `8,7,8,8` |
| RC-N1 | Pitch-move deselect does not add a home-pitch ghost | **FAIL** then follow-up: `VCACHE` 7→8 at 38.415 s; persist rematerialized home `60@64–288` vs display home `176`; sibling `60@64–224` must stay. Retire now keeps sibling end ticks. Device retest open. |
| RC-U1 | Post-stop `OverdubPassAdded` undo paints the remaining record layer; no `VCACHE,full` | **PASS** — boot `kind=1` undos; DISP `7,7,7,7` → `1,1,1,1`; no `VCACHE,full` on that path (`VCACHE,full` later is NOTE_EDIT open) |
