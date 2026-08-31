# Skip parent note lookup on live-record start

**Status:** Shipped. Device PASS [`192422`](../../captures/session_20260831_192422.log).  
**Date:** 2026-08-31  
**Kind:** bugfix  
**Parent:** [`live_record_snapshot_start_bounded_fragment_bugfix.md`](live_record_snapshot_start_bounded_fragment_bugfix.md)  
**Evidence:** [`190654`](../../captures/session_20260831_190654.log) — copy/scan bound holds; RECORDING `start` 375→1007 µs tracks `note_lookup` 12→805 µs. Closed by [`192422`](../../captures/session_20260831_192422.log): `note_lookup=0` on all 32 live-record starts; after-first `start` 167–233 µs.

**Does not start:** NoteId directory, bar-scoped `findNoteAtPitch` for session-exit, N, leftover-fill, `T_RT_reserve`, OpenSpec `3.5` / `3.6` checkoff.

---

## Invariant

A `fromCaptureStore` fragment does not call `LoopSnapshot::findNoteAtPitch`. The persisted store cursor decides which events are new. `openNotes` is the sole pairing mechanism for live-record NoteOffs. The parent snapshot is not an identity source on this path.

## Architecture checkpoint

- Ownership: NO — `appendNextPassChangeFragment`
- Musical transitions: NO
- Row-22 admission cardinality: unchanged
- Reuse: YES — skip the existing call when `progress.fromCaptureStore`

## Device

RECORDING live-record rows (`root=0x0`, `pass_change=0`): `note_lookup` stays flat; the [`190654`](../../captures/session_20260831_190654.log) start growth that tracked it is gone; remaining `start` cost stays on `refill` / `event_scan` / `change_build` / `construct`.

Device [`192422`](../../captures/session_20260831_192422.log): 32 live-record starts, `note_lookup=0` on every row. After-first `start` 167–233 µs (last 176). After-first leftover 27–70 µs. Parent max 4.771 ms. First-start leftover (~2.5 ms) is unchanged and out of scope.
