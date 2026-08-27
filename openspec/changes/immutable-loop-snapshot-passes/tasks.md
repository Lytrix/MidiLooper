## 1. Architecture and specification gate

Phase 0 is documentation only. Later sections are blocked until 1.1–1.8 are complete.

Progress checkpoint (2026-08-27): 27/45 tasks are complete. Sections 1-3 are
complete except for the activated wrap-level provisional snapshot publication
tasks 3.5-3.6. In section 4, 4.1 and 4.2 are complete. In section 4b,
4b.0, 4b.1, 4b.1a, 4b.1b, 4b.2, 4b.2a, 4b.2b, and 4b.4 are complete under
Ring B round-robin. Open now: 3.5, 3.6, 4.3-4.6, 4b.3, 5.1-5.6, and 6.1-6.5.

- [x] 1.1 Complete formal architecture reassessment for immutable snapshots, including ownership, transitions, execution bounds, storage, and compatibility removal. Recommendation is recorded in `ARCHITECTURE-REASSESSMENT.md`.
- [x] 1.2 Record the approved decision in `docs/DECISION_LOG.md` and update the refinement plan, parent runtime architecture, derived-representation authority, storage authority, and naming with the accepted `LoopPass` / `LoopSnapshot` boundary. The logical indexes are approved; the fixed-capacity physical directory is a Phase 3 admission prerequisite and remains measurement-gated.
- [x] 1.3 Keep this change selected in `docs/Runtime/CURRENT_WORK.md` and post the phase architecture gate before any firmware edits. Phase 0 is complete; tasks 3.5/3.6 are the activated wrap-publication slice and no firmware is authorized until their gates pass.
- [x] 1.4 Complete `PREFLIGHT.md` / `DECISION_REVIEW.md` for each later ownership or stop-path implementation phase. Change-scoped artifacts cover Phases 1–5; no later phase is authorized by them.
- [x] 1.5 Record the persistent reference-root invariant: successor snapshots reuse unchanged event-object references, allocate new objects only for added or modified content, and must not copy the complete reference set.
- [x] 1.6 Select the logical reference structure: notes as pitch → bar → membership lists of `NoteRef`s (span index) and events as bar → membership lists of `EventRef`s (indexed view of existing event-stream order). Path-copy is rejected as the overlap model. The physical directory representation remains Phase 1 / measurement.
- [x] 1.7 Specify snapshot reachability/reclamation, provisional session snapshots versus global undo snapshots, redo-branch clearing, and latest-saved-snapshot reboot behavior.
- [x] 1.8 Record the architectural construction contract (identify, replace, construct root, freeze, publish) and require an `ExecutionBounds.md` class before runtime publication. Treat any numeric N-candidate quantum as a measurement hypothesis only.

## 2. Immutable snapshot model

Phase 1 is selected after section 1 completed. The logical indexes are
selected; the physical directory is a hard measurement gate before its
production implementation. Candidate construction remains native/offline.

- [x] 2.1 Define immutable note and non-note event objects with loop-scoped stable references, a memory allocation contract, canonical span access for projection consumers, and a construction-unit measurement contract for later execution-bound classification.
  - Allocation contract: `allocateSnapshotShared` packs nodes into 32 KB bump
    slabs owned by `Loop` (`DEC-048`). Construction reuses one span root and
    one event root. Capture-serial logs `DIAG,snapshot_alloc`.
- [x] 2.2 After the physical-directory measurement gate, implement the selected pitch/bar membership indexes and complete immutable `LoopSnapshot` records under `Loop` / `LoopPasses`. Allocate only the minimum directory structure required by the selected representation. Keep `LoopPass` as the compatibility operation record.
- [x] 2.3 Implement native/offline candidate construction from a const active snapshot and an existing semantic mutation with explicit resumable progress and measured units. Freeze, validate, and retain a complete candidate without making it active; preserve the active snapshot on failure.
- [x] 2.4 Verify the architectural invariants with existing native gates plus narrowly scoped checks only: snapshot completeness and self-containment, unchanged-object sharing, changed-object replacement, non-note preservation and ordering, multi-bar/wrap membership, `NoteId` deduplication, snapshot identity stability, alias safety, older-snapshot stability, and failed-candidate preservation. Do not add a new test architecture.

