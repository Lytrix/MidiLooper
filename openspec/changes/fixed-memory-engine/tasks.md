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
- [x] E1.5 History and slot currents: built as E4.1.
- [x] E1.6 `PassEvent` owned by `ContentEngine` (`include/ContentEngine/PassEvent.h`); the prototype store aliases it.
- [ ] E1.7 Device placement: EXTMEM pool and DMAMEM tables, with census rows (lands with integration, E9).

## E2 — writer

- [x] E2.1 Fixed-page writer: NoteOn reservation, LIFO lane NoteOff into the owing run, closing runs, owed commit, FinalInterval, Stop closures (`test_content_engine_writer` 9, RED → GREEN).
- [x] E2.2a Writer random-input fuzz (100 k steps, R + conservation, NoteOff never refused).
- [x] E2.2b Live occupancy signal: `PlaybackTrack::liveNoteOn / liveNoteOff` (E6.2f); live vs composed compared per note, three named differences for the owner (D1–D3). The segment bound is moot since every wrap starts a new run (E3 slice 3).

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
- [x] E3.2d Edit hides: fixed hide table, lineage, EDIT-1 (ratified 2026-11-04) (`test_content_engine_compose_edit` 8, mutation-checked).
- [x] E3.2f Restart checkpoints: capture/resume, byte-identical resumed windows (`test_content_engine_checkpoint` 4, mutation-checked).
- [x] E3.3 Hide table moved to the shared, identity-keyed `EditIndexStore` (18 slots, pinning, LRU of unpinned, named exhaustion); composer 43,184 → 10,428 B; budgets statically checked (`test_content_engine_edit_index` 7).
- [x] E3.3b EDIT-1 regression: two successive edits of one NoteId plus an unrelated earlier same-pitch note.
- [x] E5.1 Checkpoint table owner: per-slot identity-stamped table, steady-state sweep with convergence, supersession, cold start (`test_content_engine_checkpoint_table` 5, mutation-checked); admitted order made canonical.
- [x] E5.1b Job leases (manifest pin, cancel releases), publication guard between grains, convergence proof and tests per mechanism; Q-OUT-2 saturation flagged (`test_content_engine_checkpoint_table` 11).
- [x] E5.2 Worker scheduler: EDF, slack check, display/persistence shares, overrun/miss reporting (`test_content_engine_scheduler` 8).
- [x] E5.3 Compose / sweep / cold start / index build in grains; edit index in its 576 reserved pool pages (`test_content_engine_edit_index` 11).
- [x] E5.4 Restart and adoption under DIN backpressure: wire balanced, carried-never-sent notes get no NoteOff (`test_content_engine_restart_backpressure`).
- [x] E5.5 Device grain measurements (`teensy41-engine-bench`); composer head cache, hide-row skip, look-ahead work units (9.7 ms → 655 µs).
- [x] E5.6 OUT-2 (Q-OUT-2 ratified 2026-11-05, source-ordered refusal): open effective-instance table, carried-in voices, proof, owner REDs (`test_content_engine_selection` 6, rig eligibility check), mutation-checked.
- [x] E5.7 Clustered look-ahead: run onset-pitch mask (RAM2); feasibility gate in `test_content_engine_scheduler` (adversarial window 100 grains, 8 tracks feasible at g = 786 µs, U_e = 50 % in simulation).
- [x] E5.8 Device: re-measure grains with OUT-2 and the mask; confirm or re-open the gate. Done 2026-11-05: worst grain 800 µs (clustered compose), g = 810 µs; resume split into two grains (was 1.1 ms); gate met. Image `59c8a6f8…`.
- [x] E5.9 Plan bounds: g = 810 µs and 44–100-grain windows into plan § 7 (owner). Done 2026-11-05: § 7.1–7.5 restated on measured figures (window 47 / 100 grains, index 58, cold start 190 / 3,009, sweep background). Owner: at most one track overdubs at a time, so clustered C2 = 42.7 % (within 50 %); clustered C1 needs B_max ≤ 136 ms.
- [x] E5.10a W: one writer at a time (owner 2026-11-05). `Config` derives rows, descriptors, manifests and pages from `kWriterCount = 1`; plan § 2, § 4, § 5 budgets restated (RAM2 −13.4 KB budget, PSRAM −43 KB).
- [ ] E5.10b Track admission refuses a second concurrent Record / overdub by name (with the writer integration).
- [x] E5.11 Reclaim worker: release-order queue in `RunTable`, unit-bounded `reclaim`, `ReclaimJob` background job of 64 units per grain (`test_content_engine_reclaim` 5, mutation-checked; device grain 29 µs).
- [x] E3.3 Composer NoteOff matching by (noteId, run): same-id-across-runs case added; the mutation is now caught.

