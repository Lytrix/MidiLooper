# NOTE_EDIT committed oracles vs current snapshot

**Status:** Stage 1 host-verified. Recapture required before Stage 2/3.
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.4e`
**Parent:** 4.4d closed
([`note_edit_committed_base_snapshot_refinement.md`](note_edit_committed_base_snapshot_refinement.md)).
4.4d is paint/select dirty-cache only. This plan eliminates a **second
representation of the same current committed root** at NOTE_EDIT open/commit.
**Evidence:** [`session_20260908_153320.log`](../../captures/session_20260908_153320.log)
**Does not start:** NOTE_EDIT hydrate
([`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md)),
Overlay-as-display, raise `N`, construct on the stop path, `take_only` as
identity source.

This is not a NOTE_EDIT performance slice. `visualCache` stays a cache.
Authority is the published `LoopSnapshot` root.

```text
4.4d
  snapshot → paint/select
       ↓
4.4e Stage 1
  snapshot → NOTE_EDIT visual baseline
       ↓
recapture
       ↓
Stage 2 if necessary
  snapshot → NOTE_EDIT event membership (same root)
       ↓
Stage 3 if necessary
  snapshot → commit base (same root)
```

---

## Debugging boundary

```
4.5a hold / adopt_partial / STOPPED DISP 75
published snapshot rev 7 current (post_stop pending=0)
stored_notes track 4 slot 0 = 75
        ← trust
        ↓
openNoteEditSession
  rebuildVisualCacheFromPasses          ← Stage 1
    tryGather + reconstructDisplayNotes   (153320: ev=157 notes=78)
    vs resolveDisplayNotes(*snapshot)     (STOPPED paint: 75)
  rematerializeEditView                   ← Stage 2 (conditional)
    passes.materialize                    (153320: session_events=153)
        ↓
commitEditAction rematerialize            ← Stage 3 (conditional)
  replay_flat / session_store / loop_materialized
  take_only                               ← do not change (capture-only)
```

`commitEditAction … missing in recon` is a `commitBaseline` home lookup
(`logChangeLengthCommitTrace`). After Pitch `63→78`, rematerialize missing
`M63@1536` means the home left that pitch. `take_only` still showing `M63`
is capture-only. After NoteRange `1248–1728`, rematerialize missing
`M78@1536` means the home left that start. Those lines are **not** this RC.

---

## Contract

Stage 1 establishes snapshot-root authority for the NOTE_EDIT visual
baseline. Recapture. If Stage 2 still observes membership divergence,
migrate the edit session to the **same published snapshot root**, then
Stage 3 migrates commit rematerialization to that same root.

```text
current snapshot root
        │
        ├── STOPPED display → resolveDisplayNotes(*snapshot)
        │
        └── NOTE_EDIT open
              │
              └── rebuildVisualCacheFromPasses
                       ↓
                     same resolveDisplayNotes(*snapshot)
```

`visualCache` is filled from that root. It is not the authority for
`EditSession` or `commitEditAction`. Stage 2 must obtain membership from
the same `publishedSnapshot()` identity, not from `visualCache.notes` and
not from a generic gather that can reconstruct a different NoteId set.

```text
                 LoopSnapshot(root)
                       │
             ┌─────────┴──────────┐
             ▼                    ▼
       visual notes          EditSession
       (Stage 1 cache)       (Stage 2, same root)
             │                    │
             └─────────┬──────────┘
                       ▼
                commitEditAction
                (Stage 3, same root)
```

Pass rematerialize remains the no-current-root fallback.

[`153320`](../../captures/session_20260908_153320.log) pin:

| Clock | Fact |
|---|---|
| 47.657 s | `why=stop,from=hold,notes=75,root_rev=3,play_rev=7` |
| 48.815 s | `post_stop` `old_root=3,new_root=7` then `pending=0` `root_rev=7` |
| 50.027–50.135 s | STOPPED `DISP` **75** |
| 50.145 s | `stored_notes,track,4,slot,0,notes,75` |
| 52.652 s | `VCACHE,full,ev,157,notes,78,dirty,0` |
| 52.656 s | `NOTE_EDIT_OPEN,visual_notes,78,session_events,153` |
| 53.251 s | first NOTE_EDIT `DISP` **77** |
| 66.213 s | pre-commit Delete `noteId=39` (`overlap_baseline_diff`) |
| 78.114 s | close `editPass=0 rows=3 (no display-diff replace)` |
| 78.159 s | `VCACHE,idle_src,SnapshotNotes,root_rev,10` keeps **75** |

Gathered event membership and the snapshot’s display interpretation are
not the same abstraction. `153320` proves it: **157 gathered events → 78
reconstructed notes**, while STOPPED snapshot resolve is **75**.

`openNoteEditSession` always calls `rebuildVisualCacheFromPasses()` so the
open base matches pass rematerialize (comment cites `020910` / `021959`
adopt_partial). After 4.5a that replace is the defect: it discards the
current-snapshot frame. 4.4d never runs at open because `full` sets
`dirty=0`.

`appendOverdubPassDisplayNotes` only walks **Active** overdub passes.
`openNoteEditSession` calls `stopOverdubbing()` first, so that append is a
no-op here.

---

## Architecture checkpoint

| Question | Stage 1–3 |
|---|---|
| **Ownership change?** | NO. Same owners: `Loop::rebuildVisualCacheFromPasses`, `Loop::rematerializeEditView`, `EditManager::openNoteEditSession`, `EditManager::commitEditAction`. |
| **State-transition change?** | NO. NOTE_EDIT enter / leave / commit timing unchanged. |
| **Unbounded MIDI-interval owner?** | NO. Same full-loop open/commit work, still off the MIDI interval path. |

