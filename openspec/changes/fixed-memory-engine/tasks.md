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
- [x] E1.7 Device placement: `EnginePsram` (EXTMEM) and `EngineRam2` (DMAMEM) with `#CAP,MEM_REGION` census rows and budget `static_assert`s; measured on the Teensy in `teensy41-engine-bench` (E9.2). The firmware image places them when the legacy owners go (E9.8).

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
- [x] E5.10b Track admission refuses a second concurrent Record / overdub by name: `SlotTransport` returns `Busy` (E9.1).
- [x] E5.11 Reclaim worker: release-order queue in `RunTable`, unit-bounded `reclaim`, `ReclaimJob` background job of 64 units per grain (`test_content_engine_reclaim` 5, mutation-checked; device grain 29 µs).
- [x] E3.3 Composer NoteOff matching by (noteId, run): same-id-across-runs case added; the mutation is now caught.

## E4 — history and edits

- [x] E4.1 `History`: per-slot undo / redo, 99 states shared, commit drops redo, full table forgets the oldest step (owner decisions 2026-11-05); reference-model fuzz (`test_content_engine_history` 7, mutation-checked).
- [x] E4.2 Overdub session undo (DEC-038): wraps are steps, undo bounded at the session start, Stop folds the session into one step; sessions in the reference-model fuzz (`test_content_engine_history` 11, mutation-checked). Writer wiring (begin / wrap commit / end) lands with integration.
- [x] E4.3 NOTE_EDIT session undo (owner: limit 99, shared): E: steps, fold on leave, abandon, overdub + edit sessions at once; the session start is protected from eviction so abandon always restores it (`test_content_engine_history` 16, mutation-checked). Edit writer (selection ≤ 512, hide + replacement rows, seal grains) and focus / selection restore: E4.5 with integration.
- [x] E4.5 Edit writer (owner: hear each edit, rewrite one run): selection admission, E: log (99), per-action edit-run reservation, rebuild in grains, one history state per session, leave / abandon (`test_content_engine_edit_writer` 9, mutation-checked). Focus / selection restore per E: step moves to the `EditManager` integration (E9).
- [x] E4.5b Edit-writer grains within g: O(changes) acceptance, unit-bounded rebuild (gather / sift / write), writer in RAM2 + log in PSRAM. Device `cfb730eb…`: apply 532 µs, undo 453 µs, rebuild grain 158 µs (was 3,486 / 1,196 / 922). The seek / `begin` rise was PSRAM placement (back to 692 / 429 µs).
- [x] E4.4 History reconstruction on load (DEC-088) — closed as not needed: the owner chose "exact current only" for V1 import (2026-11-05, E8.2).

## E6 — playback and output

- [x] E6.1 `PlaybackTrack`: three windows per track, steady + transition cursors, deterministic succession, late switch and underrun counted, catch-up targets (`test_content_engine_playback_track` 6, reference-stream comparison, mutation-checked).
- [x] E6.2a Continuous-controller limit (owner: 64 per track per tick, recorded and played; essentials and RPN never limited), shared classifier, tick spill instead of loss (`test_content_engine_tick_limits` 5, mutation-checked; device burst tick 417 → 29.7 µs).
- [x] E6.2b Playback jobs under the scheduler: steady hard (boundary deadline), transitions background (owner review vs plan § 7.3); background round robin (`test_content_engine_playback_jobs`, 8 tracks simulated; mutation-checked).
- [x] E6.2c Source-transition liveness (owner ruling): Transition job class (≥ 1 grain in 2 while pending, slack permitting), late adoption with pins held, acceptance-to-audible latency, stop closes notes and releases pins; six REDs + a checkpointed Undo (`test_content_engine_source_transitions` 7, mutation-checked).
- [x] E6.2d Retained historical checkpoints (owner: keep them): two identity-keyed sets per slot, LRU among sets no sweep is writing, Undo / Redo reuse without rebuild; checkpoint open capacity 32 → 20 (owner ceiling) to fit ~416 KB (417,284 B). Tests: A→B→C + Undo/Redo/Undo/Undo, B↔C ×11 (mutation-checked).
- [x] E6.2e § 7.3 transition targets ratified as approved targets (owner 2026-11-05, conditional): boundary counting, ratified bounds asserted in the REDs, checkpoint overflow as a named miss with safe reconstruction (`test_content_engine_source_transitions` 10).
- [x] E6.2f Live occupancy while recording, compared with the composed next cycle (`test_content_engine_live_occupancy`, mutation-checked).
- [x] E0.4b Live tolerances D1–D3 accepted by the owner (2026-11-05): no playback delay; the stored runs give the composed result from the next cycle.
- [ ] D7 Device qualification of the transition targets (E10): 8 dense tracks, cached / uncached Undo-Redo, 64-bar loops and worst checkpoint misses, continuous overdubbing with revision changes, display + USB/DIN + SD, clustered workload; boundary counts, hard misses, max grain, CPU capacity.
- [ ] E6.2 Tick path: per-tick cap from the measured cost (bench `tick_*`, 2026-11-05: ~2 µs per emit, burst 2,773 rows in one tick; the player loses emits beyond its buffer — must spill), occupancy mask, sounding ledger, port FIFOs; scheduler wiring with boundary deadlines. Owner: tick time share for playback.

