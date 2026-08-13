# Overdub overlap — playback observation (Gates 0–4)

**Status:** Active — Gate 0 native landed; Gate 1 native fixtures landed, **not closed** (two product-rule pins)  
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

## Gate 1 — bidirectional candidate equality (fixtures landed, not closed)

```text
ObservedCandidateIds == GeometrySelectedIds
```

- **Observed:** snapshot already-sounding at S, plus note-ons with `S <= t < E`; never drop on off; never include `t == E`.
- **Geometry:** same-pitch `DisplayNote.noteId`s selected by `noteIntersectsWindow(start, end, S, E-S, loopLen)`.

Helper: `OverlapNoteIdObservation` (native / later Track collector). Not called from production overlap.

### Equality holds

Interior start-during-hold; start exactly at E excluded; start at E−1; already sounding at S; ended-during-span kept; nested same-pitch; other pitch excluded; wrap tail→head vs incoming at tick 0; incoming ending after wrap.

### Pins (do not paper over)

| ID | Case | Observed | Geometry (`noteIntersectsWindow`) |
|----|------|----------|-----------------------------------|
| **G1-end-touch** | Existing ends exactly at S (`[500,1000)` vs incoming `[1000,1800)`) | not a candidate | **is** a candidate (endTick is a probe) |
| **G1-wrap-probe** | Wrap note `[L-40,20)` vs incoming `[L-30,L-5)` (25 ticks) | candidate (still sounding) | **miss** (16th-step probes skip the window; wrap on/off sit outside it) |

These are product-rule disagreements, not collector bugs. Do **not** change `DisplayWindowUtils` to make Gate 1 green. Do **not** change observation to match the probe miss.

**Pin before firmware:** overlap selection uses `[S,E)` sounding (observation) **or** stays on `noteIntersectsWindow` (including end-touch and 16th-probe holes). Gate 1 is closed only after that pin and bidirectional equality on the required fixtures.

Required fixtures still owed after the pin: split-chunk on/off; prior Shorten/Hide companions; muted/solo (after Gate 2).

---

## Gates 2–4 (not started)

- **Gate 2** — muted / solo / slot-mute continue internal playback; port emit last.
- **Gate 3** — zero candidates must not fall back to `gatherCommittedEvents` / `reconstructDisplayNotes`.
- **Gate 4** — span lookup cost scales with candidate count, not loop size.

---

## Out of scope

Pitch-retarget discovery, pitch indexes, visual-cache reuse, chunk-sliced source views, PLAYING precompute, interval reservation, RC-J, stored-MIDI verification, deferring capture note-on until note-off.
