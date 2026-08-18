# Overdub occupy at `currentTick`

**Status:** Architecture **pinned** 2026-08-18. Stage 0 identity **answered** (`MidiEvent.noteId`). `Entry.noteId`, write-before-emit, and occupy lookup **shipped**. `evaluateOccupyOverlap` parked. Do not say occupy = ledger.  
**Date:** 2026-08-18  
**Kind:** architecture  
**Decision:** [DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)  
**Parent:** [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md)  
**Related:** [`consumer_window_budget_ownership_architecture.md`](consumer_window_budget_ownership_architecture.md), [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype), [`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md)  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log) (16-bar USB `resolveWindow`), [`173842`](../../captures/session_20260815_173842.log) / [`185931`](../../captures/session_20260815_185931.log) (full-loop STOPPED gate 31–40 s), [`132806`](../../captures/session_20260818_132806.log) (Stage 1c held; 64-bar `from=span` missed because PLAYING during `prep`), [`152940`](../../captures/session_20260813_152940.log) (`max_same_pitch=322`)

**Does not authorize:** adding `length` or `endTick` to `ActiveNoteLedger::Entry`; `resolveWindow` / cold `resolveState` / `ensureOverdubSourceNotesForHold` on occupy; `collectPitchPresentNoteIdsAtTick`; PLAYING full-history `deviceGateBegin`; wait-STOPPED-for-`lcr,mat`; consume merge; `WindowManager` / `WindowRequest`; a `LogicalPlaybackState` type (`ActiveNoteLedger` is that state); shrinking `kOverdubSourceWindowBars`; deleting `overdubSourceView`.

---

## Stop

Do **not** continue Stage 1 `from=span` / wait-STOPPED-for-`lcr,mat` as product readiness. That is option **A** — already **rejected**. A 64-bar gate is 31–40 s STOPPED.

Park `evaluateOccupyOverlap` / span-collect native tests. Park consume merge. `Loop` length identity and dirty/save stall stay **shipped**.

---

## Goal

Three layers. Do not conflate them:

```text
LoopPasses
    ↓
playCommittedLoopMidi / PlaybackMergedMidiEvents
    ↓
ActiveNoteLedger @ currentTick
    ├── midiHandler.sendMidiEvent if playbackEmitMidiOutput_
    └── Loop::collectOverdubNoteOnParticipantIds lookup(pitch)
```

| Layer | Owner | Role |
|-------|--------|------|
| Stored content | `LoopPasses` | May contain overlapping same-pitch notes |
| Playback event stream | `playCommittedLoopMidi` walking `PlaybackMergedMidiEvents` | Applies events to the one-slot ledger. Does not resolve stored same-pitch overlap |
| Runtime slot | `ActiveNoteLedger::Entry` | At most one per `(channel, pitch)` at `currentTick` |
| MIDI emit | `Track::sendMidiEvent` / `midiHandler.sendMidiEvent` | Uses the `Entry`; does not create it |
| Occupy | `Loop::collectOverdubNoteOnParticipantIds` | Reads `Entry.noteId`; `Loop` still owns occupy |

**Product contract:** `collectOverdubNoteOnParticipantIds` reads `ActiveNoteLedger` at `currentTick`: at most one `Entry` per `(channel, pitch)`. `sendMidiEvent` does not create that `Entry`. Occupy stays owned by `Loop`.

**Runtime invariant:** `ActiveNoteLedger` has at most one active `Entry` per `(channel, pitch)` (16×128 slot; second `noteOn` overwrites). After `Entry.noteId`, that slot holds at most one `noteId`.

**Not a stored-content invariant.** [`152940`](../../captures/session_20260813_152940.log) `max_same_pitch=322` means `LoopPasses` may contain overlapping same-pitch notes. The ledger does not Hide/Shorten those 322 ids. `Loop::accumulatePendingNoteChangesFromSourceNotes` and `NoteGeometryResolver` remain commit-path owners for **new** overlap; they do not rewrite stored loops from this file, and they do not currently guarantee every path that reaches playback is already one-owner.

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

Do **not** say occupy = ledger. `Loop` / `LoopContentResolution` own `LoopPasses`. `LoopPlaybackRuntime` owns `ledger`. Occupy reads `Entry.noteId`.

Not:

```text
gatherActiveResolvedEvents → reconstructDisplayNotes → PresentNoteVec → occupy
sendMidiEvent → ledger.noteOn → occupy
```

**Keep ledger at `currentTick`:** `playCommittedLoopMidi` walks `PlaybackMergedMidiEvents` and writes `ledger.noteOn` / `noteOff` from the playback event, then `sendMidiEvent` may emit. A note with On at 1000 and Off at 8000 stays one `Entry` until Off. The ledger does not store `length` or compute `endTick`. No `resolveWindow`, no pass gather on USB, no `deviceGateComplete`, no second occupy resolver.

---

## Four representations (do not mix)

```text
Event stream (PlaybackMergedMidiEvents):
    NoteOn / NoteOff

Runtime state (ActiveNoteLedger::Entry):
    active until NoteOff

Geometry (NoteSpan / DisplayNote):
    startTick / endTick

Resolver checkpoint (PresentNote / StateCheckpoints::presentAt):
    snapshot at queried S; no live event stream advancing it
```

`PresentNote.onTick` exists because `resolveState(S)` reconstructs presence at an arbitrary tick. The ledger does not query arbitrary ticks. Playback already has the NoteOff that ends the slot.

**Do not add `length` (or `endTick`) to `Entry`.** Occupy does not need to know when the note ends. Adding `startTick`+`length` would make the ledger schedule geometry. The ledger represents the result of the playback schedule.

**`Entry` today** (`include/ActiveNoteLedger.h`): `active`, `noteId`, `startTick`, `velocity`. No `length`.

| Field | Why it exists (proven) | Occupy |
|-------|------------------------|--------|
| `active` | Slot occupancy | Read after `noteId` |
| `startTick` | Written for `longestActiveSpanBars` (header: “playback window sizing”). That method has **no call sites** in `.cpp` | Not required |
| `velocity` | Written in `noteOn`; `silenceSlotMidiOutput` `forEachActive` ignores the `Entry` and sends NoteOff vel 0 | Not required |
| `noteId` | Written from playback `evt.noteId` in `applyPlaybackLedgerEvent` | Required |
| `length` / `endTick` | Not present | **Do not add** |

Do not delete `startTick` in this occupy plan. Do not add `length` to revive window sizing.

**Occupy `noteId` write and lookup (shipped):**

```text
NoteOn:  ledger.noteOn(channel, pitch, noteId, tick, velocity)
NoteOff: ledger.noteOff(channel, pitch)
```

`Entry` while active: `active`, `noteId`, plus existing `startTick` / `velocity`. An 8000-tick note is one slot that stays active until NoteOff.

---

## PresentNote (why it exists; why not to keep two types)

Introduced as LCR present-at-S (`resolveState` / `StateCheckpoints::presentAt`; naming amendment 2026-08-17). Fields: `channel`, `pitch`, `noteId`, `onTick`. **No `endTick`.** `upsertPresentNote` keys by `NoteId`, so **many same-pitch rows** at one tick. Occupy uses `NoteSpan` + `displayNotePresentAtHold` (Phase 0b) because `accumulatePendingNoteChangesFromSourceNotes` needs start and end.

| | `PresentNote` / `PresentNoteVec` | `ActiveNoteLedger::Entry` |
|--|--------------------------------|---------------------------|
| Cardinality | All spans containing S | One per `(channel, pitch)` |
| Identity | `noteId` | `noteId` (from playback `evt.noteId`) |
| Lifetime | `resolveState` / `StateCheckpoints::presentAt` | Runtime slot at `currentTick` |
| Geometry | On `NoteSpan` / `DisplayNote`, not on `PresentNote` | Not required |
| When written | LCR checkpoint fill | `applyPlaybackLedgerEvent` on the `playCommittedLoopMidi` cursor path |
| Used by `collectOverdubNoteOnParticipantIds` today | No (`tryCollectPreparedPresentNoteIdsAtTick` / source-view walk stay diagnostics) | Yes — `ledger.noteId(channel, pitch)` |

The ledger does not answer “which stored notes contain S?” (`PresentNoteVec`). It answers which owner currently occupies `(channel, pitch)` at `currentTick`. Do **not** copy `PresentNoteVec` onto the ledger.

**Stage 0 answered:** `PresentNote.noteId` and playback `evt.noteId` are both `MidiEvent.noteId`. Keep `StateCheckpoints::presentAt` until a later decision. Do not add a third type. No `LogicalPlaybackState` type — `ActiveNoteLedger` is that state.

`ledger.noteOn` already runs when muted; `midiHandler.sendMidiEvent` only if `playbackEmitMidiOutput_`. `applyPlaybackLedgerEvent` writes the ledger **before** `sendMidiEvent` may emit.

---

## Formal trigger

**Timing model:** when `ActiveNoteLedger` is written relative to `currentTick`. `playCommittedLoopMidi` → `playbackCursorAdvanceSend` calls `applyPlaybackLedgerEvent`, then `sendMidiEvent` may emit.

**Owner:** `Loop` / `LoopContentResolution` = `LoopPasses`. `playCommittedLoopMidi` writes `LoopPlaybackRuntime::ledger` from the playback event. `sendMidiEvent` and `collectOverdubNoteOnParticipantIds` read it. No `WindowManager`. No new Session.

**6.0 unchanged:** `startOverdubbing` / `establishOverdubSourceView` do not call `resolveWindow`. No cold LCR on the button.

---

## Occupy contract

| Item | Pin |
|------|-----|
| Primitive | `collectOverdubNoteOnParticipantIds` reads `ActiveNoteLedger` for incoming pitch **P** |
| Today | `ledger.noteId(midiChannel, pitch)` — at most one id. CAP `from=ledger` `n=` is production occupy. `a`/`b` stay source-view vs prepared diagnostics |
| Must not mean | `PresentNoteVec`; `resolveWindow` on USB; occupy = ledger as an ownership transfer; `sendMidiEvent` as the writer of occupy identity |

Consume / Hide stays on `overdubSourceView` / `accumulatePendingNoteChangesFromSourceNotes`. Do not fold consume into occupy.

USB today (6.0): occupy is `Entry.noteId`. If the slot is inactive, occupy is empty — not “wait for `deviceGateComplete`”. Prepared helper and source-view walk stay diagnostics (`a`/`b`).

---

## `playCommittedLoopMidi` vs `sendMidiEvent`

`playCommittedLoopMidi` keeps `ActiveNoteLedger` at `currentTick`. Split:

```text
playback event
    → ledger.noteOn / ledger.noteOff
    → sendMidiEvent may call midiHandler.sendMidiEvent
```

`deviceGateBeginRange` / full-loop `deviceGateComplete` is **not** occupy readiness. PLAYING full-history `deviceGateBegin` stays rejected.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** new occupy owner. Occupy **reads** `Entry.noteId` (DEC-041) |
| Content vs runtime vs emit | `LoopPasses` may overlap; `playCommittedLoopMidi` writes the owner; `ActiveNoteLedger` represents it; `sendMidiEvent` and occupy read it |
| 6.0 | Unchanged — no `resolveWindow` on the button |

---

## Rejected as this goal

- Full-loop `deviceGateComplete` / `DIAG,lcr,mat=` as occupy readiness
- PLAYING full-history `deviceGateBegin`
- Cold LCR on `establishOverdubSourceView` (6.0)
- Experiment 1 1/2/4/8/16 as occupy policy
- `WindowManager` / `WindowRequest` / `LogicalPlaybackState` type
- “Occupy = ledger” as moving occupy onto `sendMidiEvent`
- `sendMidiEvent` as the writer of occupy identity
- `tryResolvePreparedState` as a cold USB miss
- This-pitch pass reconstruct on occupy
- `collectPitchPresentNoteIdsAtTick`
- Copying `PresentNoteVec` onto `ActiveNoteLedger`
- Adding `length` or `endTick` to `ActiveNoteLedger::Entry`

---

## Next

Wrap-committed events at S must be on the merged stream and ledger **before** occupy ([`overdub_wrap_playback_rebuild_before_occupy_bugfix.md`](overdub_wrap_playback_rebuild_before_occupy_bugfix.md); pin [`152745`](../../captures/session_20260818_152745.log)). Consume stays on `overdubSourceView`. Do not copy `PresentNoteVec`. Do not add `length`.
