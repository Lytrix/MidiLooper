# Overdub overlap — playback observation (Gates 0–4)

**Status:** Active — Gates 0–4 native landed; collection wired; note-off consumes overlapNoteIds; stop totals logging wired  
**Branch:** `feature/overdub-playback-observation-overlap`  
**Date:** 2026-08-13  
**Kind:** refinement  
**Cursor source:** `~/.cursor/plans/restore_overdub_gather_28c7ca69.plan.md`  
**Parent:** [`realtime_incremental_work_overdub_source_view_refinement.md`](realtime_incremental_work_overdub_source_view_refinement.md)  
**Trigger:** [`session_20260813_021304.log`](../../captures/session_20260813_021304.log)  
**Scheduling:** R1A / G1 in [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md)

Native Gates 0–4 landed. `PendingNote.overlapNoteIds` collects hold-duration candidates (snapshot already-sounding at S + playback note-ons). Note-off looks up those ids in `overdubSourceViewNotes_` via `appendNotesForIds`, then applies geometry + `[S, E)`. Empty set is Add only (Gate 3). Overlap-hold stop totals emit one `#CAP,DIAG,overlap_hold` at seal. Option B stays withdrawn. PLAYING idle prebuild was reverted (`73f0489`).

---

## Terminology

`OverlapNoteIdObservation` is a **test/diagnostic** concept, not a production source of truth. Production overlap selection is governed by the **normalized note geometry** and the `[S, E)` intersection rule (`existingNoteOverlapsIncomingHold`).

Do not treat observed membership, `collectObservedOverlapNoteIds`, or a later playback collector as the overlap authority.

---

## Three-layer invariant

```text
1. Diagnostic observation (test only)
   Which committed NoteIds would a playback-sounding collector report for [S, E)?
   Used to prove equality with geometry selection. Not the production rule.

2. Production selection — normalized note geometry
   Same-pitch notes whose linearized [start,end) intersects incoming [S, E).

3. Edit semantics
   Shorten/Hide via existing accumulatePendingNoteChangesFromSourceNotes
```

- `ActiveNoteLedger` = current MIDI voice (one slot per output channel + pitch; clears on off). Not the candidate store.
- `PendingNote.overlapNoteIds` = candidate ids collected during the incoming hold (set; keep after playback off). Collection is wired. Note-off consumes the set: `appendNotesForIds` on `overdubSourceViewNotes_`, then geometry + `[S, E)`. Empty set skips lookup.
- Committed storage = authoritative geometry.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. Track owns `PendingNote` and `sendMidiEvent`. Loop owns committed geometry + Shorten/Hide. |
| **State transition change?** | NO for Gate 0/1 fixtures. YES later: committed playback note-ons mutate `PendingNote` ids. YES (Gate 2): mute suppresses the track MIDI channel on the output ports. |

---

## Gate 0 — RT-state / capacity (native landed, device count open)

`OverlapNoteIdSet` is a fixed-capacity set (`kOverlapNoteIdSetCapacity = 128`). Insert is contains-then-add. Overflow fails; the set never grows. Invalid `NoteId` 0 is rejected.

Provisional capacity fits an 8-bar 16th-grid same-pitch full-loop hold (128 unique ids). Device same-pitch count is a one-shot idle `#CAP,DIAG,stored_notes,…,max_same_pitch,…` line from a clean visual cache.

[`152940`](../../captures/session_20260813_152940.log): track 0 slot 4 (68 bars) `notes=1903 unique=1903 max_same_pitch=322`. Track 6 slot 0 `max_same_pitch=195`. Both exceed capacity 128. Overflow on that hold is a failed gate, not heap growth. Do not raise capacity without a decision.

This idle `stored_notes` emit is the total-notes / `max_same_pitch` inventory. Keep `maybeLogStoredNoteCount`.

Native: `test_overlap_note_id_observation` Gate 0 cases; `test_display_note_count` for the inventory helper.

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

## Gate 1 — diagnostic equality with production geometry

```text
ObservedCandidateIds == GeometrySelectedIds
```

This equality is a **test gate**. `GeometrySelectedIds` is the production selection set.

- **Geometry (production):** same-pitch notes that pass `existingNoteOverlapsIncomingHold` (normalized spans + half-open `[S, E)` intersection above). Not `noteIntersectsWindow`.
- **Observed (diagnostic):** snapshot already-sounding at S, plus note-ons with `S <= t < E`; never drop on off; never include `t == E`. Zero-length notes rejected.

Helper: `OverlapNoteIdObservation` — native/test only. Do not call it from production overlap and do not promote it to a Track owner.

### Equality holds

