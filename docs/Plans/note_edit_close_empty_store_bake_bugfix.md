# NOTE_EDIT no-edit close empty-store bake

**Status:** Device PASS [`002450`](../../captures/session_20260909_002450.log).  
**Kind:** bugfix  
**Evidence:** [`session_20260909_001857.log`](../../captures/session_20260909_001857.log)  
**Parent:** Hydrate 4a in [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md)  
**4.4e contract:** [`note_edit_close_bake_snapshot_bugfix.md`](note_edit_close_bake_snapshot_bugfix.md)

## Contract

When NOTE_EDIT opened with an empty apply scratch (`EditSession.store`)
and the user saved no edit rows, close must not rematerialize the
published loop or bake a session-store vs snapshot diff into
`saveNoteEditPass`.

Hydrate 4a left the store empty on current-root open. Close still called
`copyNoteEditCommitBase` + `buildSessionStoreEditPasses` against that
empty scratch. Empty current state did not drop the resulting Deletes
(`dropUnrequestedSessionStoreDeletes` returned immediately).

## Capture pin ([`001857`](../../captures/session_20260909_001857.log))

| Clock | Fact |
|---|---|
| 580.819 s | Open `session_events=0` `current_state=0` `current=1` `root_rev=80` `visual_ids=77` |
| 580.824 s | Selected note 53; no saved edit rows |
| 582.908 s | Edit Mode short press; `EditSelectNoteState::onExit` |
| After | No `live capture baked`, no `NoteEditPassClosed`, no further `#CAP`. Device hung. |

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — `bakeNoteEditSessionStoreToPasses` / `dropUnrequestedSessionStoreDeletes` |
| State-transition change? | NO — bake still runs on the same close; empty scratch saves nothing |
| Unbounded MIDI-interval owner? | NO — no-edit close no longer rematerializes |

Reuse: YES — extend those two owners. Do not rematerialize on open to
make bake work. That undoes Hydrate 4a.

## Do not

- Skip bake on every empty-`editPassIds` close (live-capture fold still
  bakes a nonempty store)
- Restore `ensureCurrentStateVisibleRowsFromVisualCache` as an all-notes
  fill
- Start hydrate 4b–5

## Verification

| Gate | Pass |
|---|---|
| Capture-serial RAM1 | locals **7008** |
| Native | **1792 succeeded, 1 skipped.** Pins: `test_empty_apply_scratch_bake_is_no_op`, `test_empty_store_vs_snapshot_baseline_does_not_keep_unrequested_deletes` |
| Device | [`002450`](../../captures/session_20260909_002450.log) **PASS**. Open `session_events=0` `current=1` `root_rev=4` notes **79**. Exit `NoteEditPass close skip empty-store bake`. No hang. No `live capture baked`. |
