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
- [ ] B.2 Owner acceptance of OUT-POLY (T = 256 output limit) and of the cold-start note.
- [x] B.3 `TerminalScheduler` (fixed T): live / composed closure, duplicate suppression, late terminals, transition reconcile, live occupy count; 10 tests.
- [ ] B.4 Window carry list in the engine composer (E3).

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
- [ ] E2.2 Writer fuzz with conservation; live occupancy signal to the track's scheduler; segment bound for owed-commit runs.

## Blocked (by ruling)

- Recording writer: CE-A1 is proven at ledger level (host). The writer itself waits for owner acknowledgement of CE-A1.
- Playback consumer: waits for executable bounds for CE-B1 (counter-example open), CE-C1 (output policy) and CE-D1
  (measured grain costs).