## E7 — display

- [x] E7.1 `TileStore` + `TileSweep`: span / density tiles, atomic swap with spares, draw rule never blank, steady-cycle display composition with every effective note, dense state (`test_content_engine_display_tiles` 5, mutation-checked; 602,416 B).
- [x] E7.2 Freshness (cycle-0 publishing past the wrap reach: one loop instead of two), `DisplayJobs` (focus first, newest request wins, display class), `LiveOverlay` (`test_content_engine_display_tiles` 8, mutation-checked). Banded drawing moves to the integration (E9).

## E8 — persistence

- [x] E8.1 Engine slot file (owner: new format, new folder): `SaveJob` / `LoadJob` in grains, writer-class admission, CRC, full release on failure, history chain restored (`test_content_engine_set_file` 4, mutation-checked).
- [x] E8.2a V1 import into the engine (owner: exact current only): `ResolvedImport` writes a slot's resolved notes and events as one take and the slot's only state; refusals by name with full release (`test_content_engine_v1_import` 3, mutation-checked 12/12).
- [x] E8.2b Card-side V1 reader feeding `ResolvedImport` from a V1 slot as the old firmware resolves it; V1 files read-only, never overwritten (with E9): delivered as E9.7b (LSV1) and E9.7c-1 / E9.7c-2 (pass streams, legacy edits discarded).
- [x] E8.3 SD sink / source, save schedule and the owner's save-approval gate (with E9): delivered as E9.7a on `CardIo` (the SdFat adapter itself lands with E9.8).

## E9 — integration and deletion (plan § 10.2; first bootable image)

