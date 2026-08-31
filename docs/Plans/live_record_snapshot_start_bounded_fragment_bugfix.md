# Live-record snapshot start: journal-capped store walk

**Status:** Firmware in tree. Native **1507/1507**. RAM1 locals **6944**. Device non-growth capture remains. Do not check off OpenSpec `3.5` / `3.6`.  
**Date:** 2026-08-31  
**Kind:** bugfix  
**Parent:** [`snapshot_publication_timing_bugfix.md`](snapshot_publication_timing_bugfix.md)  
**Evidence:** [`161532`](../../captures/session_20260831_161532.log) / [`165637`](../../captures/session_20260831_165637.log) over-8.82 ms parents are RECORDING `start` (`pass_change=0`) from full-store `copyEventsTo` + scan.

**Does not start:** Row-22 admission cardinality, N, leftover-fill, `RuntimeWorkBudget::admit`, uncommitted N=2 / pages=16 knobs, `T_RT_reserve`, OpenSpec `3.5` / `3.6` checkoff.

---

## Invariant

A RECORDING snapshot start consumes only the not-yet-consumed `capture.store` events whose tick is `< includeUntil`, beginning at the persisted capture-store cursor. Each admission produces at most one journal-capped changeset (`kSnapshotWrapJournalMaxChanges` = 32). Historical events before that cursor are never rescanned. Start cost is bounded by the fragment, not by historical store length.

## Architecture checkpoint

- Ownership: NO — owner stays `startNextPendingConstruction` / `PendingSnapshotPublishBox`
- Musical transitions: NO
- Row-22 admission cardinality: unchanged — one fragment start XOR one Row-21 step
- Interval path: no new owner on `handleMidiInput` / clock
- Reuse: YES — pairing and journal caps stay `appendNextPassChangeFragment`. Source traversal is not `refillPassChangeBatch`

## Cursor invariant

```text
appliedLiveRecordEventIndex / nextStoreIndex
    = first capture.store event not yet consumed

appliedLiveRecordCompletedTicks
    = highest completed-live-record boundary for which every store event
      with tick < that boundary has been consumed
```

Advance ticks only when the window is fully consumed. `tick == includeUntil` belongs to the next window. The index is authoritative after first start; do not re-`lowerBoundIndex` from ticks. `openNotes` survives fragments and bar windows until paired or the box is cleared. Session-exit resets capture-store progress after persisting the index.

## Acceptance

- Native: range, pairing, journal cap, `tick == includeUntil` boundary
- Device: RECORDING `start` duration does not grow with historical store size. Keep `pass_change=0` on live-record rows. Native alone does not close the RC
- Do not use 3.219 ms or 8.82 ms as a guarantee
