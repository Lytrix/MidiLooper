## 1. Architecture and specification gate

Phase 0 is documentation only. Later sections are blocked until 1.1–1.8 are complete.

Progress checkpoint (2026-09-11): sections 1–3 are complete. Tasks 3.5/3.6
closed: wrap-journal publication is in tree; the remaining-interval yield
proof is parked (`kSnapshotIdleReserveYieldEnabled` is false; yield logs
stay). In section 4, 4.1–**4.6** and 4.5a are complete (4.4a–4.4h shipped;
parent 4.4 closed by inventory; **4.5** cross-gate verify SHIPPED
2026-09-10; **4.6** Stages 1–6 identity freshness SHIPPED 2026-09-11 with
§5 exclusion). In section 4b, 4b.0, 4b.1, 4b.1a, 4b.1b, 4b.2, 4b.2a, 4b.2b,
and 4b.4 are complete under Ring B round-robin. Phase **5.x** complete.
**6.1** consumer inventory SHIPPED 2026-09-11. **6.2.1b** B1 C1-a SHIPPED 2026-09-11
(HITL `231439`). **6.2.2** B3/B4 C1-a SHIPPED 2026-09-11 (HITL re-run PASS
[`001343`](../../../captures/session_20260912_001343.log)). **6.2.3** B2/B5/B6 C1-a / stale-root SHIPPED 2026-09-12. **6.2.4** B7/B8 +
**6.2 parent closeout** SHIPPED 2026-09-12 (`_6_2_BLOCKER_SITES` empty; native
**1825/1830**; HITL core [`002259`](../../../captures/session_20260912_002259.log)).
**6.4 SHIPPED 2026-09-12** (sign-off on [`164947`](../../../captures/session_20260912_164947.log); gates not re-run).
Open now: **4b.3**, **6.5** (archive gate). **6.6** post-closeout polish tracked below — **does not block 6.5**.

- [x] 1.1 Complete formal architecture reassessment for immutable snapshots, including ownership, transitions, execution bounds, storage, and compatibility removal. Recommendation is recorded in `ARCHITECTURE-REASSESSMENT.md`.
- [x] 1.2 Record the approved decision in `docs/DECISION_LOG.md` and update the refinement plan, parent runtime architecture, derived-representation authority, storage authority, and naming with the accepted `LoopPass` / `LoopSnapshot` boundary. The logical indexes and fixed-capacity physical directory are approved and implemented; remaining consumer bounds and storage layout stay separately gated.
- [x] 1.3 Keep this change selected in `docs/Runtime/CURRENT_WORK.md` and post the phase architecture gate before any firmware edits. Phase 0 is complete; tasks 3.5/3.6 and 4.1–4.3 are closed, and every later firmware slice remains separately gated.
- [x] 1.4 Complete `PREFLIGHT.md` / `DECISION_REVIEW.md` for each later ownership or stop-path implementation phase. Change-scoped artifacts cover Phases 1–5; no later phase is authorized by them.
- [x] 1.5 Record the persistent reference-root invariant: successor snapshots reuse unchanged event-object references, allocate new objects only for added or modified content, and must not copy the complete reference set.
- [x] 1.6 Select the logical reference structure: notes as pitch → bar → membership lists of `NoteRef`s (span index) and events as bar → membership lists of `EventRef`s (indexed view of existing event-stream order). Path-copy is rejected as the overlap model. The fixed-capacity physical directory is implemented under the selected representation.
- [x] 1.7 Specify snapshot reachability/reclamation, provisional session snapshots versus global undo snapshots, redo-branch clearing, and latest-saved-snapshot reboot behavior.
- [x] 1.8 Record the architectural construction contract (identify, replace, construct root, freeze, publish) and require an `ExecutionBounds.md` class before runtime publication. Treat any numeric N-candidate quantum as a measurement hypothesis only.

## 2. Immutable snapshot model

Phase 1 is complete. The logical indexes and fixed-capacity physical directory
are implemented. Candidate construction and publication have since passed the
activated runtime-bound gates in section 3; later consumer construction bounds
remain separately gated.

- [x] 2.1 Define immutable note and non-note event objects with loop-scoped stable references, a memory allocation contract, canonical span access for projection consumers, and a construction-unit measurement contract for later execution-bound classification.
  - Allocation contract: `allocateSnapshotShared` packs nodes into 32 KB bump
    slabs owned by `Loop` (`DEC-048`). Construction reuses one span root and
    one event root. Capture-serial logs `DIAG,snapshot_alloc`.
- [x] 2.2 Implement the measured fixed-capacity pitch/bar membership indexes and complete immutable `LoopSnapshot` records under `Loop` / `LoopPasses`. Allocate only the minimum directory structure required by the selected representation. Keep `LoopPass` as the compatibility operation record.
- [x] 2.3 Implement native/offline candidate construction from a const active snapshot and an existing semantic mutation with explicit resumable progress and measured units. Freeze, validate, and retain a complete candidate without making it active; preserve the active snapshot on failure.
- [x] 2.4 Verify the architectural invariants with existing native gates plus narrowly scoped checks only: snapshot completeness and self-containment, unchanged-object sharing, changed-object replacement, non-note preservation and ordering, multi-bar/wrap membership, `NoteId` deduplication, snapshot identity stability, alias safety, older-snapshot stability, and failed-candidate preservation. Do not add a new test architecture.

## 3. Capture and edit publication

The section 3 construction gate is complete. A long note can still touch every
intersecting bar; the implementation uses bounded fragments and persistent
cursors rather than treating that locality as an execution bound.

- [x] 3.1 Adapt `Loop::commitCapturePass` at its existing boundary to publish one global `LoopSnapshot` per record or overdub session-exit without changing wrap boundaries. Intermediate record prefixes and overdub wraps remain provisional.
- [x] 3.2 Adapt NOTE_EDIT close/commit to publish one global `LoopSnapshot` for a settled edit batch. In-session batch snapshots remain provisional `E:` revisions.
- [x] 3.3 Retain compatibility pass records only as parity/rollback data while consumers migrate. Use existing verification gates plus invariant-protecting parity checks for overlap, LIFO pairing, wrap pairs, equal-tick ordering, and non-note events; do not use or optimize pass replay as normal effective-state handling.
- [x] 3.4 Before runtime publication, classify each named candidate-construction operation in `ExecutionBounds.md`; define the unit cap, maximum indivisible step, progress state, completion condition, and explicit overrun/failure policy. Do not introduce content-sized construction in `handleMidiInput()`, clock dispatch, or track-tick paths.
- [x] 3.5 Publish a complete provisional `O:` `LoopSnapshot` after each settled overdub wrap through the existing `Loop` publication owner. Preserve `NoteId` for geometry-only `Replace`, remove hidden notes, allocate new IDs only for new notes, publish atomically after bounded construction, and record only one global `OverdubPassAdded` undo transition at overdub stop.
  - Capture/pairing state assigns `NoteId` incrementally to each logical NoteOn/NoteOff pair before pass promotion; promotion performs no content-sized pair-identity scan. `EventId` remains for non-note events only.
  - Existing semantic owners provide a transient per-wrap mutation journal with structural caps for mutation count, affected bars, membership entries, and bytes.
  - Oversized work is represented as ordered bounded fragments. Exact parent snapshot identity is retained across fragments and multiple wraps.
  - Hide removes paired note event references from the event index by `NoteId`. Geometry replacement creates new immutable note event objects with the same `NoteId` and canonical order; non-note replacements retain `EventId`.
  - The journal is runtime-only in this slice; compatibility pass storage remains unchanged.
  - A failed or over-budget fragment retains its journal and previous selected snapshot for retry. It is not discarded.
  - Snapshot construction uses fixed-capacity immutable membership pages and persistent bar-cursor progress. Construction initialization and per-change begin operations do not enumerate all changed bars up front, and one unit does not copy an unbounded membership vector.
  - The wrap path queues journal fragments only; ordered construction may step during `PLAYING`/`OVERDUBBING` after the first root under the replacement policy for `DEC-045`.
  - “Provisional `O:` snapshot” means a private, unselected candidate; the previous complete root remains active until atomic publication.
- [x] 3.6 Complete the first implementation slice before consumer migration: verify snapshot correctness, copy-on-write sharing, bounded maintenance, atomic publication/previous-snapshot retention, parity, maximum MIDI gap attributable to snapshot construction, maintenance time per slice, and total construction wall time on native, firmware-size, and device gates.
  - The chronological evidence below preserves intermediate failures and
    recapture decisions. Any “remains open” or “recapture required” wording
    refers to that historical checkpoint; the final task status is closed
    unless a later residual is explicitly listed as parked or open.
  - Extend the existing snapshot/take-survival native tests for repeated same-`NoteId` updates, wrap NoteOn/NoteOff replacement, Hide, equal-tick ordering, and non-note events.
    Native wrap-journal fixtures in `test_loop_take_survival` (2026-09-08):
    `test_wrap_journal_repeated_same_note_id_replace`,
    `test_wrap_journal_replaces_wrap_note_on_off_pair`,
    `test_wrap_journal_hide_removes_published_note`,
    `test_wrap_journal_equal_tick_non_note_event_order`.
    Focused native **81/81**. Device `T_RT_reserve` recapture closed by
    [`101815`](../../../captures/session_20260908_101815.log) and
    [`104353`](../../../captures/session_20260908_104353.log); the remaining-
    interval yield proof is intentionally parked.
  - `T_RT_reserve` contract is [DEC-053](../../../docs/DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary):
    `T_RT_reserve_ceiling` = 10,000 µs (policy, independent of BPM);
    per-turn `reserve_us` from snapshot-idle start to the next timing-critical
    `handleMidiInput()` entry; `T_RT_safety_margin` not frozen.
    `60% × T_clock` is diagnostic only. Pass requires construction-attributable
    MIDI gap and deadline lateness inside
    `min(ceiling, reserve_us − margin)` while `RECORDING` / `PLAYING` /
    `OVERDUBBING`, with yield/retry on overrun. STOPPED samples are excluded.
    Do not freeze an observed unit or the minimum observed reserve.
    Capture-gated `reserve_us` telemetry is in tree on
    `DIAG,snapshot_idle,reserve` (FLASHMEM). Device recapture
    [`104353`](../../../captures/session_20260908_104353.log). Remaining-interval
    admission is in tree: two-unit cost versus
    `min(ceiling, remaining − margin)`, with `DIAG,snapshot_idle,yield` and
    capture-only `!SNAPSHOT_YIELD`. Device yield HITL
    [`115043`](../../../captures/session_20260908_115043.log):
    `snapshot_reserve_admission.py` PASS; 6389 live yields all
    `remaining=0`; first construction and `pending=0` `root_rev=9` are after
    `PLAYING → STOPPED`. Remaining-interval yield proof is **parked**;
    `kSnapshotIdleReserveYieldEnabled` is false; yield logs stay compiled.
    Closed 2026-09-08 on wrap-journal publication plus
    [`101815`](../../../captures/session_20260908_101815.log) /
    [`104353`](../../../captures/session_20260908_104353.log). Snapshot
    idle cardinality is **N = 2**
    (`kSnapshotIdleConstructionUnitsPerTurn`); do not revert to 1.
  - Device `snapshot_idle` includes `journals`, `fragment=i/n`, and
    `parent_rev` so wrap-journal work is distinguishable from pass
    reconstruction. The earlier [`173826`](../../../captures/session_20260827_173826.log)
    capture is historical; later captures closed this gate.
  - RAM1 build gate is restored on this tree (`teensy41-capture-serial`
    locals **6848** with `SESSION_CAPTURE_SNAPSHOT_IDLE=1`); the device gate
    is closed.
  - Capture [`194515`](../../../captures/session_20260827_194515.log) exposed
    admission starvation after `journals=0`: queued session-exit pass work was
    not admitted in PLAYING, and the stopped-track gate blocked it while the
    transport clock remained running. Existing admission gates now recognize
    pending snapshot work while retaining `stepPendingSnapshotPublish(1)`.
  - Capture [`200115`](../../../captures/session_20260827_200115.log) exposed
    a 63.667 ms session-exit construction slice. Existing apply state now
    persists membership-copy, mutation, append, and event-sort progress so one
    admitted unit handles at most one 16-reference membership page. The
    capture-serial build remained at **7008** RAM1 locals in this intermediate
    measurement; later recapture closed the reserve gate.
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
    covers that combination. Device recapture
    [`221054`](../../../captures/session_20260827_221054.log) closed Stage 1.
    Stage 2 child timing is now on `snapshot_idle` (`start_us`,
    `pass_change_us`, `step_us`, `finish_us`, `publish_us`, `probe_us`).
    Device recapture [`222401`](../../../captures/session_20260827_222401.log)
    attributes the 10–12 ms first-record and 5–8 ms STOPPED session-exit
    slices to `start_us`. Parent max is 16.252 ms. Child fields now emit on
    a second `DIAG,snapshot_idle,child` line. Recapture
    [`224842`](../../../captures/session_20260827_224842.log) closes that
    budget and names `start` / `pass_change` / `step`. Record-stop leftover
    is 7.558 ms outside those children. Recapture
    [`225659`](../../../captures/session_20260827_225659.log) names that
    leftover as `discardPendingIfLoopLengthChanged` (`discard=15262` on
    the 20.984 ms record-stop idle). Exclusive leftover after that probe
    is ≤ 39 µs. Publication closed at `root=0x70431844` / `root_rev=8`.
    Session-exit length handoff device PASS
    [`003015`](../../../captures/session_20260828_003015.log):
    record-stop PLAYING `discard=47` (was `15262`); first root stayed
    `root=0x0` until `pending=0` / `root=0x703248cc` / `root_rev=1`.
    `T_RT_reserve` is not frozen.
  - Capture [`134803`](../../../captures/session_20260901_134803.log) showed
    no wrap candidate ever published: `root_rev` stayed `1` against
    `playback_rev` 3…11 across four settled wraps whose construction all
    completed (`applied` 0→4, back to `pending=0`). `remainingSessionExitPublishWork`
    gates on `companionsApplied`, which only the session-exit path ever sets,
    so the wrap branch was unreachable. `remainingWrapPublishWork` scopes that
    condition to `sessionExitQueued`. Each wrap journal now carries a
    `targetRevision` stamped by `recordLastOverdubSeal`, and
    `storePublishedSnapshot` selects it, so a candidate is never marked
    current for a revision it does not contain; unstamped journals stay
    unselected. Native fixtures
    `test_settled_overdub_wrap_publishes_current_snapshot` and
    `test_unstamped_wrap_journal_leaves_snapshot_unselected`. Device
    recapture remains open.
  - Captures [`094228`](../../../captures/session_20260908_094228.log) and
    [`094930`](../../../captures/session_20260908_094930.log): wrap journals
    started (`fragment=0/3`) then both overdub stops were `seal=skipped`.
    Session-exit never queued; `closeOverdubSession` discarded the in-flight
    journals and `root_rev` stayed 1. `discardUnpublishedSnapshotIfNotSessionExit`
    now retains wrap journals / in-flight wrap construction. Session-exit
    keep-alive includes wrap journals. Skipped overdub stop queues session-exit
    when wrap work is pending. Native fixtures
    `test_skipped_overdub_stop_retains_in_flight_wrap_journals` and
    `test_queue_session_exit_keeps_wrap_journals_without_new_pass`. Focused
    native **83/83**. Device recapture remains open.
  - Keep the global overdub undo entry pass-based while construction is pending; snapshot-identity undo attachment remains a later task.

