# Overdub present-at-S — implementation

**Status:** Occupy lookup **shipped**. Write-before-emit **shipped**. `evaluateOccupyOverlap` parked. Do not say occupy = ledger.  
**Date:** 2026-08-18  
**Kind:** enhancement  
**Authority:** [DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat) amendment — `ActiveNoteLedger` is runtime state at `currentTick`; do not say occupy = ledger  
**Architecture:** [`overdub_present_at_tick_jit_architecture.md`](overdub_present_at_tick_jit_architecture.md)  
**Parent:** [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md)  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log), [`132806`](../../captures/session_20260818_132806.log), [`152940`](../../captures/session_20260813_152940.log) (`max_same_pitch=322`)

**Does not authorize:** adding `length` or `endTick` to `Entry`; `evaluateOccupyOverlap`; `collectPitchPresentNoteIdsAtTick`; `resolveWindow` / `ensureOverdubSourceNotesForHold` on note-on; PLAYING full-history `deviceGateBegin`; wait-STOPPED-for-`lcr,mat`; consume merge; a `LogicalPlaybackState` type; shrinking `kOverdubSourceWindowBars`; deleting `overdubSourceView`.

---

## Product contract (pinned)

`Loop::collectOverdubNoteOnParticipantIds` reads `LoopPlaybackRuntime::ledger`: **at most one `ActiveNoteLedger::Entry` per `(channel, pitch)`**, with `Entry.noteId`. `Track::sendMidiEvent` / `midiHandler.sendMidiEvent` use that `Entry`; they do not create it.

```text
LoopPasses
    ↓
playCommittedLoopMidi / PlaybackMergedMidiEvents
    ↓
ActiveNoteLedger @ currentTick
    ├── sendMidiEvent if playbackEmitMidiOutput_
    └── collectOverdubNoteOnParticipantIds lookup(pitch)
```

Do **not** say occupy = ledger. `Loop` / `LoopContentResolution` own `LoopPasses`. `LoopPlaybackRuntime` owns `ledger`. Occupy **reads** `Entry.noteId`.

**Runtime invariant:** `ActiveNoteLedger` has at most one active `Entry` per `(channel, pitch)`. After `Entry.noteId`, that slot holds at most one `noteId`.

**Not a stored-content invariant.** [`152940`](../../captures/session_20260813_152940.log) `max_same_pitch=322` means `LoopPasses` may contain overlapping same-pitch notes. The ledger does not Hide/Shorten those ids. `accumulatePendingNoteChangesFromSourceNotes` / `NoteGeometryResolver` remain commit-path owners for **new** overlap; they do not rewrite stored loops from this file.

```text
LoopPasses:
    may currently contain overlapping same-pitch notes

playCommittedLoopMidi:
    applies PlaybackMergedMidiEvents to the one-slot-per-(channel, pitch) ledger
    (last NoteOn to that slot overwrites; the stream does not guarantee one owner)

ActiveNoteLedger::Entry:
    represents the resulting runtime owner
    does not resolve stored same-pitch overlap
```

**Keep `ActiveNoteLedger` at `currentTick`:** `playCommittedLoopMidi` writes `ledger.noteOn` / `ledger.noteOff` from the playback event, then `sendMidiEvent` may emit. The slot stays active until NoteOff. Do not add `length` or `endTick` to `Entry`. Not `LoopContentResolution::resolveState` on USB. Not a second occupy resolver. Not full-loop `deviceGateBegin`.

---

## PresentNote inspection (this look)

| Fact | Proof |
|------|--------|
| Why introduced | LCR present-at-S type (`resolveState` / checkpoint `presentAt`; DEC-037 naming 2026-08-17) |
| Fields | `channel`, `pitch`, `noteId`, `onTick`. **No `endTick`** |
| Cardinality | `upsertPresentNote` keys by `NoteId` — many same-pitch rows at one tick |
| Cold `resolveState` | Full-loop gather + reconstruct + `notePresentAt` — emits every span containing S |
| Occupy | Left `PresentNote` for `NoteSpan` (Phase 0b) because consume needs start/end |
| Ledger | One slot per `(channel, pitch)`; `active`, `noteId`, `startTick`, `velocity`; **no `length`**; written in `applyPlaybackLedgerEvent` before `sendMidiEvent` |
| `startTick` on `Entry` | For `longestActiveSpanBars` (window sizing). That method has no `.cpp` call sites. Occupy does not use it. Do not add `length`. Do not delete `startTick` this plan |
| Mute | `ledger.noteOn` still runs; `midiHandler.sendMidiEvent` only if `playbackEmitMidiOutput_` |

**Identity field:** do **not** copy `PresentNoteVec` onto the ledger. Keep `StateCheckpoints::presentAt` until a later decision. Do not add a third type.

**Stage 0 question (answered 2026-08-18):** is `PresentNote.noteId` identifying the same owner that `playCommittedLoopMidi` already has as `evt.noteId` on `PlaybackMergedMidiEvents`?

**Answer: yes — same identity (`MidiEvent.noteId`). Not a 1:1 lookup at a pitch.**

Proof:

| Path | Assignment |
|------|------------|
| Stored identity | `MidiEvent.noteId` — “stable logical-note identity (stored on note-on MidiEvent)” (`include/MidiEvent.h`) |
| Playback `evt.noteId` | That field on `PlaybackMergedMidiEvents`. Full-loop gather is `copyEffectiveCommittedEvents` → `LoopPasses::materializeToEventVector`. |
| `PresentNote.noteId` | `LoopContentResolution::resolveState` copies `DisplayNote.noteId`. `reconstructDisplayNotes` sets `note.noteId = evt.noteId`. Checkpoint `span.note.noteId` is the same copy. |
| Native | `test_stage4_shorten_matches_reconstruct` finds the same `noteId` on materialize reconstruct and `resolveNotes`. |
| LCR gather | `gatherActiveResolvedEvents` comment: same order as `materializeToEventVector` (per-layer `applyActiveEdits` then merge). |

