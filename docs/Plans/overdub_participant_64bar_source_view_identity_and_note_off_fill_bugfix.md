# 64-bar source-view identity and note-off fill

**Status:** Stage 1 **in tree** (device HITL open). Stage 2 **not started**.  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md)  
**Evidence:** [`122848`](../../captures/session_20260818_122848.log), [`123803`](../../captures/session_20260818_123803.log)

**Does not authorize:** raising `kOverlapNoteIdSetCapacity`; patching occupy/B collect; deleting `overdubSourceView`; changing `notePresentAt`; shrinking `kOverdubSourceWindowBars`; hydrate; Stage 2 consume merge until Stage 1 device PASS.

---

## Debugging boundary

```text
tryCopyPreparedSpansToDisplayNotes window NoteOn membership
    ← Stage 1 (this slice)
rebuildOverdubSourceView reconstruct fallback
    ← must not run after span copy succeeds
occupy / B collect
    ← do not patch; B already owns participant ids
collectConsumeWindow prepared-pitch merge
    ← Stage 2 after Stage 1 device PASS
```

RC1 is **source-view identity divergence**, not occupy identity. Occupy exposes it because B owns participant ids.

---

## RC1 — span copy aborted on occupy-set cap 128

**Chain:** `>128 window NoteOns` → `OverlapNoteIdSet::insert()` fails → span copy returns false → `reconstructDisplayNotes` → source-view rows leave prepared-span NoteId space → B ids can miss in A (`ao=1`, `a=1,b=0`).

1-bar stays under 128 (`from=span`, `eq=1`). 64-bar enter in [`123803`](../../captures/session_20260818_123803.log) / [`122848`](../../captures/session_20260818_122848.log) is `from=win` with ~1000 notes.

**Fix:** rebuild-local sorted vector of **unique window NoteOn IDs**. Binary search. Not `OverlapNoteIdSet`. 030219 window-NoteOn filter stays.

**Success invariant:**

1. Every copied span’s NoteOn ID is in the window-ID vector.
2. Every copied span is the prepared `NoteSpan` for that ID.
3. After membership succeeds, rebuild does not reconstruct.

`ao=0` for in-window present notes follows from that. Remaining `eq=0` after Stage 1 is only `a=0,b>0` (present at S, NoteOn outside the 16-bar window). `a=1,b=0` must be gone.

**Native:** `test_source_view_span_copy_keeps_window_note_on_ids_past_occupy_set_capacity`, `test_source_view_rebuild_uses_prepared_spans_past_occupy_set_capacity`. Native 1335/1335. 030219 filter test unchanged.

**Device gate:** 64-bar enter `from=span`. Occupied `from=prep`: `ao=0` in-window. `a=1,b=0` = 0. `late_clk=0`. Then stop — do not start Stage 2.

---

## RC2 — note-off fill (class 3, after Stage 1 PASS)

Prepared-ready consume merges **this pitch** into the source view; `resolveWindow` only on prepared miss. `preparedWindowReady` is not a blanket skip. Empty occupy still walks source-view notes. Not this slice.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Reuse | YES — extend `tryCopyPreparedSpansToDisplayNotes` |
