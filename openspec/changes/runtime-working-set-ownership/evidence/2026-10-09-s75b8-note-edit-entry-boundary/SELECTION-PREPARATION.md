# S7.5b.8b — bounded default-selection preparation primitive

## G2 execution packet: scope and dependency closure

User authorized continued S7.5b.8b implementation on `feature/unified-rolling-playback-path`; preserve unrelated dirty changes and previous boundary fixes. This slice closes the **ready-source initial-selection primitive**, not complete open/reopen/content/cleanup or main-loop integration. Existing owners and source policy remain authoritative. No source/reader migration, full-loop private vector, new coordinator, serial marker, instrumentation, upload or device/SD operation.

Owner/API: `EditManager::beginDefaultNoteEditSelectionPreparation`, `stepDefaultNoteEditSelectionPreparation`, qualified completed-result query and explicit metadata cancellation. One owner-local fixed-size progress record, not a queue/lease or second selection authority. Preparation never writes the published EditSession/EditorSelection/FSM/focus, sends feedback or advances playback. The result is a scalar candidate for later existing-owner publication; there is no hidden synchronous completion path.

Primary files: `include/EditManager.h`, new `src/EditManager/NoteEditSelectionPreparation.cpp`, the existing session reset's cancellation hook and `test_note_edit_entry_boundary`. Harness links actual owner methods/Loop/Track/TrackManager; existing feedback/FSM boundaries do not decide these worker assertions.

Preconditions: selected Track reference, allocated selected Loop, empty overlay/session store and inactive focus, no live Capture overlay. These define the fresh-entry **primitive's** input, not a changed product envelope. Unsupported overlay or stopped/dirty visual cache returns a named result with no mutation: the latter would require the legacy getter's rebuild, which this primitive does not perform. Their production preparation remains a later S7.5b.8b dependency. Missing/stale published root retains the request as WaitingForSource; the existing Loop Snapshot producer can publish before a subsequent step. This primitive does not queue or run that producer and does not claim end-to-end wakeup/service. Complete-root current/clean-visual source selection follows `selectNoteEditCommittedBaseKind`: Snapshot when dirty+current, otherwise the existing visual cache read directly (never the getter that can rebuild).

## Unit and lifetime contract

Each step performs **at most one** of: source binding/rebinding; active-pass/chunk metadata inspection or pass transition; visual extent row; snapshot directory/page transition or one reference; a prior-reference dedup inspection; candidate reduction; or final scalar completion. Empty topology transitions count too. Snapshot reads reacquire their page from the current root: at most **255 prefix links** under the existing `kSnapshotMembershipMaxPages == 256` bound, plus one reference/transition. No page pointer survives a yield; over-cap topology returns Unsupported. This fixed nested bound is included in the unit, not hidden by a loop-time check. No sort, projection/filter copy, map insertion, allocation, full-vector clear or arbitrary caller/helper invocation inside a unit. Total work is content-dependent; no device duration or feedback-latency admission is inferred from the structural unit.

The clamp is computed incrementally from the same active committed chunk metadata and visual-cache maximum end as the legacy helper; preserve the bar/grace rounding and nonzero-origin conversion sequence. Selection is first exact display-start match, otherwise minimum circular distance with first-inventory-row ties. Keep separate exact and nearest targets, because legacy re-clamps the nearest target. Missing/invalid NoteId semantics must match legacy index resolution, not be silently filtered out.

Snapshot inventory order: bar, pitch, membership chain/reference. Deduplicate **exactly** by valid `(NoteId,startTick)` against prior raw references, using a second fixed-size cursor and one comparison/transition per unit. This deliberately favors exactness and constant storage over a whole-loop seen set or an unproven first-bar shortcut; worst-case aggregate work is quadratic, not a timing guarantee. Distinct starts with one NoteId remain separate. Visual source retains row order without dedup.