## 3. Capture and edit publication

Blocked until section 1 is complete and dirty-bar construction has a declared `ExecutionBounds.md` class. A long note can still touch every intersecting bar; this gate does not treat that as already bounded.

- [x] 3.1 Adapt `Loop::commitCapturePass` at its existing boundary to publish one global `LoopSnapshot` per record or overdub session-exit without changing wrap boundaries. Intermediate record prefixes and overdub wraps remain provisional.
- [x] 3.2 Adapt NOTE_EDIT close/commit to publish one global `LoopSnapshot` for a settled edit batch. In-session batch snapshots remain provisional `E:` revisions.
- [x] 3.3 Retain compatibility pass records only as parity/rollback data while consumers migrate. Use existing verification gates plus invariant-protecting parity checks for overlap, LIFO pairing, wrap pairs, equal-tick ordering, and non-note events; do not use or optimize pass replay as normal effective-state handling.
- [x] 3.4 Before runtime publication, classify each named candidate-construction operation in `ExecutionBounds.md`; define the unit cap, maximum indivisible step, progress state, completion condition, and explicit overrun/failure policy. Do not introduce content-sized construction in `handleMidiInput()`, clock dispatch, or track-tick paths.
- [ ] 3.5 Publish a complete provisional `O:` `LoopSnapshot` after each settled overdub wrap through the existing `Loop` publication owner. Preserve `NoteId` for geometry-only `Replace`, remove hidden notes, allocate new IDs only for new notes, publish atomically after bounded construction, and record only one global `OverdubPassAdded` undo transition at overdub stop.
  - Capture/pairing state assigns `NoteId` incrementally to each logical NoteOn/NoteOff pair before pass promotion; promotion performs no content-sized pair-identity scan. `EventId` remains for non-note events only.
  - Existing semantic owners provide a transient per-wrap mutation journal with structural caps for mutation count, affected bars, membership entries, and bytes.
  - Oversized work is represented as ordered bounded fragments. Exact parent snapshot identity is retained across fragments and multiple wraps.
  - Hide removes paired note event references from the event index by `NoteId`. Geometry replacement creates new immutable note event objects with the same `NoteId` and canonical order; non-note replacements retain `EventId`.
  - The journal is runtime-only in this slice; compatibility pass storage remains unchanged.
  - A failed or over-budget fragment retains its journal and previous selected snapshot for retry. It is not discarded.
  - Snapshot construction uses fixed-capacity immutable membership pages and persistent bar-cursor progress. Construction initialization and per-change begin operations do not enumerate all changed bars up front, and one unit does not copy an unbounded membership vector.
  - The wrap path queues journal fragments only; ordered construction may step during `PLAYING`/`OVERDUBBING` after the first root under the replacement policy for `DEC-045`.
  - “Provisional `O:` snapshot” means a private, unselected candidate; the previous complete root remains active until atomic publication.
