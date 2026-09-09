# NOTE_EDIT stale-root unpublished companion admission

**Status:** shipped — unpublished-content admission; miss-path device PASS [`000740`](../../captures/session_20260909_000740.log)  
**Kind:** bugfix  
**Date:** 2026-09-08  
**Evidence:** [`session_20260908_233644.log`](../../captures/session_20260908_233644.log), [`session_20260909_000237.log`](../../captures/session_20260909_000237.log), [`session_20260909_000740.log`](../../captures/session_20260909_000740.log)  
**Decision:** [DEC-056](../DECISION_LOG.md#dec-056--note-edit-rows-enter-the-existing-pending-companion-set-when-the-published-root-is-stale)  
**Does not start:** hydrate 4b–5, B/C/D from the design session, STOPPED idle `pending=1` repair, close-NOTE_EDIT-before-overdub, a new snapshot-publication owner

## Debugging boundary

```
saveNoteEditPass
  Active editPass row is committed
  playbackRevision++
  if published root is already stale
        → append EditPassId to pendingSnapshotPublishBox.companionIds
          ← this slice
tryGatherPublishedSnapshotEventsInWindow
    already overlays box.companionIds onto snapshot MIDI events
rebuildOverdubSourceSpanCache
  miss path applies the same companionIds in place on DisplayNotes
  (pending Hide/Shorten style — not event reconstruct of the source cache)
closeNoteEditPass
  current / opened-current → existing tryPublishNoteEditSnapshot
  stale open → do not publish onto the stale parent
```

`missing in recon` / Hydrate 4a mover seed / STOPPED idle starvation are not this RC.
NOTE_EDIT left open during OVERDUBBING paint/overlap is
[`note_edit_overdub_paint_overlap_compose_bugfix.md`](note_edit_overdub_paint_overlap_compose_bugfix.md).

## Invariant

If `saveNoteEditPass` succeeds while `publishedSnapshotIsCurrent() == false`,
every newly committed edit **EditPassId** must be discoverable through the
existing pending companion set by stale-root playback gather and overdub-source
consumers, without constructing a full snapshot.

```text
published root rev 1
        +
pendingSnapshotPublishBox.companionIds
        |
        +-- overdub companions
        +-- NOTE_EDIT saved rows
        |
        v
stale-root consumers
```

This is unpublished-content admission. It is not a new publication mechanism.

## Owner

`Loop::saveNoteEditPass` admits the row id. The pending box stays the
unpublished-content list. `tryGatherPublishedSnapshotEventsInWindow` already
consumes `companionIds`. `rebuildOverdubSourceSpanCache` reuses that same list.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — extend `saveNoteEditPass` + existing companion overlay |
| State transition change? | YES — approved design session. Close publishes only on a current / opened-current root. Stale-root rows stay companions. |
| Unbounded interval owner? | NO — one id append; gather/overdub already walk `companionIds` |

## Pre-implementation review

### Ready

- `pendingSnapshotPublishBox.companionIds` already exists
- Stale gather already calls `applyNoteEditPassSequence` on those ids
- Overdub source overlays the same ids in place on DisplayNotes (do not reconstruct the source cache)
- Companion construction into a snapshot still requires `sessionExitQueued` (unchanged)

### Resolved

| Topic | Decision |
|---|---|
| Admission point | `saveNoteEditPass`, before `playbackRevision++` |
| Current root at save | Do not append. Close keeps `tryPublishNoteEditSnapshot` when `openedOnCurrentSnapshot` or the root is current |
| Stale parent at close | Do not call `tryPublishNoteEditSnapshot` |
| New pending box | Create if missing. Do not set `sessionExitQueued` |
| Session-exit queue | Union incoming companions with already-admitted ids (do not replace) |
| Overdub while NOTE_EDIT | Out of scope |
| Scan all Active editPasses | Rejected (option B) |

### Open before coding

None.

### Proceed?

YES.

## Tests

Native in `test_loop_take_survival`:

1. Stale root (`root_rev` held, `playbackRevision` ahead) → save N rows → those EditPassIds are on `companionIds` → gather overlay contains them → overdub source contains them → published revision unchanged.
2. Current root → save → id is not admitted → `tryPublishNoteEditSnapshot` still replaces the published note.

## Device gate

- Current-root close publish PASS [`000237`](../../captures/session_20260909_000237.log): open `current=1` `root_rev=4`; close `rows=2`; `root_rev` 4→6; no `skip stale-parent publish`.
- Miss-path companion overlay PASS [`000740`](../../captures/session_20260909_000740.log): open `current=0` `root_rev=8`; close `skip stale-parent publish rows=3`; after save `root_rev=12` `playback_rev=14`; overdub source `id=93 p=85 s=1728 e=2016` while `root_rev=16` `play_rev=19`. No `pending=0` from that close.