Only scalar candidates and traversal coordinates survive yields. Before reacquiring a page, qualify selected Track/slot/Loop address and LoopId, Track generation, playback revision, current root identity/revision, visual revision/dirty state, Capture content generation, geometry and unchanged session/selection context. Snapshot ownership remains with Loop; local shared root/membership references exist only within one validated unit and are never the final owners. A changed source clears only this primitive's metadata and rebinds/restarts on later units. A changed target/session/selection cancels as Superseded; reset cancels explicitly. Query never advances work and returns no candidate for incomplete or stale identity. Its returned pointer is owner-local observation only: callers must query again after a yield, not retain it or copy a result and later publish without owner qualification.

## Transition oracle fixed before behavior

1. Fresh current-root selected Loop -> begin -> Progress, no session/selection/feedback mutation; withhold all steps -> result unavailable indefinitely.
2. Grant single steps -> progress/unit counter advances at most one; no partial result; completion exposes exact NoteId, tick and logical inventory index. Compare explicit expected data and legacy default-entry behavior on prepared source fixtures.
3. Snapshot rows duplicated across bars/pages, split/wrap segments and equal-distance notes -> preserve exact gather dedup/order/ties and index; no private vector or reference pin.
4. Replace source mid-dedup or after completion -> query rejects stale result; next worker turn discards coordinates before dereference, then prepares new source only.
5. Current root absent/stale -> WaitingForSource, no published mutation; actual Loop source-owner progress/publication -> resume with same accepted transport tick. No claim that this primitive dispatches the producer.
6. Track/slot change, session/source mutation, reset/clear -> stale request cannot publish or revive old content. The primitive never sends MIDI; original 14 entry/ledger/boundary cases remain mandatory.

## Historical review / verification