- [ ] 3.6 Complete the first implementation slice before consumer migration: verify snapshot correctness, copy-on-write sharing, bounded maintenance, atomic publication/previous-snapshot retention, parity, maximum MIDI gap attributable to snapshot construction, maintenance time per slice, and total construction wall time on native, firmware-size, and device gates.
  - Extend the existing snapshot/take-survival native tests for repeated same-`NoteId` updates, wrap NoteOn/NoteOff replacement, Hide, equal-tick ordering, and non-note events.
  - Measure and freeze `T_RT_reserve` before device validation. Pass requires construction-attributable MIDI gap and deadline lateness to remain within that reserve, with the documented retry result on overrun.
  - Device `snapshot_idle` now includes `journals`, `fragment=i/n`, and
    `parent_rev` so wrap-journal work is distinguishable from pass
    reconstruction. Recapture after upload; [`173826`](../../../captures/session_20260827_173826.log)
    cannot close this gate.
  - RAM1 build gate is restored on this tree (`teensy41-capture-serial`
    locals **7008**), so device recapture can proceed.
  - Capture [`194515`](../../../captures/session_20260827_194515.log) exposed
    admission starvation after `journals=0`: queued session-exit pass work was
    not admitted in PLAYING, and the stopped-track gate blocked it while the
    transport clock remained running. Existing admission gates now recognize
    pending snapshot work while retaining `stepPendingSnapshotPublish(1)`.
  - Capture [`200115`](../../../captures/session_20260827_200115.log) exposed
    a 63.667 ms session-exit construction slice. Existing apply state now
    persists membership-copy, mutation, append, and event-sort progress so one
    admitted unit handles at most one 16-reference membership page. The
    capture-serial build remains at **7008** RAM1 locals; device
    `T_RT_reserve` verification remains open.
  - Capture [`202635`](../../../captures/session_20260827_202635.log) confirms
    the 18-fragment journal drain and eventual `pending=0`, but still records
    79.169 ms and 74.453 ms `snapshot_idle` slices from whole-pass session-exit
    start. Existing journal caps now also bound that start through a persistent
    `CommittedEventRange` cursor; one admitted unit begins at most one
    journal-capped pass-change fragment. Device `T_RT_reserve` recapture
    remains open.
  - Capture [`204136`](../../../captures/session_20260827_204136.log)
    published `root_rev=8` when wrap journals drained while `queued=3`
    session-exit passes remained. Playback then read that incomplete
    snapshot and dropped the first record pass. Wrap completion now leaves
    the candidate unpublished until queued passes, construction progress,
    and companion changes complete. Native fixture
    `test_wrap_journal_drain_keeps_first_record_while_session_exit_pending`
    covers the gather invariant. Device recapture remains open.
  - Stage 1 of
    [`snapshot_publication_timing_bugfix.md`](../../../docs/Plans/snapshot_publication_timing_bugfix.md)
    admits pending STOPPED Snapshot work over `visualCacheNeedsFirstPaint`.
    Native fixture `test_stopped_pending_snapshot_wins_over_first_paint`
    covers that combination. Device recapture remains open; `T_RT_reserve`
    stays Stage 2.
  - Keep the global overdub undo entry pass-based while construction is pending; snapshot-identity undo attachment remains a later task.

## 4. Direct consumers

- [x] 4.1 Make playback preparation read the active snapshot directly and use snapshot identity for freshness while retaining safe fallback; select the bounded playback span range before applying projection and keep `playbackEventPhase` scalar in the hot path.
      RC4 measurement follow-up: the deferred `playback_build` diagnostic now
      includes `path=snapshot|compatibility|note_edit`, captured at
      `Track::ensurePlaybackMergedMidiEventsBuilt`; the existing maximum-sample
      and aggregate-count policy is unchanged. Device capture
      [`202654`](../../../captures/session_20260826_202654.log) validates 82
      samples: 74 `compatibility`, 8 `snapshot`, and no `note_edit`.
- [x] 4.2 Preserve `Track` / `LoopPlaybackRuntime` ownership of cursor, order, ledger, and horizon state; verify output with existing playback gates.
- [ ] 4.3 Convert `LoopContentResolution` to stateless explicit-snapshot queries without parent traversal or maintained replay.
- [ ] 4.4 Migrate overdub source resolution, display, NOTE_EDIT, overdub analysis, and validation callers independently with rollback gates. Source resolution reads selected pitch/bar `NoteRef`s, deduplicates by `NoteId`, and derives immutable geometry. Select bounded canonical span ranges before `IntervalProjection`; define long-span `k` limits or empirical overrun policies as consumer bounds, not snapshot-architecture units.
- [ ] 4.5 Verify record, overdub, wrap, short/long loop, slot, load, undo/redo, active-capture separation, same-`NoteId` geometry versioning, old-snapshot stability, and latest-selected-version behavior with existing HITL and native gates.
- [ ] 4.6 Update every migrated derived representation to use snapshot identity for source freshness while retaining its existing owner, invalidation, bounded build, and stale/fallback policy. Playback, display, NOTE_EDIT, analysis, validation, persistence, undo, redo, clear-slot restore, and load must use snapshot content as their normal source. Compatibility replay/materialization remains only an oracle or rollback path and is not optimized.

