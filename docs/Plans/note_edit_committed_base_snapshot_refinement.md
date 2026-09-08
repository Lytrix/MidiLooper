# NOTE_EDIT committed-base snapshot (4.4d)

**Status:** Closed. Host-verified.  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.4d`  
**Parent:** Remaining 4.4 caller migration after 4.4c / 4.5a.

## Contract

When NOTE_EDIT paint/select rebuilds a dirty committed base and the published
`LoopSnapshot` is current, that base is
`LoopContentResolution::resolveDisplayNotes(*snapshot)`. Clean `visualCache`
and no-current-root stay on the existing owners.

```text
visualCacheDirty
        │
        ├── publishedSnapshotIsCurrent → resolveDisplayNotes(*snapshot)
        └── else → passes.materializeToEventVector + reconstruct
```

`EditManager::materializedLoopEventsForNoteEditFocus` and commit rematerialize
stay on pass materialize. This slice is paint/select base only.

## Owner

`EditManager::ensureNoteEditDisplayProjectionCachesBuilt`

## Do not

- Change NOTE_EDIT enter/leave/commit transitions
- Replace the commit oracle (`commitEditAction` rematerialize)
- Snapshot-gather on the MIDI interval path
- Treat Overlay as a NOTE_EDIT source

## Pre-implementation review

### Ready
- Owner is `EditManager::ensureNoteEditDisplayProjectionCachesBuilt`.
- Query already exists: `LoopContentResolution::resolveDisplayNotes(const LoopSnapshot&)`.
- Gate helper is the same pattern as 4.5a (`DisplayWindowUtils`).

### Resolved
| Topic | Decision |
|---|---|
| Dirty + current | Snapshot display notes |
| Dirty + no current root | Existing pass rematerialize |
| Clean cache | Existing `getVisualNotesForSlot` |
| Focus / commit rematerialize | Unchanged; stays on passes |

### Open before coding
None.

## Verification

- Capture-serial RAM1 locals: **6848**
- Native: **1760 succeeded, 1 skipped**