DEC-043 — execution bound class: bound nested work, not just polls. DEC-055 — overlay delta: no second full-loop representation. Existing EditManager/Loop/Track ownership reused; no formal ownership/schema/undo/timing-model trigger. Scope/task/spec references: [entry boundary design](../../design.md#s75b8-note-edit-midi-boundaries), [previous increments](ENTRY-INCREMENTS.md). Existing RAM2 EditManager object holds fixed metadata; target build must measure actual delta. Do not classify whole open as bounded.

The new worker did not exist before this slice. Frozen fixtures linked the declared API before its implementation and failed at missing symbols (not behavioral RED). No fake passing baseline or no-op worker is credited. Independent stepping/source/dedup mutation controls provide runnable behavioral falsification after linking. Then focused/full native and default capture build/RAM1, one focused independent lifecycle review, strict OpenSpec validation and whitespace checks. No upload. Required commands:

```
pio test -e native -f test_note_edit_entry_boundary
pio test -e native
pio run -e teensy41-capture-serial
openspec validate runtime-working-set-ownership --strict
git diff --check
```

## Completion evidence

**Implemented and native/build verified; not integrated into entry/main-loop dispatch.** Focused suite **30/30**, including all original 14 mandatory cases and 16 new preparation/read-unit cases. Full native **2,922 cases / 2,915 succeeded / 7 skipped / 0 failed**, 112 PASSED suites, exit 0. Default `teensy41-capture-serial` build SUCCESS: RAM1 variables **96,192**, code **390,360**, padding **2,856**, locals **34,880** unchanged; fixed RAM2 variables **163,168** (prior 162,912, **+256 bytes**), static EXTRAM **9,312** unchanged. No budget rebase, runtime high-water or active-transport timing claim. The new worker is not dispatched by open/reopen/main; a target build does not establish deployed worker execution or a duration bound.

**Review:** one focused independent static review approved the worker with no concrete violations. Its two coverage recommendations were added and pass: the actual read kernel traverses the 256th membership page / 4,096th reference and rejects page 257; the public worker preserves same-NoteId snapshot head/tail starts and both indices. The maximum-page case is an I/O-boundary read-unit test, not a full 4,096-row quadratic selection drain or a timing result. No repeat review loop.

**Behavioral mutation controls (each isolated):** acceptance-time hidden owner step -> only withheld-unit assertion RED (`Expected 0 Was 1`, 29 passed); suppress root-pointer identity comparison -> stale complete result and mid-dedup replacement RED (28 passed); ID-only dedup -> split-start inventory RED (`Expected 2 Was 1`, 29 passed). Each exits 1 with an explicitly recorded SIGHUP/SIGINT harness error, not a green suite. All source mutations restored byte-identically: SHA-256 `9ef41e036d033472116835aaf09aafeb974d37f0d758a7a10b0503b5f93d0e7d`, verified with `cmp` before final full native/build.

**Parent gate:** `-DS75B8_ENTRY_BOUNDARY_ORACLE` remains **30 succeeded / 1 original button-entry withheld-owner failure**, exit 1 plus SIGHUP / suite ERRORED (PlatformIO counts the harness error as its 32nd case). This primitive does not make the full entry pipeline ready. Full S7.5b.8b stays unchecked: stale-source content preparation, cleanup, immediate-use caller tails, bounded real FSM entry and atomic session/main-loop integration remain open. S7.5b.8c — feedback pipeline, S7.5b.8d — hardware timing and parent continuity remain separate. No upload, serial/device/SD action, new marker/instrumentation or canonical stimulus change.

## Fixture readiness correction

The new synthetic four-note trace is created by `committedOnTick(length,count,index) = (length / (count + 1)) * (index + 1)`: for length 1,536 its first pair is **On@307/Off@355**, not On@12/Off@60. Initial new assertions wrongly expected a pair within ticks 0–128, then used a rootless normal-reader shape without a served Record-return view. The worker was not changed to fabricate serving readiness or relax musical expectations. The exact musical check is now `test_selection_preparation_pending_work_preserves_real_current_root_midi`: real Snapshot owner and button entry, exact On307/Off355/type/velocity/sink-count agreement and NoteId ledger hold/closure while preparation is private. The rootless waiting test separately drives the real source owner and proves retained acceptance at tick50 (clock advances to500), four identities, first selection307 and no publication. Neither is rootless pre-publication MIDI or main-loop source scheduling qualification. All original 14 baseline/boundary cases remain unchanged.

## Empty cache qualification and source setup

The real legacy current-root reader calls mutable `sessionMidiEvents()` for an empty overlay and can mark its cache dirty without adding values. A dirty flag alone is therefore not non-empty overlay content. The new O(1) `LoopEventVectorCache::hasDirtyEventValues` query does not create/sync/discard the cache. The primitive permits an empty dirty cache but rejects actual pending values, nonempty canonical store/current-state/focus/apply scratch. A dedicated regression appends a pending NoteOn, checks rejected readiness, and proves the worker left the value intact; only fixture teardown discards it. No existing cache mutation/sync or reader policy changed.

Maximum-page source construction is cold fixture I/O via the real `LoopSnapshotConstruction`, batched at 128 units with a topology-derived finite turn limit. The earlier arbitrary 100,000 single-unit fixture limit exhausted before the legal 4,096-reference source completed; no worker quantum or runtime timing limit was raised. Scope remains preparation/read-unit verification.

Evidence logs retained next to this packet: `selection-prelink.log` (missing API only), `selection-focused.log`, `selection-full-native.log`, `selection-capture-build.log`, `selection-boundary.log`, `selection-mutation-stepping.log`, `selection-mutation-source.log`, `selection-mutation-dedup.log`. Strict spec/whitespace/link checks recorded at final closeout.

**Closeout:** strict `openspec validate runtime-working-set-ownership --strict` valid; `git diff --check` clean. Final native log confirms all 16 new case names and all 112 suites PASSED. Runtime scope/status, task/design evidence and deliverable status updated, with no checkbox changes. ExecutionBounds now distinguishes this unit-bound primitive from the previously over-broad row-26 composed-open/focus classification; avoiding full rematerialization alone does not bound those nested scans. No new owner, lease, queue, persistent representation, timing-policy change or logged-decision supersession; DEC-043/DEC-055 are followed, so no new DEC entry. No overlap, ledger, occupy/catch-up or playback-order logic changed. Session closeout leaves all integration and device gates open.