- [x] 3.7 Fix dense-bar snapshot representability and fragment transaction (2026-09-16).
      Device record/overdub pair `task23-dense-snapshot-cc-playback` has 734
      committed effective rows; bar 2 alone needs 260 event references, above
      the fixed 16-page (256-reference) per-bar ceiling. Construction failed at
      `appendCurrentReference`, and because pass-change progress had already
      advanced the fragment was discarded, so the truncated candidate was
      published (regression
      `test_dense_device_capture_snapshot_preserves_every_committed_row`
      reported 710/734). DEC-047 amendment 2026-09-16 owns the contract.
  - Express `kSnapshotMembershipMaxRefsPerBar` as an explicit design envelope
    and derive the page-count limit from it; apply the same envelope to
    event-bar and note-bar membership; keep pages lazily allocated; widen
    page-count state to `uint16_t`.
  - Make fragment incorporation transactional: authoritative pass-change
    progress advances only after successful incorporation; a failed fragment
    retains its exact checkpoint, discards any partially constructed candidate,
    and the previous published root is never replaced by a prefix.
  - Distinguish transient/budget failure (retry the exact fragment) from
    persistent representability failure (keep the previous root, block
    publication, emit a diagnostic, do not loop silently).
  - Add a 257-reference boundary fixture, a partial-apply failure/retry
    exactly-once fixture, and retain the 734-row dense device regression.
  - **SHIPPED 2026-09-16.** `kSnapshotMembershipMaxRefsPerBar = 4096` (256 pages)
    derived envelope; four `uint8_t` page counters widened to `uint16_t`;
    event and note membership share the envelope; partial candidate discarded on
    failure; representability failure parks publication after two consecutive
    failures (`DIAG,snapshot_fragment,...` capture-gated). Focused native
    `test_loop_take_survival` **132/132** (734-row device regression, 257-ref
    boundary, partial-apply-exactly-once, parking). Full native **2166 passed,
    1 skipped, 3 known task-2.3 reentry failures**; capture-serial build SUCCESS
    with RAM1 free for locals **6752** (unchanged).
  - **Hardware verification 2026-09-16** (build `5bb90b47bdae299d630be640…`,
    `captures/task23-dense-snapshot-fix-record` + `...-fix-playback`): the dense
    recipe stored 766 effective rows with bar 2 at **270 refs (> 256)**; four
    restart cycles played 765 rows with **0 duplicates** and only the
    pre-existing terminal loop-boundary Off (`NoteId = 0`, tick 3839) absent.
    The 24-row representability omission is fixed on device. Ordering parity and
    the terminal-Off ledger/pairing behavior remain separate pre-existing items.
  - **Re-stress `task23-dense-fix-rerun3`** (fresh upload/reset, focused
    interruption PASS: stop +10.1 ms, all-notes-off 3.0 ms, 19.0 s silence):
    bar 2 held **260 refs (> 256)**, 732 effective rows; four cycles played 730
    with **0 duplicates**. The two absent rows are both ledger/pairing
    semantics, not capacity: the loop-boundary Off (`NoteId = 0`, tick 3839)
    and one of two coincident same-pitch Offs (`NoteId` 26/138, tick 1669).
    Confirms the representability fix under a fresh >256-ref bar.

## 4. Direct consumers

- [x] 4.1 Make playback preparation read the active snapshot directly and use snapshot identity for freshness while retaining safe fallback; select the bounded playback span range before applying projection and keep `playbackEventPhase` scalar in the hot path.
      RC4 measurement follow-up: the deferred `playback_build` diagnostic now
      includes `path=snapshot|compatibility|note_edit`, captured at
      `Track::ensurePlaybackMergedMidiEventsBuilt`; the existing maximum-sample
      and aggregate-count policy is unchanged. Device capture
      [`202654`](../../../captures/session_20260826_202654.log) validates 82
      samples: 74 `compatibility`, 8 `snapshot`, and no `note_edit`.
- [x] 4.2 Preserve `Track` / `LoopPlaybackRuntime` ownership of cursor, order, ledger, and horizon state; verify output with existing playback gates.
- [x] 4.3 Convert `LoopContentResolution` to stateless explicit-snapshot queries without parent traversal or maintained replay.
- [x] 4.4 Complete the remaining display fallback/identity caller
      migrations independently with rollback gates. Closed 2026-09-09:
      sub-slices 4.4a–4.4h and 4.5a shipped; inventory found no remaining
      current-root normal-path display / NOTE_EDIT / analysis / validation
      oracle inside this task. Pass rematerialize stays no-current-root
      fallback or capture/deferred oracle only. Plan:
      [`immutable_loop_snapshot_4_4_closeout_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_4_4_closeout_refinement.md).
      Source resolution reads selected pitch/bar `NoteRef`s, deduplicates by
      `NoteId`, and derives immutable geometry. Select bounded canonical
      span ranges before `IntervalProjection`; define long-span `k` limits
      or empirical overrun policies as consumer bounds, not
      snapshot-architecture units. Idle `legacyMidiEventsFromCommitted` /
      effective-store freshness and snapshot-identity undo are **4.6** /
      **§5**, not leftover 4.4 work.
  - [x] 4.4a Migrate the selected-snapshot overdub source branch to gather
        only the bounded source-window bar memberships. Preserve
        `overdubSourceSpanCacheNotes_` / `overdubSourceViewNotes_` ownership,
        fallback order, pending overlays, and compatibility identity recovery.
        Native `test_overdub_source_view` and full native suite pass;
        capture-serial build has RAM1 locals **6848**.
  - [x] 4.4b Migrate committed display window reconstruction and current-root
        stopped-display fallback to use gathered `LoopSnapshot` content.
        Preserve pass replay for stale, absent, or overlay-rejected roots.
        Capture-serial build has RAM1 locals **6848**; full native suite:
        **1755 succeeded, 1 skipped**.
  - [x] 4.4c Stamp span-cache published revision on snapshot fill. Reuse at
        overdub open/hold/prewarm only via `overdubSourceSpanCacheReusable()`
        so a fallback prewarm at the same `playbackRevision` cannot hide a
        current root (`135337` `from=cache` 123 vs SnapshotNotes 121).
        Pass reconstruction remains the no-current-root fallback.
        Capture-serial build has RAM1 locals **6848**; full native suite:
        **1757 succeeded, 1 skipped**.
  - [x] 4.5a After overdub stop, hold the composed `liveDisplayNotes` frame
        (`refreshViewportAfterOverdubStop` does not pass-gather). Elevate
        Snapshot idle turns while `postOverdubPlayingMidiDrain` and
        `sessionExitQueued`. First new committed paint uses the current
        published `LoopSnapshot` only. Plan:
        [`overdub_stop_snapshot_publication_convergence_refinement.md`](../../../docs/Plans/overdub_stop_snapshot_publication_convergence_refinement.md).
        Capture-serial RAM1 locals **6848**; native **1759 succeeded, 1 skipped**.
        Device PASS [`145633`](../../../captures/session_20260908_145633.log):
        both stops `from=hold`, display child 0, no stop-stack `pairrows`;
        `post_stop` `root_rev == playback_rev`; next open `from=snapshot`.
  - [x] 4.4d When NOTE_EDIT paint/select rebuilds a dirty committed base and
        the published `LoopSnapshot` is current, use
        `LoopContentResolution::resolveDisplayNotes(*snapshot)`. Keep pass
        materialize when no current root. Do not change commit rematerialize
        or `materializedLoopEventsForNoteEditFocus`. Plan:
        [`note_edit_committed_base_snapshot_refinement.md`](../../../docs/Plans/note_edit_committed_base_snapshot_refinement.md).
        Capture-serial RAM1 locals **6848**; native **1760 succeeded, 1 skipped**.
  - [x] 4.4e Stage 1: `rebuildVisualCacheFromPasses` on a current published
        `LoopSnapshot` uses `resolveDisplayNotes(*snapshot)` for
        `visualCache.notes`. That query copies note-span membership
        (`appendSnapshotNotesForBarRange`). Do not event-flat reconstruct.
        Current-root rebuild skips wrap-held append after spans.
        Device PASS [`162207`](../../../captures/session_20260908_162207.log)
        questions 1–2.
        Q3 [`170211`](../../../captures/session_20260908_170211.log) earned
        Stage 2 (`only_store=1`, NoteId `64`).
        Stage 2: `rematerializeEditView` on a current root keeps snapshot
        events whose `NoteId`s are in span membership.
        Stage 3: `commitEditAction` rematerialize uses
        `copyNoteEditCommitBase` (pinned open root + session edit rows).
        Device PASS [`173531`](../../../captures/session_20260908_173531.log).
        Close bake follow-up: `bakeNoteEditSessionStoreToPasses` uses
        `copyNoteEditCommitBase` so an empty-id second exit does not
        invent `NoteEditPassClosed`. Device PASS
        [`174646`](../../../captures/session_20260908_174646.log). Plans:
        [`note_edit_committed_oracle_snapshot_bugfix.md`](../../../docs/Plans/note_edit_committed_oracle_snapshot_bugfix.md),
        [`note_edit_close_bake_snapshot_bugfix.md`](../../../docs/Plans/note_edit_close_bake_snapshot_bugfix.md).
  - [x] Close publish: `appendEditRowChange` reuses the current snapshot
        pitch for `NoteRange` (does not write unset `row.pitch` 0). Same
        close set rebases on an earlier same-`NoteId` change.
        Plan:
        [`note_edit_snapshot_note_range_pitch_bugfix.md`](../../../docs/Plans/note_edit_snapshot_note_range_pitch_bugfix.md).
  - [x] 4.4f Leftover NOTE_EDIT oracles after 4.4e: current-root open does
        not flatten `loop.midiEvents()` for NoteId assign; E: rows undo/redo
        fallback copies the opened snapshot root instead of
        `materializeExcludingEditPassIds`. Pass rematerialize stays the
        no-current-root fallback. `take_only` unchanged. Plan:
        [`note_edit_leftover_oracle_snapshot_refinement.md`](../../../docs/Plans/note_edit_leftover_oracle_snapshot_refinement.md).
  - [x] 4.4g Analysis/validation leftover oracles: idle validate on a current
        root copies span-filtered snapshot events instead of
        `mergeActiveCapturePasses`. Overdub analysis current-root gather
        already skips pass flatten (4.4a). Pass rematerialize stays the
        no-current-root fallback. SEVT / `take_only` unchanged. Plan:
        [`analysis_validation_oracle_snapshot_refinement.md`](../../../docs/Plans/analysis_validation_oracle_snapshot_refinement.md).
        Capture-serial RAM1 locals **6848**; native **1773 succeeded, 1 skipped**.
        Device [`193700`](../../../captures/session_20260908_193700.log):
        second overdub open `from=snapshot` `root_rev=7` `play_rev=7`.
        [`194039`](../../../captures/session_20260908_194039.log):
        NOTE_EDIT open `current=1` `root_rev=8` **177** / `only_*=0`.
        [`194438`](../../../captures/session_20260908_194438.log):
        Stage 1 idle validate at 600.015 s on current `root_rev=1`.
        Both overdub opens `from=snapshot`.
  - [x] 4.4h Display fallback/identity: windowed paint
        (`rebuildDisplayNotesInWindow`) on a current root copies span
        notes instead of event reconstruct. Overlay gather is rejected.
        Live-capture merge keeps the existing gather. Pass rematerialize
        stays the no-current-root fallback. Plan:
        [`display_fallback_identity_snapshot_refinement.md`](../../../docs/Plans/display_fallback_identity_snapshot_refinement.md).
        Capture-serial RAM1 locals **6848**; native **1774 succeeded, 1 skipped**.
        Device [`201106`](../../../captures/session_20260908_201106.log):
        second overdub open `from=snapshot` `root_rev=5` `play_rev=5`.
        NOTE_EDIT open `current=1` `root_rev=6` **137** / `only_*=0`.
- [x] 4.5 Verify record, overdub, wrap, short/long loop, slot, load, undo/redo, active-capture separation, same-`NoteId` geometry versioning, old-snapshot stability, and latest-selected-version behavior with existing HITL and native gates.
      Plan:
      [`immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md).
      Device 2026-09-10: `base` PASS [`122812`](../../../captures/session_20260910_122812.log);
      `overdub_occupy_paint` PASS [`123445`](../../../captures/session_20260910_123445.log);
      `note_edit_overdub_compose` `02` PASS **14/14**
      [`142119`](../../../captures/note_edit_overdub_compose_20260910_142119_serial.log)
      (after RC1+RC-A PassMaterialize deferral). Native 1820/1; RAM1 locals **7008**.
