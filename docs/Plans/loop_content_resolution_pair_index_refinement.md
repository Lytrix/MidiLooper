# Loop content resolution — pair index (5.18 design)

**Status:** Design only — contract from code. Do not flatten yet.  
**Change:** `openspec/changes/loop-content-resolution/` (DEC-037 Stage 9)  
**Parent:** 5.7c **frozen** — [`loop_content_resolution_spans_dframe_gap_refinement.md`](loop_content_resolution_spans_dframe_gap_refinement.md)  
**Evidence:** [`170024`](../captures/session_20260815_170024.log) `pair` `DFRAME` **1.277 s** (`frameIndex` 390→420, consecutive)

**Does not start:** flattening `byNoteId` or `openOnByPitch`, `recon`, B, A2, 5.1 / 5.2, Stage 6, restoring the arm cap, reopening 5.7c.

**Boundary:** trust `resolveState` / `resolveWindow` answers and `walk=0`. Investigate **what pair writes and what later queries read**, not whether pairing notes is the right algorithm.

---

## Investigation sequence (same as 5.15)

Before changing anything:

```text
What does pair actually mean?
        ↓
What exact query does it provide?
        ↓
What are its key/value/multiplicity semantics?
        ↓
Which operations require ordering?
        ↓
Which operations require uniqueness?
        ↓
What is the minimum representation?
```

Do not flatten from the container type. Flatten from the query. `byNoteId` and `openOnByPitch` have different contracts.

---

## Architecture checkpoint

1. **Ownership change?** NO — still `TickIndex::pairCapturePassEventRange` / `pairNotesInPassRange`.
2. **State transition change?** NO — investigation only.

No firmware until the contract table is complete and a representation is picked from the query, same method as 5.15.

---

## Goal

Determine the minimum representation that satisfies the current **pair** contracts without assuming “another map → flat append+sort.”

Not the goal: a faster `emplace()` into `byNoteId`. Not the goal: flatten `openOnByPitch` because it is a `std::map`.

---

## Two structures (do not collapse)

`pair` is not one index. `pairNotesInPassRange` mutates both:

| Structure | Owner | Lifetime | Allocator in code |
|-----------|--------|----------|-------------------|
| `TickIndex::byNoteId` | `TickIndex` | whole index until rebuild | PSRAM `unordered_map` (`ExternalMemoryFirstAllocator`) |
| `openOnByPitch` | pairing walk | **one pass**; must persist across 8-event slices of that pass | default `std::map<uint8_t, std::vector<uint32_t>>` (not the PSRAM allocator) |

Device [`170024`](../captures/session_20260815_170024.log) does not yet say which of those (or both) produces the 1.277 s `DFRAME`. Measure before picking a swap.

---

## Contract (from code)

### `byNoteId`

Write in `pairNotesInPassRange` on NOTE_ON:

```text
byNoteId[noteId] = { pass.id, onIndex, offIndex: -1 }
```

That is **assignment, last-wins**, not `emplace` first-wins.

On NOTE_OFF, if the stacked ON has a `noteId` and `byNoteId[noteId].passId == pass.id`, set `offIndex`.

Read in `TickIndex::appendNoteEvents`:

```text
NoteId → NoteLocation { passId, onIndex, offIndex }
  copy events[onIndex]
  if offIndex >= 0: copy events[offIndex]
```

`resolveWindow` calls `appendNoteEvents` for each active note-edit `targetNoteId`.

```text
Query:        NoteId → at most one {passId, onIndex, offIndex}
Multiplicity: unique key; last NOTE_ON assignment wins
Ordering:     none for query (point find)
Mutation:     during pair walk (build), then read
```

This is **not** the channel-lookup contract (first-wins `NoteId → channel`). Do not reuse `channelByNoteId` unique-keep-first without proving last-wins is unused or equivalent.

### `openOnByPitch`

Write in the same walk:

```text
NOTE_ON:  openOnByPitch[pitch].push_back(eventIndex)   // pitch = note number only
NOTE_OFF: pop_back that pitch’s vector                 // LIFO
```

