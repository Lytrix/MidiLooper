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
**6.1** consumer inventory SHIPPED 2026-09-11. Open now: **4b.3**, **6.2–6.5**.

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

## 6. Compatibility retirement and closeout

Compatibility sources are not an implementation target. They remain only long
enough to prove parity and provide rollback while each normal consumer migrates
to immutable snapshot content.

- [x] 6.1 Prove no normal consumer depends on maintained LCR history, `passesMaterializedStore_`, or full replay before removing each path.
  - Plan [`immutable_loop_snapshot_6_1_compatibility_consumer_inventory_refinement.md`](../../../docs/Plans/immutable_loop_snapshot_6_1_compatibility_consumer_inventory_refinement.md).
  - Host gate `scripts/test_compatibility_consumer_inventory.py` (7 tests): hot paths forbid compat execute; fallback allowlists; B1–B8 site inventory stable.
  - HITL unchanged from 5.6 sign-off [`203803`](../../../captures/session_20260911_203803.log).
- [ ] 6.2 Remove superseded maintained-LCR state and replay APIs only after caller migration and rollback gates pass.
- [ ] 6.3 Remove compatibility effective-state derivation and old readers only after documented triggers close.
- [ ] 6.4 Run capture-serial firmware build first with RAM1 locals at least 4000, then `pio test -e native`, followed by required existing HITL verification.
- [ ] 6.5 Update runtime docs, deliverable tracking, the refinement plan, and archived specs; sync and archive only after all gates pass.
