# In-edit overdub GEOM_APPLY reconstruct cost (bugfix)

**Status:** RC1 shipped (with RC-A). Compose PASS [`142119`](../../captures/note_edit_overdub_compose_20260910_142119_serial.log).  
**Successor:** [`note_edit_apply_passmaterialize_deferral_bugfix.md`](note_edit_apply_passmaterialize_deferral_bugfix.md) (RC-A removes PassMaterialize from selector).  
**Evidence (fail):** `captures/note_edit_overdub_compose_20260910_124645_serial.log` (resolve 61442 µs; reconstruct 33348 µs).

## Invariant (RC1)

A session preview revision must not invalidate the committed display base.

| Cache layer | Preview bump invalidates? |
|---|---:|
| Committed base | NO |
| Projection / paint | YES |
| Selection filtering | YES |
| Explicit full invalidation | YES |

## RC1 (shipped)

In `ensureNoteEditDisplayProjectionCachesBuilt` + `bumpSessionPreviewRevision`:

- Remove `previewRevision` from committed-base validity predicate; add to paint keys
- Do **not** call `invalidateProjectedNoteEditDisplayCache()` from preview bump

RC1 alone is insufficient when wrap makes snapshot non-current → PassMaterialize. Closed by RC-A.
