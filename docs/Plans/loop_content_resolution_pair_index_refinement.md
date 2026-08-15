# Loop content resolution — pair index (5.18 design)

**Status:** **5.18 FROZEN** 2026-08-15 — last-wins flat `byNoteId` device PASS [`173842`](../captures/session_20260815_173842.log). `openOnByPitch` retained. Do not flatten `openOnByPitch`. Do not reopen 5.18.  
**Change:** `openspec/changes/loop-content-resolution/` (DEC-037 Stage 9)  
**Parent:** 5.7c **frozen** — [`loop_content_resolution_spans_dframe_gap_refinement.md`](loop_content_resolution_spans_dframe_gap_refinement.md)  
**Evidence:** [`173842`](../captures/session_20260815_173842.log) `bn=225` `nsort=10003` `tot=7950`. Baseline [`172927`](../captures/session_20260815_172927.log) `bn=5345535`.

**Does not start:** flattening `openOnByPitch`, `recon`, B, A2, Stage 6, restoring the arm cap, reopening 5.7c.

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

5.18a firmware is counters + CAP lines only. Do not change pairing representation.

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

Write in `pairNotesInPassRange` on NOTE_ON (5.18b):

```text
append {noteId, pass.id, onIndex, offIndex: -1}
```

That is **append**, then after all pairing unique **keep-last** — not map assignment and not 5.7c first-wins.

On NOTE_OFF, reverse-scan the unsorted list; if the last matching `noteId` has `passId == pass.id`, set `offIndex`.

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

## 5.18a — instrument (this slice)

Do not flatten. One complete `pair` phase reports:

```text
pair.total
pair.byNoteId
pair.openOnByPitch
pair.lookup
pair.other
```

Plus:

```text
byNoteId:        entries, inserts, overwrites, alloc calls, alloc bytes, extmem bytes, build µs
openOnByPitch:   pushes, pops, peak depth (max stack size on one pitch), allocs, heap bytes, build µs
```

`openOnByPitch` is not PSRAM (`std::map` / `std::vector` default allocator). Heap bytes are vector capacity growth only (map node size is not guessed). `byNoteId` insert count is new keys; a header allocator probe was not used — it inlines into every ITCM TU.

Device: second complete line `DIAG,lcr,pair,...`. 1 Hz `phase,pair` lines add `bn=` `op=` `lk=` `pk=` so a `DFRAME` gap can be attributed.

Native: last-wins (`byNoteId` overwrite, not first-wins unique) and peak-depth ≥ 2 for two stacked same-pitch ons.

Decision tree after device remasure — not before:

```text
                    pair
                      │
                measure owners
                 /          \
         byNoteId        openOnByPitch
             │                  │
       expensive?          expensive?
          /   \              /   \
        yes    no           yes    no
         │      │            │      │
      design   done       design   done
      last-wins          compact stack
      lookup             (peak depth first)
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

1. Which structure produces the stall? **`byNoteId`** [`172927`](../captures/session_20260815_172927.log). `openOnByPitch` is not the owner (`op=2433` µs, `pk=1`).
2. Minimum representation for `byNoteId`: last-wins unique flat list — **5.18b**, not this slice. Do not flatten `openOnByPitch`.

### Proceed?

5.18a measurement **closed**. Flatten only `byNoteId`, and only when asked.

---

## 5.18a native shipped

`pairNotesInPassRange` fills `ResolutionCostCounters` pair fields. Device emits `DIAG,lcr,pair` on gate complete. Phase `pair` lines include cumulative `bn`/`op`/`lk`/`pk`. Pairing representation unchanged.

---

## 5.18a device PASS — [`172927`](../captures/session_20260815_172927.log)

```
pair,tot=5356927,bn=5345535,op=2433,lk=1424,oth=7535,ent=2396,ins=2396,ow=0,pu=2396,po=2395,pk=1,oa=58,hb=116
```

```text
1.277 s DFRAME
   └── byNoteId   5.346 s / 5.357 s pair  (99.8%)
       openOnByPitch  2.4 ms
       lookup         1.4 ms
       other          7.5 ms