Interior start-during-hold; start exactly at E excluded; start at E−1; already sounding at S; ended-during-span kept; nested same-pitch; other pitch excluded; wrap tail→head vs incoming at tick 0 (diagnostic sounding uses the same one-loop shift so a wrap note is still sounding at S=0); incoming in tail against wrap; incoming ending after wrap; endpoint touch at S excluded; zero-length excluded; split-chunk reconstructed span (on@50 / off@400 vs incoming `[300, 350)`); prior Shorten companion uses shortened `[50, 119)`; prior Hide companion absent; unrelated other-pitch companion not selected.

Required fixtures still owed: muted/solo same-id check after playback collection is wired. Gate 0 same-pitch count is measured in [`152940`](../../captures/session_20260813_152940.log) (`max_same_pitch=322` on the 68-bar loop). Split-chunk storage and companion seal stay owned by `test_pending_note_change`; Gate 1 uses those effective `DisplayNote` spans. Geometry selection does not take mute as an input.

---

## Gate 2 — mute suppresses the track MIDI channel on the output ports (native landed, device open)

Enabled slots keep running `playMidiEvents` / `playMidiEventsForSlot`. `isTrackAudible` and `slotMuted` feed `PlaybackMidiOutput::shouldSend` into `Track::sendMidiEvent`. Cursor, merged stream, and `ActiveNoteLedger` still advance.

Mute-edge silence is MIDI-output only: track mute sends CC 123 on that track's `midiChannel` through `MidiHandler` (USB, DIN, USB Host). Slot mute sends NoteOff for that slot's active ledger notes on the same channel/ports. Neither clears ledger or `pendingNotes`. Unmute does not dump a backlog.

Native (`test_playback_midi_output`): cursor advances while MIDI send is suppressed; unmute does not resend crossed events; wrap still advances while muted; `ledger.clear()` (all-notes-off) empties the ledger, mute silence does not. Device still owed: mute mid-note silences that track channel on the output ports; unmute does not replay missed note-ons.

## Gate 3 — empty candidates do not gather or reconstruct (native landed)

If candidate discovery produces zero `NoteId`s, do **not** call `gatherCommittedEvents` / `reconstructDisplayNotes` on note-off. A miss is a failed selection, not a quiet reconstruct.

`OverlapCandidateLookup::shouldLookupSpans` is false for an empty set. `appendNotesForIds` copies from an already-available `DisplayNote` list only. Production note-off consumes `PendingNote.overlapNoteIds`; empty set is Add only and does not fall back to the full source view or gather. Native: `test_overlap_candidate_lookup` and `test_pending_note_change` (`fullMaterializeCount == 0` after reset, including companion edit rows and empty-id overlap).

## Gate 4 — lookup cost is one pass, not candidates × loop (native landed)

Do **not** flatten the loop and call `findLinearNoteSpanForNoteId` per id (that helper walks the event vector twice per note). Do **not** build a NoteId index yet.

`appendNotesForIds` walks the already-available `DisplayNote` list once and stops when every candidate is found. On a 3714-note list with ids 10/20/30 it examines 30 notes, not 3714×3. Production note-off uses this on `overdubSourceViewNotes_`. Native: `test_lookup_examines_through_last_match_not_candidates_times_loop`.

## Hold-candidate collection (wired)

`Track::noteOn` snapshots same-pitch notes already sounding at S from `overdubSourceViewNotes_` into `PendingNote.overlapNoteIds`. `Track::sendMidiEvent` inserts committed playback note-on `NoteId`s of the same pitch while the hold is open. Playback offs do not erase. Native: `test_overlap_hold_candidates`.

Collection bodies stay in `TRACK_COLD_MEM` (`TrackCaptureInput.cpp`). Do not include `OverlapHoldCandidates.h` / `OverlapNoteIdObservation.h` from firmware TUs — those header inlines land in ITCM and cross a 32 KB RAM1 block. `silenceTrackMidiOutput`, `silenceSlotMidiOutput`, and `sendAllNotesOff` are also `TRACK_COLD_MEM` so the overdubbing call site in `sendMidiEvent` fits the last ITCM block. After withdrawn-path cleanup, `teensy41-capture-serial` RAM1 is `code:424444` padding:1540 free:7968 — same 32 KB ITCM block.

## Note-off consumes overlapNoteIds (wired)

`Loop::accumulatePendingNoteChangesForIncomingNote` takes `PendingNote.overlapNoteIds`. Empty set skips `appendNotesForIds` (Add only). Non-empty set copies matching notes from `overdubSourceViewNotes_` in one pass, then `accumulatePendingNoteChangesFromSourceNotes` applies geometry + `[S, E)` (`existingNoteOverlapsIncomingHold`, not `noteIntersectsWindow`) and writes Add/Shorten/Hide. The production overlap body lives in `LoopPendingNoteChange.cpp` — do not include `OverlapNoteIdObservation.h` there. Native: `test_pending_note_change` including `test_empty_overlap_ids_add_only_when_source_overlaps`.

## Overlap-hold stop totals (wired)

