# Tasks — fixed-memory content engine (direct cutover)

Branch `rewrite/fixed-memory-engine` (RED / unbootable allowed). Authority: DEC-252 and `design.md` clauses. Plan
revision 3 is the baseline; no whole-plan revisions.

## E0 — semantic oracle

- [x] E0.1 Independent reference model for overlap/occupy (`test/test_engine_semantic_oracle/reference_model.h`).
- [x] E0.2 Register clauses OVL-0..8, ORD-1 with draft fixtures (12 cases green).
- [x] E0.3 CE-B1 counter-example and the effective-row identity as executable tests.
- [ ] E0.4 Owner review of OVL / ORD clauses; Q-OVL defaults encoded (HIB-8, HIB-9 found against legacy).
- [ ] E0.5 Register Stop closures, ledger closure, wrap, interval, interactions, undo, DEC-159, C1, controllers, switch timing.
- [ ] E0.6 Legacy cross-check harness; classify every disagreement (HIB / fixture error / owner question).
- [ ] E0.7 Device measurements on the current image:
  - non-content PSRAM / RAM2 census;
  - SD worst-case write latency;
  - display draw per band;
  - `U_isr` and `U_other`;
  - per-job grain costs.
- [ ] E0.8 Classify all 253 DECs as musical or mechanism.

## CE-B1 — window representation

- [x] B.1 Span-window reference model (START + TERMINAL entries, terminal set T = 256), exact-stream equality with
      expanded rows, tight bound `startRows + T`, comparison against the three row-based options (`test_engine_window_bound`).
- [x] B.2a Effective-polyphony analysis: same-pitch exclusivity lemma tested; T not guaranteed (384 constructed).
- [x] B.2 OUT-POLY ratified: T = 16 (`Config::kMaxSoundingPerTrack`); bound 2,832 ≤ 3,072; RAM2 recalculated.
- [x] B.3 `TerminalScheduler` (fixed T): live / composed closure, duplicate suppression, late terminals, transition reconcile, live occupy count; 10 tests.
- [x] B.4 Window carry list in the engine composer (E3).

## E1 — storage core

- [x] E1.1 `PagePool` with owner tags, `RunTable` with directory pages and bounded reclaim, `ManifestTable` with
      monotonic identity, refcounts and pins; explicit `init()`.
- [x] E1.2 `RowReservations` (CE-A1) with proofs: long sequential take, 128 open, closing runs, Stop at exhaustion,
      random interleaving; mutation check.
- [x] E1.3 Allocation witness (0 after init) and the heap gate script.
- [x] E1.4 Production-capacity init over poisoned memory; budget `static_assert`s; measured region sizes.
- [ ] E1.5 History ring and `SlotTable` (moved to E4 with undo/redo semantics).
- [x] E1.6 `PassEvent` owned by `ContentEngine` (`include/ContentEngine/PassEvent.h`); the prototype store aliases it.
- [ ] E1.7 Device placement: EXTMEM pool and DMAMEM tables, with census rows (lands with integration, E9).

## E2 — writer

- [x] E2.1 Fixed-page writer: NoteOn reservation, LIFO lane NoteOff into the owing run, closing runs, owed commit, FinalInterval, Stop closures (`test_content_engine_writer` 9, RED → GREEN).
- [x] E2.2a Writer random-input fuzz (100 k steps, R + conservation, NoteOff never refused).
- [ ] E2.2b Live occupancy signal from writer to scheduler; segment bound for owed-commit runs.

## E3 — composer (slice 1)

- [x] E3.1 `Composer` + `SpanWindow` + `WindowPlayer`: integrated with writer runs and `TerminalScheduler`
      (`test_content_engine_compose` 4, RED → GREEN).
  - Scenarios: 384 source notes / 16 sent across windows with cross-window shortening; live held note closed live
    then its recorded end next cycle; revision switch.
  - Mutation: disabling shortening fails 2 cases.
- [x] E3.2a Wrap-held notes over multiple cycles (`test_content_engine_compose_wrap` 6, mutation-checked); look-ahead defect fixed.
- [x] E3.2b Multi-cycle runs removed: one run per cycle (writer); sealed queue resized; RAM2 corrected.
- [x] E3.2c Q-LEN-1 re-strike once per loop (owner decision): writer flag, composer re-strike, player TERMINAL ordering fix; hand-specified tests.
- [x] E3.2e Q-OVL-4 ratified as OVL-9; owner's cases, adoption, unwrapped-endpoint and look-ahead fixtures.
- [x] E3.2d Edit hides: fixed hide table, lineage, EDIT-1 (draft) (`test_content_engine_compose_edit` 8, mutation-checked).
- [ ] E3.2f Restart checkpoints / mid-loop cold start.
- [x] E3.3 Composer NoteOff matching by (noteId, run): same-id-across-runs case added; the mutation is now caught.

## CE-C1 — output

- [x] C.1 `OutputPort` with protected NoteOff capacity; DIN saturation 10 s (`test_content_engine_output` 3).
- [x] C.2 (superseded) occupancy cap C/4 from e85fe039 — replaced by C.4.
- [x] C.4 Port rework: sounding-table NoteOffs, ordered essential/parameter queue, continuous coalescing, priority scheduler, per-class lateness (`test_content_engine_output` 8).
- [x] C.6 Output memory ownership: shared logical notes + per-port sparse lanes (`OutputHub`), 88,680 → 16,400 B; fan-out, shared capacity and lane tests (`test_content_engine_output` 11).
- [x] C.7 Expired-note cancellation (owner decision): scheduled end, per destination, `expiredBeforeTransmit` (`test_content_engine_output` 14).
- [ ] C.5 Device-measured USB message time; final continuous share and note space; RPN overflow policy.
- [ ] C.3 Multi-port routing per track.

## Blocked (by ruling)

- Recording writer: CE-A1 is proven at ledger level (host). The writer itself waits for owner acknowledgement of CE-A1.
- Playback consumer: waits for executable bounds for CE-B1 (counter-example open), CE-C1 (output policy) and CE-D1
  (measured grain costs).
