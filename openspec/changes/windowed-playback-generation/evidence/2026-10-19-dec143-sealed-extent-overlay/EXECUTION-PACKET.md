# G2 execution packet — DEC-143 sealed-extent record overlay (task 3.15g)

Authority: [IMPLEMENTATION_READINESS.md](../../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).
Contract: **DEC-143 — a published Record slice is read material before its root publishes**
([DECISION_LOG](../../../../../docs/DECISION_LOG.md)), narrowed by its own withdrawal note: the overlay **must** be sourced
from the presentation's sealed extent, never from `LoopPasses::recordPass.committedChunkIds`.

## Identity and scope

- Packet/task IDs and approved contract: task **3.15g — wrapped short-loop Record finalization**, DEC-143, DEC-142.
- Status: specified (this file) → fixture → native verified → device **NOT RUN** (needs owner approval to flash).
- Owner and path: `Loop::tryGatherPublishedSnapshotEventsInWindow` (content resolution) ← `Track`'s merged playback
  fallback `ensurePlaybackMergedMidiEventsBuilt` → `RuntimePlaybackMergedEvents` (`mergedEvents`) → playback order
  build → MIDI emission. No new owner, no new queue, no scheduler.
- Primary files: `src/Loop/LoopSnapshotPlayback.cpp`, `include/Loop.h` (doc comment only),
  `src/Track/TrackPlaybackWindowBuild.cpp`; fixtures `test/test_loop_take_survival`,
  `test/test_rolling_record_retirement`.
- Preconditions already present: DEC-142 slot-owned `Track::CaptureStopPresentation` (+ sealed `rawRange`),
  `Track::hasPublishedRecordPresentationForSlot`, `Loop::borrowSealedCaptureRange` / `captureReadSource`,
  `LoopEventStore::projectRecordStopTick`, the `RecordOverlay` gather path and its three native cases.