One `#CAP,DIAG,overlap_hold,…` at overdub-stop seal (`logOverdubStopStage` when stage is `seal`). Counters live on `Loop` (`OverlapHoldTotals`). Increment on each evaluated note-off (integers only). `micros()` wraps `appendNotesForIds` only. Add/Shorten/Hide are the current pending-row counts after that note-off.

Reset in `establishOverdubSourceView` only. `clearOverdubSourceView` runs inside commit/discard **before** the seal-stage emit, so resetting there would wipe the line. A Skipped empty capture still prints zeros if establish ran.

`establishOverdubSourceView`, `clearOverdubSourceView`, the note-off increment, and `emitOverlapHoldTotals` are `LOOP_COLD_MEM` / `FLASHMEM`. Do not include `OverlapHoldCandidates.h` or `OverlapNoteIdObservation.h` from firmware TUs. Counters sit on the EXTMEM `Loop` shell, not RAM1.

Native: `test_pending_note_change` (empty set increments `emptySets` not `lookedUp`; non-empty increments `lookedUp` and `examined`; establish resets).

4-bar device confirm [`162856`](../../captures/session_20260813_162856.log) — slot 1, 57 notes, `max_same_pitch=4`, three overdubs, `noterecon=0`, `clockrate=47` after the first window. Display 57→84→104→112.

| Stop | note_offs | empty_sets | looked_up | max_ids | max_examined | max_lookup_us | add | shorten | hide | notechg | notepair |
|------|-----------|------------|-----------|---------|--------------|---------------|-----|---------|------|---------|----------|
| 1 | 19 | 0 | 19 | 7 | 57 | 41 | 19 | 0 | 0 | 68 | 3 |
| 2 | 8 | 0 | 8 | 5 | 82 | 209 | 8 | 1 | 1 | 417 | 136 |
| 3 | 8 | 3 | 5 | 4 | 84 | 319 | 8 | 4 | 2 | 601 | 231 |

`sum_examined / looked_up` is 57, 82, and 83.6 — each lookup walked the whole source view. `overflows=0`. Third-stop `ODUB,stop,seal` is absent from the log; `overlap_hold` is present (Tier-A).

68-bar slot 4 [`163422`](../../captures/session_20260813_163422.log) — `VCACHE` `total,68` `notes,1828`, `loopLengthTicks=52224`, five overdubs, `noterecon=0`. Capture starts mid-session (ring overflow). `overflows=0`, `max_ids` 1–5.

| Stop | note_offs | empty_sets | looked_up | max_ids | max_examined | max_lookup_us | add | shorten | hide | notechg | notepair |
|------|-----------|------------|-----------|---------|--------------|---------------|-----|---------|------|---------|----------|
| 1 | 11 | 8 | 3 | 1 | 1592 | 959 | 11 | 0 | 0 | 986 | 3 |
| 2 | 14 | 11 | 3 | 4 | 1599 | 439 | 14 | 0 | 2 | 961 | 1 |
| 3 | 14 | 7 | 7 | 4 | 1599 | 1100 | 14 | 0 | 1 | 2705 | 66 |
| 4 | 29 | 21 | 8 | 2 | 1605 | 1029 | 29 | 1 | 1 | 1052 | 56 |
| 5 | 21 | 15 | 6 | 5 | 1612 | 543 | 21 | 2 | 2 | 775 | 127 |

Stops 1–3: `sum_examined / looked_up` equals `max_examined` (1592 / 1599 / 1599). First-stop `notechg` 986 and `max_lookup_us` 959. `begin_capture` 83 ms on stop 1; 8.82 / 8.75 / 8.97 s on stops 2, 3, 5 (`PERS,bundle,LoopUndoHistory` 25.2 s / 26.6 s) — not this path.

The 4-bar and 68-bar lookup numbers exist. Production note-off is `PendingNote.overlapNoteIds` → `appendNotesForIds(overdubSourceViewNotes_)` → geometry + `[S, E)`. Lookup-source change is not started.

---

## Withdrawn-path cleanup (removed)

Option A slice-cache tests, Option B windowed matrix, `gatherOverdubSourceView*InWindow`, `gatherCommittedNoteEventsForPitch`, `appendChunkRefNoteEventsForPitch`, and `forEachChunkEvent` are removed. `test_overdub_source_view` keeps establish/clear/immutability cases against `overdubSourceViewEvents_` / `overdubSourceViewNotes_`. `committedEventsFullMaterializeCount` stays for Gate 3. `maybeLogStoredNoteCount` stays (total-notes inventory).

`gatherCommittedEventsInWindow` stays — visual cache, display, and playback still call it. Gate 1 still owes a muted/solo same-id check after collection.

---

## Out of scope

Pitch-retarget discovery, pitch indexes, visual-cache reuse, chunk-sliced source views, PLAYING precompute, interval reservation, RC-J, stored-MIDI verification, deferring capture note-on until note-off.