- [x] E9.1 `SlotTransport`: Record / overdub / Stop / Undo / Redo on a Clock timeline → writer, manifests, history, playback tracks. One writer (`Busy`); wrap publishes `current + wrapped run` as a session step, a closing run included (owner 2026-11-05: publish at the wrap), completed in place under a new identity when it seals; Stop folds the session; empty wraps and empty takes leave no state; a full run list ends the take at a wrap (named); live occupancy released at Stop (`test_content_engine_slot_transport` 6, mutation-checked 15/15).
- [x] E9.1b Handoff without a gap (plan § 8.3): overdub takes a provisional source (current + the run being written) 384 ticks before each wrap, so window 0 of the next cycle is ready at the wrap; Record composes window 0 of cycle 1 from the prefix once the take passes it, and Stop hands it over through ordinary succession; gestures aim at their own tick (`test_content_engine_slot_transport` 8, mutation-checked).
- [x] E9.2 `EngineRoot`: production instances in two placed regions, one `init()` over unzeroed memory (1.9 ms on the Teensy), census, budgets (owner: two sweep scratch windows budgeted; PSRAM 6,796,508, RAM2 143,360); main-loop `submitJobs` / `runGrain` with per-track timelines, reclaim and one checkpoint sweep at a time for sealed playing currents (`test_content_engine_engine_root` 2, host + device `aa985803…`, mutation-checked 12/12). Tile sweeps are wired with E9.6. Found: poisoned cold-start leases (init), Record prepare replaying a 2^30-tick provisional loop.
- [x] E9.3 Clock path (engine side): `EngineRoot::clockTick` → every track's events → `ClockOutput` (per-track key slots with generations, port masks, NoteOffs to every port, scheduled ends) → `OutputHub` → `transmit` per port (0 DIN, 1 USB device, 2 USB host); emits carry status, data2, track and end; gesture wrappers route their NoteOffs (`test_content_engine_clock_path` 5, mutation-checked 12/13 + 1 equivalent). Teensy `dac84427…`: tick ≤ 11 µs for 2 tracks, drain ≤ 9 µs, RAM2 144,408 B. The firmware's `ClockManager` and the MIDI senders call these when the legacy owners go (E9.8).
- [x] E9.4 Gestures and input (owner 2026-11-05: one sounding slot per track, Clear undoable, bounded late join): `TrackControl` — queued Record (next bar / playing slot's loop start; waits for the one writer), queued Stop (bar), slot switch (next grid / loop end, fresh from position 0, window 0 composed from the request: `PlaybackTrack::launch`), empty target, undoable Clear, Undo / Redo, mute / solo, transport stop / start, selected track, live input to the writer and through (`ClockOutput` thru keys) (`test_content_engine_track_control` 8). Bounded late join for every transition (`test_content_engine_late_join` 7, the owner's REDs). Mutation-checked. Surface actions (`MidiButtonActions`, GPIO) call `TrackControl` when the legacy owners go (E9.8).
- [ ] E9.4b Layered slots: several sounding slots per track (owner: after the first bootable image, sized from real use; ~172 KB PSRAM per extra sounding slot).
- [ ] E9.4c Jam: bar-range playback and bar / 16th seek (owner: a later slice).
- [ ] E9.4d Auto-align / master loop length: a take's length forced to the master needs truncation semantics for rows past it (legacy default off).
- [x] E9.5 NOTE_EDIT (engine side): `NoteEditSession` — selection gathered in worker grains as the piano roll shows it (display cursor, hidden notes not selectable) with stored geometry (a shortened note keeps its recorded length; overlaps derived at composition, EDIT-1), ≤ 512 or `TooMany`; E: steps through the edit writer with a focus token per step returned by undo / redo; rebuilds as transition jobs, the slot's playing tracks follow each publish; leave / abandon (`test_content_engine_note_edit` 5, mutation-checked 10/11 + 1 equivalent). Teensy `aeac1ab7…`: RAM2 147,056 B. `EditManager` / `NoteEditFocus` call it when the legacy owners go (E9.8). Open: the gather grain on the device; the 9,216 B selection's budget line (E9.7).
- [x] E9.6 Display (engine side): tile sweeps for every slot's state (commits, Undo / Redo, edits, completions; Clear blanks), focus and successor get span tiles, one sweep worker on the display cursor that yields to a NOTE_EDIT gather, the live overlay for a take on the shown slot until its tiles show it; `PianoRollRender` draws bands of the detailed roll (span / dense / density fallback, highlight, overlay), the overview strip and the pitch extent from tiles at the legacy geometry (`test_content_engine_piano_roll` 8, mutation-checked 13/13). Teensy `e6512efa…`: a frame (4 bands + overview) 233 µs, a band ≤ 55 µs; RAM2 149,432 B. `DisplayManager` keeps grid, playhead, bracket and sidebar and calls these when the legacy owners go (E9.8).
- [x] E9.7a Persistence (owner 2026-11-05: approval once, remembered by the engine folder; autosave when settled; PSRAM lines for the NOTE_EDIT selection 9,216 B and persistence job state 7,224 B): `Persistence` on a `CardIo` — nothing written before approval, a changed slot saved 2 s after its last change and never while a take or NOTE_EDIT holds it, `.tmp` then rename (an interrupted save keeps the old file), boot load of every engine file with failures named per slot; one selection owner enforced (`test_content_engine_persistence` 5, mutation-checked 10/11 + 1 untested-eviction). Teensy `b5933b32…`: PSRAM 6,810,984 B, RAM2 149,432 B. The SD `CardIo` over SdFat lands with E9.8 (device writes need the owner's permission).
- [x] E9.7b V1 import (E8.2b; owner: skip and report pass-stream files): `Lsv1Reader` reads LSV1 slot files' `LoopSnapshot` (10-byte epoch header, sections) and imports the notes (sorted by noteId in the file: windowed passes over 64 spans, heap-sorted per window) and the kept events (own run, first) through `ResolvedImport`; pass-stream, shifted-loop-start and empty snapshots skipped and named, failures named; per-track output channel in `ClockOutput` (the legacy remap) (`test_content_engine_v1_lsv1` 5, legacy parity in `test_storage_loop_io`, mutation-checked). Owner's card, read only (`1ab15b47…`): 2 slots imported (641 and 177 notes), 20 pass-stream slots with content and 1 empty snapshot skipped, 40 empty placeholders, 0.58 s.
- [x] E9.7c-1 Pass-stream V1 slots without edit passes (owner 2026-11-05: add E9.7c): the reader also reads the legacy pass stream — Active passes only, record first then overdubs by mergeSequence, each pass with notes its own run (`ResolvedImport::bind` / `begin` / `nextPass`, ≤ 16), notes paired last-open-first and cyclically (passes are stored in tick order: a held note's NoteOff comes first), a NoteOn with no NoteOff in its pass held for the whole loop and counted, kept events of all passes in the event run, per-pass windows; edit passes skipped and named (`v1EditedMask`), loop-0 placeholders empty; per-pass tables in the import scratch, persistence state still within its 7,224 B + 1 KiB line on ARM (`test_content_engine_v1_lsv1` 8, legacy-writer parity in `test_storage_loop_io`, mutation-checked). Owner's card, read only (`d71224a9…`): 12 slots imported (all 10 unedited pass-stream slots, notes as surveyed, and the 2 LSV1 slots), 9 skipped for edit passes, 1 for a shifted loop start, 40 placeholders empty, 0 failed, 0 notes held whole, 0.75 s.
- [x] E9.7c-2 Edited pass-stream slots (owner 2026-11-06: the import only feeds the test setup — discard the legacy note edits, import the passes as recorded; no edit-run mapping): a pass stream with edit passes is imported like any other, its slot named in `v1EditedMask` (`test_content_engine_v1_lsv1`, mutation-checked). Survey first, read only: all 9 slots' rows Active — Create 39, Pitch 38, Length 182, Move 107, Delete 136, no velocity or CC rows — every target present. Owner's card, read only (`0bbf94d9…`): 21 of 22 slots with content imported (9 with edits discarded), slot 2 skipped for its shifted loop start, 0 failed, 4 notes held whole (in edited slots), 0.89 s.
- [ ] E9.8 Delete the legacy owners (§ 12.1) and their tests; first bootable image; D1 build gates. Owner 2026-11-06: boot first, then delete — legacy keeps compiling until E9.8d; `src/main.cpp` (with its uncommitted TEMP boot breadcrumbs) is not touched before E9.8d.
  - [x] E9.8a Engine entry point and env: EngineRoot boots on the Teensy (clock, MIDI in / out, SD `CardIo` adapter — no write before approval — card import, boot proof over serial). `teensy41-engine` builds only `src/EngineFirmware` (`ENGINE_FIRMWARE`), legacy envs unchanged; `EngineClock` (internal tempo or external MIDI clock, 8 ticks a pulse, never ahead of its source, tick numbers only rise and never skip, a start moves to the next bar line; `test_content_engine_engine_clock` 7); `SdCardIo` (SdFat rename cannot replace: the old file is removed first, and boot load takes a complete `.tmp` when the `.mle` is missing, an incomplete one falling back to the V1 file; `test_content_engine_persistence` 6); DIN / USB device / USB host in and out; serial `!ENGINE` commands until E9.8b; mutation-checked 10/10. Device `c58fb6ac…`: `boot_reset,crash=0`, `BOOT,load,ok` after 1.09 s (21 V1 slots, not approved, nothing written), launched V1 slots played over USB MIDI with every note closed at stop.
  - [ ] E9.8b Controls: buttons, faders and LEDs to `TrackControl` and NOTE_EDIT. Owner 2026-11-06: performance first, the legacy gesture map kept exactly (DROID ini unchanged).
    - [x] E9.8b-1 Performance controls: `Surface` (DROID control plane channels 13–16, never recorded nor thru; `PressDetector` as the legacy `MidiButtonProcessor`; record 36, track switch 37, transport 39, play / stop 40, slots 50–57, tracks 60–67 as `MidiButtonActions`; a gesture that starts the clock acts on its first tick) and its LEDs (legacy velocities, changes only, one message a ms on every port); `TrackControl::stopTrack` / `punchIn` and queued getters; emptiness is `contentOf` (a cleared slot is an empty state). The legacy GPIO buttons were never enabled (no `ENABLE_GPIO_BUTTONS`): not wired. `test_content_engine_surface` 9, mutation-checked. Device `bd7560ed…`: driven as the DROID over USB and confirmed by the owner on the DROID.
    - [x] E9.8b-2 Core NOTE_EDIT surface (owner 2026-11-06: core NOTE_EDIT, matching legacy; motor feedback off as the legacy default): `EditSurface` — edit mode 38, F1 select (16th + stacked notes), F2 coarse, F3 fine, F4 pitch with the legacy mappings, thresholds and 750 ms grace, one undo step a fader gesture, NOTELEN 35 (start / end, delete / create at the bracket), record double / triple as session undo / redo; the frozen detailed window (halved when denser than the 512-note selection); `EditWriter::create` / `NoteEditSession::create` (a created note is one undoable step); the screen's 16-bar detailed window (legacy), bracket, highlighted note and edit status. `test_content_engine_edit_surface` 8, `test_content_engine_note_edit` +1, mutation-checked (15 caught, 1 equivalent). Device `734df605…`: driven as the DROID over USB and confirmed by the owner on the DROID.
    - [x] E9.8 device fixes (owner reports 2026-11-06): hangs (playback no longer sent to the DROID's USB host port — the host driver's `write_packed` spins while the DROID is busy; legacy never sends playback there); MIDI Start / Stop / Continue robust while running; boot breadcrumbs (main-loop stage, tick); record arms first (legacy `handleArmedRecordPress`: on a stopped transport the second press starts it and the take, on a running one it cancels; a transport start re-queues armed Records on its first tick); legacy track letters (selected slot's content) and the 1 Hz pulse; live notes while recording (the overlay is a note list drawn at the view's scale, a first take's view grows with the current tick and ends at it, as legacy); external clock as legacy (a running clock rides out a gap at the last tempo and resumes on the pulses; the tempo over the span of 24 pulses, so bunched USB pulses do not move it; a span outside 1–300 BPM ignored); each track plays on its own MIDI channel (track n → channel n, legacy default; thru follows). Device `ac263ef8…`: verified over USB and by the owner; the remaining stalls were an Ableton MIDI feedback loop on channel 3.
    - [ ] E9.8b-3 Later surface: LOOP_EDIT (loop start / length / tempo faders; loop start needs shifted-loop support), copy / paste, quantize, move-tick and set loop start / end (41–48), the set browser (38 / 40 double), bar / 16th steps (E9.4c), motor-fader feedback.
  - [x] E9.8c Display: grid, playhead and piano roll from tiles (owner 2026-11-06: before the controls). `EngineDisplay` on the `SSD1322_OLED` library in the legacy layout — track column, 4-bar detailed view paged with the playhead (beat / bar grid, notes from tiles and the live overlay via `drawRollBand`, playhead), overview strip from density tiles only for loops longer than the view, status line and sidebar (tempo, clock source, transport, read-only); a frame every 33 ms after the previous DMA transfer, the first frame over blocking SPI (the panel stayed blank when the first frame was DMA); `!ENGINE select` / `oled-test`. Device `ada72d97…`: owner confirmed slot 24 drawn and playing; worst frame draw 733 µs.
  - [ ] E9.8d Delete the legacy owners and their tests; the engine becomes `teensy41-capture-serial`; D1 static gates.

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