## 4b. Post-stop MIDI stability

Stop-display hold is device PASS
[`004934`](../../../captures/session_20260826_004934.log)
(`ODUB,stop,display` 8.8 / 12.2 ms) unless wrap immediately precedes stop.
[`011829`](../../../captures/session_20260826_011829.log) last stop was
**155.6 ms** after wrap cleared the composed frame. Device PASS
[`092755`](../../../captures/session_20260826_092755.log) last stop
**11.1 ms**. **Now:** Ring B / RC2 is closed. **4b.1** and **4b.2** are closed
as Ring B round-robin admission + first-record live-start contracts — see
[`playback_window_round_robin_admission_refinement.md`](../../../docs/Plans/playback_window_round_robin_admission_refinement.md).
Open residual: **4b.3** (post-overdub PLAYING window attribution). 4b.4 is
closed for `DisplayResolveLiveCapture`.

- [x] 4b.1a Implement Ring B idle admission with one unconditional five-owner
      cursor turn; remove the snapshot cadence, visual cadence, and post-stop
      persist/snapshot pairwise gates. Keep the wrapped-content dependency skip
      non-priority and owned by the cursor position.
- [x] 4b.1b Ring B / RC2 device gate closed. Dirty bars drain during
      OVERDUBBING; wrap coverage closed by 4b.2b
      ([`183452`](../../../captures/session_20260826_183452.log),
      [`185141`](../../../captures/session_20260826_185141.log):
      `from=passes`, `DISP` 286→466 / 271→437; first-overdub `midi_gap`
      222.596→146.802 ms vs [`175350`](../../../captures/session_20260826_175350.log);
      later steady 24.325 ms vs 366 ms). Reconstruct residual transferred to
      [`playback_window_build_owner_boundary_refinement.md`](../../../docs/Plans/playback_window_build_owner_boundary_refinement.md)
      § *4b.1b* — ExecutionBounds row 8 is still unbounded; F6 is unchanged.
      No cooperative `reconstructDisplayNotes` is authorized.
- [x] 4b.2a Implement RC2 representation coverage enforcement: overdub paint
      refuses uncovered visual-cache reads, wrap append is admitted through the
      Ring B owner, and dirty visual-cache source seeds fall back to committed
      pass reconstruction.
- [x] 4b.2b Run the RC2 device gate: verify wrapped content reaches `DISP` and
      source-span diagnostics never use an uncovered dirty-cache seed.
      Device PASS [`183452`](../../../captures/session_20260826_183452.log):
      first wrap reports `lcr,srcafter ... from=passes` before
      `VCACHE,stale_range`, and the following `DISP` frame rises from 286 to
      466 visual notes. Native suite: 1476 tests passed.

- [x] 4b.0 After overdub wrap, keep the composed `DisplayNote` vector when
      resetting live compose bookkeeping (`invalidateLiveDisplayCache(true)`).
      `ODUB,stop,display` holds that frame while `visualCache` is dirty and
      does not require a `playbackRevision` match. Do not snapshot-gather on
      the stop stack. Evidence: [`011829`](../../../captures/session_20260826_011829.log)
      last DISP 121.203 s, wrap 121.318 s, no DISP until display **155.6 ms**;
      OD1 stop without wrap was 9.8 ms. Device PASS
      [`092755`](../../../captures/session_20260826_092755.log): wrap at
      151.099 s then DISP, last `ODUB,stop,display` **11.1 ms** (no `cer_win`
      on the stop stack); OD1 stop **21.4 ms**. BPM after last stop 117.7–122.4.