Reuse: YES — extend those owners. No new domain noun. No Overlay-as-display.
No hydrate. `N` stays 2.

---

## Stages

### Stage 1 — open visual baseline from current snapshot

**Owner:** `Loop::rebuildVisualCacheFromPasses`

```text
current published snapshot
        ↓
tryGatherPublishedSnapshotEventsInWindow
        ↓
resolveDisplayNotes(*snapshot)
        ↓
visualCache.notes
```

not:

```text
current published snapshot
        ↓
gather events
        ↓
reconstructDisplayNotes(flat)
        ↓
visualCache.notes
```

When `tryGatherPublishedSnapshotEventsInWindow` succeeds and the path is
`Snapshot` (current root, not Overlay), set `visualCache.notes` from
`LoopContentResolution::resolveDisplayNotes(*publishedSnapshot())`.
Do not `reconstructDisplayNotes` on the gathered flat for that current root.
The gather still names the usable current root and Overlay reject; it is
not the display interpreter.

No-current-root and Overlay-rejected stay on `gatherCommittedEvents` +
`resolveDisplayNotes(passes)`.

**Invariant:** After NOTE_EDIT open with a current root, the visual
baseline is that root:

```text
visualCache.builtFromRevision == publishedSnapshotRevision()
visualCache.notes == resolveDisplayNotes(*publishedSnapshot())
```

Count equality alone can hide a wrong NoteId set. Native asserts note
identity and geometry, not only `size()`.

**Device recapture (`153320` shape) asks three questions:**

1. Does `NOTE_EDIT_OPEN visual_notes` become **75** (last STOPPED Snapshot
   paint / `stored_notes`)?
2. Does `VCACHE,full` **notes** stay **75**, rather than re-inflate to 78?
   (`ev` may still be the gathered event count.)
3. Does NOTE_EDIT subsequently still reconstruct a different NoteId set
   (`session_events` / current-state vs those 75 notes)?

If #1 and #2 pass and #3 fails, Stage 2 has earned its existence.
Do not start Stage 2/3 in the same implementation session as Stage 1.

### Stage 2 — open session store from the same root

**Owner:** `EditManager::openNoteEditSession` / `Loop::rematerializeEditView`

Conditional. Only after Stage 1 recapture shows membership divergence
(question 3).

Load `EditSession.store` from the **same** `publishedSnapshot()` identity
Stage 1 used. Do not chain `LoopSnapshot → visualCache → EditSession`.
Do not switch `passes.materialize()` to a generic gather that reconstructs
a different NoteId set.

No-current-root keeps `rematerializeEditView` → `passes.materialize`.

**Invariant:** `NOTE_EDIT_OPEN` event membership and `visual_notes` share
that root’s NoteId set.

Do not redesign `EditSession.store`. Do not start hydrate Stage 4a.

### Stage 3 — commit rematerialize from the same root

**Owner:** `EditManager::commitEditAction`

Conditional. Follows Stage 2 if that stage ran.

`replay_flat` and the session-store reload after save use the same current
snapshot root. `loop_materialized` / `materializedLoopEventsForNoteEditFocus`
follow that same source. `take_only` stays `mergeActiveCapturePasses`.

**Invariant:** The NOTE_EDIT baseline and the committed-base reconstruction
originate from the same current snapshot root.

Consequence / recapture check: a pre-open `stored_notes` identity is not
emitted as `overlap_baseline_diff` Delete unless that `NoteId` exists on
that root. `153320` edit 18 (`Delete noteId=39`) is that check, not a
separate overlap-seal RC.

---

## Do not

- Treat `take_only` vs rematerialize disagreement as a fail
- Treat `NOTE_EDIT commit parity mismatch` as corruption (canonical wins)
- Call `rebuildVisualCacheFromPasses` on the overdub-stop stack (`020910`)
- Raise `kSnapshotIdleConstructionUnitsPerTurn`
- Use Overlay as a NOTE_EDIT source
- Make `visualCache` the authority for EditSession or commit
- Implement [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md)
- Change NOTE_EDIT enter/leave/commit transitions
- Bundle loaded-hydration / USB-batch WIP

---

## Verification

| Gate | Pass |
|---|---|
| Capture-serial RAM1 | locals **6848** |
| Native | **1761 succeeded, 1 skipped.** `rebuildVisualCacheFromPasses` with a current root stamps `builtFromRevision == publishedSnapshotRevision()` and notes equal `resolveDisplayNotes(*snapshot)` |
| Device | Stage 1 recapture questions 1–3 above |

4.5 HITL known non-wrap FAILs do not close this plan.

---

## Pre-implementation review

### Ready

- Query exists: `LoopContentResolution::resolveDisplayNotes(const LoopSnapshot&)`.
- Gather exists: `Loop::tryGatherPublishedSnapshotEventsInWindow` (`Snapshot` vs `Overlay`).
- 4.4d already uses the snapshot query for dirty paint/select.

### Resolved

| Topic | Decision |
|---|---|
| Sequencing | Stage 1 only → recapture → Stage 2/3 only if #3 fails |
| `missing in recon` on Pitch/NoteRange | Not this RC |
| `take_only` | Capture-only; unchanged |
| Open visual source when current | `resolveDisplayNotes(*publishedSnapshot())` |
| `visualCache` | Cache only; Stage 2 reads the same root independently |
| Hydrate | Parked; not this plan |

### Open before coding

None for Stage 1.