- [x] 4.6 Update every migrated derived representation to use snapshot identity for source freshness while retaining its existing owner, invalidation, bounded build, and stale/fallback policy. Playback, display, NOTE_EDIT, analysis, validation, persistence, undo, redo, clear-slot restore, and load must use snapshot content as their normal source. Compatibility replay/materialization remains only an oracle or rollback path and is not optimized.
      Plan (inventory + stages):
      [`immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md).
      **Stage 1 SHIPPED (2026-09-10):** `VisualCache::builtFromSnapshotIdentity` +
      `visualCacheIsCurrentPublishedSnapshot`; native A→B@R;
      `test_overdub_source_view` wrap-append skip stamps identity; RAM1 **7008**;
      native **1821 succeeded, 1 skipped**; HITL `base` PASS [`151433`](../../../captures/session_20260910_151433.log);
      `overdub_occupy_paint` PASS [`151700`](../../../captures/session_20260910_151700.log).
      **Stage 5 SHIPPED (2026-09-11):** `CommittedMidiScratch` refresh for
      `legacyMidiEventsFromCommitted` (snapshot fill + revision/identity;
      pass fallback stamps `nullptr`);
      `tryCopyCurrentSnapshotEditViewEvents(..., observedIdentity)`;
      native four-case
      `test_legacy_committed_scratch_snapshot_identity_freshness`; RAM1 **6976**;
      native **1838/1**; HITL `base` PASS [`140021`](../../../captures/session_20260911_140021.log).
      **Stage 6 SHIPPED (2026-09-11):** `noteEditFocusMaterializePublishedSnapshotIdentity_`
      + `noteEditFocusMaterializeKeysMatch`; native A→B@R focus materialize keys;
      RAM1 **6976**; native **1839/1**; HITL `base` PASS
      [`141547`](../../../captures/session_20260911_141547.log).
      **§5 exclusion:** persistence / undo / redo / clear-slot / load format remain
      tasks **5.1–5.6** — not implemented in 4.6 Stages 1–6. Materialize /
      `passesMaterializedStore_` retirement remains **§6**.

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
      [`020309`](../../../captures/session_20260912_020309.log) (HITL `base` PASS
      after 6.3.2): attribution **holds** — `ODUB,stop,display` 4.1 ms / 9.3 ms;
      first scored post-OD2 PLAYING `midi_gap` **44.2 ms** @ 189.0 s
      (`late_on=22298`, `load_frame=6906`, `usbdisp=571`, T4 `track_order=15510`);
      second PLAYING sample **43.5 ms**. Bound **not met** (reference
      [`233642`](../../../captures/session_20260901_233642.log) first post-OD PLAYING
      gap **6.9 ms**). No 6.3.2 regression. Keep open — next owners `late_on` +
      `track_order`, not stop-display.
      [`030210`](../../../captures/session_20260912_030210.log) (HITL `base` PASS
      after 6.3.7): same class — `ODUB,stop,display` **4.3 ms** / **7.1 ms**;
      first scored post-OD PLAYING `midi_gap` **43.7 ms** (OD1) /
      **44.2 ms** (OD2). OD1 window: `persist_save=42764`, `track_order=16845`,
      `tracks=17028`, `late_on=12525`, `load_frame=6840`, `usbdisp=429`.
      OD2 window: `persist_save=42910`, `late_on=24526`, `late_clk=22113`,
      `load_frame=6838`, `usbdisp=1043`, `track_order=708`. Reference
      [`233642`](../../../captures/session_20260901_233642.log) OD2 bound window had
      `midi_gap=6930`, `load_frame=5260`, `late_on=11296`, `persist_save=1` — bound
      met when deferred persist is absent from the scored window. No 6.3.x
      improvement. Keep open — add `persist_save` to attribution; owners remain
      post-stop PLAYING maintenance (`late_on` / deferred persist / first-OD
      `track_order`), not `ODUB,stop,display`.
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

- [x] 5.1 Make `TrackUndo` and clear-slot restore select before/after snapshot identities while preserving strict LIFO, runtime-only undo/redo, and redo-branch clearing after new work.
      **Incomplete clause found on device 2026-09-18 (owned by new task 5.8 below):** the deferred `after` attach (Pin A) leaves the redo path a no-op when it has not landed, so a redo restores the passes but keeps the pre-undo published root -- the redone content is then inaudible even though it is in `passes`/`effective`. 5.1's identity selection itself is correct; the missing piece is the redo-without-after-root case.
      (2026-09-11) `beforePublishedSnapshot` / `afterPublishedSnapshot` on `UndoEntry`;
      deferred after-attach by originating `UndoEntryId` (Pin A); before = session-start
      published root (Pin B); pass/ClearSlot restore retained (Pin C). Native
      **1841/1**; RAM1 **6880**; HITL `base` PASS
      [`150709`](../../../captures/session_20260911_150709.log). Plan:
      `docs/Plans/immutable_loop_snapshot_5_1_undo_identity_select_refinement.md`.
- [x] 5.2 Approve the physical storage version/layout for immutable objects, the latest saved snapshot, metadata, and local handle rebuild. Do not persist undo history or post-reboot undo lineage. **APPROVED 2026-09-11 — [DEC-057](../../../docs/DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)** (`LSV1` + `formatVersion == 1`, dual-write). Firmware = **5.3**. Plan: [`immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md).
- [x] 5.3 Implement the new content-only storage reader/writer through existing `StorageManager` deferred units and compatibility migration with rollback behavior; classify any interval-path unit in `ExecutionBounds.md` and keep runtime addresses and derived representations out of the format.
      (2026-09-11) Stages 5.3.1–5.3.4 SHIPPED: LSV1 reader/writer, save dual-write when
      `publishedSnapshotIsCurrent()`, `LoadLoopJob` load authority (no rebuild on LSV1),
      migration fixture + ExecutionBounds rows 14/15. Commits `0d45bdb7`–`6b2eba89`.
      Native **1822/1**; device smoke
      [`175309`](../../../captures/session_20260911_175309.log). Plan:
      [`immutable_loop_snapshot_5_3_lsv1_reader_writer_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_5_3_lsv1_reader_writer_refinement.md).
- [x] 5.4 Verify save/load round trips, selected-snapshot equivalence, clear-slot restore, and old-format migration with existing persistence gates plus invariant-protecting checks. After load, undo depth is 0 and redo is empty.
      (2026-09-11) Native matrix + HITL `base` PASS [`181945`](../../../captures/session_20260911_181945.log);
      post-save reboot `LoadLoopJob done 4/0` [`182529`](../../../captures/session_20260911_182529.log).
      SD `LSV1` byte probe: advisory (`scripts/probe_lsv1_slot_body.py`). Plan:
      [`immutable_loop_snapshot_5_4_lsv1_verification_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_5_4_lsv1_verification_refinement.md).
- [x] 5.5 Verify deferred persistence remains owned by `StorageManager` without blocking playback or `handleMidiInput()`.
      (2026-09-11) Call-site gate: persist execute is `processDeferredSaveState` from
      `main.cpp` remainder; HITL `base` PASS [`200657`](../../../captures/session_20260911_200657.log)
      `VERIFY deferred persist nonblocking` `loop_persist=295 sliced=True drain=0 mo=600 ok=True`.
      Plan: [`immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md).
- [x] 5.6 Verify large-loop save and migration progress, overrun/failure preservation, active-snapshot availability, and absence of content-sized synchronous work at stop, input dispatch, clock dispatch, and track-tick paths.
      (2026-09-11) Host gates + HITL verifier `deferred_persist_large_loop_bounds`; native matrix
      (`test_storage_loop_io`, `test_persistence_sync_drain_budget`, `test_persistence_failure_policy`);
      full native **1823/1**; HITL `base` PASS [`203803`](../../../captures/session_20260911_203803.log)
      `windows=5 progress=True max_pers=44624us max_save=44931us active=True stop=True ok=True`;
      report [`203843`](../../../captures/host_midi_automation_baseline_20260911_203843.json).
      Prior verify-only replay [`200657`](../../../captures/session_20260911_200657.log).
      Plan: [`immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md).
- [x] 5.7 Harden `PersistenceWorkQueue` against boot-save stall when a work item is
      left `Writing` without an active `PersistenceWorkItemJob` (pre-record wedge
      caused false 5.5/5.6 HITL failure on otherwise PASS core baseline).
      - **Evidence:** FAIL [`000210`](../../../captures/session_20260912_000210.log)
        (`SlotMeta,start` only; `persist_save` 1 µs no-ops); flake PASS
        [`001343`](../../../captures/session_20260912_001343.log)
        (`persistence_handoff result=ok`, `loop_persist=231`).
      - **SHIPPED 2026-09-12:** `recoverOrphanedWritingItem` + `recoverPersistenceWorkQueueStall`
        in `stepPersistenceWorkItem` / `resetPersistenceWorkItemJobState`; `requeueWritingItem`
        writing-index fallback; native `test_persistence_work_queue` (13/13); host stall warning
        in `parse_persistence_buildup.py`; RAM1 **6880**; native **1828/1833**.
      - **5.7.5 SHIPPED 2026-09-12:** boot drain continues across slot-load SD gate between
        work items, transport defer on new items, and `otherSdIoActive` when
        `prioritizeWorkspaceSaveDrain`; HITL `base` PASS
        [`012147`](../../../captures/session_20260912_012147.log)
        (`loop_persist=217 sliced=True`; report
        [`012230`](../../../captures/host_midi_automation_baseline_20260912_012230.json)).
      - Plan:
        [`immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md).
      - **5.7.6 SHIPPED 2026-09-18 (after the RAM1 bank reduction): a full work-entry table must not block the workspace finalize.** Fix `12fdf38d`, reverted `d3290020` while it could not be flashed, re-landed `0a036c63` on top of the linker change `ea9918ac`.
        Second stall class in the same owner: the table is **monotonic** -- no path ever returns an entry to
        `NotScheduled`, and `LoopPersist` is keyed by `LoopId`, which is fresh for every new loop, so each
        clear/record cycle permanently consumes one of the 256 slots. Once exhausted, `admitWork` fails for
        *every* key including `FinalizeWorkspace` while `currentWorkspaceSave.pending` stays set, so
        `hasPersistenceWorkPending()` is true on `pending` alone with nothing to step: the blocking `saveState`
        drain spins its whole stuck budget, and the firmware refuses CLEAR for the rest of that device generation
        (`Persistence drain stuck after 33 iterations (max 33, expected ~1 slice steps, ~0 SD bytes)`, runs
        `captures/rcpub-gate-01`/`-02`; a clean upload cleared it, `captures/rcpub-gate-05` logs
        `Clear completed`). The state was pinned by arithmetic before any code change: `SyncDrainBudget.cpp:66`
        sets `finalizeSliceSteps` only when `pending && queueDepth == 0 && writingWorkItemCount == 0 &&
        !itemActive`, so `expectedSliceSteps = 1` with 0 SD bytes means `pending` alone, and
        `maxSliceSteps = 1*3/2 + 32 = 33` / `maxStuckIterations = min(64, 33) = 33` match the log exactly.
        Eliminated: PSRAM exhaustion (`refused=0/0B`, 1.6 MB free) and the transport-flush deferral
        (`deferDispatchUntilMs` is only ever written 0; `deferWorkspaceSaveDispatchDuringPlayback` has no
        callers). Zero `PERS` rows in the wedged runs against 13 `PERS,work` in a working run of the same
        capture configuration placed it in admission, not in the finalize slice.
        - Fix: `admitWork` reclaims a `Persisted` entry when no slot is free (a tombstone is lossless --
          `markItemPersisted` is its only writer, `workState` has no production consumers,
          `visitScheduledWorkItems` skips it, and a fresh admission reproduces its one use from the same
          `(type, key)`); `Queued`/`Writing` entries are never touched. `maybeAdmitFinalizeWorkspaceAfterDrain`
          returns whether a work item is available instead of discarding `admitWork`'s result, and
          `drainPersistenceWorkBlocking` fails fast with `SyncDrainFailureReason::AdmissionRefused` and a
          PROGMEM diagnostic naming `pending`/`queue`/`writing`/`active` rather than burning 33 no-op
          iterations on a state that cannot change.
        - Native: `test_exhausted_entry_table_still_admits_the_finalize` and
          `test_full_table_reclaims_a_tombstone_and_never_a_queued_item` in `test_persistence_work_queue`;
          both fail on the pre-fix behaviour (verified by disabling the reclaim) and pass after; suite 15/15,
          full native **2252 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS.
        - **First blocked, then unblocked by the RAM1 bank reduction.** The fix reads `variables 91,968 -> 96,064`
          and at 13 ITCM banks that left `locals free` 2,240, which **hung the device boot** during the workspace
          load (`captures/drain-fix-01`/`-02`: 19-21 serial lines, stopping at `BOOT,scan,start`, no `Memory]`
          ticks, no `#CAP`, USB enumerated, no crash record). Controls proved the 4 KB and not the logic: the
          pre-fix firmware booted and streamed **1136 rows in 20 s** with the same SD state, and a **semantically
          neutral edit** (`volatile int probe = 0; (void)probe;`) to either `PersistenceWorkQueue.cpp` or
          `SyncDrainSaveState.cpp` gave the same 96,064 / 2,240 and the same hang (**105 rows in 35 s, no
          `BOOT,load,ok`**) with none of this fix's logic. The growth adds no symbol (the two builds' full symbol
          tables are byte-identical; the sections grow by padding) and survives removing every new literal and
          struct field, so it is the documented no-symbol-hint alignment snap.
        - **Unblocked by `ea9918ac` (linker bank reduction).** No movable DTCM was left, so the lever was ITCM:
          `linker/imxrt1062_t41_lcr.ld` now routes ten cold TUs (LoadLoopJob, CurrentSetBootLoad, RevisionCommit,
          CurrentWorkspaceStorage, RevisionLoad, LoopInternalColdHelpers, SetBrowserRead, StorageManagerHitlSerial,
          DebugSessionCapture, NoteEditSessionUndoStack) to FLASH. `code + padding` 425,984 -> 393,216 = **13 -> 12
          ITCM banks**, and `free for local variables` 6,336 -> **39,104**; with this fix re-landed on top,
          `variables 96,064` still leaves **35,008**. Device: `captures/drain-fix-03` boots (`BOOT,load,ok`,
          1,023 rows in 30 s) and its clear precondition is **satisfied** (`Clear completed (serial log)`), with
          **0 `Persistence drain stuck`**, 0 `admission_refused`, 0 `FATAL`, 0 `#MEM_EXT_REFUSED` and **12
          `PERS,work`** rows (the wedged runs had 0). Note `padding` is now only 1,256 B, so the next ~1.2 KB of
          new ITCM code crosses back; taking the next bank means moving another ~32 KB of cold code.