- [x] 4b.1 Persist and snapshot share PLAYING / OVERDUBBING / RECORDING idle
      only through the Ring B five-owner cursor: **one owner per turn**.
      Adjacent-turn `snapshot_idle` then `PERS,work` while both are pending is
      allowed Round-robin alternation, not a failure. Pass condition is **no
      true same-turn stack** (`PERS,work` must not fall inside a
      `snapshot_idle` `[t−us, t]` window). Do not move save onto the stop
      commit path. Do not convert `VerifyAndRename`. The pre-Ring-B 2 s
      playback-save grace stays withdrawn.
      Evidence: [`131852`](../../../captures/session_20260826_131852.log) was
      the pairwise-gate baseline (stacked costs). After Ring B + same-turn
      guard, recent overdub/record captures show **0** true same-turn wall
      overlaps (`183452`, `185141`, `202654`, `213537`, `214703`, `220551`).
      Closed under Ring B; residual post-first-root idle policy is **4b.2c**.
- [x] 4b.2 First-record live `snapshot_idle` under Ring B / `DEC-045`: start
      construction only after the playhead has left bar 1
      (`currentTick - startLoopTick >= TICKS_PER_BAR`), use the overdub
      live-enqueue setup, update first-take `loopLengthTicks` with
      `computeRecordStopLengthTicks`, append later completed bars when the
      parent still has tick room, and do not rebuild the settled prefix when
      the parent is too short. Growing record does not clamp capture ticks to
      that working length. First-take display length stays continuous playhead.
      Ring B admits the snapshot owner on its five-way cursor.
      Device: first `snapshot_idle` is ~2 s after `record_length=768` and ~1 s
      before `record_length=1536` on [`183452`](../../../captures/session_20260826_183452.log),
      [`185141`](../../../captures/session_20260826_185141.log),
      [`202654`](../../../captures/session_20260826_202654.log),
      [`220551`](../../../captures/session_20260826_220551.log) — matches the
      left-bar-1 tick rule (not the withdrawn start-at-`768`-while-writing fail).
      Closed for this first-record / Ring B scope. Successor-after-root is
      **4b.2c**, not part of this checkoff.