- Prerequisites included in this slice: the overlay's **source** is the Loop's own sealed read borrow (the presentation
  record's `rawRange`), and the merged-fallback reuse key carries that identity.
- Explicit non-goals: no widening of the retained window (DEC-143 option (a)), no earlier snapshot publication, no
  stale-root + newer-Record overlay (absence cannot be proven), no raw live-capture rows, no default rolling
  activation, no RAM1 budget change, no change to DEC-136 coverage or to the frozen-window reader.
- Commit boundary: Loop gather source + Track wiring + fixtures. Independently buildable; the previous behaviour
  (`publishedRecordOverlay = false` everywhere) is restored by reverting one Track predicate and the identity key.

## Oracle fixed before behavior edits

| Case | Initial state | Actions | Expected observation per step |
|---|---|---|---|
| O1 overlay serves the sealed take | wrapped Record take crossed `RecordPublished`, extent sealed **and borrowed into the Loop**, no published root | gather(full loop) → gather(full loop) again | first call `true` / `RecordOverlay` / the take's own note-ons; second call identical, no growth, no duplicate |
| O2 mutable capture never rides | same as O1, then live overdub rows appended to the producer store | gather(full loop) | `true` / `RecordOverlay`; the live rows are absent, the take's rows unchanged |
| O3 no publication edge, no overlay | take sealed but **no borrow** (job still finalizing) | gather(full loop, overlay allowed) | `false` / `None` — the pre-publication take is not read material |
| O4 the root supersedes | as O1, then the queued snapshot publishes | gather(full loop) | `Snapshot` path, the same note count once (no duplicate), overlay gone |
| O5 projection + closure | raw take longer than the loop (`raw > final`), on held past the loop end | gather(full loop) | overflowing note-ever remapped in-loop; a note left open is closed exactly once at `loopLengthTicks - 1` carrying its own `noteId` |
| T1 merged fallback closure (production path) | wrapped two-bar take at `RecordPublished`, no root, live overdub audible | drive the deferred owners + `ensurePlaybackMergedMidiEventsBuilt` | `mergedEvents` non-empty and equal to the take's content in one full-loop window; a maintenance turn with the same identity performs **no** rebuild; after the root publishes, one rebuild serves `Snapshot` content with no duplicate |

Preserved invariants: no accepted content truncated by the overlay; the frozen reader and DEC-136 obligations
untouched; exactly one served transition per identity change; no allocation or blocking added to a MIDI edge
(`ensurePlaybackMergedMidiEventsBuilt` is the existing maintenance path).

Counterexample that must fail: feeding the gather a pass chunk-id list (the withdrawn form) or reading the producer
store — O2 and O3 fail if the overlay ever sources mutable rows.

## Controls and coverage

- Seams reused: `LoopEventStore::resetPoolForTests`/`initPool`, `Loop::beginCapture`/`appendCaptureEvent`/
  `commitCapturePass`/`drainPendingSnapshotPublish`, `Loop::borrowSealedCaptureRange`, the real deferred owners in
  `test_rolling_record_retirement` (`MidiServiceDrain` selector, `Track::processDeferredIdleMaintenance`,
  `serviceRollingPlaybackWindow`, `Track::playMidiEvents`).
- Production paths exercised: `Loop::tryGatherPublishedSnapshotEventsInWindow` and
  `ensurePlaybackMergedMidiEventsBuilt` are the shipped functions; no state machine is copied.
- Independent expectation: the JSON-free expected content is written by the fixture from its own input events, not
  read back from the gather's output.
- Bounded drain: `test_rolling_record_retirement` uses its existing step loop with a fixed maximum and prints the last
  state on failure.

## Verification recipe

- Focused: `pio test -e native -f test_loop_take_survival -f test_rolling_record_retirement`.
- Neighbours: `-f test_shared_capture_finalization -f test_continuous_capture -f test_playback_prewarm`.
- Broad: `pio test -e native`.
- Builds: `pio run -e teensy41-capture-serial` and `-e teensy41-capture-serial-window-qualification`; RAM1 floor 840 B
  unchanged (`scripts/ram1_budget.json` untouched).
- Host checks: `git diff --check`, `openspec validate windowed-playback-generation --strict`.
- Device: dedup-verified qualification image, registered base 2-bar stimulus, authorized scratch slot only; the claim
  is "the post-stop silence closes and the transport keeps a serial heartbeat". **NOT RUN in this slice.**



### Completion record (2026-10-19)

- Oracle/contract unchanged: DEC-143 as written, narrowed by its own withdrawal note (source = the presentation's sealed
  extent, reached through the Loop's read borrow). No bound, floor or verifier touched.
- Fixture implemented and discriminating: `test_loop_take_survival` gained `PublishedRecordTake` /
  `beginPublishedRecordTakeOnly` (seal → borrow → commit the same take) and three cases —
  `test_the_record_overlay_needs_the_sealed_extent_not_a_committed_pass` (O3, the withdrawn form's control),
  `test_the_record_overlay_projects_a_wrapped_take_and_closes_a_held_note_once` (O5, projection + whole-loop-only
  closure), `test_the_record_overlay_serves_the_same_take_on_every_gather` (O1b) and
  `test_the_record_overlay_refuses_an_undone_record_pass` (the pass-state guard). `test_rolling_record_retirement`
  gained `test_the_merged_fallback_serves_a_published_record_slice_before_its_root` (T1, production path: non-empty
  merged window at `RecordPublished` with no root, one rebuild, 16 no-op turns without a rebuild, one rebuild on the
  root's publication, no duplicate note).
- Red proofs (both mutations restored byte-identically, `cmp` verified): `false &&` on the Track predicate fails only
  the new T1 case at "the merged fallback must not hold an empty window" (11 others green); `false &&` on the Loop's
  overlay source fails exactly the five serving cases (O1, O2, O4, O5, O1b) while the O3 refusal control stays green.
- Native verification: focused `pio test -e native -f test_loop_take_survival -f test_rolling_record_retirement -f
  test_shared_capture_finalization -f test_continuous_capture` → **356 cases, 356 succeeded**. Full native
  `pio test -e native` → **3,323 cases / 7 skipped / 3,316 succeeded / 0 failed** (baseline 3,318; delta = the five new
  cases).
- Build/RAM1: `teensy41-capture-serial` **SUCCESS** `variables 96,192 / code 392,088 + padding 1,128`;
  `-window-qualification` **SUCCESS** `96,192 / 392,376 + padding 840` — both byte-identical to the pre-slice lines,
  floor 840 unchanged. `scripts/ram1_budget.json` carries a pre-existing working-tree edit from an earlier session
  (an env alias line, floor untouched); this slice did not modify it.
- One updated neighbour assertion: `test_deferred_record_interval_is_audible_only_with_a_bound_window` (the layered
  entry used to emit 0 events because the merged fallback was empty; the interval is published read material in that
  fixture, so it now emits its own pair once each — the "no fixed view for this target" intent is kept and stated).
- Host checks: `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
- Device verification: **NOT RUN** — the qualification image is not flashed; the claim owed is "the post-stop silence
  closes and the serializer keeps its heartbeat" on the registered base 2-bar recipe, authorized scratch slot only.
- Required focused review (one pass, code-reviewer) raised two concrete risks and one coverage gap; all three fixed and
  re-verified inside this slice:
  1. *An incremental overdub admit was dropped by the overlay.* `Loop::tryAdmitLastOverdubSealIntoMergedEvents` stamps
     `builtFromSnapshotIdentity = nullptr`, so with the overlay serving (identity = the extent) the next maintenance turn
     rebuilt from the extent alone and silently lost the wrap just admitted. Fixed in the admit call site of
     `ensurePlaybackMergedMidiEventsBuilt`: while the overlay serves, the increment keeps the window's own identity; the
     root-backed path's stamp is untouched.
  2. *An undone Record take kept sounding.* Undo inside the pre-root window disables the pass and bumps the revision, but
     the extent stays sealed, so the overlay re-served the undone take until a root republished without it. Fixed in the
     gather: a take is refused when a Record pass exists and is no longer `Active`
     (`test_the_record_overlay_refuses_an_undone_record_pass`, red-proved by neutralising the term).
  3. *No negative gate coverage.* Added: a sealed-but-not-yet-`RecordPublished` extent stays out of the merged
     representation (sampled on the first turn the admitted stop owns its extent), and a failed row read is now a whole
     miss rather than a truncated success (the reviewer's residual note).
- Task 3.15g stays **unchecked**: its device acceptance (and 6.4 / 3.15f) are still open.

### Device verification (2026-10-19) — read authority proven, the audible gap is downstream

- Run: `captures/i5-dec143-sealed-extent-base-01`, registered stimulus `STIM-BASE-2-2-01@v1` (2-bar record, 2-bar
  overdub, second overdub, post-stop undo/redo) on track 8 / slot 4, channel 8, `--no-boot-reset`, managed capture.
- Image: `teensy41-capture-serial-window-qualification`, HEX
  `7f48a41b30392897fafdbeb3b0ff3a05dc0cd27a96d59f618a08a9c47560616f` (built from this slice's tree; RAM1
  `392,376 + 840`), flashed with `-t nobuild -t upload` (12.7 s).
- Verdict: **`INCONCLUSIVE_CAPTURE` (EVIDENCE_MISSING)**, sole issue `persistence_active_snapshot_unavailable_during_save`,
  5 of 8 playback-active persistence windows witnessed — the same single issue as the pre-slice measurement.
- **DEC-143's read authority served on hardware:** `RECS,stop … 1885 → 1536` (wrapped take) at **40.139 s**, then
  `DIAG,playback_build,record_overlay` at **41.642 s** (+1.502 s) with a full-loop window (`0…1536`) and **93 events**,
  and the root published at **42.734 s** with the next build taking `snapshot` at **42.743 s** — superseded in exactly
  one rebuild, nothing doubled.
- **The wire stayed silent anyway:** the first `DIAG,playback_read` is at **43.213 s** and the first note on the wire
  (`MO,144,…`) at **43.216 s**, i.e. **3.076 s** after the stop (pre-slice: 3.078 s), and both fall *after* the wrap at
  43.214 s that followed the root's publication. No playback turn served a reader in that window even though the merged
  representation already held the take. So DEC-143 closed the **content** gap and the remaining silence is an
  **emission-side** gate, not a read-authority gap.
- Everything else in the recipe held: record/overdub/second-overdub on/off `32/32`, `16/16`, `8/8`; first-note latency
  `8/2` clocks ≤ 12; loop length `1536/1536`; stored grid `bad=0/2`; slowest record-stop stage `finalize 634 µs`;
  persistence handoff `ok` 45.8 ms; deferred persist sliced, `progress=True`; MIDI-boundary intervals
  `{OVERDUBBING 2, OVERDUB_STOPPING 1, PLAYING 3, RECORDING 1}`, overflow-free; min RAM2 131,072 B; capture cleanup
  `ok`; display frames consistent; run-end heap observation complete (`v=1`).
- Leave-behind: the pinned default image `0cf0f538c41d8d9495db9e3bfd4e63422e4aa1f708dc93cefa652a248ee58684` re-flashed
  (11.08 s, hash verified) and a read-only postflight `captures/i5-dec143-restored-final-02` reads **PASS**: boot
  banner, `reconcile=0`, `free_chunks=427`, admission/interactive/undo/stopped/deferred-prewarm all 1, readiness
  attested, S2b.3 met (`free=427` vs required 16), library observation complete over 64 slots (22 pass-backed, root
  demoted 0), walk flat `512 → 512`.
- **Next slice named by this run:** the merged representation holds the take but no turn serves it until a pass
  boundary. The candidates are the post-stop emission cursor seed (a rewound `nextEventIndex` with no mid-loop
  catch-up) and a turn-level early return in the merged path; the overlay build's own 1.5 s latency (maintenance
  demand) is a separate variable and must not be conflated with the silence. No verifier, bound or floor was touched.

### Follow-up slice (2026-10-19) — the post-stop phase latch that kept the pass silent

The device run above proved the overlay and localised the audible gap downstream of content: the merged window held the
take, yet no turn served it. The cause is a **latched phase reset** in the clock-edge reader
(`src/Track/TrackPlaybackHotPath.cpp`, `playCommittedMidi`'s first-turn branch): when the previous in-loop phase is
unknown (`lastTickInLoop == UINT32_MAX`, set by the stop / queued start / re-anchor) and the playhead is already past
the first bar, the turn returned **without writing the phase back**, so every later turn re-entered the same branch for
the remainder of the pass — the interval stayed silent until the wrap. The turn is meant to establish the phase and
suppress a catch-up burst, which is exactly one turn; the fix writes `lastTickInLoop = tickInLoop` there (one added
line, no new state, no bound change, RAM1 unchanged).

- RED → GREEN: `test_a_post_stop_phase_reset_does_not_silence_the_rest_of_the_pass`
  (`test_rolling_record_retirement`) builds the device state — a committed second-bar interval, a published root,
  PLAYING, `lastTickInLoop = UINT32_MAX` — drives the production entry at phase 800 and then 1105, and requires the owed
  note-on/off on the wire. Red-proved by removing the write-back (fails exactly at "the owed note-on must reach the
  wire", 12 others green; file restored byte-identically).
- Focused gates: `test_rolling_record_retirement` 13/13, plus `test_shared_capture_finalization`,
  `test_loop_take_survival`, `test_playback_cursor_advance` → **254 cases, 254 succeeded**. (Full native deliberately
  not re-run in this session, at the owner's instruction to save time; the previous full run was green.)
  Qualification build `392,376 + padding 840` (floor 840) — RAM1 unchanged.
- Device re-run on the fix: image HEX `704c2b36…` (same RAM1 line), registered `STIM-BASE-2-2-01@v1`, track 8 / slot 4:
  `captures/i5-dec143-phase-seed-base-01`. Wrapped stop at **35.868 s**; overlay build at **37.468 s**; first
  `playback_read` **37.471 s**; **first note on the wire 37.580 s** — 112 ms after the content existed and **before the
  root published** (38.644 s), with 332 note rows in the pass. Silence **3.076 s → 1.712 s**, and the surviving 1.712 s
  is the finalization window itself (stop → immutable publication), where DEC-143 deliberately leaves the take unreadable.
- Witness coverage improved **5/8 → 8/9** playback-active persistence windows. The single uncovered window is
  `slot:7:4` at **36.487–36.562 s**, inside that pre-publication interval: with the overlay's eligibility edge at
  `RecordPublished` there is no read material to witness there, so
  `persistence_active_snapshot_unavailable_during_save` remains (verifier untouched). Closing it needs an **owner
  decision**, not a silent relaxation: either the eligibility edge moves to the seal at admission (immutable, but it
  weakens this decision's own boundary), or the frozen Record window must cover the bar the playhead is actually in
  (the DEC-143 option (a) that was rejected as the larger change, and DEC-136's `adopt_wait,e2_boundary` shape).
- Verdict of the re-run: `INCONCLUSIVE_CAPTURE` on that single issue. The recipe's own content legs (second overdub,
  undo/redo, latency, grid, cleanup, display) all pass; the board is left on the qualification image by owner instruction
  (no default restore this session).

### Option 2 — current-window frozen return progression (2026-10-19, owner-selected)

DEC-143's `RecordPublished` edge is **unchanged**. The finalization interval is now served by DEC-136's frozen return
reader, whose first prepared window is the one the playhead is **in** — not always the later bar.

**Production changes (two, both small):**
1. `Track::stepPlaybackViewMaintenance`, the frozen-return branch: the prepared window is derived from whether the
   playhead's **own** interval is already served (`playheadWindowServed` = the current role is complete, published, not
   raw, same loop length and its coverage contains the phase). Unserved ⇒ prepare the playhead's window; served ⇒ advance
   to the next one; the pass's own last tick keeps the old wrap rule (the successor there is the first window). This is
   current-window + forward-window progression — no whole-loop expansion, no new state.
2. `Track::adoptOrServiceOnwardRecordWindow`: a complete, unpublished, non-raw, non-first-return ready whose coverage
   contains the playhead is now adoptable **mid-window** under **either** identity — `Committed` (unchanged) or
   `Capture` (the sealed extent's own frozen token, qualified through the job's presentation). Qualification runs before
   the token comparison; the window keeps its own identity and is never relabelled as newer content.

**RED → GREEN:** `test_the_first_frozen_return_window_covers_the_playhead_interval` (`test_rolling_record_retirement`)
builds the device state — a two-bar take, the playhead held in bar one, the job pending before publication, the merged
fallback empty — and requires the take's note pair on the wire from the frozen window. Red-proved twice and restored
byte-identically: with the old "later bar" rule applied unconditionally the existing bar-two cases fail (the progression
is load-bearing), and with the `Capture` adoption removed the new case fails at "sent=0".

**Gates:** focused `test_rolling_record_retirement`, `test_shared_capture_finalization`, `test_loop_take_survival`,
`test_playback_cursor_advance`, `test_playback_prewarm` → **496 cases, 496 succeeded**. Qualification build
`variables 96,192 / code 392,376 + padding 840` (floor 840) — RAM1 unchanged; full native not re-run (owner instruction).

**Device (registered `STIM-BASE-2-2-01@v1`, track 8 / slot 4, qualification image HEX `0b4b6fe2…`,
`captures/i5-frozen-current-window-base-01`): verdict `PASS`, `issues: []`.**
- Record stop **40.735 s** → first served playback turn **41.464 s** (the frozen return window, +0.729 s) → DEC-143
  overlay build **42.375 s** (+1.639 s) → root publish **43.264 s** with one `snapshot` rebuild at **43.278 s**.
- Playback witness windows **8 of 8** covered (was 5/8 before this session's fixes, 8/9 after the phase-latch fix).
- Recipe content checks all pass (on/off 32/32, 16/16, 8/8; latency and grid within their limits; persistence ok;
  cleanup and display ok; no timing warnings; heap observation complete).
- **Named residual, not claimed as clean:** one seam artifact — a single re-articulated note-on (pitch 53 at 42.656 s)
  where the overlay replaced the frozen window while that note was sounding, i.e. the ledger saw the incoming content
  under a different identity. It does not affect the recipe's own content checks (`CLN dedup=0`), and closing it is the
  next slice: identity reconciliation across the frozen→overlay seam (one closure/one articulation per sounding).
- Board left on the qualification image by owner instruction (no default restore this session).

**CORRECTION (same day): the "seam residual" does not exist.** A per-pass recount of the same run's `MO` rows (passes
taken from its `BAR` boundaries: 39.474 / 41.466 / 43.455 / 45.444 / 47.467 s) gives **no pitch with two note-ons inside
any pass** (`dup_ons=[]` in every pass). The single flag in the earlier scan was an artifact of its window: a note held
across a pass boundary has its off in the next pass, so a coarse 40–46 s scan sees an unmatched on. The frozen→overlay
handoff therefore articulates each sounding once, as required; no identity reconciliation is owed. Nothing was changed
in production code for this.

### Focused review of the option-2 slice, and the task's closeout

**Review (one pass, code-reviewer).** Four areas closed as no-finding with file:line evidence: the `passEnding` carve-out
(`nextBar = 0` is bar-aligned and the existing `== nextBar` idle plus the wrap rule still hold), oscillation/starvation
(the choice moves forward only, one direction per event), the Capture-identity adoption cannot take a foreign or released
source (owning presentation + loop/length/coverage + the re-derived presentation generation + the fill's token equality
guard), and the mid-window cursor reset is required and safe (the frame, not the cursor, bounds what is emitted).

**One recorded risk (not fixed, next slice).** The frozen content token `captureContentGeneration` is also stamped into
live rolling windows, so a complete, unpublished **live** copy covering the playhead carries the same token as the
presentation's frozen extent. After a *refused* `bindFirstRecordingBar` such a copy can therefore pass
`playheadWindowServed` and can be adopted as the frozen reader. Bounded: it is the same take (the token is monotonic per
loop, so a foreign take cannot match), nothing is relabelled (adoption passes the window's own identity), a successful
bind clears every non-current view, and the alternative is silence. It would need a fixture that parks a live ready
window through the stop and asserts the served window's provenance/content; no symptom has been observed on hardware.

**Closeout.** Task **3.15g — wrapped short-loop Record finalization** is checked off against its own acceptance list:
registered retirement fixtures for (1)–(4), full native **3,325 / 7 skipped / 3,318 succeeded / 0 failed**, both builds
`392,088 + 1,128` and `392,376 + 840` (floor 840 unchanged), strict validation valid, and the registered
qualification-image base 2-bar recipe **`PASS`** with `issues: []` and playback witness 8/8. 6.4 and 3.15f (all-route
cutover, multi-track and external-clock) remain open, as do S4f.8's waiting-stop receipts.