## 6. Compatibility retirement and closeout

Compatibility sources are not an implementation target. They remain only long
enough to prove parity and provide rollback while each normal consumer migrates
to immutable snapshot content.

- [x] 6.1 Prove no normal consumer depends on maintained LCR history, `passesMaterializedStore_`, or full replay before removing each path.
  - Plan [`immutable_loop_snapshot_6_1_compatibility_consumer_inventory_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_6_1_compatibility_consumer_inventory_refinement.md).
  - Host gate `scripts/test_compatibility_consumer_inventory.py` (7 tests): hot paths forbid compat execute; fallback allowlists; B1–B8 site inventory stable.
  - HITL unchanged from 5.6 sign-off [`203803`](../../../captures/session_20260911_203803.log).
- [x] 6.2 Remove superseded maintained-LCR state and replay APIs only after caller migration and rollback gates pass.
  - **6.2.1b SHIPPED:** B1 generic compat retired; C1-a via `isPlaybackSessionExitPublicationGap`; HITL [`231439`](../../../captures/session_20260911_231439.log).
  - **6.2.2 SHIPPED:** B3/B4 generic compat retired; C1-a via `isPlaybackSessionExitPublicationGap`; `PassesResolve` on Overlay-reject / pass-only miss. HITL re-run PASS [`001343`](../../../captures/session_20260912_001343.log).
  - **6.2.3 SHIPPED:** B2 `HoldEmpty` on non-C1-a/C4 miss; B5 idle materialize C1-a only; B6 `copyEffectiveCommittedEvents` replaces `loop.midiEvents()`. HITL core [`002259`](../../../captures/session_20260912_002259.log).
  - **6.2.4 SHIPPED:** B7 LCR device-gate construction-only; B8 `copyCommittedPassReplayEvents` in capture (no direct `materializeToEventVector` in `LoopCapture`). `_6_2_BLOCKER_SITES` empty.
  - **6.2 parent closeout SHIPPED:** all B1–B8 consumer fallbacks retired; construction machinery remains for **6.3**. Persist wedge follow-up: **5.7**.
- [x] 6.3 Remove compatibility effective-state derivation and old readers only after documented triggers close.
  - **6.3.1 SHIPPED 2026-09-12:** C1-a session-exit compat bridges retired (B1/B3/B4/B2/B5 hold-empty
    during `isPlaybackSessionExitPublicationGap`; DEC-045 idle publish unchanged). HITL `base` PASS
    [`013611`](../../../captures/session_20260912_013611.log). Plan:
    [`immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md).
  - **6.3.2 SHIPPED 2026-09-12:** post-record visual continuity during publication gap. HITL `base` PASS
    [`020309`](../../../captures/session_20260912_020309.log). Plan:
    [`immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md).
  - **6.3.3 SHIPPED 2026-09-12:** `passesMaterializedStore_` / `materializeEditViewFromPasses` /
    `midiEvents()` / `getMidiEvents()` removed; `copyEffectiveCommittedEvents` →
    `copyCommittedPassReplayEvents`; `CommittedMidiScratch` snapshot-only. Native **1830/1835**;
    RAM1 **6880**. HITL `base` PASS [`022843`](../../../captures/session_20260912_022843.log).
  - **6.3.4 SHIPPED 2026-09-12:** `gatherCommittedEvents*` /
    `mergeMaterializedPassesWithCapture` removed; callers use `copyEffectiveCommittedEvents*`.
    Native **1699/1705**; RAM1 **6880**. HITL `base` core PASS
    [`023806`](../../../captures/session_20260912_023806.log) (persist `result` flake — 5.7).
  - **6.3.5 SHIPPED 2026-09-12:** capture `gatherConstructionCommittedEvents` snapshot-only;
    `copyCommittedPassReplayEvents` removed from `LoopCapture`; `liveEventCount` snapshot-first.
    Native **1699/1705**; RAM1 **6880**. HITL `base` core PASS
    [`024434`](../../../captures/session_20260912_024434.log) (persist `result` flake — 5.7).
  - **6.3.6 SHIPPED 2026-09-12:** `copyEffectiveCommittedEvents` snapshot-only;
    LCR `Phase::Materialize` removed; `materializeToEventVector` confined to `LoopPasses`
    (oracle/tests). Native **1699/1705**; RAM1 **6880**. HITL `base` PASS
    [`030210`](../../../captures/session_20260912_030210.log) (persist `result=ok`).
  - **6.3.7 SHIPPED 2026-09-12:** gap predicate retained for visual hold-only; dead B1
    `session_exit_compat` telemetry removed; host closeout gates finalized. Native **1699/1705**.
- [x] 6.4 Run capture-serial firmware build first with RAM1 locals at least 4000, then `pio test -e native`, followed by required existing HITL verification.
  - **6.4 SHIPPED 2026-09-12** (user sign-off — gates not re-run this session):
    - **RAM1** **6848** (`teensy41-capture-serial`, RC-P1 close build `325d6d6a`).
    - **Native** **1707/1707** at RC-P1 close (`325d6d6a`); 6C-A (`7b935a79`) adds survival fixtures — count not re-run.
    - **Device** [`164947`](../../../captures/session_20260912_164947.log): 25-bar record + multi-OD; LSV1 OD3 save
      `tail_gather_begin,bc=25` → `loop_snapshot_tail,done` → `payload_done` → `SAVE,completed` (×2 full chains);
      0 `payload_slice failed`; no `#CAPTURE_RECONNECT`. Supersedes dedicated HITL `base` re-run for §6.4 closeout
      (6.3.7 HITL [`030210`](../../../captures/session_20260912_030210.log) remains baseline regression anchor).
- [ ] 6.5 Update runtime docs, deliverable tracking, the refinement plan, and archived specs; sync and archive only after **6.4** gates pass. **6.6 does not block archive.**

## 6.6 Post-closeout polish (linked plans — non-blocking)

Long-loop record/overdub + LSV1 persist **quality** slices that complete a good
working solution but are outside the §6.3 compatibility-retirement contract.
Each item keeps its **plan doc as implementation authority**; this section is
OpenSpec tracking only. Post the phase architecture gate before firmware when
ownership, transitions, or execution-bound class changes.

| Task | ID | Plan | Status (2026-09-12) |
|---|---|---|---|
| Persist recovery resume | RC-P1.3 | [`deferred_persist_lsv1_edittail_slice_bugfix.md`](../../../docs/Plans/deferred_persist_lsv1_edittail_slice_bugfix.md) § Post-RC-P1 | **SHIPPED** — `loopPersistTailWriteResumeActive` skips gather reset on job retry |
| LSV1 tail sort grain efficiency | RC-P1-SORT | same § Post-RC-P1 | **SHIPPED** — `std::sort` one grain per axis (matches monolithic gather) |
| Live-record `kExtraBars` reduction | 6C-B | [`record_stop_snapshot_live_record_candidate_capacity_design.md`](../../../docs/Plans/record_stop_snapshot_live_record_candidate_capacity_design.md) | **SHIPPED** — `kExtraBars = 2`; native 25-bar fixture PASS |
| Record-stop bars 2–5 gap UX | GAP-8 | [`record_stop_snapshot_publication_gap_display_bugfix.md`](../../../docs/Plans/record_stop_snapshot_publication_gap_display_bugfix.md) stage 8 | **SHIPPED** — unpublished snapshot supplements uncovered bars during gap; native fixture; device [`191442`](../../../captures/session_20260912_191442.log) stop→publish 0.94 s |
| Preview compose parity | stage 7 | [`record_stop_stage_7_preview_compose_parity_bugfix.md`](../../../docs/Plans/record_stop_stage_7_preview_compose_parity_bugfix.md) | **IN PROGRESS** — post-publish non-zero in [`130411`](../../../captures/session_20260912_130411.log) |

Parallel persist track (not §6.6): **RC-P2** slot-meta debt — defer
[`deferred_persist_lsv1_edittail_slice_bugfix.md`](../../../docs/Plans/deferred_persist_lsv1_edittail_slice_bugfix.md).

- [x] 6.6.0 Index post-RC-P1 / gap follow-ups in OpenSpec (this section). RC-P1 core **SHIPPED** [`164947`](../../../captures/session_20260912_164947.log); 6C-A **SHIPPED** [`144434`](../../../captures/session_20260912_144434.log).
- [x] 6.6.1 **RC-P1.3** — resume `LoopSnapshotTail` from `writePayloadOffset` on job recovery; no `CapturePass*` replay + `tail_gather_begin` unless scratch invalid. `armLoopPersistTailWriteResume` + skip `resetDeferredLoopWriteState` on matching `LoopPersist` retry; native `test_lsv1_stepped_save_tail_resumes_write_payload_without_regather`.
- [x] 6.6.2 **RC-P1-SORT** — `sortNotesOneGrain` / `sortEventsOneGrain` use monolithic `std::sort` comparators (one grain each); byte identity tests PASS.
- [x] 6.6.3 **6C-B** — `kExtraBars` **16 → 2** in `liveRecordNewParentLength`; native `test_loop_take_survival` 18/25-bar fixtures PASS; device recapture optional.
- [x] 6.6.4 **GAP-8** — `supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars` during `shouldHoldSessionExitPublicationGapHandoff`; display-only bridge from `unpublishedSnapshot()`; native `test_gap_handoff_supplements_uncovered_bars_from_unpublished_snapshot`; device improved gap [`191442`](../../../captures/session_20260912_191442.log).
- [ ] 6.6.5 **GAP-8b** — stale published root during session-exit publish; **REVERTED** with `3c9dcd4a` (bundled GAP-8c); deferred — do not re-land without design session.
- [ ] 6.6.6 **GAP-8c** — record-stop viewport gap supplement; **REVERTED** [`204310`](../../../captures/session_20260912_204310.log); design session — [`record_stop_viewport_gap_supplement_bugfix.md`](../../../docs/Plans/record_stop_viewport_gap_supplement_bugfix.md).
- [ ] 6.6.7 **Stage 7** — **IN PROGRESS** — post-publish `missed_preview` / `compose_miss` in [`130411`](../../../captures/session_20260912_130411.log); plan [`record_stop_stage_7_preview_compose_parity_bugfix.md`](../../../docs/Plans/record_stop_stage_7_preview_compose_parity_bugfix.md).

**Suggested implementation order:** 6.6.7 (active) → design GAP-8b/8c → 6.6.1 (failure-path hardening last).

## 5.8 Redo with no attached after-root must republish the published root (2026-09-18)

- [x] 5.8 Fix the redo hole in 5.1's Pin A deferred after-attach: a global redo of a `RecordPassAdded` / `OverdubPassAdded` entry whose `afterPublishedSnapshot` was never attached must rebuild the published root from the redone committed passes instead of leaving the pre-undo root selected.
      **Finding (device, OpenSpec `successor-capture-reentry-unguarding` task 5.9; live capture, no reset).** `TrackUndo::applyPublishedSnapshotSelect` selects `undo ? beforePublishedSnapshot : afterPublishedSnapshot` and is a **no-op when the redo's after-root is empty**. The redo's pass restore (`restorePassesSnapshot`) bumps `playbackRevision` afterwards, so the surviving pre-undo root becomes stale, and the stale-root overlay has no unpublished receipt left to fold the redone passes in. Evidence -- `captures/content-verify-60` with the DEC-071 multi-pass shape (8-bar record, 8-bar overdub from bar 3, track 4 slot 1, acceptance build `teensy41-capture-serial-no-grain`):
      `playback_build` eventCount/count sequence over the same 6912-tick window **384 -> 576** (after the session-exit publish at 68.85 s) **-> 384 after `Undo` (78.53 s) -> still 384 after `Redo` (81.67 s, `overlay` path)**, while `!CAPTURE_CONTENT`'s `passes`/`effective` keep **576** (`record 384 + overdub 157 + 35`, `storage_matches_effective: true`). A MIDI-only capture with no serial open (`scripts/hitl/live_playback_capture.py`) shows the same 0 rows for the overdub notes (24-39) in the **live** session, so this is not save/load and not the publisher -- the loss is caused by the undo/redo pair. `content-verify-52` (the DEC-071 reference) ran the same undo/redo and passed, so the attach is a race.
      **Fix (implemented, awaiting this change's device gate) -- two parts.**
      (1) `Loop::requestCommittedPassesRepublish()`: queues a publication of **every active committed pass** on the normal session-exit lane (`fromLoadedPasses = false`, which the idle owner services in-session; the boot hydration lane is gated out by the deliberate `allowLoadedSnapshotHydration` stub) and **deliberately does not clear the published root**, so the previous source stays audible until the replacement lands. `TrackUndo::applyPublishedSnapshotSelect` calls it after **both** an undo and a redo of a `RecordPassAdded` / `OverdubPassAdded` entry: the captured before/after root is only an optimisation, and the resulting pass set must be represented.
      (2) `Track::processDeferredIdleMaintenance`'s `snapshotIdleAllowed` clause admitted only `liveRecordPastFirstBar || hasInFlightSnapshotConstruction() || hasWrapJournal`, so a publication **queued** with none of those (a non-wrapping overdub stop, or the rebuild above) never started: it now also admits `snapshotLoop.pendingSnapshotSessionExitQueued()`. This is the half that made a committed pass stay displayed-but-silent on a plain stop/play cycle, and it is what makes (1) actually run. Per-turn reserve/yield budgets are unchanged.
      Verify: a native case that redos a record/overdub entry with an empty `afterPublishedSnapshot` proves the root is requeued from the committed passes and that the gather carries every committed row once the publication completes; then the device gate -- the DEC-071 shape with the recipe's undo/redo must report `inventory_equal: true` on every playback cycle (`verify_playback_content.py`), with `storage_matches_effective: true` and `max_bar_refs` recorded, and no `companion path stalled` in the session log.
      **CLOSED 2026-09-18 (device):** `captures/content-verify-65` + `-65-playback` on firmware `f82ee9b9…` built from `31933183` (git clean, acceptance build) report `result: PASS`, `storage_matches_effective: true` (384 + 157 + 35 == 576) and **all five cycles `inventory_equal: true` / `exact_order: true` / 0 missing / 0 extra**, `max_bar_refs: 72`, with 0 `companion path stalled`, 0 `RING,overflow`, 0 `FATAL`, 0 `MEM_DENIED` and the deferred save `ok`. Full native **2,239 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 variables 91,904 / code 425,496 / padding 488 / locals 6,400. The native redo-requeue fixture is still owed and recorded here as follow-up coverage; the device gate above is the closure evidence.

## 5.8a A pass-state change must release the sealed capture draw-source handoff (2026-09-18)

- [x] 5.8a `Loop::setCapturePassState` must drop the sealed capture handoff when a pass is disabled. The handoff retained by `retainCapturePreviewAtSeal` is a display bridge for the just-sealed pass's rows; `sealedCapturePreviewIsCovered()` proves coverage against the committed `visualCache`, so once an undo disables the pass its rows leave the committed set and coverage can never hold again. `applyDrawSourceOverlays` then unioned those rows into every frame: the OLED kept painting the undone overdub while playback (which follows the published root) was already record-only. `resetPassTimeline` already named this hazard and clears the handoff; `setCapturePassState` is the same state change reached from a global undo/redo of a `RecordPassAdded`/`OverdubPassAdded` entry.
      **Finding (device, `successor-capture-reentry-unguarding` task 5.9's run `captures/content-verify-65`).** With the 5.8 fix in place the undo/redo playback is correct, but the display frame does not follow: `DFRAME` 194 (before `Undo`) -> **193** after, and `DISP` keeps `frame=193` against `visual=180` for the whole undo window; the redo leaves the frame at 193. The pass-resolved cache after the undo is the record-only **180**, and the frame is that cache plus the **13** sealed handoff rows (`VCACHE,slice_clean notes,180`; `194 = 181 + 13` before the undo, `193 = 180 + 13` after), so the overdub never left the display and the redo had nothing to show back.
      **Fix:** in `Loop::setCapturePassState`, when a record or overdub pass transitions to `CapturePassState::Disabled`, call the existing `clearSealedCapturePreview()` owner hook before `notifyCommittedContentChanged()`. Enabling a pass does not clear it -- the undo that precedes a redo has already released the bridge, and the re-enabled rows resolve from the committed pass set.
      Verify: native `test_disabling_a_capture_pass_releases_the_sealed_draw_source_handoff` (`test_loop_take_survival`) asserts that the draw-source frame drops the disabled pass's note on the undo and carries it again on the redo; it fails without the fix at `TEST_ASSERT_FALSE(loop.hasSealedCapturePreview())`. Full native **2,240 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 variables 91,904 / code 425,496 / padding 488 / locals 6,400 (unchanged).
      **CLOSED 2026-09-18 (device):** `captures/display-undo-03` (managed `base`, git `9eda0984` clean, firmware `47e05029…`, `teensy41-capture-serial-no-grain`, the DEC-071 shape with the recipe's undo/redo: 8-bar record sealed at 6912 ticks, 8-bar overdub from bar 3 crossing the wrap, `pairs=62 sevt=384`, `stored overdub span 64/63 count_ok/span_ok/bars_ok=True`) is **`Result: PASS`**, `undo=1 redo=1`, 0 `RING,overflow`, `overflow_free=True`, persistence `result=ok` (45.5 ms), display `overdub_ok=True transport_ok=True`, and **`DISP across undo/redo: before=194 after_undo=179 after_redo=193`** -- the frame loses the overdub on `Undo` and regains it on `Redo`, against the buggy `194 -> 193 -> 193` that the new verifier clause now rejects (`display_redo_did_not_restore_the_overdub`). The harness clause was added in the same commit (`_verify_undo_redo_display`; `scripts/test_capture_transitions.py` 46/46), so this class can no longer pass green on a stale display.

## 5.9 The publication boundary neither admits memory nor parks an allocation failure (2026-09-18)

- [x] 5.9 Give the wrap / session-exit publication boundary memory admission and failure parking, so a full PSRAM pool parks publication with the previous root selected instead of resetting the device.
      **Finding (device, `captures/session_20260918_101615.log`. User report: *it crashed a few bars after the overdub wrap, overdubbing the existing overdub*).** The crash is not a fault. No crash record exists (`DIAG,boot_reset,crash=0` on the next boot; `abort()` raises no `CrashReport`), and the device reset only after `[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` at 10:18:44, immediately followed by `#CAPTURE_RECONNECT`. The trigger window is the wrap publication. Uptime sequence in that boot: `Recording started @ tick 0` at 18.672 s on an 18432-tick loop, `Overdubbing started @ tick 488` at 67.800 s, **`Overdub wrap committed @ tick 18432` at 114.519 s**, and then for six seconds: `ExternalPoolAccounting`'s `<=32K` live class gains **one 32768-byte block every second** (96 -> 102 blocks), the internal heap falls **108 -> 72 -> 36 -> 0 KB** over three samples, the pool begins refusing `bytes=32768` requests (`#MEM_EXT_REFUSED` at 119.465 s; 8 logged, more unlogged because `recordPoolRefusal` reports only the first few and size changes), then `bytes=8224`, and `ExternalMemoryFirstAllocator::allocate` calls `abort()`.
      **Attribution.** The growing class *is* snapshot slab storage: `sizeClassFor(32768)` is class 3 (`ExternalPoolAccounting.h`), `kSnapshotSlabBytes = 32 * 1024` (`LoopSnapshot.h`), and 32768 is exactly `snapshotSlabOpen`'s `extmem_malloc(kSnapshotSlabBytes)` request; nothing else in the tree allocates 32 KB blocks in a repeating pattern. The overdub session itself did **not** leak: `live` is pinned at 8201-8203 KB and the `<=32K` class at 2919-2927 KB for the whole 45 s from 68.8 s to the wrap, against the boot's reported `pool=8182 KB`. The pool was already at its ceiling when the wrap committed, because a 12-bar record sits on top of the loaded workspace's reference root (~1.5-1.7 MB of slabs installed by the boot load) plus the prewarmed view sets (~0.9 MB of `<=128K` blocks, 715 KB of it tagged `TagViewPrewarm`), plus a constant ~3.6 MB `>128K` class present from boot and unattributed here.
      **Not RC2 / not this change's persistence work.** That boot ran no persistence at all: 15 `PERS` lines, all boot-time `GlobalMeta`/`FinalizeWorkspace`, none after the wrap, because the deferred save is only requested at overdub *stop* and the session never stopped. `hasSettledPublishedSnapshotForSave` is not on this path.
      **Control.** The identical post-wrap burst appears in the DEC-073 RC1 device run (`captures/rc1-coalescing-device/session_20260918_100646.log`; wrap at 61.070 s, `<=32K` 2510 KB/81 blocks -> 2749 KB/89 blocks over ~6 s then settles, heap 136 -> 104 KB, 0 `FATAL`, LSV1 `loop_snapshot_tail` completes). A wrap publication therefore costs ~8 slabs (~256 KB) plus an internal-heap working set **by design**, and the crashing session reached that window with no headroom. The defect is not "the wrap publication is unbounded" -- it is "this boundary neither admits nor parks".
      **RC-PUB-1 -- the publication start has no memory admission (primary).** `MemoryMonitor::OperationKind::SnapshotReserve` exists with an 8 KB margin (`defaultMarginFor`) and the operation name `snapshot_reserve`, but has **zero call sites**: the crashing boot logged exactly one `#MEM_ADMIT` (the boot probe) and zero `#MEM_DENIED`, and `admitOperationMemory` is wired only to record start, overdub start and `CompanionHistoryReserve`. The one bound that could have throttled this lane, `MidiServiceDrain::kSnapshotIdleReserveYieldEnabled`, is parked **false**, so during OVERDUBBING `stepPendingSnapshotPublish` runs at the full per-turn construction budget with no memory or yield gate. **Fix:** admit before starting content-sized snapshot work -- at `startNextPendingConstruction` and again before the freeze -- using the point-of-use **probe** form (`probeBlockAvailable`, the mode `CompanionHistoryReserve` uses because the probe is the authority rather than an extra veto) sized to the measured demand (one `kSnapshotSlabBytes` plus the fragment working set) alongside the existing pool-refusal recency check. On denial, return without starting and leave the work pending -- exactly as the persistence side leaves a loop body unwritten while it waits. The previous root stays selected, `publishedSnapshotIsCurrent()` stays false, and DEC-073's coverage contract keeps the save on the passes-authoritative path.
      **RC-PUB-2 -- an allocation failure is classified transient and retried forever (contributing).** `noteFragmentIncorporationFailure` parks publication only for `LoopSnapshotFailureReason::MembershipCapacity` (`consecutiveFragmentFailures >= kSnapshotFragmentMaxConsecutiveFailures`). An `Allocation` failure re-enters `startNextPendingConstruction` with no backoff and no admission, and the freeze-failure branch in `stepPendingSnapshotPublish` does not count a failure at all. Each retry builds a fresh `LoopSnapshotConstruction` -- including a default-allocator copy of the fragment's `LoopSnapshotChangeSet`, which is internal heap -- and re-attempts the slab allocation, so a full pool becomes a runaway retry that drains the internal heap and ends in `abort()`. **Fix:** treat `Allocation` as parking too, and count the freeze-failure branch, so a full pool parks with the previous root selected and resumes only once RC-PUB-1's admission passes again. This is the publication-boundary analogue of the defect already recorded in `docs/Runtime/CURRENT_WORK.md` for the record/overdub-start boundary ("`abort()` turns a transient allocation failure into an unrecoverable silent hang").
      **RC-PUB-3 -- the slab allocator still owns a fatal backstop.** `SnapshotSlabAllocator::allocate` calls `abort()` when `snapshotSlabAllocate` returns null. `allocateSnapshotShared` already returns an empty ref and the construction already carries a failure reason, so the backstop must route through that same non-fatal path rather than reset the device.
      **Verify.** Rerun the reported shape (loaded workspace, 12-bar record, live overdub past the wrap) and require 0 `FATAL`, 0 `#MEM_EXT_REFUSED`, device alive, and either a completed wrap publication or a parked one with the previous root selected and `hasPendingSnapshotPublish()` true -- and no `snapshot_fragment,fail` storm. Add a native case that injects a slab `Allocation` failure (reuse the `failAfterUnitsForTest` / `clearFailureInjectionForTest` injection point) asserting the publication parks and then completes once headroom is reported, which pins RC-PUB-2 rather than only the gate. Keep `captures/rc1-coalescing-device`'s post-wrap profile (~8 slabs, ~32 KB heap, then settle) as the acceptance baseline, and record `cls`/`clsn`/`heap_free` per second across the wrap in both runs.
      **Recorded dependency (not this change's RC).** The steady-state footprint is the other half: at the wrap the accounted pool live (8203 KB) already equalled the reported pool (8182 KB). That is the "idle occupancy leaves no headroom" item already tracked in `docs/Runtime/CURRENT_WORK.md` against the memory-pressure / DEC-065 track -- cite it there, do not re-derive it here. RC-PUB-1..3 change failure semantics only and cost no footprint; the ITCM budget makes that the right first cut (`code + padding = 425,984` sits on the FlexRAM bank boundary).
      **Status 2026-09-18: implemented as DEC-075; native + firmware green, device gate owed.**
      - **RC-PUB-1 landed.** `Loop::stepPendingSnapshotPublish` admits every turn that could start, step or freeze a
        candidate, through the previously dead `OperationKind::SnapshotReserve` with
        `kSnapshotPublicationReserveBytes = kSnapshotSlabBytes + 8 KB` (the gate adds its 32 KB heap floor and its
        pool-refusal recency window). A denial breaks out with nothing written: the publication stays pending and the
        previous root stays selected. Publishing an already-frozen candidate is deliberately not gated -- it allocates
        no new content memory. Because this boundary is the gate's only per-turn caller,
        `MemoryMonitor::admitOperationMemory` now bounds the repeat `#MEM_DENIED` *report* to one per second per shape
        while the *decision* stays exact on every call, the same way `recordPoolRefusal` bounds its own storm.
      - **RC-PUB-2 landed.** `noteFragmentIncorporationFailure` now parks `LoopSnapshotFailureReason::Allocation`
        after `kSnapshotFragmentMaxConsecutiveFailures` (`MembershipCapacity` stays a permanent park, `Injected` keeps
        retrying so the DEC-047 transaction fixture is unaffected), and the freeze-failure branch in
        `stepPendingSnapshotPublish` counts through the same path instead of not counting at all. The park is
        **transient**: `PendingSnapshotPublishBox::fragmentBlockTransient` is released by
        `admitSnapshotPublicationTurn` only after `fragmentParkSawDenial` records that admission denied while parked
        and then admitted again, so the same admission state that let the fragment fail cannot re-arm it.
      - **RC-PUB-3 corrected, not implemented as worded.** Routing `SnapshotSlabAllocator::allocate`'s `abort()` to
        the non-fatal path is not expressible (device `-fno-exceptions` leaves `allocate_shared` no failure channel,
        and a custom deleter would put a control block per node on the internal heap, ~450 KB at observed peaks) and
        is not reachable (every allocation goes through `allocateSnapshotShared`, which pre-reserves
        `128 + align(sizeof(Type))` first, so pressure surfaces as the already-non-fatal `snapshotSlabOpen` failure).
        The abort stays as the invariant check, tagged `site=slab_allocate` against the reachable `site=slab_open`,
        and is documented in place. The allocator that actually aborted is the shared
        `ExternalMemoryFirstAllocator`, whose abort-instead-of-deny semantics stay with the wider
        `docs/Runtime/CURRENT_WORK.md` defect entry; this task stops the publication from driving it there.
      - **Evidence.** `test_publication_turn_waits_for_admitted_headroom`: under a forced denial a step returns
        pending with the wrap journal count, `publishedSnapshot()` pointer and `publishedSnapshotRevision()`
        unchanged, no construction in flight, no failure recorded; the same publication then completes and the wrap
        note is gathered. `test_allocation_failure_parks_publication_until_admission_recovers`: two injected
        `Allocation` failures park; a further admitted turn makes no attempt (the count cannot advance); a denial
        records the pressure; the next admission releases the park and the publication completes current. Both in
        `test/test_loop_take_survival`; full native **2249 / 1 skipped / 0 failures**;
        `teensy41-capture-serial` SUCCESS with RAM1 `variables 91,968 / code 425,560 / padding 424 / locals free
        6,336` -- byte-identical to DEC-074, so no FlexRAM bank cost. Decision recorded as **DEC-075**.
      - **Device evidence 2026-09-18 (parked path): the crash class is prevented, with the boundary parking.**
        `captures/rcpub-gate-05` (git `dc9a1107` clean, `teensy41-capture-serial`, hex `7f6bd8e3…`, metadata
        `git_commit`/`firmware_hex_sha256` confirm the run identity; base preset, track 5 slot 1, TEMP stimulus
        `STIM-BASE-2-2-01-TEMP-A`, `--record-bars 8 --overdub-bars 8 --overdub-start-delay-bars 2`): **0 `FATAL`, 0
        `#MEM_EXT_REFUSED`**, device streaming to the end of the run, and **49 `#MEM_DENIED` — every one
        `op=snapshot_reserve`, `reason=heap_low`, `need=40960B`, `heap_free=68KB`** (below the 32 KB floor + 40 KB
        margin, with `pool_refused=0/0B`, so the heap was the binding constraint, exactly the crash's condition). The
        denial report is rate-bounded as designed: 49 lines over 71.8 s -> 122.1 s, ~1/s, instead of one per
        construction turn. The parked state is direct: **`snapshot_unit` 0, `snapshot_fragment` 0,
        `snapshot_slab_fail` 0, `snapshot_alloc` 0** -- no construction was started, stepped, frozen or published
        while denied, so no attempt was made and no incomplete prefix could become authoritative. Memory held flat:
        `heap_free` 68 KB min = median = max over all 73 samples, `snap` 2214 KB unchanged, pool `live` 6507 -> 7881
        KB at record-arm staging then flat. The crash log is the counterfactual: the same boundary without the gate
        went heap 108 -> 0 KB and `snap` 2409 -> 2543 KB in six seconds, then `abort()`.
      - **Owed (not device-proven).** (a) The **wrap** case specifically: this run's denials landed during
        `RECORDING` (the live-record window uses the same construction path), and the harness aborted before the
        wrap because the 1 MiB `#CAP` ring shed (`RING,` 1, `Result: INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`;
        verdict `second overdub transition not confirmed`) -- the `droid_queue`/volume issue already recorded in
        `docs/Runtime/CURRENT_WORK.md`. (b) That publication **resumes and completes** once the heap recovers: the run
        ended while still denied, so the resume path is native-proven only.
        (c) RC-PUB-2's park/release has no device occurrence, which is the intended outcome (the gate prevents the
        allocation attempt that would park).
      - **Harness reach limits the repro.** `TICKS_PER_BAR = 768`, so the crashing session's 18432-tick loop was **24
        bars**, but `--record-bars` accepts only `[1, 2, 4, 8]` (`scripts/hitl/legacy_edit_baseline.py`). The
        largest supported shape cannot reproduce the near-exhaustion window; extending the cap belongs with the
        follow-up device run above.
      - **Blocker hit and cleared (persistence state, not this change).** The first two attempts
        (`captures/rcpub-gate-01`, `-02`) aborted at the harness's clear precondition with
        `Persistence drain stuck after 33 iterations (max 33, expected ~1 slice steps, ~0 SD bytes)`. The budget
        arithmetic pins the state exactly: `finalizeSliceSteps` is set only when
        `pending && queueDepth == 0 && writingWorkItemCount == 0 && !itemActive`
        (`src/StorageManager/SyncDrainBudget.cpp:66`), so `hasPersistenceWorkPending()` was true *solely* because
        `currentWorkspaceSave.pending` was set with no work item to step -- `stepPersistenceWorkItem` returns success
        on that state (`queueDepth == 0 && writing == 0 && !itemActive` -> `return true`) and
        `maybeAdmitFinalizeWorkspaceAfterDrain` discards `admitWork`'s result, so the blocking drain could neither
        advance nor terminate. The pool never refused anything in that run (`refused=0/0B`, 1.6 MB free), so it is
        not PSRAM exhaustion, and `deferDispatchUntilMs` is only ever written 0
        (`deferWorkspaceSaveDispatchDuringPlayback` has no callers), so it is not the transport-flush deferral. A
        clean `pio run -t upload` cleared it: `captures/rcpub-gate-05` logs
        `Clear completed (serial log); precondition satisfied`. That liveness hole is a **persistence-owner defect**
        and is not fixed here (see the CURRENT_WORK entry for 2026-09-18).

## 5.10 Snapshot slab memory is live content, not reclaimable slack (2026-09-18)

- [ ] 5.10 Attribute the snapshot arena's `<=32K` class before changing its allocator. **Status: premise
      falsified by measurement; re-aimed.** The plan was a generation-local arena that would return ~3 MB of
      "mostly-empty slabs stranded by interleaved generations". The capture's own counters show the slabs are
      ~99.5 % full of live nodes, so that ~3 MB is snapshot *content*, and no allocator reclamation can return it.
      The instrument that proves this (live slab count, used/unused bytes, `pin1`/`pin24` pinning counts) is
      implemented and wired into the once-per-second `[Memory] ext` tick line, replacing the misleading cumulative
      `snap=` reading; the arena change itself is **not** implemented, because it would cost a rewrite and return
      at most the ~14 % slack plus whatever churn strands.
      **Counter-evidence (device, `captures/ab24-nogate/session_20260918_142527.log`).** The six
      `#CAP,...,DIAG,snapshot_alloc,nodes=,bytes=,slabs=` rows -- one per completed freeze -- give the arena's
      cumulative shape, and they are the data the audit's ~3 MB estimate was missing: at 12.60 s
      `nodes=16,798, bytes=2,205,184, slabs=78`. `bytes` is `sum(sizeof(Type))` (`snapshotAllocRecord`,
      `src/Loop/LoopSnapshot.cpp`). Compiling the node types with the device toolchain gives
      `SnapshotMembership<Ref>` = **140 B** (16 reference slots + count + `next`), `SnapshotBarPage<Ref>` = 256 B
      (32 membership slots), `SnapshotSpanRoot` = 1,024 B (128 pitch directories), `SnapshotPitchDirectory` and
      `SnapshotEventRoot` = 12 B, `LoopSnapshotNote` = 20 B, `LoopSnapshotEvent` = 32 B, `LoopSnapshot` = 36 B.
      Average `sizeof` is therefore 131.3 B, i.e. the population is dominated by 140 B membership pages. Real
      footprint is `sum(sizeof)` plus one `allocate_shared` control block per node (~20 B), i.e. ~2.54 MB against
      78 x 32,736 B = 2.553 MB of payload capacity: **99.5 % packed**. There is no stranded slack to reclaim, and
      `alloc=13,371 KB` against `freed=5,087 KB` shows the existing `liveCount == 0` slab release already returns
      whole slabs during churn; what remains live is referenced content.
      **What the bytes actually are.** ~2.6 MB of the class is the loaded workspace's published snapshots (83 slabs
      at 16.3 s, before any record) and ~448 KB is the 24-bar record (83 -> 97 slabs, **18.7 KB per completed
      bar**). The record creep is the live-record lane republishing a full-content snapshot once per completed bar
      (`liveRecordCompletedTicks`, `src/Track/TrackDeferredMaintenance.cpp:994-1003`), and each generation's own new
      pages stay live because the newest root shares its predecessors' subtrees by design. At ~152 B per node and
      ~3 nodes per event that is ~450 B of immutable snapshot per MIDI event, on top of the chunk pool that already
      holds the same events.
      **Re-aimed target (RC-MEM-1R).** The lever is node count per reference, not the allocator: (a) the 140 B
      membership page with 16 slots is the dominant node and path-copy churn creates a fresh chain per changed
      bar/list, so refs-per-page is the number to raise or the page to shrink; (b) the per-completed-bar whole-loop
      republication is what makes a record grow, so the live-record publication should carry only the window the
      display/playback actually resolves; (c) the chunk pool and the snapshot hold the same content twice. A narrow
      win *inside* 5.10's original scope is therefore bounded at ~150 KB for a 24-bar loop (the record lane's churn
      above its own content), not 3 MB -- so it must not be sold as the footprint fix.
      **Invariant (still binding, as a guard).** *A snapshot root or candidate generation must not retain physical
      slab memory solely because its nodes were interleaved with nodes owned by unrelated surviving generations. When
      that generation becomes unreachable, its reclaimable backing must be returned deterministically.* It holds
      today by construction (`liveCount`, whole-slab release) and must be preserved by any representation change;
      `pin1`/`pin24` is its regression signal -- a rising `pin1` means slabs are being kept alive by one node each,
      which is the state that would justify the arena work after all.
      **Structural sharing stays the design blocker** for that work if it is ever needed: path copying lets a
      successor root reference predecessor-created nodes, so a literal one-private-arena-per-root scheme would
      release backing a live successor still points at; the form would be epoch-owned slab groups with explicit
      shared references, and never more aggressive hole scanning in the existing bump allocator.
      **Verify (unchanged acceptance list, now a regression guard rather than a 3 MB fix).**
      1. Repeated candidate construction does not ratchet slab live bytes upward indefinitely.
      2. Discarded candidates return their generation-local backing.
      3. Replacing a published root does not free slabs still reachable from undo/redo or wrap ownership.
      4. Trimming an undo entry releases its root's arena when no other owner references it.
      5. Snapshot publication content stays byte/content equivalent.
      6. No new allocation happens on the timing-critical MIDI path.
      7. The existing snapshot failure/retry and stale-parent rules (DEC-073, DEC-075) stay unchanged.
      The acceptance metric is the **slope**, not the peak: report `slab_live`, `slab_unused`, `pin1` and `clsn3`
      per second across the 24-bar record and the wrap, and require a plateau rather than a ramp.
      **Implemented in this task (instrument only).** `SnapshotSlabCensus` + `snapshotSlabCensus()`
      (`include/LoopSnapshot.h`, `src/Loop/LoopSnapshot.cpp`): a doubly-linked live-slab registry inside the 32 KB
      slab header (no RAM1 cost, O(1) unlink so retiring a slab stays cheap on the MIDI edge), and live
      count / used / unused / `pin1` / `pin24` / full / partial. Emitted in the `[Memory] ext` tick line and in the
      `DIAG,snapshot_alloc` row. `snap=` keeps its historical cumulative meaning and is now labelled next to the
      live numbers so the two cannot be confused again.
      **Observed creep, for the record.** Across the plain RECORD in that capture the `<=32K` class grew 83 -> 97
      live blocks at one 32 KB slab per ~2 s while `chunks_free` (380-383) and every other size class stayed
      constant; `live` ratcheted to 8,283 KB against a reported `pool=8182 KB`, the internal heap fell
      80 -> 44 -> 8 KB in two steps, and the pool began refusing `bytes=32768` (`snapshotSlabOpen`'s request) --
      the reported overdub-start denial and, without DEC-075's gate, the original abort.

## 5.11 The total-length / retention inventory still resident in the runtime (2026-09-18)

- [ ] 5.11 Track, as named RCs, the memory owners that are still sized by the whole loop rather than by a resolved
      window, so the windowed/queued intent is enforced instead of assumed. 5.10 landed as its instrument (the live
      slab census) after its reclamation premise was falsified; the RCs below are recorded with their bytes, their
      growth law and their reclamation condition so each can be taken on its own evidence.
      **Measured budget at the denial (device `captures/ab24-nogate/session_20260918_142527.log`, pool `live`
      8,283 KB against a reported `pool=8182 KB`).** Untagged 6,858 KB: snapshot slab arena **~3,022 KB** (class 3,
      97 blocks); `LoopEventStore` chunk pool **2,566 KB** (512 x `sizeof(EventChunk)` 5,132 B = 131,072 event
      slots, one boot `extmem_malloc`; `sizeof(MidiEvent)` = 20 and `sizeof(EventChunk)` = 5,132 verified by
      compiling the constants with the device toolchain); `DebugSessionCapture` ring **1,024 KB**
      (`kCaptureRingBytes` = 1024*1024, allocated unconditionally by `ensureCaptureRingAllocated`); remainder
      ~250 KB (LSV1 save gather vectors, SD/parse staging, work queue, loop objects). Tagged **1,465 KB -- all of it
      for one active slot**: ViewPrewarm 715, CaptureStaging 148, CanonicalPassInputs 144, CompanionPasses 96,
      CaptureStopJobs 87, BridgeMap 68, Apply 66, Reconstruction 64, PlaybackOrder 58, PairLookup 10,
      TransferHorizon 7, Spans 2. The ViewPrewarm tag is one slot's fixed whole-take bundle:
      `RuntimePlaybackViewSet::fixedExternalBytes()` is 727,040 B (5 views x 2048 x 35 B + 2 bridges x 2048 x 90 B),
      and `RuntimeCanonicalPassInputs::prewarm(2048)` is exactly 147,456 B over 8 arrays, which is the figure
      `ExecutionBounds.md` already records.
      - **RC-MEM-2 -- per-slot bundles are sized by the representable ceiling, not by the window.** `kEventsPerView`
        and `kRecordsPerBridge` are 2048 (`include/RuntimePlaybackView.h:19-22`), so an armed slot reserves ~1.16 MB
        whether or not the loop has content for it. Device evidence that this is reservation rather than use: the
        prepared-pair and stopped-oracle publishers require `coverage.lengthTicks == loopLengthTicks`
        (`src/Track/TrackPlaybackWindowBuild.cpp:625-630`, `src/Track/TrackPlaybackRuntime.cpp:620-625`, `:648-653`),
        so on a 24-bar loop **no** fixed view is ever published and playback instead takes the 2-bar merged window
        (`src/Track/TrackPlaybackHotPath.cpp:273-291`).
        **Invariant (landed).** *A playback-view owner may reserve capacity only for a representation that can
        actually become authoritative for the current loop/horizon. Long-loop/windowed playback must not prewarm
        whole-take fixed-view capacity that its publication contract can never admit.* The implementation is
        therefore **mode-sensitive, not a global reduction of 2048**.
        **Landed.** `Loop::fullTakePlaybackViewAdmissible()` (`include/Loop.h`) is the single owner answer -- false
        past `DisplayWindowUtils::kMaxDetailedWindowBars`, i.e. exactly where the windowed policy takes over.
        `RuntimePlaybackViewSet::prewarm(bool reserveWholeTake)` reserves the three rolling views and both capture
        stagings always, and the two bridges plus their two immutable bases only when the loop can publish a
        whole-take view; `wholeTakeReserved` then makes the refusal explicit rather than incidental --
        `beginCandidateBridge`/`selectCandidateBridge` deny before touching bridge storage,
        `publishStoppedOracleView(const RuntimeCompanionPasses&, ...)` and `publishStoppedCanonicalOracleView` check
        `wholeTakeViewsReserved()`, and the prepared pair is skipped because its only publisher requires
        `isFullLoopMergedPlaybackWindow`. `LoopPlaybackRuntime::prewarmViews(capacity, fullTakeAdmissible)` threads
        the answer from `Track::prewarmPlaybackForSlot`, whose only production caller is the cold record-arm /
        overdub-start admission path. The reservation is **not** an irreversible downgrade: a slot admitted
        windowed-only upgrades in place if the loop later becomes short, which is why the bridge prewarm stays
        reachable from that cold boundary.
        **Size of the dead reservation.** 2 bridges x 2048 x 90 B + 2 bases x 2048 x 35 B = **512,000 B** plus the
        prepared pair's 2048-row buffer (~59 KB) = **~558 KB per armed long-loop slot**. `fixedExternalBytes()` now
        reports only the reserved part, so the figure is the slot's real reservation; the remaining per-slot bytes
        (three views 215,040 B, staging 118,784 B, and the companion/canonical owners ~450 KB) are *not* view
        bundles and are still required by the windowed lanes and the deferred stop path, so they are deliberately
        out of scope here and must not be read as reclaimable.
        **Verify.** Native `test_playback_prewarm`: `test_full_take_admissibility_follows_the_windowed_policy` (the
        predicate flips exactly at the threshold), `test_windowed_only_slot_reserves_no_whole_take_representation`
        (windowed-only slot has `capacity() == 0` for both bridges and both bases, no prepared pair, and
        `fixedExternalBytes()` equals the views-plus-staging arithmetic exactly),
        `test_windowed_only_slot_refuses_the_whole_take_publishers` (differential: the same `beginCandidateBridge()`
        is admitted on a whole-take slot and refused on a windowed-only one; the canonical oracle refuses a
        full-loop coverage; the `MidiEvent*` stopped-oracle overload, which is the *windowed* publisher, still
        publishes a partial coverage into a prewarmed view), and
        `test_windowed_slot_upgrades_to_whole_take_at_a_cold_admission` (in-place upgrade re-arms the bridges and the
        pair without resetting admitted staging). The existing 173 fixtures pin the short-loop branch unchanged.
        **Device A/B 2026-09-18 (clean worktree `c0aadc0f` = census only, vs `5ba7d109` = census + RC-MEM-2;
        24-bar record + 24-bar overdub, track 5 slot 1, base preset).** The comparison did **not** show the
        footprint drop, and the reason is now a recorded finding rather than an assumption: a record arm on an
        *empty* slot has `loopLengthTicks == 0`, so `fullTakePlaybackViewAdmissible()` is legitimately true and the
        whole-take bundle is reserved (`tags=91/715/148/0/7/0/58KB`, arm delta +1,372 KB live). The follow-up cold
        admission that would release it -- the overdub start on the now-24-bar loop -- never reached the prewarm
        because it was **denied on the internal-heap floor first**:
        `#MEM_DENIED,op=overdub_start,reason=heap_low,need=16384B,heap_free=44KB` (4 times, against 129
        `op=snapshot_reserve` denials), so `prewarmViews(..., false)` was never called. The `before` leg of the same
        shape ran the pool and heap out and aborted with `[InternalHeapFirstAllocator] FATAL` at 104 s
        (`live=8374KB, heap_free=16KB`, `#MEM_EXT_REFUSED bytes=23040`); the `after` leg survived with 0 `FATAL`,
        0 refusals and a 44 KB heap floor -- but the reservation was identical in both legs, so that difference is
        run-to-run workspace variance and is **not** attributed to this RC.
        **Release path added in the same change.** `RuntimePlaybackViewSet::releaseWholeTakeViews()` returns the
        bridge/base backing when the loop is known long at a later cold admission (and refuses while a bridge is
        selected, since that bridge is the authoritative representation), and `prewarmViews(..., false)` also clears
        the prepared pair. `LoopPlaybackRuntime::prewarmViews` calls it, so the 715 KB is no longer held for the
        whole session after a record that turned out long. **Owed (device).** A shape in which the long-loop cold
        admission actually succeeds -- i.e. one whose overdub start is not denied by the internal-heap floor -- to
        measure the release: expected `tag2 ViewPrewarm` 715 -> ~215 KB at that boundary. Fixing the heap floor
        itself is a separate RC (the shortfall is the admission's 16 KB need plus its 32 KB floor against 44 KB
        free, and it binds before pool pressure does, which is the opposite of the original crash).
        **Also settled by the same run (this is what re-aimed 5.10).** The new census fields read
        `slab_used=2453..2857KB` against `slab_live=78..91` slabs (2,453-2,857 KB of a 2,496-2,912 KB payload),
        `slab_unused=42..54KB` and `pin1=0..1` for the entire session: the arena is ~98 % packed with at most one
        slab pinned by a single node, so there is nothing to reclaim and the generation-local arena is confirmed
        unnecessary.
        Still out of scope: deriving the per-window *capacity* (2048) from the resolved window, and prewarming only
        the armed slot rather than every enabled slot -- both recorded here for a follow-up.
      - **RC-MEM-3 -- undo trimming cannot see pool pressure.** `overUndoMemoryPressure` (`src/PassReclaim.cpp:67-79`)
        trims only on `freeChunkCount() <= PassConfig::CHUNK_RESERVE` or internal heap
        `< Config::HEAP_RESERVE_BYTES`; pool slab bytes are never consulted, and its third clause is unreachable in
        effect because the immediately preceding disjunction already returned true for the same two conditions, so
        `PREFERRED_UNDO_DEPTH` (documented in `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` and
        `openspec/specs/undo-memory-trim/spec.md`) never influences anything. That matters because
        `GlobalUndoStack::entries` retain one to two published roots per entry, which is what keeps older
        generations' slabs alive. Fix: consult the O(1) pool counters (live bytes / live slab count), and either
        implement the depth arm or delete the constant and its doc claims.
      - **RC-MEM-4 -- the compatibility pass replay was not retired, it moved.** `LoopPasses::materializeToEventVector`
        is still linked and still called in production (`src/Track/TrackOverdubLifecycle.cpp:184`), and
        `LoopContentResolution::resolveDisplayNotes/resolveWindow/resolveState(const LoopPasses&, ...)`
        (`src/LoopContentResolution.cpp:1675`, `:1059`, `:1092`) still replay every pass chunk list into display, the
        visual cache and the **overdub overlap source**. On a 24-bar loop the largest single whole-loop path is
        `rebuildOverdubSourceSpanCache`'s passes fallback at ~116 KB peak (`src/Loop/LoopCapture.cpp:1105-1134`),
        reachable from the record-arm path and from the MIDI note-off path
        (`src/Loop/LoopPendingNoteChange.cpp:477-480`), because `overdubSourceSpanCacheReady()` requires the
        full-loop flag (`src/Loop/LoopCapture.cpp:1022-1032`) so a windowed fill can never satisfy readiness. Fix:
        migrate each of these callers and then remove the replay entry points, correcting
        `docs/Authority/Architecture/DerivedViews.md:141-146`, which claims `materializeToEventVector` survives only
        as an oracle. Note the §6.1 gate that was supposed to prove this is marked `[x]` but **fails 2 of 14 tests at
        HEAD** (`scripts/test_compatibility_consumer_inventory.py`:
        `test_materialize_to_event_vector_only_in_machinery_owners`,
        `test_session_exit_gap_predicate_only_in_visual_hold_owners`), and it never scans the
        `LoopContentResolution::*(const LoopPasses&, ...)` overloads where the replay actually lives now -- so the
        gate must be widened before it can be re-ticked.
      - **RC-MEM-5 -- the fixed boot stores are sized for the pool ceiling. LANDED (chunk pool, lazy blocks);
        capture ring still owed.** The chunk pool reserves 2,566 KB at
        boot for 131,072 event slots with no cap on loop length, and the `SESSION_CAPTURE` ring reserves 1,024 KB
        regardless of session shape.
        **Measured high-water (all captures on this device, `[Memory] ext` `chunks_free` minimum per run).** The
        worst observed occupancy of the 512-chunk pool is **167 chunks = 837 KB** (`captures/bar-anchor-08`/`-09`,
        `chunks_free=345`); the other worst cases are 164, 158, 152, 151 and 150 chunks. So **at least 345 of 512
        chunks (1,729 KB of the 2,566 KB reservation) have never been reached in any captured run** -- the pool is
        ~33 % used at its observed peak. This is the largest single fixed store and the one whose release would do
        most for the *functional* symptom below, because PSRAM-first containers fall back to `std::malloc` when the
        pool refuses (`include/Utils/ExternalMemoryFirstAllocator.h`), so pool headroom is what keeps the internal
        heap free.
        **Recommended form: grow on demand, do not lower the ceiling.** Keep `POOL_CHUNK_COUNT` 512 as the
        admission ceiling but allocate the `EventChunk` array in blocks (e.g. 64 chunks = 328 KB per block) on the
        first id that lands in each block, so a run that peaks at 167 chunks backs ~3 blocks instead of all 8. The
        flat id space must stay intact -- chunk ids cross `PersistenceQueue::onChunkFreed`, the committed chunk-id
        lists and the storage I/O format -- so this is a block table plus one `chunkAt(id)` accessor behind the 31
        `pool_[...]` sites in `src/LoopEventStore.cpp`, with `poolUsed_`/`poolLifecycle_`/`poolChunkRefCount_`
        staying flat (512 entries of DMAMEM). `allocChunk` opens a block when it first hands out an id inside it and
        denies as it does today when that block cannot be allocated. A lower fixed ceiling is **not** the fix: the
        167-chunk peak is an observation, not a bound, and `initPool`'s all-or-nothing reserve is what makes
        capture admission deterministic today.
      - **RC-MEM-7 -- the capture ring conflated "Tier-A thinned" with "capture invalid", so a long run aborted.
        LANDED (degradation); Tier-A volume profile sized but not yet chosen.**
        The ring's own policy is that Tier-A text *may only be displaced by Tier-A text* and that this "keeps the
        newest windows and cannot wedge the ring" (`src/Utils/DebugSessionCapture.cpp`), and its 1 MiB budget is
        explicitly a fixed PSRAM spend "rather than weakening Tier-A evidence". But both kinds of loss set one
        `overflowPending` flag, and the host treats `RING,overflow` as session-invalidating, so the designed
        degradation was indistinguishable from real evidence loss. `captures/rcemem5-lazy-pool` aborted at ~150 s of
        a 24-bar two-overdub run purely on ring capacity, after the pool fix had made the run itself healthy.
        **Fix.** `CaptureLineTier::evictionKindFor` / `incomingMayDisplace` now classify displacement in the
        header-only tier helper (host-testable without Arduino), and the ring keeps three separate accounts:
        `TimingShed` (routine diagnostics, unchanged), `TierAText` (degradation: `tierADegradedPending`,
        `tierAEvictedCount`, `tierAFirstEvictedUs`, emitted as `#CAP,<us>,RING,tier_a_evicted,<count>,first=<us>`),
        and `CriticalText` (the only fatal kind, still `RING,overflow`). Tier-A degradation also drains at the 256
        record budget now, since every record not yet sent is one the ring may have to displace again. Because the
        firmware emits `RING,overflow` only for critical loss, the existing fatal harness clause stops firing for
        Tier-A thinning without being weakened; the harness parses the new notice into
        `tier_a_evicted_count`/`tier_a_evicted_first_us`/`tier_a_degraded` and raises an **advisory**
        (`capture_tier_a_evicted:<n>`) so a timing verdict is down-weighted rather than read as clean.
        **Device result 2026-09-18 (`captures/rcemem7-tier-degrade`, same 24-bar record + two-overdub shape).** The
        run no longer aborts on ring capacity: `capture_ring_overflow` is absent from the issues and the verdict is
        now a substantive `FAIL (FIRMWARE_CONTRADICTION)` against the harness oracle instead of
        `INCONCLUSIVE_CAPTURE`, with both overdubs stored correctly (`count=192/191` and `152/7`,
        `count_ok/span_ok/bars_ok` all true) and `DISP overdub_ok=True transport_ok=True`. The advisory measures the
        thinning at **`capture_tier_a_evicted:92655`** records.
        **What that number sizes (next step, not chosen here).** ~92.7k Tier-A records beyond 1 MiB over ~162 s of
        transport puts Tier-A at roughly **30-35 KB/s**, i.e. the 1 MiB ring covers only ~30 s of active transport;
        no affordable ring holds a multi-hour run, so evidence *completeness* needs either a Tier-A volume profile
        (a long-run divisor on the per-tick span families, the `DIAG,droid_queue` gate's shape) or the SD sink. Which
        Tier-A families may be sampled is a policy decision about evidence, so it is deliberately not made here: the
        mechanism, the measurement and the advisory that make it decidable are in place.
        **Verify.** Native `test_capture_line_tier` pins the truth table: a Tier-A head classifies as `TierAText`
        (non-fatal) and a critical head as `CriticalText` even though it is also Tier-A; `incomingMayDisplace`
        refuses every case where ordinary diagnostics or non-critical lines would evict Tier-A or critical evidence.
        Harness `test_capture_transitions.py` 48/48 and `test_baseline_canonical_args.py` 3/3 still pass.
      - **RC-MEM-6 -- the admission gate is self-blinding once the internal-heap floor binds (found in the RC-MEM-2
        device A/B, and this is the user-reported "cannot activate overdub on a 24-bar loop" symptom).**
        `admitOperationMemory` denies when `heapFree < kOperationHeapFloorBytes (32 KB) + requiredBytes`
        (`src/Utils/MemoryMonitor.cpp:266`). At the A/B's steady state the heap sat at **44 KB free** for ~100 s, so
        `OverdubStart` (required 16 KB, floor 32 KB -> 48 KB) and `SnapshotReserve` (required 40 KB -> 72 KB) were
        both denied while `pool_refused=0/0B`: **because the gate denies before the request ever reaches the pool,
        the pool-refusal signal the same gate consults can never fire**, so the "pool is fine" reading and the
        heap-floor denial coexist indefinitely (129 `op=snapshot_reserve` + 4 `op=overdub_start` denials in one
        150 s run, `pool_refused=0/0B` throughout, `since_refusal=UINT32_MAX`). The device is at a genuine ceiling
        (pool `live` ~8.27 MB), so the denial is not *wrong* -- but the recovery path is, and the fix direction is
        footprint rather than gate policy: the 32 KB floor is what stands between 44 KB free and a 16 KB request.
        Recommended: (a) treat RC-MEM-5 and RC-MEM-1R as the unblock (pool headroom stops the `std::malloc`
        fallback that put RAM2 at 44 KB in the first place), and (b) give the gate a *measured* floor rather than an
        absolute one -- record the heap high-water the admitted operations actually consume and derive the floor
        from it, so the reserve is evidence-based like `companionRowHorizonBound` rather than a constant that can
        wedge a session. Do not simply lower the constant without that measurement: the floor exists to keep the
        `abort()`-on-exhaustion path unreachable, which is the DEC-075 defect class.
        **Verify.** A native fixture for the pool's lazy blocks (admission still reaches any id up to 512, a denied
        block denies the chunk instead of aborting, `freeChunkCount()` and `usedChunkCount()` unchanged, and the
        flat-id contract across `PersistenceQueue::onChunkFreed` unchanged); and on device the 24-bar shape must show
        `chunks_free` never reaching its old floor, `pool_refused=0` with `op=overdub_start` **admitted**, and
        `heap_free` above the floor for the whole run.
        **Device result 2026-09-18 (`captures/rcemem5-lazy-pool`, 24-bar record + 24-bar overdub, track 5 slot 1).**
        The functional symptom RC-MEM-6 diagnosed is **fixed**: `Overdubbing started` twice (76.582 s, 138.665 s)
        where the fixed-pool legs managed **zero** starts against 4 `op=overdub_start` denials; `op=overdub_start`
        denials are now **0**, `snapshot_reserve` denials fell 129 -> **6**, and there are **0 `FATAL`** and
        **0 `#MEM_EXT_REFUSED`** where the `before` leg aborted. Boot pool `live` fell **3,837 -> 1,913 KB**
        (-1,924 KB) because the pool now backs `ceil(used chunks / 64)` blocks instead of all 512: at
        `chunks_free=408` (104 chunks used) that is 2 blocks = 656 KB rather than the previous 2,566 KB, and the
        peak observed here is 143 used chunks (3 blocks). The decisive mechanism is the one RC-MEM-6 predicted:
        with the pool no longer falling back to `std::malloc`, `heap_free` held **80 KB through both overdubs**
        and only dipped to 44 KB at 162 s, so `OverdubStart`'s 48 KB requirement (16 KB need + 32 KB floor) was
        admitted -- against 44 KB and denial in the fixed-pool legs. Native `test_pool_backs_storage_only_as_
        content_reaches_it` pins the block form (0 blocks at boot, 1 block after one event, 2 after crossing the
        boundary, ids still contiguous across it, backed bytes far below the full array, and
        `freeChunkCount() + usedChunkCount() == POOL_CHUNK_COUNT` unchanged).
        **Still owed in this RC:** the `SESSION_CAPTURE` ring. The same run aborted at
        `firmware capture ring overflow` -- the 1 MiB `kCaptureRingBytes` can no longer hold a 24-bar two-overdub
        run now that both overdubs actually execute, so the ring is the last fixed store and the last diagnostic
        limit (the run's content itself was fine: `stored overdub span count=192/191 count_ok/span_ok/bars_ok` all
        true). Ring policy -- size per session, or shed-shape instead of overflow-abort -- is the remaining half.
      - **RC-CHUNK-1 (latent correctness) -- a chunk reference is released without returning the chunk.**
        `LoopEventStore::stepRewriteFromOrderedSource` calls `releaseChunkReference(retired)` with no paired
        `tryFreeChunk` (`src/LoopEventStore.cpp:549-552`), unlike every other release site (`:210-222`, `:366`,
        `:659`, `:706`, `:1289`); the refcount reaches 0 while `poolUsed_` stays true, so the chunk is never
        reusable and is still counted by `usedChunkCount()`. Latent today because the production stop path uses the
        copy variant (`src/Track/TrackOverdubLifecycle.cpp:902`). Fix: pair the call, and add a fixture that drives
        the rewrite path and asserts `freeChunkCount()` returns to its pre-rewrite value.
      - **RC-CHUNK-2 (latent correctness) -- committed chunk ids are copied without retaining.**
        `LoopEventStore::tryAssignCommittedChunkIds` (`src/LoopEventStore.cpp:243-267`) builds `dest` from raw ids
        without `retainChunkReference`, so the name says copy while the semantics are transfer; a caller that later
        releases `dest` can drop a chunk another owner still references. Not reachable today because the sole caller
        only `clear()`s its destination (`src/Track/TrackDeferredMaintenance.cpp:1282-1291`). Fix: retain on assign
        (or rename and document the transfer), with a fixture that asserts the refcount is unchanged across the call.
      - **Unbounded without any recorded bound** (same scan, tracked here so they are not rediscovered):
        `PendingSnapshotPublishBox::journaledPassIds`/`appliedPassIds` have no erase anywhere and are only reset with
        the whole box; `Loop::pendingNoteChanges_` and its `Track::CaptureStopJob::pendingChanges` mirror have no cap
        and the job mirror is never cleared at retirement (`src/Track/TrackOverdubLifecycle.cpp:1175-1205`);
        `VisualCache::markBarDirty` resizes to `bar + 1` with no ceiling (`include/VisualCache.h:102-107`).
      - **Dead bounded alternatives to revive or delete** (all verified caller-less in `src/` + `test/`):
        `Loop::advanceFallbackGatherSession` -- the budgeted fallback gather -- has no call site and its
        `eventBudget` argument is never read inside `rebuildVisualCacheIdleSlice`, so the idle lane runs the
        whole-loop `resolveDisplayNotes(passes, ...)` at `src/Loop/LoopVisualCache.cpp:436` instead of the budget
        `DerivedViews.md:195` documents; `TrackManager::prewarmSelectedDisplayVisualCache`,
        `DisplayManager::resolveDisplayNotesCommitted`, `DisplayManager::resolveDisplayNotesLiveCapture`,
        `TickIndex::commitLoopPasses`, `Track::getMidiEventCount` / `Loop::liveEventCount` and
        `EditManager::materializedLoopEventsForNoteEditFocus` are likewise unreachable, so `DerivedViews.md:93`,
        `DerivedViews.md:193` and `docs/Authority/Architecture/Display.md:36` describe paths that no longer run.
      - **Doc corrections owed with RC-MEM-4/-MEM-5.** `DerivedViews.md:93`, `:141-146`, `:193`, `:195`,
        `Display.md:36`, and `ExecutionBounds.md`'s fallback-gather row (which documents a 96/192/768-event budget on
        a function nothing calls) all describe behaviour the tree does not have.

## 5.12 Starting a second overdub drops the previous overdub from the display (2026-09-18)

- [ ] 5.12 Starting a second overdub must not remove the previous overdub's rows from the display. **User report
      (2026-09-18):** *if the second overdub starts it removes the entire previous overdub from the display; it
      returns at the end.* Reproduced in `captures/rcemem7-tier-degrade` (24-bar record + two overdubs, track 5 slot
      1) and present in `captures/rcemem5-lazy-pool`.
      **Signature (device).** The frame is not being served by the visual cache at all during the overdub sequence:
      `#CAP,130257180,DISP,0,PLAYING,18432,frame=382,visual=0,...,buffer=382` and
      `#CAP,120238530,DISP,0,OVERDUBBING,18432,frame=373,visual=0,...,buffer=373` against
      `VCACHE,stale_all,ev,-1,notes,0,...` and `VCACHE,stale_range,...,notes,0`. So `visual=0` while
      `frame=373/382`: every displayed row comes from the draw-source overlay / pass-resolve path, and the
      `VCACHE` build is empty and stale for the whole sequence. The second overdub start is `Overdubbing
      started @ tick 24386` at 139.079 s, and the frame recovers only later via
      `VCACHE,idle_src,OverlayRejected,src,PassesResolve,root_rev,5,playback_rev,6` at 160.358 s followed by
      `DISP,0,STOPPED,18432,frame=280,visual=71,...,232` -- i.e. the rows return when a full resolve finally runs,
      which is what "it returns at the end" describes.
      **Mechanism to prove.** The first overdub's rows are carried only by the sealed capture draw-source handoff /
      overlay, not by the committed `visualCache`, so whatever the second `beginCapture(Overdub)` invalidates
      (handoff, overlay set, or draw-source reuse key) takes those rows out of the frame with nothing to re-derive
      them from until a whole-loop pass resolve runs. This is the §5.8a family -- the handoff is a display bridge for
      a just-sealed pass's rows, and a state change that drops it leaves the rows unreachable -- so the fix belongs in
      the same owner (`Loop::beginCapture` / the capture-preview handoff and `applyDrawSourceOverlays`), not in the
      publisher.
      **Verify.** Native: a fixture that seals a first overdub, starts a second overdub, and asserts the first
      overdub's note is still present in the draw-source frame while `publishedSnapshotIsCurrent()` is false; then
      stops and asserts it is present from the committed cache. Device: the same two-overdub shape must show
      `visual` non-zero (or the frame unchanged across the second `Overdubbing started`) at the second start, with no
      loss of the first overdub's row count in `DISP`.