- [x] 4b.2c Resolve the apparent PLAYING / OVERDUBBING `snapshot_idle` after
      the first published root. [`DEC-045`](../../../docs/DECISION_LOG.md#dec-045-overdub-snapshot-publish-is-idle-stepped)
      and device PASS [`233308`](../../../captures/session_20260825_233308.log)
      require **no** successor steps on PLAYING/OVERDUBBING after the first
      root until STOPPED. Investigation found that `RECS,stage,publish … ok`
      is the compatibility `LoopPass` commit marker, not immutable-root
      publication. In [`183452`](../../../captures/session_20260826_183452.log)
      and comparable captures [`202654`](../../../captures/session_20260826_202654.log),
      [`213537`](../../../captures/session_20260826_213537.log),
      [`214703`](../../../captures/session_20260826_214703.log), and
      [`220551`](../../../captures/session_20260826_220551.log), the long
      `pending=1` run ends at the final `pending=0` turn; no later
      PLAYING/OVERDUBBING `snapshot_idle` is present. The apparent violation
      is therefore not reproduced by these captures.
      `PendingSnapshotPublishBox` does retain individual `PassId`s, but the
      `snapshot_idle` diagnostic exposes only boolean pending state and cannot
      distinguish one construction from several queued pass constructions.
      SESSION_CAPTURE-gated root identity and queue/pass-count telemetry is now
      emitted from the existing `Loop` publication owner. Capture
      [`232633`](../../../captures/session_20260826_232633.log) makes the
      boundary explicit: during PLAYING/OVERDUBBING before root publication,
      `construction=1`, `queued=0`, `applied=0`, `exit=1`, and `inflight=1`;
      the final `pending=0` turn publishes `root=0x7030ad1c` with
      `root_rev=1`/`playback_rev=1`, after which no further PLAYING or
      OVERDUBBING `snapshot_idle` occurs. The run therefore proves that the
      apparent volume was the single first-record root construction, not
      successor construction or stacked overdub passes. This closes the
      historical `DEC-045` behavior boundary. `DEC-047` supersedes that
      restriction only for the newly activated tasks `3.5`/`3.6`; the
      successor construction there is bounded journal-fragment work, not pass
      reconstruction. Closed with the telemetry boundary verified.
      Do not close by filtering `T0/S6`; this is idle-policy evidence, not gap
      attribution.
- [ ] 4b.3 Attribute and bound the post-overdub-stop PLAYING window whose
      `midi_gap` is `usbdisp` + `load_frame` + `late_on`, not
      `ODUB,stop,display`. Evidence: `004934` 67.4 ms at 113.911 s
      (`usbdisp=60134`, `late_on=57719`, `load_frame=17575`; display was
      8.8 ms) and 152.3 ms at 168.952 s (`usbdisp=93249`, `late_on=103564`,
      `load_frame=27039`; display was 12.2 ms). Do not implement with 4b.1.
      [`131852`](../../../captures/session_20260826_131852.log) reinforces
      this owner split: OVERDUB `midi_gap=236090` (`usbdisp=143649`,
      `load_frame=25638`) and STOPPED `midi_gap=430552`
      (`usbdisp=538063`, `load_frame=401498`, `idle_maint=337246`) while
      `ODUB,stop,display` stays 21.9-50.7 ms.
      [`132627`](../../../captures/session_20260826_132627.log) still shows
      post-overdub PLAYING `midi_gap=175230`
      (`usbdisp=121603`, `load_frame=14728`, `late_on=184364`) and STOPPED
      `midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`,
      `late_on=30491`) while `ODUB,stop,display` remains bounded
      (18.5-38.1 ms). In tree: post-overdub drain now polls MIDI during
      `runDeferredLoadAndDisplayFrame` between display/load slices; recapture
      required before checkoff. A capture-only per-track profiler now emits
      `DIAG,track_update,<track>,<maxUs>,<overCount>` once per runtime timing
      window from the existing `TrackManager::updateAllTracks` owner; use the
      rows to attribute the aggregate `tracks`/`usbclk` stall before checkoff.
      [`133517`](../../../captures/session_20260826_133517.log) kept
      post-overdub PLAYING windows in the 26-52 ms class after stop-display
      lines (`ODUB,stop,display` 19.6-51.4 ms), while larger windows remained
      in OVERDUBBING (`midi_gap=200549` with `usbdisp=129828`,
      `load_frame=23600`, `late_on=26036`).
      [`134438`](../../../captures/session_20260826_134438.log) still shows
      post-overdub PLAYING `midi_gap=242938`
      (`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
      `ODUB,stop,display=44222`; STOPPED still reached `midi_gap=323973`
      with `loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
      and `load_frame=38737`.
      In tree: `MidiHandler::handleMidiInput` now caps per-call dispatch batch
      size to `32` (from `128`) while
      `anyPlayingMidiDrainAfterOverdubStop()` is active, so post-overdub
      drain turns cannot spend a full 128-message burst in one `usbdisp`
      slice. Recapture required before checkoff.
      [`135317`](../../../captures/session_20260826_135317.log) first
      post-stop cycle remained bounded (`ODUB,stop,display=15428`), but later
      `midi_gap=181392` appeared after PLAYING re-entered OVERDUBBING; later
      cycles were `midi_gap=42033` and `midi_gap=22132`. Keep open.
      [`140201`](../../../captures/session_20260826_140201.log) improved
      post-stop PLAYING `midi_gap` to `48361` (from `142534` in `135317`), but
      first-bars OVERDUBBING still reached `midi_gap=133468`. The same run also
      showed overdub reopen source drift (`DIAG,lcr,src,why=open` notes
      `0`/`165`/`228` versus nearby PLAYING `DISP` totals `240`/`253`/`308`).
      In tree: source-cache reopen hardening now invalidates span-cache reuse on
      restamp miss and seeds from clean `visualCache` when prepared spans miss
      and no prior cache seed exists. Keep open until recapture.
- [x] 4b.4 Bound `DisplayResolveLiveCapture` during OVERDUBBING so a live
      compose does not take a 50 ms class slice. Evidence: `004934`
      `timing_max,DisplayResolveLiveCapture,58188` at 150.535 s. Owner stays
      `rebuildOverdubCommittedDisplayLayer`. Do not CER-gather on overdub
      paint. Do not implement with 4b.1.
      [`131852`](../../../captures/session_20260826_131852.log) records
      `timing_max,DisplayResolveLiveCapture,215619` at 194.429 s.
      [`134438`](../../../captures/session_20260826_134438.log) records
      `timing_max,DisplayResolveLiveCapture,225601`.
      [`135317`](../../../captures/session_20260826_135317.log) records
      `timing_max,DisplayResolveLiveCapture,163008`.
      [`140201`](../../../captures/session_20260826_140201.log) records
      `timing_max,DisplayResolveLiveCapture,7107` (improved from `163008`).
      [`141034`](../../../captures/session_20260826_141034.log) records
      `timing_max,DisplayResolveLiveCapture,8608` (still below the 50 ms class).
      In tree: `DisplayManager::rebuildOverdubCommittedDisplayLayer` now skips
      overdub paint snapshot gather+reconstruct when `visualCache` already has
      notes and falls through to held-layer/window-filter branches. This keeps
      the owner-local fallback bounded. Closed for this owner; first-overdub
      stalls and wrap note-loss remain tracked under 4b.3 / source-cache reopen.

## 5. Undo, load, and persistence

- [ ] 5.1 Make `TrackUndo` and clear-slot restore select before/after snapshot identities while preserving strict LIFO, runtime-only undo/redo, and redo-branch clearing after new work.
- [ ] 5.2 Approve the physical storage version/layout for immutable objects, the latest saved snapshot, metadata, and local handle rebuild. Do not persist undo history or post-reboot undo lineage.
- [ ] 5.3 Implement the new content-only storage reader/writer through existing `StorageManager` deferred units and compatibility migration with rollback behavior; classify any interval-path unit in `ExecutionBounds.md` and keep runtime addresses and derived representations out of the format.
- [ ] 5.4 Verify save/load round trips, selected-snapshot equivalence, clear-slot restore, and old-format migration with existing persistence gates plus invariant-protecting checks. After load, undo depth is 0 and redo is empty.
- [ ] 5.5 Verify deferred persistence remains owned by `StorageManager` without blocking playback or `handleMidiInput()`.
- [ ] 5.6 Verify large-loop save and migration progress, overrun/failure preservation, active-snapshot availability, and absence of content-sized synchronous work at stop, input dispatch, clock dispatch, and track-tick paths.

## 6. Compatibility retirement and closeout

Compatibility sources are not an implementation target. They remain only long
enough to prove parity and provide rollback while each normal consumer migrates
to immutable snapshot content.

- [ ] 6.1 Prove no normal consumer depends on maintained LCR history, `passesMaterializedStore_`, or full replay before removing each path.
- [ ] 6.2 Remove superseded maintained-LCR state and replay APIs only after caller migration and rollback gates pass.
- [ ] 6.3 Remove compatibility effective-state derivation and old readers only after documented triggers close.
- [ ] 6.4 Run capture-serial firmware build first with RAM1 locals at least 4000, then `pio test -e native`, followed by required existing HITL verification.
- [ ] 6.5 Update runtime docs, deliverable tracking, the refinement plan, and archived specs; sync and archive only after all gates pass.