```

| | |
|--|--:|
| `walk` / `hist` | 0 / 2394 |
| `byNoteId` inserts / overwrites | 2396 / 0 |
| `openOnByPitch` peak depth | **1** |
| `openOnByPitch` heap | 116 bytes |
| pass-0 `bn` µs/event | 207 → **2058** (grows with map size) |
| pair `DFRAME` 390→420 | **1.273 s** consecutive `frameIndex` (paint 12.8 ms) |
| LCR `idle_maint` during pair | 25.3 ms / 15.9 ms (no `loop_rem`) |

Decision tree:

```text
byNoteId        expensive? YES  → 5.18b last-wins flat (same class as 5.7c, unique keep-last)
openOnByPitch   expensive? NO   → retain LIFO stack
```

Do not flatten `openOnByPitch`. `pk=1` on this fixture; a compact stack is not required to close the stall. No 5.1. No B. No `recon`. Do not reopen 5.7c.

---

## 5.18b native shipped

`byNoteId` is `{noteId, loc}[]`. NOTE_ON appends. NOTE_OFF reverse-scans the unsorted list and updates the last matching `noteId` in this pass. After all pairing: `stable_sort` by `noteId`, unique **keep-last**. Device one `nsort` slice after the last pair pass, before `isort`. Pair complete line adds `nsort=`. `openOnByPitch` unchanged.

Native **1195/1195** including `test_stage518a_pair_by_note_id_last_wins` (append then unique) and `test_stage518b_pair_by_note_id_unique_keep_last`. Firmware `teensy41-capture-serial` SUCCESS, RAM1 free **6592**.

Device remasure next: same 139-bar fixture. Compare `bn` / pair `DFRAME` to [`172927`](../captures/session_20260815_172927.log). Expect `bn` to collapse like channel `14.7 s → 12.4 ms`. `pk=1` / `op` stay small. `walk=0`.

---

## 5.18b device PASS — [`173842`](../captures/session_20260815_173842.log)

```
pair,tot=7950,bn=225,op=2256,lk=546,oth=4923,ent=2396,ins=2396,ow=0,pu=2396,po=2395,pk=1,oa=58,hb=116,nsort=10003
```

```
mat=0,win=7129,reb=368240,st=531,rep=350,hist=2394,walk=0,app=1341,sort=9237,iapp=197743,isort=28116,capp=10682,csort=1897
```

| | [`172927`](../captures/session_20260815_172927.log) | [`173842`](../captures/session_20260815_173842.log) |
|--|--:|--:|
| `bn` | **5.346 s** | **225 µs** |
| `nsort` | n/a (map) | **10.0 ms** |
| `tot` | 5.357 s | 7.95 ms |
| `op` / `pk` | 2.4 ms / 1 | 2.3 ms / 1 |
| pair `DFRAME` | **1.273 s** (390→420) | **0.980–1.026 s** consecutive `frameIndex` |
| healthy after-complete `DFRAME` | ~0.968 s | **0.968 s** (1170→1200) |
| `walk` / `hist` | 0 / 2394 | 0 / 2394 |
| `idle_maint` during pair | no `loop_rem` | **25.2 ms**, no `loop_rem` |

`nsort` (10.0 ms) and `isort` (28.1 ms) stay under the 50 ms idle bar. `openOnByPitch` unchanged.

### 5.18 closed

The remaining Stage 5.7 device-latency violation was traced to `byNoteId` PSRAM associative construction. Replacing that construction with the minimum representation matching its last-assignment-wins query contract reduced `bn` from 5.346 s to 225 µs and total pairing from 5.357 s to 7.95 ms. `openOnByPitch` remains a LIFO stack because its mutation contract is not equivalent to a sorted/unique index. No new >50 ms construction slice was observed. Pair is frozen.

Do not flatten `openOnByPitch`. Do not reopen 5.18. Do not rewrite `recon`. Next is 5.1 whole-gate measurement, not another index.
