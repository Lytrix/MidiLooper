## 1. Stage 0 — gates

- [x] 1.1 D0 attribution PASS — [`035414`](../../../captures/session_20260814_035414.log)
- [x] 1.2 Amend plan — [`loop_layer_d_overdub_rebuild_architecture.md`](../../../docs/Plans/loop_layer_d_overdub_rebuild_architecture.md)
- [x] 1.3 Propose this OpenSpec change (proposal, design, spec, ARCHITECTURE-REVIEW)
- [x] 1.4 Record DEC-036 in DECISION_LOG
- [x] 1.5 Post architecture gate in implementation session (D1 table from ARCHITECTURE-REVIEW)

## 2. Phase D1 — Incremental effective event source

- [x] 2.1 Inventory mutation hooks: `commitCapturePass`, undo/redo pass toggle, edit apply, load complete
- [x] 2.2 Promote `passesMaterializedStore_` to eager incremental maintenance (delta on mutation, not lazy on read)
- [x] 2.3 Add `range(start, length, out)` on effective store; revision counter
- [x] 2.4 Native equivalence: effective store matches `passes.materialize` after commit, undo, edit fixtures
- [x] 2.5 Remove full-loop `gatherCommittedEvents` from `beginOverdubSession` path (`reconstructDisplayNotes` removal → D2)
- [x] 2.6 Native guard: overdub entry does not increment full-materialize counters
- [x] 2.7 `pio test -e native` — 1141/1141 PASS (2026-08-14)

## 3. Phase D2 — Range-driven overdub source

- [ ] 3.1 Rewrite `establishOverdubSourceView` to use `effectiveEvents().range(overdubWindow)` only
- [ ] 3.2 Remove full-loop `reconstructDisplayNotes` from overdub entry; windowed note/span data for overlap only
- [ ] 3.3 Ensure `markDisplayCachesStale` does not synchronously block overdub entry
- [ ] 3.4 Native: `test_overdub_source_view`, overlap hold, pending note change fixtures PASS
- [ ] 3.5 `pio test -e native`
- [ ] 3.6 Device gate: `035414` class — `ODUB,begin_capture` < 50 ms; overlap behavior spot-check

## 4. Closeout

- [ ] 4.1 Update CURRENT_WORK, PROJECT_STATE, DELIVERABLE_TRACKING
- [ ] 4.2 Scope guard: D3 persist checkpoint and D4 range-first load are **not** in this change

## Out of scope (separate slices)

- Post-stop `PlaybackFullMaterialize` / `LegacyMidiEvents` at overdub stop
- `LoopPersist` slice count reduction
- In-overdub `midi_input` 300–400 ms display spikes
- Persisted checkpoint + tail (D3)
- `LoadLoopJob` range publication (D4)