## E4 — history and edits

- [x] E4.1 `History`: per-slot undo / redo, 99 states shared, commit drops redo, full table forgets the oldest step (owner decisions 2026-11-05); reference-model fuzz (`test_content_engine_history` 7, mutation-checked).
- [x] E4.2 Overdub session undo (DEC-038): wraps are steps, undo bounded at the session start, Stop folds the session into one step; sessions in the reference-model fuzz (`test_content_engine_history` 11, mutation-checked). Writer wiring (begin / wrap commit / end) lands with integration.
- [x] E4.3 NOTE_EDIT session undo (owner: limit 99, shared): E: steps, fold on leave, abandon, overdub + edit sessions at once; the session start is protected from eviction so abandon always restores it (`test_content_engine_history` 16, mutation-checked). Edit writer (selection ≤ 512, hide + replacement rows, seal grains) and focus / selection restore: E4.5 with integration.
- [x] E4.5 Edit writer (owner: hear each edit, rewrite one run): selection admission, E: log (99), per-action edit-run reservation, rebuild in grains, one history state per session, leave / abandon (`test_content_engine_edit_writer` 9, mutation-checked). Focus / selection restore per E: step moves to the `EditManager` integration (E9).
- [x] E4.5b Edit-writer grains within g: O(changes) acceptance, unit-bounded rebuild (gather / sift / write), writer in RAM2 + log in PSRAM. Device `cfb730eb…`: apply 532 µs, undo 453 µs, rebuild grain 158 µs (was 3,486 / 1,196 / 922). The seek / `begin` rise was PSRAM placement (back to 692 / 429 µs).
- [ ] E4.4 History reconstruction on load (DEC-088), with persistence (E8).

## E6 — playback and output

- [x] E6.1 `PlaybackTrack`: three windows per track, steady + transition cursors, deterministic succession, late switch and underrun counted, catch-up targets (`test_content_engine_playback_track` 6, reference-stream comparison, mutation-checked).
- [x] E6.2a Continuous-controller limit (owner: 64 per track per tick, recorded and played; essentials and RPN never limited), shared classifier, tick spill instead of loss (`test_content_engine_tick_limits` 5, mutation-checked; device burst tick 417 → 29.7 µs).
- [x] E6.2b Playback jobs under the scheduler: steady hard (boundary deadline), transitions background (owner review vs plan § 7.3); background round robin (`test_content_engine_playback_jobs`, 8 tracks simulated; mutation-checked).
- [x] E6.2c Source-transition liveness (owner ruling): Transition job class (≥ 1 grain in 2 while pending, slack permitting), late adoption with pins held, acceptance-to-audible latency, stop closes notes and releases pins; six REDs + a checkpointed Undo (`test_content_engine_source_transitions` 7, mutation-checked).
- [x] E6.2d Retained historical checkpoints (owner: keep them): two identity-keyed sets per slot, LRU among sets no sweep is writing, Undo / Redo reuse without rebuild; checkpoint open capacity 32 → 20 (owner ceiling) to fit ~416 KB (417,284 B). Tests: A→B→C + Undo/Redo/Undo/Undo, B↔C ×11 (mutation-checked).
- [x] E6.2e § 7.3 transition targets ratified as approved targets (owner 2026-11-05, conditional): boundary counting, ratified bounds asserted in the REDs, checkpoint overflow as a named miss with safe reconstruction (`test_content_engine_source_transitions` 10).
- [x] E6.2f Live occupancy while recording, compared with the composed next cycle (`test_content_engine_live_occupancy`, mutation-checked).
- [ ] E0.4b Owner: accept live tolerances D1–D3 (shorten 1 tick later, equal start 0-tick blip, ≤ 8-tick blip) or require a playback delay.
- [ ] D7 Device qualification of the transition targets (E10): 8 dense tracks, cached / uncached Undo-Redo, 64-bar loops and worst checkpoint misses, continuous overdubbing with revision changes, display + USB/DIN + SD, clustered workload; boundary counts, hard misses, max grain, CPU capacity.
- [ ] E6.2 Tick path: per-tick cap from the measured cost (bench `tick_*`, 2026-11-05: ~2 µs per emit, burst 2,773 rows in one tick; the player loses emits beyond its buffer — must spill), occupancy mask, sounding ledger, port FIFOs; scheduler wiring with boundary deadlines. Owner: tick time share for playback.

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
