# Overdub note-off pitch-query optimization

**Status:** Native PASS — audit **B**; device vs [`003009`](../../captures/session_20260813_003009.log) open  
**Parent:** [`realtime_incremental_work_overdub_source_view_refinement.md`](realtime_incremental_work_overdub_source_view_refinement.md)  
**Trigger:** [`captures/session_20260813_003009.log`](../../captures/session_20260813_003009.log)  
**Related:** Option B — pitch-scoped overdub source query (windowed chunk gather already rejected)  
**Scope:** Targeted bounded-work optimization of the realtime overdub note-off path

North star:

> Realtime overdub should query the committed state relevant to the note being played; it must not reconstruct unrelated loop state synchronously on the MIDI note-off path.

---

## Purpose

Option B removed the full-loop flatten from overdub entry (`begin_capture` 10–13 µs). After overlapping overdubs seal Shorten/Hide companion rows, `Loop::gatherCommittedNoteEventsForPitch()` fell back to full `midiEvents()` materialization.

[`003009`](../../captures/session_20260813_003009.log): `notechg` 3.1–3.8 ms → **227 → 773 → 868 ms**. Architectural criterion: **no full-loop materialization and no all-active-edit application** on the note-off path. `notechg < 5 ms` is a device gate only.

Not S1, RC-J, RC-S0c, Option A, or a new cache.

---

## Audit (A/B/C)

**Question:** Can committed Pitch edits retarget a source note into the queried pitch?

**Answer: B.** `applyChangePitchById` mutates events by `targetNoteId`. The Pitch row stores that id and the destination `pitch`. The last Active Pitch row per `NoteId` identifies retargets into P in **O(edit rows)**, not O(events × edits).

Current `gatherCommittedNoteEventsForPitch` contract when edit passes exist: **effective** committed pitch after `applyNoteEditPassSequence`, not raw source pitch (`test_source_view_includes_edit_pass_geometry`).

Minimum candidate set that reproduces the oracle:

- committed note-on/off already at P
- plus source events whose last committed Pitch destination is P
- plus Create `addedEvents` that are at P or whose `NoteId` is in that retarget set
- then `applyNoteEditPassSequence` of **relevant** Active note rows (target in the candidate set, or Create as above)
- then filter to P

Length/Delete on a `NoteId` not in that set cannot change the pitch-P result (`applyChangeLengthById` only mutates same-pitch notes as its target). Closed over the reduced set.

Not C: do not add a pitch index.

---

## Implementation

- `LoopEventStore::forEachChunkEvent` — generic chunk walk; no NOTE_EDIT/overdub meaning
- `Loop::gatherCommittedNoteEventsForPitch` — linear source scan + relevant-row apply; does not call `gatherCommittedEvents` / `midiEvents()`
- Work counters for native structural assertions (source scanned, candidates, edit rows applied, full-materialize count)

Linear `O(all source events) + O(relevant edits)` is the accepted first cost. `O(all events × all active edits)` is not.

---

## Device gate (after native)

Grown-loop overlapping overdubs vs [`003009`](../../captures/session_20260813_003009.log): `begin_capture` stays ~10–13 µs; `notechg` / `noterecon` / `usbnote` low-ms; no hundreds-ms stall after note-off; full materialization absent on the pitch query. Do not broaden into `manager_done` / `complete`.
