# NOTE_EDIT leftover oracles vs current snapshot

**Status:** Stage 1–2 shipped. Device [`191346`](../../captures/session_20260908_191346.log)
both `current=1` opens `only_*=0`.  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.4f`  
**Parent:** 4.4e closed
([`note_edit_committed_oracle_snapshot_bugfix.md`](note_edit_committed_oracle_snapshot_bugfix.md)).
**Does not start:** hydrate Stage 4a, Overlay-as-display, raise `N`, analysis,
validation, 4.5, 4.6.

## Debugging boundary

```text
4.4e Stage 1–3 + close bake + NoteRange pitch
    ← closed
open visual / rematerializeEditView / copyNoteEditCommitBase
    ← trust
assignMissingNoteIds(loop.midiEvents()) at open
    ← Stage 1 leftover pass flatten
applySessionEditRows → materializeExcludingEditPassIds
    ← Stage 2 leftover E: undo/redo rows fallback
take_only / mergeActiveCapturePasses
    ← capture-only; do not change
```

## Leftover oracles (code)

| Caller | What it still does | After 4.4e |
|---|---|---|
| `openNoteEditSession` → `assignMissingNoteIds(loop.midiEvents())` | Rebuilds `passesMaterializedStore_` from `passes.materializeToEventVector` and assigns IDs on that cache only | Session store already comes from `rematerializeEditView`; chunk IDs from `assignMissingNoteIdsInCommittedCapturePasses`. This flatten does not write back to chunks. |
| `applySessionEditRows` | `materializeExcludingEditPassIds` then apply historical rows | Live E: undo uses `hasUndoCurrentState`. Rows fallback still pass-rematerializes when the open root is current. |
| `materializePassesExcludingEditPasses` | Wrapper around the same exclude API | No callers. Leave; not this slice. |
| `take_only` in `commitEditAction` | `mergeActiveCapturePasses` | Capture-only. Do not change. |

`editAwareMidiEvents()` is the session store. It is not `loop.midiEvents()`.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — extend `openNoteEditSession`, `Loop` snapshot copy, `applySessionEditRows` |
| State-transition change? | NO — NOTE_EDIT enter / E: undo / redo timing unchanged |
| Unbounded MIDI-interval owner? | NO — same open/undo work, off the interval path |

Reuse: YES. No new domain noun. Pass rematerialize stays the no-current-root fallback.

## Invariant

```text
When NOTE_EDIT opened on a current published root, leftover identity and
E: rows fallback use that root's span-filtered events. They do not flatten
passes through loop.midiEvents() or materializeExcludingEditPassIds.
```

## Stages

### Stage 1 — open does not flatten `midiEvents()` on a current root

**Shipped.** Skip `assignMissingNoteIds(loop.midiEvents())` when
`publishedSnapshotIsCurrent()`. Keep it on the no-current-root path.

### Stage 2 — E: rows fallback uses the opened snapshot root

**Shipped.** `applySessionEditRows` copies opened-root snapshot events
(`Loop::tryCopyNoteEditOpenedSnapshotEvents`) then applies the entry rows.
No-current-root / pin miss keeps `materializeExcludingEditPassIds`.

Thread `openedOnCurrentSnapshot` / `openedSnapshotRevision` from
`EditManager::restoreSessionUndoEntry` and the redo path.

## Out of scope

- Hydrate Stage 4a
- Overlap analysis / validation callers
- Display identity/fallback outside NOTE_EDIT
- Removing `materializeExcludingEditPassIds`
- Changing `take_only`

## Tests

| Gate | What |
|---|---|
| Native Stage 1 | Existing open / rematerialize fixtures stay green |
| Native Stage 2 | `test_try_copy_note_edit_opened_snapshot_events_uses_span_note_ids`, `test_session_rows_fallback_uses_opened_snapshot_span_note_ids` |
| RAM1 | `teensy41-capture-serial` locals **6848**. Full native **1772 succeeded, 1 skipped** |
| Device | [`191346`](../../captures/session_20260908_191346.log): both
  `current=1` opens `only_*=0` (120 then 119).
  [`192155`](../../captures/session_20260908_192155.log): `current=0`
  `root_rev=27` uses pass rematerialize (`only_store=5` ids `1,2,3,100,96`).
  Three later `current=1` `root_rev=41` opens are **141** / `only_*=0`. |

## Pre-implementation review

### Ready

- Leftover callers named from `EditManager` / `Loop` after 4.4e
- Snapshot copy helper already exists

### Resolved

| Topic | Decision |
|---|---|
| `take_only` | Unchanged |
| Dead `materializePassesExcludingEditPasses` | Leave |
| Live E: undo with `hasUndoCurrentState` | Unchanged; Stage 2 is rows fallback only |

### Open before coding

None.
