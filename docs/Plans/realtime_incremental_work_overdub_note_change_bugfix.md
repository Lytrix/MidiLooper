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

RC-K1 first used an ordered `std::set` (one `extmem_malloc` tree node per note). Boot visual-cache rebuild of 1430 notes then measured **1.38 s** ([`215128`](../../captures/session_20260812_215128.log), [`215357`](../../captures/session_20260812_215357.log)). **RC-K1b:** rank into one `ExternalMemoryFirstAllocator` vector, `sort`+`unique` by key, restore original index order.

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

## After (device re-measure)

S0e probes stay in place as the gate. Expected vs [`204221`](../../captures/session_20260812_204221.log):

| Probe | Before | Expected after |
|-------|--------|----------------|
| `noterecon` | 177 ms on every note-off | 0 on the note-off path (reconstruct moved to overdub entry) |
| `notepair` | 98 ms | one pass over the cached note list |
| `notechg` | 274 ms | well under the 5 ms observational soft ceiling |
| `noteappend` | 59 µs | unchanged |

Device re-run: continuous overdub over a grown loop, `teensy41-capture-serial`. Also expect `RING,overflow` pressure to ease once `handleMidiInput` stops blocking.

**After capture:** _pending upload / re-run._