**Firmware path (this identity):**

```text
NoteOn:  ledger.noteOn(channel, pitch, evt.noteId, tick, velocity)
NoteOff: ledger.noteOff(channel, pitch)
```

`PresentNote` stays LCR / checkpoint. Do not copy `PresentNoteVec` onto the ledger.

**Not 1:1 at a pitch.** `PresentNoteVec` can have many same-pitch ids at S. The ledger slot is last NoteOn overwrite. Occupy (later) reads that last writer, not a PresentNote row.

**Capture stream (emit only).** During OVERDUBBING, `playCommittedLoopMidi` advances `PlaybackEmitPolicy::ActiveCaptureOverdub` through `playbackCursorAdvanceSendCapture` (`sendMidiEvent` only). Playback `mergedMidiEvents` is committed-only. Last-writer on occupy’s ledger is committed-only `mergedMidiEvents`, wrap-pass, and loop-head. Live capture must not fold into `mergedMidiEvents`. [`overdub_occupy_merged_capture_ledger_bugfix.md`](overdub_occupy_merged_capture_ledger_bugfix.md).

`Entry` while active: `active`, `noteId` (plus existing `startTick` / `velocity`). No `length`. `PresentNote` stays LCR / checkpoint.

Questions 1 (one `Entry` vs all `PresentNote`s) and 2 (`sendMidiEvent` lag) are **closed**: one `Entry` per `(channel, pitch)`; `playCommittedLoopMidi` calls `applyPlaybackLedgerEvent` **before** `sendMidiEvent` may emit.

---

## Stage 0 — `PresentNote.noteId` vs `evt.noteId` **answered**

**Invariant:** `ActiveNoteLedger::Entry` plus `noteId` is the runtime slot at `currentTick`. `PresentNoteVec` is not what `collectOverdubNoteOnParticipantIds` returns.

**Answer:** **Yes** — same identity (`MidiEvent.noteId`). `Entry.noteId` is written from playback `evt.noteId`. `PresentNote` stays LCR. Not a 1:1 pitch lookup.

**Firmware this slice:** `collectOverdubNoteOnParticipantIds` reads `ledger.noteId(channel, pitch)`. Do not copy `PresentNoteVec`. No `length`. Consume stays on `overdubSourceView`.

---

## Later (blocked)

| Stage | Intent |
|-------|--------|
| `LoopPasses` overlap | Commit-path Hide/Shorten stays on `accumulatePendingNoteChangesFromSourceNotes` / `NoteGeometryResolver`. Do not rewrite stored loops from this file. Do not treat that as the playback owner. |

---

## Parked

| Item | Why |
|------|-----|
| `evaluateOccupyOverlap` / span-collect native tests | Wrong cardinality (`PresentNoteVec` / all spans at S) |
| `collectPitchPresentNoteIdsAtTick` | Collection API |
| `deviceGateBeginRange` as occupy readiness | Occupy reads `ActiveNoteLedger` at `currentTick`, not LCR range spans |
| Wait-STOPPED-for-`lcr,mat` | `deviceGateComplete` is not occupy readiness |
| Consume merge | Stays on `overdubSourceView` / `accumulatePendingNoteChangesFromSourceNotes` |

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change if occupy = ledger? | **Wrong question.** `collectOverdubNoteOnParticipantIds` **reads** `LoopPlaybackRuntime::ledger`. Content stays `Loop` / `LoopContentResolution`. |
| Transition change? | **YES** this slice — occupy snapshot reads `Entry.noteId` (DEC-041) |
| 6.0 | Unchanged |

---

## Hard don'ts

- Do not implement `evaluateOccupyOverlap`.
- Do not say occupy = ledger.
- Do not add a `LogicalPlaybackState` type.
- Do not copy `PresentNoteVec` onto `ActiveNoteLedger`.
- Do not add `length` or `endTick` to `ActiveNoteLedger::Entry`.
- Do not have `ActiveNoteLedger` pick among 322 stored same-pitch `NoteId`s.
- Do not wait for `deviceGateComplete` / `DIAG,lcr,mat=`.
- Do not call `resolveWindow` from note-on.
- Do not run full-loop `deviceGateBegin` while PLAYING.

---

## Stage log

| Stage | Status |
|-------|--------|
| `evaluateOccupyOverlap` / span-collect tests | Parked |
| One `Entry` vs all `PresentNote`s; `sendMidiEvent` lag | **Closed** |
| `PresentNote.noteId` vs `evt.noteId` | **Answered** — same `MidiEvent.noteId` |
| `Entry.noteId` from `sendMidiEvent` (`evt.noteId`) | **Shipped** |
| Split `playCommittedLoopMidi` vs `sendMidiEvent` | **Shipped** |
| `collectOverdubNoteOnParticipantIds` reads `ledger` | **Shipped** |
| Capture emit does not write occupy ledger ([`221334`](../../captures/session_20260818_221334.log)) | **HITL FAIL** [`224719`](../../captures/session_20260818_224719.log) — [`overdub_occupy_capture_stream_ledger_bugfix.md`](overdub_occupy_capture_stream_ledger_bugfix.md) |
