# B.3 — overlap-resolve results and NOTE_EDIT, parity by output

Date: 2026-11-01. Host-only; no firmware change. Suite: `test/test_revision_store_differential`.

## What is compared

Production decides **what** an overdub hides or shortens (overlap-resolve, unchanged owner) and **what** a NOTE_EDIT
changes (EditManager, unchanged owner). B.3 asks whether the store can **represent** those results so the effective
stream matches production's, per the owner's rule: "parity by output, not by fitting today's implementation."

Production path driven:
- **Overdub with overlap results.** `beginCapture(Overdub)`, pending `Hide` / `Shorten` changes injected with the
  existing hook `pushPendingNoteChangeForTest`, capture, then `commitCapturePass`. Next,
  `sealPendingNoteChangesToEditPasses`, the same order as `TrackCaptureStopCommit`. Last, publication and the B.2a
  oracle.
- **NOTE_EDIT.** `Loop::saveNoteEditPass` with `Update` (Pitch, Length, Velocity) or `Delete`, then publication.
- **Undo.** What `TrackUndo` `applyUndoEntry` does: the capture pass and its companion edit passes go `Disabled`,
  edit passes alone for NOTE_EDIT, `++playbackRevision`, then republish.

Store representation:
- `Hide`: a `HideNote(noteId)` row in the overdub's run.
- `Shorten`: `HideNote(noteId)` plus a replacement NoteOn/NoteOff with the same noteId on the note's own channel.
- NOTE_EDIT: an `Edit` run (new `PassKind::Edit`, composed like an overdub) with `HideNote(target)` plus replacement
  rows carrying the edited pitch, velocity or span. `Delete` is the hide alone.
- Undo: `Store::undo`.
- The store's NoteOff rows carry the noteId of their NoteOn (paired per pass by channel and pitch), so a hide removes
  both ends of the note.

## Result

| Case | Result |
|---|---|
| Overdub hides a Record note | parity; production no longer has the note |
| Overdub shortens a Record note | parity; new end present, old end gone |
| Overdub shortens a wrap-crossing note | parity |
| Second overdub hides a first-overdub note | parity |
| Shorten of a channel-3 note | parity; the replacement stays on channel 3 |
| Undo of an overdub that carried a Hide | parity; the note is back |
| NOTE_EDIT pitch / length / velocity / delete | parity, each with a direct check that the edit took effect |
| Undo of a NOTE_EDIT | parity; original back |
| Two NOTE_EDITs on one note (pitch, then length) | parity |
| **Seeded mix** — 12 seeds × 10 steps: overdubs with 0–2 random Hide/Shorten on visible notes, random NOTE_EDITs, undo | **132 / 132 revisions in parity** (61 overlap changes, 43 edits, 10 undos) |

Parity is the B.2a key: (tick, class) groups compared as multisets, plus the ordering check.

## Reading

- **The run model covers overlap-resolve and NOTE_EDIT without a new mechanism.** One primitive does it: a later run
  hides a noteId from earlier runs and may add replacement rows. Undo stays manifest selection. Repeated edits of
  one note work because each edit's hide also covers the previous edit's replacement rows (same noteId, earlier run).
- **The hard-coded `channel = 1`** in `appendCompanionEventChanges` (`LoopSnapshotPublish.cpp`) does not reach the
  effective view on these paths: the channel-3 Shorten keeps channel 3. It may matter on the session-exit snapshot
  path, which this harness does not drive; noted, not claimed.
- **Requirement on the writer:** the store needs NoteOff rows tagged with their note's identity. Production NoteOffs
  carry no noteId, so the capture layer, which already pairs notes, has to stamp it when it writes into a run.
  That is an interface requirement for stage D, not a store change.

## Still open

- Q1b, span-less (zero-length) notes: production's effective view drops them, the store keeps them (pinned). A
  seal-time rule ("a run never contains a zero-length note") would close it. Owner choice.
- B.2b finding 2, production's cross-pass identical-event collapse: owner decision pending.
- Not driven here: real overlap-resolve computing the changes during a live `Track` overdub, NOTE_EDIT Tick moves,
  CC edit passes, redo of edits.
