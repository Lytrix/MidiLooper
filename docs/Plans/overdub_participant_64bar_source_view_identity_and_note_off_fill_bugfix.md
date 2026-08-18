# 64-bar source-view identity and note-off fill

**Status:** Stage 1 membership **shipped**. Stage 1b prepared-session identity **in tree**. Device HITL open (wait STOPPED for 64-bar `lcr,mat` after track switch). Stage 2 **not started**.  
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md)  
**Evidence:** [`122848`](../../captures/session_20260818_122848.log), [`123803`](../../captures/session_20260818_123803.log), [`125542`](../../captures/session_20260818_125542.log)

**Does not authorize:** raising `kOverlapNoteIdSetCapacity`; patching occupy/B collect; deleting `overdubSourceView`; changing `notePresentAt`; shrinking `kOverdubSourceWindowBars`; hydrate; LCR slices while PLAYING; cold LCR on the overdub button; Stage 2 consume merge until Stage 1 device PASS.

---

## Debugging boundary

```text
tryCopyPreparedSpansToDisplayNotes window NoteOn membership
    ← Stage 1 (shipped)
prepared session length identity (tryCollect / publish / idle re-queue)
    ← Stage 1b (this slice)
rebuildOverdubSourceView reconstruct fallback
    ← must not run after span copy succeeds on the matching loop
occupy / B collect
    ← length miss is honest from=miss; do not patch B
collectConsumeWindow prepared-pitch merge
    ← Stage 2 after Stage 1 device PASS
```

RC1 is **source-view identity divergence** from cap-128 span-copy abort. RC1b is **prepared-session identity**: the one-shot LCR session was the 1-bar loop. Occupy exposed it because collect did not check live length.

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

**Native:** `test_source_view_span_copy_keeps_window_note_on_ids_past_occupy_set_capacity`, `test_source_view_rebuild_uses_prepared_spans_past_occupy_set_capacity`. Native 1335/1335 at ship. 030219 filter test unchanged.

**Device** [`125542`](../../captures/session_20260818_125542.log): **`a=1,b=0` = 0** (membership fix held). 64-bar enter still `from=win` — RC1b.

---

## RC1b — prepared session is one loop; occupy collect skipped length

[`125542`](../../captures/session_20260818_125542.log): one `lcr,mat` at 15.2 s, `lcr,vch notes=32` (1-bar). `deviceGateFinished` is one-shot; slices are STOPPED-only. 64-bar session 2: `why=open,from=win,ev=997,notes=1019` then occupy `from=prep` `ao=1` 34 ms later.

`tryResolvePreparedWindow` / `tryCopyPreparedSpansToDisplayNotes` miss when live `loopLengthTicks` ≠ prepared session (50688 ≠ 768). `tryCollectPreparedPresentNoteIdsAtTick` did not. `publishPreparedOverdubPass` restamped the 64-bar revision onto the 1-bar session.

**Fix:**

- Collect requires live `loopLengthTicks` == session and checkpoint length (same miss as resolve/copy).
- Publish no-ops on length mismatch (no append, no restamp).
- `maybeQueueContentResolutionDeviceGate` resets and begins when finished session length ≠ selected loop. STOPPED-only slices unchanged.
- `lcr,src` includes `live=` / `prep=` when `from` is not `span`.

**Native:** `test_prepared_session_length_mismatch_misses_resolve_copy_collect`, `test_publish_prepared_overdub_pass_ignores_loop_length_mismatch`, `test_prepared_session_remeasure_after_reset_copies_long_loop`. Native 1338/1338.

**Device gate (unchanged criteria):** 64-bar `why=open,from=span`; in-window occupy `ao=0`; `a=1,b=0` = 0; remaining `eq=0` only `a=0,b>0`; `late_clk=0`. After 1-bar, select 64-bar, stay **STOPPED until `lcr,mat`** for that loop (`vch` notes in the 64-bar class, not 32), then overdub. Unprepared enter is still `from=win`. Do not start Stage 2.

---

## RC2 — note-off fill (class 3, after Stage 1 PASS)

Prepared-ready consume merges **this pitch** into the source view; `resolveWindow` only on prepared miss. `preparedWindowReady` is not a blanket skip. Empty occupy still walks source-view notes. Not this slice.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Reuse | YES — extend `tryCollectPreparedPresentNoteIdsAtTick`, `publishPreparedOverdubPass`, `maybeQueueContentResolutionDeviceGate` |
