# NOTE_EDIT close bake vs snapshot store

**Status:** Device PASS
[`174646`](../../captures/session_20260908_174646.log).  
**OpenSpec:** `immutable-loop-snapshot-passes` (4.4e follow-up)  
**Parent:** [`note_edit_committed_oracle_snapshot_bugfix.md`](note_edit_committed_oracle_snapshot_bugfix.md) Stage 3  
**Evidence:** [`session_20260908_173531.log`](../../captures/session_20260908_173531.log)

## Contract

When NOTE_EDIT opened on a current snapshot root and the user saved no
edit rows, close must not bake a session-store vs pass-rematerialize diff
into `saveNoteEditPass` / `NoteEditPassClosed`.

`bakeNoteEditSessionStoreToPasses` baseline is
`Loop::copyNoteEditCommitBase` (same root as Stage 2/3 session store).
`take_only` stays unused here.

## Pin

| Clock | Fact |
|---|---|
| 98.062 s | First exit: `keep committed` rows=6 + `NoteEditPassClosed` (real edits) |
| 101.328 s | Reopen `current=1` visual/store **97**, `only_*=0` — no new edits |
| 103.980 s | Second exit: `live capture baked` rows=6 + `NoteEditPassClosed` |
| 104.032 s | `OverlayRejected` `root_rev=11` `playback_rev=17` |

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — `bakeNoteEditSessionStoreToPasses` |
| State-transition change? | NO — bake still runs on the same close; empty diff saves nothing |
| Unbounded MIDI-interval owner? | NO |

Reuse: YES — `Loop::copyNoteEditCommitBase`.

## Verification

| Gate | Pass |
|---|---|
| Capture-serial RAM1 | locals **6848** |
| Native | **1767 succeeded, 1 skipped.** Commit-base events match rematerialize store (no extra NoteId `64`). |
| Device | [`174646`](../../captures/session_20260908_174646.log) **PASS**. No `live capture baked`. Empty third exit has no `NoteEditPassClosed`. Reopen stays `current=1` `root_rev=13` notes **126**. |

## Do not

- Skip bake on all empty-`editPassIds` closes (live-capture fold still bakes)
- Treat Overlay as the bake baseline
- Start hydrate Stage 4a
