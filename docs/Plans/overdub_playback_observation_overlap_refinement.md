# Overdub overlap — playback observation (Gates 0–4)

**Status:** Active — Gate 0 native landed; Gate 1 uses half-open intersection; zero-length notes invalid  
**Date:** 2026-08-13  
**Kind:** refinement  
**Cursor source:** `~/.cursor/plans/restore_overdub_gather_28c7ca69.plan.md`  
**Parent:** [`realtime_incremental_work_overdub_source_view_refinement.md`](realtime_incremental_work_overdub_source_view_refinement.md)  
**Trigger:** [`session_20260813_021304.log`](../../captures/session_20260813_021304.log)  
**Scheduling:** R1A / G1 in [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md)

Do **not** implement overlap-on-`PendingNote` or change `sendMidiEvent` until Gates 0–4 pass. RC-K3 remains the production overlap path. Option B stays withdrawn. PLAYING idle prebuild was reverted (`73f0489`).

---

## Three-layer invariant

```text
1. Playback observation
   What committed NoteIds sounded during this incoming hold [S, E)?

2. Authoritative geometry
   What are the actual [start,end) spans of those NoteIds in committed storage?

3. Edit semantics
   Shorten/Hide via existing accumulatePendingNoteChangesFromSourceNotes
```

- `ActiveNoteLedger` = current MIDI voice (one slot per output channel + pitch; clears on off). Not the candidate store.
- `PendingNote.overlapNoteIds` = historical observations during the incoming hold (set; keep after playback off). Not wired yet.
- Committed storage = authoritative geometry.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. Track owns `PendingNote` and `sendMidiEvent`. Loop owns committed geometry + Shorten/Hide. |
| **State transition change?** | NO for Gate 0/1 fixtures. YES later: committed playback note-ons mutate `PendingNote` ids. YES (Gate 2): mute becomes a port gate. |

---

## Gate 0 — RT-state / capacity (native landed, device count open)

`OverlapNoteIdSet` is a fixed-capacity set (`kOverlapNoteIdSetCapacity = 128`). Insert is contains-then-add. Overflow fails; the set never grows. Invalid `NoteId` 0 is rejected.

Provisional capacity fits an 8-bar 16th-grid same-pitch full-loop hold (128 unique ids). The 68-bar / 3714-event [`021304`](../../captures/session_20260813_021304.log) unique same-pitch count is **not** measured yet. Overflow on that loop is a failed gate, not heap growth.

Native: `test_overlap_note_id_observation` Gate 0 cases.

---

### Zero-length note invariant

Pin zero-length notes as **invalid**, not as a special overlap case.

A valid sounding note must satisfy:

```cpp
startTick < endTick
```

`startTick == endTick` has no meaningful timing duration and must not participate in overdub overlap selection. Do **not** introduce a dedicated zero-length-note resolver.

The overlap rule remains simple half-open interval intersection:

```cpp
existingStart < incomingEnd &&
existingEnd > incomingStart
```

Therefore notes that only touch at an endpoint do not overlap.

Wrap display (`endTick < startTick`) unwraps by `loopLength` so the same `startTick < endTick` test applies. That is linearization, not a second overlap rule. Incoming or existing spans that cross the loop apply the same inequality after a one-loop shift.

During this migration, a defensive `startTick >= endTick` rejection may be kept in the overlap candidate path, but the preferred long-term ownership is to enforce the validity invariant at the note creation/commit boundary.

`DisplayWindowUtils::noteIntersectsWindow` remains unrelated to this domain rule.

---

## Gate 1 — bidirectional candidate equality

```text
ObservedCandidateIds == GeometrySelectedIds
```

- **Observed:** snapshot already-sounding at S, plus note-ons with `S <= t < E`; never drop on off; never include `t == E`. Zero-length notes rejected.
- **Geometry:** same-pitch notes that pass `existingNoteOverlapsIncomingHold` (half-open intersection above). Not `noteIntersectsWindow`.

Helper: `OverlapNoteIdObservation` (native / later Track collector). Not called from production overlap.

### Equality holds

Interior start-during-hold; start exactly at E excluded; start at E−1; already sounding at S; ended-during-span kept; nested same-pitch; other pitch excluded; wrap tail→head vs incoming at tick 0 (observation uses the same one-loop shift so a wrap note is still sounding at S=0); incoming in tail against wrap; incoming ending after wrap; endpoint touch at S excluded; zero-length excluded.

Required fixtures still owed: split-chunk on/off; prior Shorten/Hide companions; muted/solo (after Gate 2). 021304 same-pitch count still open on Gate 0.

---

## Gates 2–4 (not started)

- **Gate 2** — muted / solo / slot-mute continue internal playback; port emit last.
- **Gate 3** — zero candidates must not fall back to `gatherCommittedEvents` / `reconstructDisplayNotes`.
- **Gate 4** — span lookup cost scales with candidate count, not loop size.

---

## Out of scope

Pitch-retarget discovery, pitch indexes, visual-cache reuse, chunk-sliced source views, PLAYING precompute, interval reservation, RC-J, stored-MIDI verification, deferring capture note-on until note-off.
