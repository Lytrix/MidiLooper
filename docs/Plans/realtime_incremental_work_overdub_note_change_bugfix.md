# Overdub note-off cost (S0e follow-through)

**Status:** Active — RC-K1 / RC-K2 / RC-K3 firmware shipped; device re-measure pending  
**Date:** 2026-08-12  
**Parent:** [`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md) §31n / §31o  
**Evidence (before):** [`session_20260812_204221.log`](../../captures/session_20260812_204221.log)  
**Decision:** [DEC-033](../DECISION_LOG.md#dec-033-overdub-overlap-ignores-per-note-channel)

This is a **targeted bounded-work fix** on the measured overdub note-off path. It is **not** admission-model S1 (`RuntimeWorkBudget`). Admission remains deferred.

---

## Architecture checkpoint

- **Ownership change?** NO. `Loop` already owns `overdubSourceViewEvents_`; `overdubSourceViewNotes_` is a sibling derived member. `NoteUtils` keeps owning reconstruction.
- **State transition change?** NO. Cache fill/clear rides `establishOverdubSourceView` / `clearOverdubSourceView`.

---

## Before (S0e, [`204221`](../../captures/session_20260812_204221.log))

Per 5 s window during OVERDUBBING (`DIAG,name,maxUs,overCount`), grown loop `loop_events=4257`, `DISP` notes=2109:

| Probe | maxUs | Meaning |
|-------|-------|---------|
| `notechg` | 274409 | `accumulatePendingNoteChangesForIncomingNote` |
| `noterecon` | 176625 | `reconstructDisplayNotes` over the source view |
| `notepair` | 97855 | candidate scan including `noteIdHasChannel` |
| `noteappend` | 59 | `appendCaptureEventWithResult` |

`usbnote` ≈ `notechg`. Display FPS dropped (22.1 → 15.2) while per-frame cost stayed 13–16 ms — the display was starved, not slow.

---

## RC-K1 — reconstruction dedup is quadratic

**Invariant:** `reconstructNotesImpl` deduplicates in time proportional to the note count, not its square.

`reconstructNotesImpl` rescanned every accepted note for each projected note (`std::any_of`). At 2109 notes that is 2,223,486 comparisons; 170,170 µs / 2,223,486 = 76.5 ns each.

**Fix:** collapse by `(note, startTick, endTick)` in first-seen order. `noteId` stays out of the key.

RC-K1 first used an ordered `std::set` (one `extmem_malloc` tree node per note). Boot visual-cache rebuild of 1430 notes then measured **1.38 s** ([`215128`](../../captures/session_20260812_215128.log), [`215357`](../../captures/session_20260812_215357.log)). **RC-K1b:** rank into one `ExternalMemoryFirstAllocator` vector, `qsort`+unique, restore original index order (`std::sort` templates overflowed RAM1/ITCM).

Deleting the dedup was rejected: `test_reconstruct_dedupes_identical_segments` requires collapse; capture-level event dedup is off while a source view exists; `upsertSourceTransform` is a different stage (pending Shorten/Hide per noteId).

**Tests:** `test_noteutils_reconstruct` including `test_reconstruct_dedupes_many_identical_geometry_notes`.

**Commit:** `a836179` (set) · RC-K1b (vector rank) follows

---

## RC-K2 — per-candidate channel lookup

**Invariant:** overlap candidate selection costs one pass over the pass note list, with no per-candidate event scan.

`noteIdHasChannel` walked all 4257 PSRAM source events for every same-pitch candidate because `DisplayNote` has no channel field.

**Fix:** delete the helper and the filter. Loop notes are already loop-scoped. NOTE_EDIT already resolves overlap with `track.getMidiChannel()` and never inspects a per-note channel. [DEC-033](../DECISION_LOG.md#dec-033-overdub-overlap-ignores-per-note-channel).

**Behaviour delta:** a stored same-pitch note whose recorded channel differs from the incoming channel now participates in overlap resolution.

**Tests:** `test_pending_shorten_ignores_recorded_channel`.

**Commit:** `91a4a77`

---

## RC-K3 — full reconstruction repeated per note-off

**Invariant:** the overdub source view is reconstructed once per overdub pass, not once per note-off.

`overdubSourceViewEvents_` is immutable between `establishOverdubSourceView` and `clearOverdubSourceView`.

**Fix:** `Loop::overdubSourceViewNotes_` filled in `establishOverdubSourceView`, cleared in `clearOverdubSourceView`. `accumulatePendingNoteChangesForIncomingNote` and `gatherOverdubSourceViewNotesInWindow` read the member. Not `CachedNoteList` — that hashes its whole input on every call, and the per-loop instance is keyed to live materialized events.

**Tests:** `test_overdub_source_view` — notes populated after `beginCapture(Overdub)`, stable across capture appends, empty after discard/commit.

**Commit:** `6df3f0a`

---

## RC-K1–K3 verified on device — [`223033`](../../captures/session_20260812_223033.log)

The note-off gate is met. Every DIAG window reads `noterecon` 0; `notechg` peaks at 3.6 ms and otherwise sits at 0.75–2.0 ms, against the 5 ms soft ceiling.

| Probe | Before ([`204221`](../../captures/session_20260812_204221.log)) | After ([`223033`](../../captures/session_20260812_223033.log)) |
|-------|--------|-------|
| `noterecon` | 176625 | **0** — no longer on the note-off path |
| `notepair` | 97855 | **3616** peak, then 700–2000 |
| `notechg` | 274409 | **3626** peak, then 750–2000 |
| `noteappend` | 59 | 83–558 |

RECORD stays cheap (`midisvc` 0.6–0.7 ms, `clockrate` 47–48).

---

## RC-L1 — one external-memory allocation per note in projection

**Invariant:** a reconstruct performs a constant number of external-memory pool operations, not one per note.

RC-K3 exposed this; it did not create it. Total work per overdub pass fell, but the reconstruct that used to be spread one-per-note-off now runs in a single synchronous block inside `Track::startOverdubbing` → `Loop::beginCapture` → `establishOverdubSourceView`, with no MIDI service point. [`223033`](../../captures/session_20260812_223033.log) `ODUB,stage,begin_capture`:

| Loop | Notes | `begin_capture` |
|------|-------|-----------------|
| 90 bars, first overdub after boot restore of 8 slots | 2084 | **1377674 µs** |
| 65 bars | 1035 | 58476 |
| 65 bars | 1221 | 86961 |
| 65 bars | 1284 | **295974** |
| 65 bars | 1392 | **409540** |

`msi` follows at 1.400 s; BPM collapses 110 → 31.9; the first USB note lands 1.76 s after the button. On the unchanged 65-bar loop, +35 % notes costs 7× — steeper than any N², so the per-note term itself is growing.

### Root cause (code, not measurement)

`IntervalProjection::projectDisplayNotes` allocated and freed one external-memory block **per canonical span**: `generateEquivalentIntervals` returned `ProjectedIntervalVec` (an `ExternalMemoryFirstAllocator` vector) by value with a single `reserve`, and the result was destroyed each iteration. A 2084-note reconstruct therefore issued ~4168 pool operations. `projectNoteIntervals` did the same twice per span (`candidates` + `selected`).

`extmem_malloc` routes to smalloc, and `sm_malloc_pool` restarts a linear walk of the pool header chain from `spool->pool` on every call, verifying each block's hashed tag — no free list. Per-allocation cost is the block count before the first adequate gap, which grows as the session's chunks fill the low pool. The repo already measured a full traversal of that chain at **593 ms** (RC-S0b). That is why cost tracks session history and why the worst case followed `Queuing boot playback loop slot restore 8 pending`.

Same mechanism explains the RC-K1 boot hang: its `std::set` added a second per-note allocation whose nodes were all live at once, lengthening the chain mid-call. RC-K1b removed that one; the projection allocation predates RC-K1 and was untouched.

**Fix:** hoist the buffers out of the per-span loops and `clear()` per span — capacity is retained, so a batch allocates once. `generateEquivalentIntervals` and `selectProjectedIntervalsForDisplay` keep their by-value forms for single-span callers and tests, and gain out-param overloads for batch callers. Same candidates, same order, no behaviour change. Also fixes the boot visual-cache backfill, which reaches the same function via `rebuildVisualCacheIdleSlice`.

Durable rule: [`INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](../Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md) § Batch loops.

**Tests:** `pio test -e native` 1046/1046. `teensy41-capture-serial` RAM1 free 8160 B (unchanged).

---

## Still open

- **Overdub entry is synchronous.** Even with RC-L1, `establishOverdubSourceView` runs a full `gatherCommittedEvents` plus reconstruct inside `startOverdubbing` — gather alone measured 35–119 ms in [`204221`](../../captures/session_20260812_204221.log). Deferring or slicing it changes when `overdubSourceViewNotes_` becomes valid relative to the first note-off of the pass: a state-transition change, so design session before any patch. Not admission S1.
- **Overdub stop.** [`223033`](../../captures/session_20260812_223033.log) at 535.392 s: next window `midisvc` 2.16 s (`usbdisp` 2.16 s), `clockrate` 47 → 31 → 0. Separate from entry, uninvestigated.
- **RC-J** — final stop `clockrate` 12. Unchanged, still behind S0b.
- **RC-S0c** — 20 `RING,overflow` in `223033`; some `ODUB,stage` lines were dropped.

**Device re-measure after RC-L1:** `begin_capture` on a grown loop must not scale with session history; target is the gather term alone.
