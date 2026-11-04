# Tasks — fixed-memory content engine (direct cutover)

Branch `rewrite/fixed-memory-engine` (RED / unbootable allowed). Authority: DEC-252 and `design.md` clauses. Plan
revision 3 is the baseline; no whole-plan revisions.

## E0 — semantic oracle

- [x] E0.1 Independent reference model for overlap/occupy (`test/test_engine_semantic_oracle/reference_model.h`).
- [x] E0.2 Register clauses OVL-0..8, ORD-1 with draft fixtures (12 cases green).
- [x] E0.3 CE-B1 counter-example and the effective-row identity as executable tests.
- [ ] E0.4 Owner review of OVL / ORD clauses and the Q-OVL questions.
- [ ] E0.5 Register Stop closures, ledger closure, wrap, interval, interactions, undo, DEC-159, C1, controllers, switch timing.
- [ ] E0.6 Legacy cross-check harness; classify every disagreement (HIB / fixture error / owner question).
- [ ] E0.7 Device measurements on the current image:
  - non-content PSRAM / RAM2 census;
  - SD worst-case write latency;
  - display draw per band;
  - `U_isr` and `U_other`;
  - per-job grain costs.
- [ ] E0.8 Classify all 253 DECs as musical or mechanism.

## E1 — storage core

- [x] E1.1 `PagePool` with owner tags, `RunTable` with directory pages and bounded reclaim, `ManifestTable` with
      monotonic identity, refcounts and pins; explicit `init()`.
- [x] E1.2 `RowReservations` (CE-A1) with proofs: long sequential take, 128 open, closing runs, Stop at exhaustion,
      random interleaving; mutation check.
- [x] E1.3 Allocation witness (0 after init) and the heap gate script.
- [x] E1.4 Production-capacity init over poisoned memory; budget `static_assert`s; measured region sizes.
- [ ] E1.5 History ring and `SlotTable` (moved to E4 with undo/redo semantics).
- [ ] E1.6 Extract `PassEvent` from the prototype header so the engine does not include `StoreCore`.
- [ ] E1.7 Device placement: EXTMEM pool and DMAMEM tables, with census rows (lands with integration, E9).

## Blocked (by ruling)

- Recording writer: CE-A1 is proven at ledger level (host). The writer itself waits for owner acknowledgement of CE-A1.
- Playback consumer: waits for executable bounds for CE-B1 (counter-example open), CE-C1 (output policy) and CE-D1
  (measured grain costs).