Key is `uint8_t` pitch (`event.data.noteData.note`). **Not** `(pitch, channel)`.

Recorded `event.channel` is not a loop-internal identity. [DEC-033](../DECISION_LOG.md#dec-033-overdub-overlap-ignores-per-note-channel): committed notes are loop-scoped; overlap is pitch + tick. Playback remaps channel 1–16 to `Track::midiChannel` in `Track::sendMidiEvent`. `NoteUtils::DisplayNote` has no channel field. Pairing must stay pitch-only.

No `resolveWindow` / `resolveState` reader. It exists so a later NOTE_OFF in this pass (including a later 8-event slice) can find the matching open ON.

```text
Query:        none after the pass finishes
Work:         pitch → stack of currently-open ON event indexes in this pass
Multiplicity: many open ons per pitch
Ordering:     LIFO stack, not tick order
Mutation:     every NOTE_ON / NOTE_OFF in the pairing walk
Lifetime:     one pass; persist across kDeviceGateEventsPerSlice ranges
```

A sorted unique `{pitch, index}` list is the wrong default. A small working stack keyed by pitch may stay a stack.

---

## Derived-structure contract table

Channel lookup copies a stored MIDI byte onto `SoundingNote.channel`. It is **not** a musical query inside a loop (DEC-033; track output channel is `Track::midiChannel`). Do not treat it as a pairing or overlap key. Do not reopen 5.7c to delete it in this investigation.

| Index | Query | Multiplicity | Ordering | Mutation | Candidate |
|-------|-------|--------------|----------|----------|-----------|
| `spanBoundaries` | tick range → start/end apply | many | tick + C-order at equal tick | build, then read | flat A (5.15 **frozen**) |
| `tickEvents` | tick window → Active `(passId, eventIndex)` | many | tick + C-order at equal tick | build, then read | flat A (5.17 **frozen**) |
| channel lookup | stored-byte copy: `NoteId` → first NOTE_ON `event.channel` | unique, first-wins | `noteId` after sort | build, then read | flat A (5.7c **frozen**); not a loop identity |
| `byNoteId` | `NoteId` → `{passId, on, off}` for `appendNoteEvents` | unique key, **last assignment wins** | none | pair walk, then read | **measure** |
| `openOnByPitch` | pairing walk only: pitch → open ON indexes | many per pitch | **LIFO** | every on/off in the pass | **measure** |
| `passById` | `PassId` → `capturePasses` slot | unique | none | begin pass | out of 5.18 unless it appears in the stall |

Do not flatten from the container type. Flatten from the query.

---

## What 5.18 measures (after this design, if asked)

Same sequence as 5.15:

```text
current C
    ↓
contracts above (pinned)
    ↓
native: which structure’s insert time dominates pair
    ↓
only then a representation that matches that contract
    ↓
device DFRAME / idle_maint on the 139-bar fixture
```

Do **not** add B. Do not start 5.1. Do not reopen 5.7c. Do not rewrite `recon`.

---

## Pre-implementation review

### Ready

- Call sites traced: `pairNotesInPassRange`, `pairCapturePassEventRange`, `appendNoteEvents`, device `pair` phase, `test_stage57_pair_keeps_open_note_across_event_slice`.
- Open notes across an 8-event slice already required: `openOnByPitch` persists; `byNoteId.offIndex = -1` until the off slice.

### Resolved (code)

| Topic | Decision |
|-------|----------|
| Two structures | Do not collapse `byNoteId` and `openOnByPitch` |
| `openOnByPitch` key | Pitch only. Recorded channel is not part of the query (DEC-033; `Track::midiChannel`) |
| `byNoteId` uniqueness | Last assignment wins |
| 5.7c | Frozen; channel index is a stored-byte copy, not a pairing key. Do not delete it in 5.18 |

### Open before coding

1. Which structure (or both) produces the 1.277 s `DFRAME` on [`170024`](../captures/session_20260815_170024.log)?
2. After that, what is the minimum representation for **that** contract?

### Proceed?

NO firmware in this session. Design artifact only.
