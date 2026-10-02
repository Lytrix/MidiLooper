# Runtime architecture — execution bounds

**Authority:** bound classification for owners that execute on the MIDI interval path. Subordinate to [ARCHITECTURE_RULES.md](../ARCHITECTURE_RULES.md). **Decision:** [DEC-043](../../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property).

**Contract:** invariants **N** (bound classification) and **O** (owner boundary) in [runtime_scheduling_admission_model_architecture.md](../../Plans/runtime_scheduling_admission_model_architecture.md). This file is the **maintained inventory**. Measurements and leverage order stay in the scheduling contract and [runtime_scheduling_owner_boundary_admission_refinement.md](../../Plans/runtime_scheduling_owner_boundary_admission_refinement.md).

This document does **not** authorize interval reservation, extra `handleMidiInput()` call sites, a new scheduler, or `RuntimeWorkBudget::admit(WorkClass)` (rejected).

---

## Bounded Record tail-repair prerequisite

`LoopStopFinalize::TailRepairCursor` is structurally bounded per call: reset and
output enumeration visit at most the supplied Q lanes (and never more than 2,048),
input consumes one event, and begin/acknowledge/discard do constant work. Scratch
is a caller-owned 24,576-byte fixed MIDI-key workspace; the ARM cursor is 48 bytes.
Neither is allocated or added to a resident owner by this slice. Integration must
prewarm an exclusive preparation role and account for it in admission. Output is
retained until acknowledgement so downstream allocation denial cannot lose a row.
Generation mismatch denies further work. Total input traversal is proportional to
take length; no 2,048-event ceiling is introduced.

This prerequisite is not dispatched by production Record normalization yet.
`finalizeWrapWindowOnStore` and the existing sort/minimum-length paths retain their
previous unbounded classification. See [tail-repair execution evidence](../../../openspec/changes/runtime-working-set-ownership/S4F18-TAIL-EXECUTION.md).

## Bounded minimum-length decision prerequisite

`CaptureIncrementalSanity::MinimumLengthCursor` resets at most Q of 2,048 MIDI-key
lanes per call and consumes one ordered source row per call. Receipt read/acknowledge,
finish and discard are constant work. ARM scratch is **16,384 bytes**, cursor
**48 bytes**; both are borrowed preparation resources, with no resident allocation
introduced. Input length is not limited to the number of MIDI keys. A short-pair
receipt blocks new input/completion until acknowledged; stale generations deny it.

This is a decision primitive, not canonical compaction. The production
`removePairsShorterThanNoteMinLength` still copies/sorts/reloads synchronously and
retains its unbounded classification. Future integration must budget/prewarm an
exclusive role and apply receipts to the same ordered source generation. See
[minimum-length execution evidence](../../../openspec/changes/runtime-working-set-ownership/S4F18-MINIMUM-EXECUTION.md).

## Canonical rewrite retirement

`LoopEventStore::stepRewriteFromOrderedSource` retires surplus directory entries
from the tail. Each trim grain visits/clips one retained chunk or releases and
pops one surplus reference; it never shifts the surviving directory. The former
middle erase made a single retirement proportional to the remaining chunk count.
The retained prefix, terminal row inventory and ownership checks are unchanged.
The current finalizer uses canonical copy; this corrects the in-place rewrite API,
whose wrapper is currently exercised by native fixtures, without switching routes.
See [compaction execution evidence](../../../openspec/changes/runtime-working-set-ownership/S4F18-COMPACTION-EXECUTION.md).

## Private prepared-chunk tick ordering

`LoopEventStore::stepPrivateTickOrder` performs at most Q constant-work units per
turn: a state transition, binary comparison, swap, or chunk visit. It sorts stable
raw ticks in place using divide-and-rotate merges, with O(N log² N) worst-case
moves/comparisons and a fixed stack. ARM cursor **864 bytes**, unchanged **56-byte**
store; no whole-take scratch or resident per-slot field. Per-chunk preflight and
access checks require exclusive references and NotScheduled persistence state.
Owner generation, pool lifetime, count and directory-size changes deny the cursor.
The persistence-state accessor is a FLASH-resident, non-allocating read; queue
prewarming is never triggered from a sort grain.
Only completion exposes refreshed chunk metadata and valid ordered output; the
caller must keep partial output private. No production Record dispatch changes.
See [ordering execution evidence](../../../openspec/changes/runtime-working-set-ownership/S4F18-ORDER-EXECUTION.md).

## Private prepared filter compaction

`LoopEventStore::stepPrivateFilter` is structurally bounded to Q fixed-work units:
one chunk preflight, row validation/copy, tail-reference retirement, metadata
refresh or control transition. For N rows and K chunks, preflight uses N+K+2 units;
compaction uses N+K+3. Marking a rejected pair checks both constant-time dense
ordinal accesses before either write. Repeated marks are idempotent. Read, mark,
finish, begin and discard do constant work; no allocation or directory shifting.
ARM cursor **56 bytes**, unchanged **56-byte** store; no resident role field added.

The source ordinal decision belongs to `MinimumLengthCursor`; this API applies
it to exclusive, unpublished output. A temporary out-of-domain tick marker is
allowed only after every original tick passes validation. No partial output is
readable by playback, undo or persistence. Store/pool identity, directory shape,
exclusive refs and NotScheduled state qualify every access. Raw frozen Capture
remains separate. Production Record normalization remains inline and unbounded;
ordering/policy composition and worker admission/publication are still open.
See [filter execution evidence](../../../openspec/changes/runtime-working-set-ownership/S4F18-FILTER-EXECUTION.md).

## Interval path

The **interval path** is work that executes between consecutive `handleMidiInput()` entries (the MIDI Input Gap). Clock dispatch inside `handleMidiInput()` and `ClockManager::updateInternalClock` → `TrackManager::updateAllTracks` also consume that interval.

`main.cpp::loop()` owns service order. Domain owners own the work they step.

---

## Bound classes

Wording is the contract §3.3 classification. Do not invent a fourth class.

| Class | Meaning | May be reserved as a timing guarantee? |
|-------|---------|----------------------------------------|
| **Structurally bounded** | Code enforces a fixed maximum amount of work and storage. | Yes. |
| **Empirically bounded** | Measurements provide an engineering limit, but code does not enforce it. | Only with an explicit overrun guard and failure policy. |
| **Unbounded** (or unknown) | Scans, drains, allocates, sorts, or loops on a content-dependent condition without an enforced cap. | No. |

Structural bounds are guarantees. Measurements alone are tuning evidence.

A local µs budget checked *between* steps does not make a step structurally bounded. An indivisible step larger than the budget remains empirically bounded or unbounded.

---

## Large-work handling

“Off-clock”, “deferred”, and cold/flash placement are not execution-bound classes.
They describe when or where code is placed, not how much work one indivisible
operation can perform.

An operation is still timing-critical when it runs from `handleMidiInput()`, a
button or transport action dispatched by MIDI input, or clock dispatch through
`TrackManager::updateAllTracks`. An existing commit boundary does not make
content-sized candidate construction safe. The caller's bound does not transfer
to a nested operation.

Before a large operation is introduced or extended on a runtime path, its owner
must define:

- the input and storage cap for one work unit;
- the maximum indivisible step within that unit;
- persistent progress and completion state;
- the structural bound, or the measured bound plus an explicit overrun guard
  and failure policy; and
- the behavior when the unit does not fit or overruns, without losing pending
  work or publishing partial state.

If a complete result is required, construction may be incremental, but the
active result remains selected until construction completes. Publication is a
single atomic operation over the complete result. A local timer checked only
between steps is not a substitute for the indivisible-step bound.

For any new runtime owner or extended owner, add or update its inventory row in
the same change that introduces the runtime work. Until that row has an
admissible class and policy, the work is not admitted to the interval path.

---

## Rules

1. An owner that executes on the interval path **must** have a declared row in the inventory below.
2. Adding or extending an **unbounded** owner on the interval path is a design session — same weight as an ownership or state-transition change ([ARCHITECTURE_RULES.md](../ARCHITECTURE_RULES.md#execution-bound-rules) and architecture-checkpoint question 3).
3. Reclassifying a row (unbounded → empirically or structurally bounded) updates this table in the same commit as the firmware that enforces the new class.
4. Interval reservation, if introduced later, coordinates existing owners and never becomes a domain owner (invariant O).

---

## Owner inventory

Seeded from Stage R1C (2026-08-21). Update **Class** and **Bound** here; keep measured maxima in the research doc.

| # | Owner | Where | Bound | Class |
|---|-------|-------|-------|-------|
| 1 | `TrackManager::updateAllTracks` from clock dispatch | `ClockManager::onMidiClockPulse` | none | **Unbounded** |
| 2 | `Track::ensurePlaybackMergedMidiEventsBuilt` | `TrackPlaybackWindowBuild.cpp` | selected playback window (2 bars on long loops; full loop on short) | **Unbounded** |
| 3 | `DisplayManager::update` — resolve phase | `resolveDisplayNotes` / `resolveDisplayNotesByDrawSource` | 5000 µs measurement + overrun reuse of prior frame | **Empirically bounded** |
| 4 | `DisplayManager::update` — draw + transfer | `_display.api.display()` | fixed frame size | **Structurally bounded** (size), **blocking** |
| 5 | `Loop::advanceFallbackGatherSession` traversal | `LoopVisualCache.cpp` | 96 / 192 / 768 events | **Structurally bounded, but NOT on the live path** (2026-09-18): the function has no call site and `rebuildVisualCacheIdleSlice` never reads its budget argument, so the idle lane's real fallback is the unbounded whole-loop `resolveDisplayNotes(passes, ...)`. Treat this row as a bound on retired code until RC-MEM-4 revives or deletes it. |
| 6 | Fallback gather sort | `sortSessionEventsByTick` | none | **Unbounded** |
| 7 | Fallback gather edit projection | `applyNoteEditPassSequence` | none | **Unbounded** |
| 8 | `NoteUtils::reconstructDisplayNotes` in slice | `rebuildVisualCacheIdleSlice` | none | **Unbounded** — residual **4b.1b** is Stage 2 of [#41](https://github.com/Lytrix/MidiLooper/issues/41) / [`display_visual_cache_snapshot_reuse_refinement.md`](../../Plans/display_visual_cache_snapshot_reuse_refinement.md); inventory note remains in [`playback_window_build_owner_boundary_refinement.md`](../../Plans/playback_window_build_owner_boundary_refinement.md) § *4b.1b*; Ring B/RC2 closed without bounding this row |
| 9 | `Loop::appendOverdubPassWrapPairedNotes` | `LoopVisualCache.cpp` | none | **Unbounded** |
| 10 | `removeDisplayNotesOverlappingBars` | `rebuildVisualCacheIdleSlice` | none | **Unbounded** |
| 11 | `Loop::materializeEditViewFromPasses` | STOPPED idle | none | **Unbounded** |
| 12 | `Loop::prewarmOverdubSourceSpanCache` | STOPPED idle | gated, not bounded | **Unbounded** |
| 13 | `validateAndCleanupMidiEvents` | deferred, 60 s policy | none | **Unbounded** |
| 14 | `StorageManager::processDeferredSaveState` | `DeferredSaveScheduler` | 300 µs / 3000 µs between steps | **Empirically bounded** |
| 15 | `DeferredJobScheduler::runFrame` | `LoadLoopJob` | 250 / 1200 / 5000 µs between steps | **Empirically bounded** |
| 16 | `enqueueRemainingLoopSlotRestores` | boot fill | 4 entries per turn | **Structurally bounded** |
| 17 | `reclaimUnreferencedDisabledPasses` | `loop()` at Critical pressure | none | **Unbounded**, not resumable |
| 18 | `processHitlSerialCommands` | capture builds only | none | **Unbounded** |
| 19 | `processDeferredIdleMaintenance` × 8 | `main.cpp` | per-track only | **Unbounded** in aggregate |
| 20 | `MidiButtonProcessor::processPendingPresses` | inside `handleMidiInput` | none | **Unbounded** |
| 21 | `LoopSnapshotConstruction::step` | Record session-exit drain, NOTE_EDIT close, and Phase 3 wrap-journal idle construction | One admitted step applies at most one persistent-cursor bar and copies at most 8 membership pages of 16 refs. Per-bar membership is bounded by the explicit `kSnapshotMembershipMaxRefsPerBar` design envelope, applied equally to event-bar and note-bar membership; pages stay lazily allocated. It does not rebuild a snapshot, enumerate a pass, or publish. An incomplete step leaves the candidate unpublished and resumable. A failed incorporation discards the partial candidate and leaves authoritative pass-change progress unchanged, so the exact fragment retries. | **Structurally bounded** (page count per unit); bar capacity is the `kSnapshotMembershipMaxRefsPerBar` envelope |
| 22 | `Loop::queueOverdubWrapSnapshotJournal` / `Loop::queueSessionExitSnapshot` / `Loop::stepPendingSnapshotPublish` / `Loop::tryPublishSessionExitSnapshot` / `Loop::tryPublishNoteEditSnapshot` | Wrap and session-exit queue at their commit/stop owners; idle publishes from `stepPendingSnapshotPublish` | Queue only journal-capped fragments with exact parent identity. One idle turn admits two units (`kSnapshotIdleConstructionUnitsPerTurn` = 2). Each unit is one fragment start or one row-21 step. Pass-change progress becomes authoritative only after successful fragment incorporation; a failed fragment retains its exact checkpoint and discards any partially constructed candidate before retry. `storePublishedSnapshot` runs only after every queued wrap fragment, session-exit pass, and companion change is complete. Failure keeps the previous published root, does not publish a prefix, and emits a diagnostic for a persistent representability failure instead of retrying silently. | **Structurally bounded** |
| 23 | `EditManager::processKindBoundaryUndoWarm` | Remainder after a completed display admission (`finishAdmittedDisplayPaintTurn` / prefix NOTE_EDIT force-paint). Not `ControlSurfaceManager::update()`. Playing geometry may still call it from `processPendingPlayingEditGeometry`. | One coalesced warm. Starts only after `kDeferredNoteEditPlaybackRefreshIdleMs` (80 ms) with no newer select change. Seed bound **133200 µs** from [`201106`](../../../captures/session_20260908_201106.log) (137 rows, 91.7–133.2 ms). Builds into a local `SessionUndoEntry`. Overrun or incomplete: remain pending, re-arm settle, do not publish `kindBoundaryUndoCache_`. Relocating the warm after display is not itself a bound. | **Empirically bounded** |
| 24 | `SelectNavigation::buildSelectNavigationSlots` | Select encoder (`stepSelectNavSlot`) and select fader (`resolveFader1SelectTarget`) on the MIDI interval path | At most `2 * kSelectNavigationNeighborhoodRadiusSteps + 1` 16ths (3). One pass over the already-built selectable list; does not rematerialize or walk every loop 16th. Overrun: emit the neighborhood only; do not expand to a full-loop onset list. | **Structurally bounded** (steps); note filter is one pass of the pre-built selectable inventory |
| 25 | `EditManager::tryCollectPreparedOverlapHoldNoteIds` / `collectAnalyzeOverlapNoteIds` / `unionAnalyzeOverlapNoteIds` | `NoteGeometryResolver::resolve` on geometry fader (MIDI interval) | Hit: `noteMembership(pitch, bar)` for hold bars only (±1 for analyze abut). Capture-active hit unions one pass of the already-built Stage 1 composed DisplayNotes. Miss: one pass of that same composed list (not visual cache). Result capped at `kOverlapNoteIdSetCapacity` (128). Overflow keeps the inserted ids and does not expand to a full-lane `baselineMap` fill. Scoped current-state upsert is at most those 128 ids. | **Structurally bounded** (hit bars + set cap); miss/union is one pass of the pre-built composed cache |
| 26 | `EditManager::openNoteEditSession` current-root; `rebuildNoteEditFocusForDisplayNote` mover seed | NOTE_EDIT button; first select / live-edit focus rebuild | Open does not call `rebuildVisualCacheFromPasses`, `rematerializeEditView`, or `buildFromSessionStore`. Current state starts empty. First focus rebuild `upsertRow`s the selected mover only. First-frame paint uses the published snapshot / already-built visual cache. Miss path keeps the existing rematerialize owner. | **Structurally bounded** (no full-loop build on current-root open; one-row seed on focus) |
| 27 | `Track::stepPlaybackViewMaintenance` (landed 2026-10-02, slice 3.1; successor lane, slice 3.4) | Existing post-`handleMidiInput()` owner turn; never clock dispatch | One declared grain per admitted turn: a fixed phase/index fill or merge unit with a fixed event/pair quantum, a persistent build cursor, fixed storage, and atomic publication only after the build completes. A replacement publishes as the **`ready` successor** (one real successor at a time; only a complete view is ever installed) and the window being served is untouched until an adoption swaps it. Until the code **measures** maximum phase advance and denied-turn coverage, active windowed playback is denied. | **Unbounded** — implemented; **not admitted** (caps declared, bounds unmeasured) |
| 28 | `Track::stepOverdubStopAdmission` (planned) | Existing post-`handleMidiInput()` owner turn while `OVERDUB_STOPPING` | Planned fixed stop-finalization unit backed by per-accepted-note closure credits. It must retain playback, preserve the frozen candidate, and expose a grain counter/latency; a contract breach takes deterministic STOPPED recovery. | **Unbounded** — not implemented or admitted |
| 29 | `main.cpp::runDeferredLoadAndDisplayFrame` selected child (planned refinement) | Existing load/display owner after MIDI service | Exactly one display phase, qualified load-job phase, or prewarm unit per active-transport turn. A load Commit is active-eligible only when its complete body has a content-independent bound; otherwise it remains STOPPED-only. | **Unbounded** — compound owner until one-child continuation is implemented |
| 31 | `Track::serviceAcceptedInteractionTurn` -> `driveAcceptedInteraction` -> `parkAcceptedStopForCanonicalOutput` (runtime-working-set-ownership S4f.2/S2b.6) | Every deferred idle-owner lane, immediately after the admitted stop's grain and before the owner-specific branches | At most one accepted-interaction grain per turn: one admission attempt for the interval's slot plus, on refusal, one canonical-reservation replenish and one wait report. The drive is skipped while the ring is non-empty, so an accepted request never stacks a second stop job. The park never freezes, never exchanges staging roles, and never cancels the request. | **Structurally bounded** (one admission attempt, one replenish, one report per turn); device latency for the acceptance-to-execution wait remains open |
| 30 | `Track::processDeferredCanonicalPrefix` / `MidiServiceDrain::serviceCanonicalPrefixTurn` | Existing VisualCache idle owner after MIDI service | At most two fixed track-count scans and one setup, stale cleanup, or Q=32 normalization child across all tracks. Only preallocated overdub runtimes participate. Stop/recording and active visual work have priority. Pending work retains cursors; source/capacity denial hides results and latches until a key change. The owner returns before other track maintenance. | **Structurally bounded**; device cost and stop-service/latency proof remain open |

Thirteen rows are unbounded. Bounding those owners is Owner-Boundary Gate work (roadmap L1–L7). It is not a reason to introduce a scheduling class or a new Manager.

### Resumable playback and deferred-stop admission gates (rows 27–29)

Rows 27–29 are declarations of **required implementation contracts**, not proof
of an active-transport bound. They remain unbounded and excluded from admission
until the firmware and native fixtures establish the following fields.

| Contract | Owner and interval entry | Declared quantum and continuation | Required proof / denial policy |
|---|---|---|---|
| `CoverageServiceContract` | `Track::stepPlaybackViewMaintenance` after the existing MIDI service boundary | Fixed phase/index fill or merge pairs; persistent view-build cursor and current/ready/pending identity | Maximum phase advance, maximum consecutive denied turns, required grain count, and lead margin prove the next view is ready by its handoff. If not provable, reject active windowed selection or the source mutation; use full-loop prewarm or STOPPED-only work. |
| `StopAdmissionContract` | `Track::stepOverdubStopAdmission` after the existing MIDI service boundary while `OVERDUB_STOPPING` | One reserved closure, canonical raw-order, canonical rewrite, or audible-order unit; frozen generation, remaining-grain count, command epoch, and requested destination | Per accepted NoteOn reserves its closing work. Admission counts both fixed-storage orders and the canonical rewrite before it permits the atomic transfer. The contract bounds finalization grains and latency; a breach emits all-notes-off and enters STOPPED recovery without discarding capture. Transport Stop supersedes PLAYING completion. |
| `LoadFrameChildContract` | `main.cpp::runDeferredLoadAndDisplayFrame` after the existing MIDI service boundary | One selected display, qualified load-job, or prewarm child; saved child continuation | No same-turn child stacking. An unqualified Commit/preparation/cleanup phase is denied during RECORDING, PLAYING, OVERDUBBING, and `OVERDUB_STOPPING` and remains pending until STOPPED. |

Capture evidence for these rows must identify owner, grain kind, quantum,
duration, complete/yield/denied outcome, source/coverage state, stop state and
latency, load phase, and `RING,overflow`. Scored RECORDING, PLAYING,
OVERDUBBING, and `OVERDUB_STOPPING` windows reject any overflow. Observed
service cadence is diagnostic only; it is never a policy ceiling or substitute
for the contracts above.

The frozen-capture order owner now resumes its existing generation-bound sorter
on each begin/step turn. Filtered rows consume the order quantum, and final
zero-entry state transitions finish in the last reserved turn. Native fixtures
check the declared order-grain bound up to 2,048 rows and the production call
sequence across canonical order/rewrite/audible order with transport Stop.
`RuntimeCaptureBridgeFill::step(entryQuantum)` copies at most that many frozen
capture/companion input rows (including filtered rows); capacity is checked
before fill. The runtime now retains fill/compile continuation and a source
admission bound to frozen generation, target identity, and command epoch;
read-only selection preflight validates coverage and the live ledger. These
helpers do not yet supply the complete stop admission bound.

`RuntimeNotePairLookup::step` now provides the bounded NoteOff lookup for the
DEC-059 revised reference: contiguous equal-tick runs in the filtered
channel/pitch lane, stable Offs before Ons, unchanged LIFO and wrap fallback.
It charges at most the caller's input quantum across filtering, run discovery,
Off/On walks, and wrap scanning; at most `6 * sourceCount` charged operations
complete one lookup. Capacity is 2,048 source rows with 10,240 prewarmed bytes
per runtime. The caller holds source/retention immutable; incomplete lookup
publishes nothing. This component is not yet the complete Delete/Length
companion resolver or a production stop-owner grain reservation.

`RuntimeNoteDeleteLookup::step` selects original-source deletion indexes without
mutating the retained source. Its target scan, LIFO lookup, and wrap-head
lookup run on separate owner turns with the declared input quantum Q. For N
source rows (maximum 2,048), completion takes no more than
`ceil(N/Q) + ceil(6*N/Q) + ceil(2*N/Q)` turns. Wrap-head selection replaces the
legacy nested preferred-tail scan with a maximum-tail pass and a head pass,
using the unchanged head/tail predicate. No additional backing allocation or
publication is performed. Retention/source lifetime and exclusive use of the
shared pair lookup belong to the caller. Full companion/stop reservation and
production bridge integration remain open.

`RuntimeCompanionOverlapWalk::step(Q)` inspects at most Q immutable
reconstructed-note rows, including ignored rows. It scans shortenings and then
deletions in reference order; a produced action is held until consumed. With N
rows (cap 4,096), it inspects at most 2N rows, with at most
`ceil(2N/Q) + actionCount + 1` calls excluding action-application/hold turns.
No allocation, note mutation, reconstruction, or publication occurs in the
walk. The caller owns the immutable snapshot and separate candidate. This
bounds action traversal only, not full Length resolution or stop admission.

`RuntimeNotePairLookup::beginReconstructionWrap` selects or verifies a
reconstruction wrap tail and computes the occupied head end in at most two
source passes. Each `step(Q)` inspects at most Q rows, including filtered rows;
N <= 2,048 yields at most 2N visits and `ceil(2N/Q)` non-denied calls. Latest-tail
proof removes the nested intervening-On scan from the linear-closer test.
Reconstruction-specific mid-loop and linear-closer rules remain distinct from
Delete wrap lookup. No backing allocation, mutation, or publication occurs.
This bounds one query, not a full reconstruction or aggregate stop latency.

`RuntimeCompanionSpanWalk::step(Q)` charges all pitch-stack flag scans,
equal-tick consumption, wrap marking, loop-end checks, and nested wrap-query
visits against Q, holding each emitted span until consumed. N <= 2,048 rows use
N prewarmed flag bytes and at most `6N² + 4N` visits. Ordinary reconstruction
semantics are retained; this is not overdub-pass-specific reconstruction mode.

`RuntimeCompanionReconstruction::step(Q)` also charges projection, dedupe
comparisons, and output append. It owns up to 4,096 prewarmed DisplayNotes and
exposes output only after complete source-identity-validated construction.
Stale source/capacity failure latches denial without exposing a prefix. A
conservative aggregate bound is `10N² + 7N` operations and
`ceil((10N² + 7N)/Q) + 1` calls, with no active allocation. This finite component
bound does not establish aggregate stop latency or extend stop credits. The
production stop owner must prove those before selecting this continuation.

`RuntimeCompanionApply::beginHistoryBatch` adds unpublished canonical-history
support for Create and Pitch/Velocity/NoteRange updates alongside Delete/Length.
The caller supplies only active Note rows. Consecutive same-target Pitch or
Velocity updates collapse to their last row through Q-bounded comparisons.
The identity guard charges prior rows and Create payload events to Q: Delete
removes an identity and a later created nonzero NoteOn reactivates it, preserving
sequence membership separately from surviving duplicate events.
Pitch reuses bounded LIFO/wrap lookup and writes at most Q of three resolved
indexes. Velocity changes the first matching On after bounded lookup. NoteRange
uses one fixed-size note mutation (On, selected Off or synthesized Off, optional
old LIFO-Off retention bit); any compaction runs as a separate Q-bounded grain.
Update stored order is preserved. Create appends at most Q events per grain,
then uses bounded stable raw-tick ordering/permutation, including when empty
(DEC-061). No new prewarmed arrays or active allocations are introduced.
`requiredHistoryGrainsUpperBound(C,R,Q)` returns a uint64 batch bound:
`(R+1)*(G(C,Q)+ceil(R*(C+1)/Q)+ceil(R/Q)+8)`. C/R are capped at 2048; every
successful prior Create payload is at most C and contributes payload visits
plus one row transition. Source/capacity failures expose no prefix. Created
events use UINT32_MAX provenance. The tick-only companion mapper refuses history results;
they must first become effective baseline inputs. This is not yet full history
normalization, producer routing, or an aggregate stop-admission proof.

`LoopEventStore::stepReadSealedChunks(cursor, Q)` gathers one raw sealed pass
into caller-prewarmed unpublished storage. Each visit copies one event or
advances one chunk reference, so N events across K references require
`max(1, ceil((N+K)/Q))` calls for positive Q. Begin/discard are constant-time;
neither allocates, retains, nor releases chunks. The owning preparation must
retain immutable refs and a generation token, invalidating the token before
release/replacement (including pool reuse). Every step and completed-result
access checks that token. Invalid/unsealed refs or capacity failure deny the
whole result; zero quantum defers nonempty work. The caller owns buffer
disjointness and forbids publication before readiness. This preserves stored
order without sorting or applying historical edits. Canonical pass selection,
historical normalization, capture binding, and aggregate stop admission remain
producer obligations; this component is not yet routed into production.

`LoopPlaybackRuntime::canonicalPassInputs` now owns the bounded connection
from sealed chunk reading to historical application and separate effective-pass
storage. Its `RuntimeCanonicalPassInputs::step(Q)` executes one read, history
apply, or copy phase per call, or one constant-time setup/transition. Begin and
discard do not traverse refs or acquire/release chunks. The caller still selects
and freezes active passes in canonical order and filters active Note rows;
source identity must be invalidated before ref release/replacement/pool reuse.
Every result is hidden until all passes complete. Source changes, invalid refs,
aggregate capacity overflow or borrowed-worker reuse deny the candidate, and
only the generation actually borrowed may be cancelled.

Prewarm adds eight external-first arrays, not the three originally listed here:
`2C*sizeof(MidiEvent)` (one C-event raw scratch array and one C-event effective
array) + `2C*sizeof(RuntimeCompanionPasses::Pass)` (current and next output
descriptors) + `C*sizeof(SealedPass)` (selection) + `kRecordsPerBridge*sizeof(const
EditPass*)` (history pointers) + `2C*sizeof(uint16_t)` (capture order and capture
scratch), i.e. **147,456 bytes at C=2048 on device** (`sizeof(MidiEvent)=20`,
`sizeof(Pass)=sizeof(SealedPass)=8`), matching the measured
`tag=16 canonicalPassInputs 144 KB` record-arm attribution. Allocation is capped by
`capacity > kEventsPerView`; every array is allocated and fully constructed in
`prewarm`, and each array alone is enough to fail the whole reserve. No allocation
occurs in begin/step/discard. Pass count P and
aggregate effective count are capped at C<=2048, raw pass size at C, each ref
list at K<=512, history rows at R<=2048. A uint64 bound is
`1+P*(H(C,R,Q)+ceil((C+512)/Q)+ceil(C/Q)+3)`, where H is the history batch bound
above. `RuntimeCompanionPasses` has a typed input overload that validates the
normalizer's generation/readiness throughout apply/merge and origin lookup;
normalizer reuse cannot silently mutate a pending or completed consumer.
The direct `begin(..., const LoopPasses&, ...)` overload selects an active,
nonempty valid record pass first, then active nonempty overdubs by ascending
unique mergeSequence, using Q-bounded repeated minimum scans. It filters active
Note history through a Q-bounded pointer scan, without copying Create payloads.
Disabled/non-Note rows disappear before contiguous-collapse planning; active
no-op Note updates remain collapse barriers, matching the Session materializer.
Raw overdub count must be <=C and raw history count <=2048. Duplicate active
mergeSequence values deny the candidate without changing canonical content;
their legacy sort-tie parity remains an open gate, not an invented tie rule.

`beginFrozen` adds a fixed `kMaxCaptureNoteReservations` scan to reject
outstanding closures, then binds live revision/generation/command-epoch/length
and frozen capture generation/count. Every step and output-readiness query
checks a fixed number of scalars, including downstream typed consumer queries.
It adds no event visits, allocation, or preparation grains. This receipt does
not retain chunks or freeze canonical vectors: live owners must remain valid
until discard and invalidate before mutation/release. Production lifetime
integration and aggregate stop admission are still open.

The typed prepared-row handoff additionally checks `RuntimeCaptureCompanions`
readiness against the same capture owner and records its preparation generation.
Discard/reuse invalidates pending and completed per-pass output, even when the
capture generation is unchanged. Each check adds fixed pointer/scalar/vector
identity comparisons; no row traversal, copy, allocation, or extra grain is
introduced. Row and input owners must remain alive until consumer discard.

`beginFrozenOverdub` adds retained frozen capture as the final/newest overdub
pass. Two private C-entry uint16 index arrays add `4*C` prewarmed bytes; shared
staging audible order/phases are neither read nor written. One turn sets up
canonical ordering; later turns order at most Q entries, copy Q raw events,
run a history grain, or copy Q effective events. Add
`orderGrains(C,Q)+2*ceil(C/Q)+H(C,R,Q)+4` to the selected-input bound. The final
pass count and aggregate effective event count must still fit C; wholly filtered
capture adds no pass. Closure/ID/retention decisions must already be frozen.
This is not yet charged to the production stop contract or actively routed.

Normalized-input companion application now carries the immutable history prefix
into its membership scan. Each prior row and each Create payload event consumes
Q; events are not mutated by history again. For C events, H historical rows and
R companions, use `A=(R+1)*(G(C,Q)+ceil((H*(C+1)+R)/Q)+3)` (uint64), with C/H/R
at most 2048, for each continuation application. The typed per-pass bound is
`1+P*(A+ceil(C/Q)+4)`. Ordinary raw-input batches retain their earlier bound.
No new buffers or allocation are introduced. Producer retention and aggregate
stop reservation/timing proof remain required.

The ranked runtime admission adapter admits one Map or Compile child through
`stepFrozenCaptureBridge(Q)`; compiler entries start on the turn after mapping
completes. Begin/setup, source/owner/generation checks, and final admission checks
are fixed metadata work (plus the existing fixed ledger snapshot/match). It adds
no allocation or event traversal outside the mapper/compiler grains. Preparation
and mapper generations remain bound until selection, including completed-state
queries. This adapter is not yet charged to the production deferred-stop contract;
aggregate preparation/map/compile/commit latency still requires proof before routing.

Executable indexed mapping bound: `ceil((4B+2B*L+A*(S+2))/Q)+4` for B base
and A candidate events, L=ceil(log2(B)), S=floor(log2(B))+1 for B>0 (both zero
for B=0). One extra C-entry uint16 array (4096 bytes at C=2048) is prewarmed;
unpublished record ranks serve as sort scratch. Index merge/copy, adjacent
duplicate checks, binary-search comparisons and final lookup each consume Q.
No source array is sorted or changed. Maximum B=A=2048,Q=32 mapping is 2564
grains. The earlier quadratic mapping term is superseded. Executable ranked compiler bound for K records:
`ceil((3B+3K)/Q)+orderGrains(B,Q)+orderGrains(K,Q)+6`. Fixed ledger loops are
included in per-entry timing; they are not content-sized extra turns.
`StopAdmissionContract::requiredRankedPreparationGrains(C,H,R,Q)` composes these
with canonical inputs and per-pass continuation: `I+ceil(R/Q)+1+C*(A+ceil(C/Q)+4)
+M(C,C,Q)+B(C,C,Q)+4`. It returns uint64 or MAX for invalid horizons. The full
C/H/R=2048, Q=32 conservative aggregate remains above uint32 after substituting
the indexed mapping term. This does
not predict latency or authorize active routing. Existing uint16 stop credits
remain unchanged. Closures, canonical rewrite/transfer, cleanup, service denials
and acceptable latency still require a complete producer admission proof.

`appendFrozenOverdub` reuses a completed, current sealed prefix without any
selection/read/history replay for that prefix. Admission performs fixed source
checks and a fixed reservation-array scan; it bumps generation before tail
writes and denies if the apply worker is borrowed. Capture-only normalization
uses `orderGrains(C,Q)+2*ceil(C/Q)+Happly(C,H,Q)+4`; the ranked-tail bound
substitutes that term for full normalization in the aggregate. No prefix copy
or allocation occurs. Pre-stop prefix work now has the row-30 owner; admitted-tail
capacity/service proof and atomic transfer remain prerequisites for audible routing.

When canonical capture order is already complete and bound to the same frozen
capture, tail admission can borrow a prepared-order receipt. The normalizer
copies at most Q indexes per grain into its existing private array, checking
retention, index range and strict raw-tick/source-index order. A live uint64
shared-order generation denies reuse or discard during this copy, including
rebuild at the same capture generation. Both canonical and audible builder
bindings also retain the storage generation at begin. Every resume, step,
canonical rewrite and audible candidate-readiness check compares it, preventing
an old completed builder from becoming valid after the other order is discarded.
Tail admission falls back to private sorting for a stale canonical binding.
These are constant-time checks; all grain bounds are unchanged.
The receipt is released after copying;
audible order reuse then cannot invalidate the private indexes. An audible or
unprepared shared order uses the existing private sorter. The producer must
supply the complete retained canonical order from the frozen source.
`requiredPreparedFrozenTailGrainsUpperBound(C,H,Q)` is
`3*ceil(C/Q)+(H ? Happly(C,H,Q) : 0)+4`: 196 at C=2048,H=0,Q=32, versus
1604 for the general tail. This excludes the initial canonical sort, which
must still be charged upstream. Existing ranked-tail aggregate helpers and
production stop credits are unchanged. No extra array or allocation is added.

`LoopPlaybackRuntime::stepCanonicalPrefix` supplies one setup, stale-owner
cleanup, or `canonicalPassInputs.step(Q)` per call. It owns the source token and
binds live revision/generation/length counters through `beginCurrent`; completed
outputs are hidden immediately when these change. Ready/Denied source keys
request no further service until changed. Worker reuse and capture-tail handoff
are protected by normalizer generation and capture attachment, including an
incomplete tail. No new content loop or external array is added. Row 30 now
schedules this owner on VisualCache turns with Q=32. A first fixed track scan
gives STOPPING, recording, and active visual debt priority; a second scan picks
one eligible prewarmed overdub runtime from a rotating cursor. A consumed turn
returns before all other track maintenance, including cleanup. Missing runtimes
are not allocated; ready/denied prefixes consume no turn. No guarantee on denied
turn count is inferred from this policy.
Live counters detect invalidation; they do not retain chunks/history. A required
uint64 storage-generation reference now guards `beginCurrent` independently of
audible revision. `Loop::resetPassTimeline` advances it before releasing content
and resetting revision; disabled-edit reclamation advances it on each removed
row before `remove_if` moves retained history. `saveNoteEditPass` advances it
before append, including when the caller defers audible revision invalidation.
No-op reclamation leaves it intact.
These add constant work to reset and each existing reclamation predicate, with
no new scan or allocation. Prefix restart keys include storage owner/value and
the guard survives frozen-tail attachment. Other source mutation/owner lifetime
paths at frozen/atomic handoff remain integration gates; this is not a stop-service proof.

`RuntimePlaybackOrderBuilder` checks each retained row against the preceding
retained row during its existing Q-bounded fill. It uses the same comparator as
merge (canonical raw tick or audible phase/kind/raw tick/rank/index). An inversion
latches the existing merge path. If no inversion exists, fill completes the
builder and publishes its completion flag without touching merge scratch.
Filtered inputs still consume Q; each retained fill adds at most one comparison.
No additional scan, allocation or source mutation is introduced. Already ordered
N-row inputs take `ceil(N/Q)` grains, including filtered rows; N=2048,Q=32 takes
64, versus the conservative full-sort allowance of 1472. General bounds remain
unchanged: ordering is established during work, not assumed at admission, and
worst-case per-fill entry timing must still be verified on device.

`StopAdmissionContract::requiredRankedTailGrainsForPasses(C,P,B,H,R,Q)` uses an
explicit upper bound P on effective passes (including capture) and published-base
event count B, instead of substituting C for both. C must bound all intermediate
candidate states, not merely the final output. It composes tail normalization,
at least one row-preparation turn, P-bounded companion work, B/C mapping and
B/C compiler bounds plus eight setup/transition turns. Invalid horizons/zero Q
return UINT64_MAX. The older full-horizon helpers use this composition with
P=B=C; eight replaces the earlier four-turn aggregate setup allowance.
For C=2048,P=2,B=2048,H=R=0,Q=32 this is 7648 additional ranked-tail grains
(146776 with P=2048). Both exclude closures, canonical rewrite/transfer, cleanup
and service denials. Neither authorizes the existing 3072-order-grain permit.
`requiredPreparedRankedTailGrainsForPasses` substitutes the prepared-order tail
normalization term into that aggregate. It retains every other child and all
eight setup/transition turns, returning UINT64_MAX before arithmetic on invalid
horizons. A completed, current canonical order must be established separately;
its initial sort remains upstream work. C=2048,P=2,B=2048,H=R=0,Q=32 gives
6240 additional grains, saving 1408 but still exceeding 3072. The estimate does
not grant admission, change production credits or establish service latency.
Native coordinator-through-bridge-preflight coverage checks both ordinary and
prepared-order paths, including command-epoch invalidation at final preflight.

DEC-062 approves `kProvisionalRankedPreparationGrains = 6240` separately from
legacy order/rewrite credits for continued integration and HITL preparation.
The 3072 comparison is not a product latency rejection. Realtime reservations
and elapsed-stop qualification will be determined with HITL once runnable;
missing measurements do not block implementation of the provisional path.
This does not reclassify the bound as measured/verified realtime service, waive
source/capacity/epoch checks, or cover excluded work and larger horizons.

`PendingStopAdmission::begin` can reserve an explicit uint64 ranked-preparation
bound alongside legacy order and closure work. Before any state write, it rejects
ranked bounds above 6240 (including MAX) and declared combined totals above
UINT16_MAX. It narrows only after validation. A separate uint16 counter consumes
one ranked credit per call after closures finish; order credits are untouched.
Finalization requires closure, order and ranked counters all empty. Transport
Stop changes epoch/destination while preserving outstanding work. Reset clears
all counters. These are fixed-work operations with no content scan/allocation.
Exhaustion proves budget accounting only; typed source/epoch/bridge readiness
must still be validated at atomic selection. Legacy production callers use the
default zero ranked reservation until the new preparation path is routed.

`stepReservedFrozenCanonicalTail` and `stepReservedFrozenCaptureBridge` check
pending stop, closed reservations, frozen generation, command epoch and the
quantum captured at reservation before advancing a child. They charge exactly
one ranked credit before each admitted tail/map/compiler step, including its
existing transition work. An exhausted reservation cannot advance a child.
Completed readiness queries consume no credit. Source/owner invalidation still
denies; deeper child failures may consume the admitted grain. Fixed checks
add no array, allocation or content scan. Begin/setup remains separate charged
owner work; native end-to-end coverage charges both setups explicitly. Actual
production stop-stage dispatch and atomic source handoff remain unconnected.

The typed `publishStoppedOracleView` overload accepts a completed unedited
effective-pass baseline and full-loop coverage. It completes provenance/event
fill and audible ordering synchronously in Q=32 steps, then publishes. This is
STOPPED-only throughput, not one bounded active grain. No allocation or extra
array is introduced; the current view owns the copied origin table. A flat
same-identity view is upgraded rather than treated as a provenance cache hit.
Invalid source/coverage preflight leaves the existing view untouched. Track-level
construction now runs in `Track::ensurePlaybackMergedEventsForSlot` only under
its existing all-tracks-stopped guard. `publishStoppedCanonicalOracleView` uses
idle prewarmed normalizer/apply/pass workers, completes selected history and
canonical merge in Q=32 steps, compares the resulting sequence against the
existing oracle with field-aware MIDI equality, and publishes only on a full
match. It releases temporary worker ownership on completion/denial. Busy workers,
window coverage, capacity/source failure or mismatch leave flat publication as
the compatibility path. This synchronous history/merge/compare work is excluded
from active transport; it does not establish active grain timing or stop admission.

`canSelectReservedFrozenCaptureBridge` supplies the read-only audible half of
atomic stop preflight: AtomicTransfer phase, ranked reservation, all counters
empty, matching command epoch/PLAYING destination, base/target source identities,
and the existing typed bridge/ledger/cursor readiness. Canonical transfer capacity
and publication of companion edit rows are separate required checks. The shared
bridge preflight now rejects zero loop length and phase >= loop length before
calling modulo coverage membership; an unnormalized cursor cannot authorize
selection. No mutation, allocation or content traversal is added beyond the
existing fixed ledger/source checks.

`Loop::prewarmPreparedCompanionHistory` owns a separate edit-history candidate
on the existing canonical owner. Allocation/old-buffer release is STOPPED-only.
Prewarm bounds history rows plus companion capacity by 2048 and copied Create
payload events by 2048, reserving each existing payload's storage. Active begin
checks storage generation, row count, prepared companion owner and prospective
ID overflow; it writes no canonical row or ID. Each step visits at most Q units:
one row's scalar metadata (including its two ID appends), one Create payload
event, one existing snapshot ID, or one unused empty tail row.
`requiredPreparedCompanionHistoryGrains(H,E,R,Q,S=0)` is
`ceil((H+E+R+S+2)/Q)`; S+R must fit 2048; invalid horizons/Q return MAX. Payload pushes use proven
capacity. Source revision/storage/ID counters and frozen row generation guard
steps and readiness. Logical discard releases borrowed owners without freeing
vectors. Candidate output is hidden until complete and current. Snapshot IDs
are append-only between clears; size guards detect appends, and a monotonic
clear/swap generation rejects replacement even if its size/address repeats.
The atomic history helper swaps two prebuilt vectors, advances IDs/revision,
marks snapshot companions unapplied and invalidates derived caches using scalar
writes. It omits the usual visual coverage diagnostic traversal. Empty companion
sets change no canonical identity. Old vectors remain retained until STOPPED
prewarm, and the prepared ID receipt remains available to existing undo/snapshot
session-exit bookkeeping. Capture/audible transaction composition, undo attachment
and bounded active retirement remain integration work. This is separate canonical staging
work, not part of the 6240 ranked-preparation allowance. No new owner, schema,
undo policy or canonical visibility rule is introduced by preparing this buffer.

`Loop::canCommitPreparedCaptureAndCompanions` combines history readiness with
constant-work pending pass ID/phase, sealed-vector nonemptiness, mutable-store
emptiness and transfer-capacity checks. Preparation binds the pending capture
ID before any canonical publication. The joint commit moves the sealed chunk
vector, calls internal no-fail history publication, and retires scalar capture
state. It does not recheck history after capture advances revision, clear the
store, release retired history, or invoke visual coverage diagnostics. Capture
adds one revision; nonempty companions add one more, preserving existing seal
semantics. This is the canonical half only: matching frozen capture content,
command epoch, source identity, ledger and audible selection remain Track's
joint transaction prerequisites. The production stop path is not switched yet.

`Track::commitPreparedOverdubStopBridge` uses a stateless native-testable body to
compose canonical and audible preflight before mutation. Pointer identity checks
bind LoopPasses, live revision/generation/length/storage counters and prepared
companion/capture owners. Stop credits, PLAYING destination, epoch, exact base/
target identities, cursor and fixed ledger checks remain mandatory. Revision
increment overflow denies. Canonical transfer precedes no-fail bridge publication;
no owner validation is repeated after the canonical revision changes. Publication
uses scalar lifecycle/index/anchor updates only. The turn must not interleave
emission or owner mutation. DeferredCleanup is entered without doing its work.
When the bridge base is the ready canonical window, that same turn now invokes
the existing preflighted view lifecycle handoff. It updates fixed view indices
and at most one retired view's metadata; it does not traverse event payloads.
The handoff body resides in flash to preserve the ITCM bank budget. Its integrated
atomic timing still requires device deadline correlation. Cleanup retires the
canonical-window receipt while leaving the selected current base intact, so a
later stop can prepare another window.
The production AtomicTransfer dispatch now returns a populated PLAYING joint
commit's failure without invoking another transfer. Empty completion and stopped
canonical recovery are explicit branches in the same native-tested helper;
neither can authorize a populated PLAYING canonical-only commit.
Cycle anchors now read the existing runtime cycle in a constant-work helper;
ordinary playback increments it once per serviced wrap if no bridge stream is
selected. Transport projection reanchoring resides in the existing cold flash
section to preserve the ITCM bank budget. This placement and cycle bookkeeping
do not establish the current-phase or integrated emission-deadline bound.
The production scheduler still must prove canonical capture was staged from the
same frozen input and admit history preparation before calling this entry point;
this helper alone does not activate ranked-stop routing or establish HITL timing.

Loop prepared history optionally verifies the pending sealed capture using the
normalizer's raw retained capture and private canonical indexes. Track's joint
handoff requires a completed, current proof from that exact normalizer. At most Q
units run per step: one field-aware event comparison or one chunk-boundary visit.
Invalid/unsealed refs, any event mismatch, missing or extra events deny readiness.
No copied-event buffer is allocated. Pending pass ID and chunk-vector identity/
count, normalizer generation, frozen capture and existing source guards remain
bound. Canonical preparation's full bound is ceil((H+E+R+S+N+K+2)/Q); N <= 2048
and K <= ACTIVE_OVERDUB_TRANSFER_CHUNKS. N+K is separate from the 6240 ranked
allowance and must be admitted by the production scheduler before activation.
The frozen canonical rewrite cursor also rejects a different destination store
while paused or complete; explicit canonical-order discard clears that binding.

PendingStopAdmission has separate canonical-preparation remaining credits and Q.
Its optional wide admission input defaults to zero for legacy callers. Values
above UINT16_MAX and legacy maximum + ranked + canonical + history-recycle totals above UINT16_MAX
deny before narrowing or admission mutation. This is counter representability,
not a latency ceiling. Canonical charging requires closed note reservations and
positive remaining credit. Finalization checks all four preparation counters; transport Stop
preserves credit while changing epoch/destination. The prepared Track handoff
requires a canonical reservation as well as the ranked/source proof.
`TrackPreparedStopCommit::stepPreparation` checks matching Q, current frozen
owner/epoch and prepared history/input identity before consuming one canonical
credit for at most Q existing preparation units. Completion queries do not
charge. Begin/setup and conservative excess credits need separate admitted
turns. Existing production stop callers still reserve zero canonical/ranked work;
production stage dispatch and reusable preparation/retirement remain open.

STOPPED companion-history prewarm reserves both canonical and staging row/ID
vectors for fixed row limit L and snapshot limit. A canonical reserve that moves
history advances storage generation first. `beginRecyclePreparedCompanionHistory`
requires a committed, current source and explicitly ends the consumed ID receipt's
lifetime. `stepRecyclePreparedCompanionHistory(Q)` performs at most Q units:
pop one receipt/snapshot ID, pop one retired payload event, visit one empty row,
or grow one empty row within reserved capacity. Buffers are retained; canonical
rows and counters are untouched. The bound is ceil((L+E+A+S+4)/Q), with E retired
payloads, A receipt IDs and S retired snapshot IDs. The remaining companion horizon
is the minimum row/snapshot slack; exhaustion denies new companion preparation.
Only H plus that usable slack is restored as empty rows, preventing a tighter
snapshot horizon from adding unreserved Trim work. Prewarm readiness also checks
the canonical mailbox retains its reserved capacity.
Revision/storage/ID/history or mailbox changes deny recycling until STOPPED
prewarm. Receipt consumption, cleanup admission/scheduling and mailbox replacement
refill must be integrated before enabling the production path.
PendingStopAdmission now reserves history-recycle credits and Q separately;
the prepared joint commit requires a nonzero reservation and leaves it intact.
Consumption requires DeferredCleanup, completed preparation and positive credit.
Transport Stop preserves the reservation; reset clears it. These post-commit
credits do not block commit readiness and do not cover other deferred cleanup.
TrackPreparedStopCommit's beginHistoryRecycle and stepHistoryRecycle now enforce
separate setup and Q-bounded execution turns. The joint commit binds the exact Loop
and preparation generation; prewarm/new preparation/discard invalidate that binding.
Begin acknowledges receipt consumption before ending receipt access. Wrong owner,
generation, Q or phase and exhausted credits deny without advancing work. A stale
Loop source discovered during an attempted grain consumes that grain and denies.
Completed queries are free only while canonical/mailbox identity remains current.
The production dispatcher must select these turns, retire conservative surplus
separately, and prove the complete required horizon before capture acceptance.
The native joint fixture exercises commit followed by admitted recycling without
changing canonical content or selected-bridge data. Undo/session ID collection
still grows vectors; bounded preparation and attachment remain required.

Loop's bounded session undo-ID collector visits at most Q pass IDs, companion IDs
or row/completion boundaries. Begin binds the exact Loop/session generation and
empty caller-reserved private outputs; it does not clear or allocate. The bound
is ceil((2P+C+1)/Q) for P,C <= 2048 (193 grains at both maxima and Q=32).
Session open/close, pass append, redo-tail removal and undo/redo cursor changes
invalidate the result before it may be consumed. Output capacity exhaustion
denies without growth; partial output is not publishable. Duplicate IDs and
stored order match the legacy collector, and undone redo rows remain excluded.
The stop variant adds FinalPass/FinalCompanions stages with bound
ceil((2P+C+A+3)/Q), A final receipt IDs and C+A <= 2048. It appends the final pass
only when included, valid and different from the last collected ID, then appends
all final companion IDs in stored order. Both output ID lists are capped at 2048.
The receipt remains borrowed until consumption: its preparation generation,
Ready/Committed phase, size and canonical storage/revision/next-ID identity guard
every resumed step and completed query. Recycling/discard/replacement denies
further use; the normal Ready-to-Committed handoff retains the same receipt.
These bounds cover collection only, separately from the 6240 ranked allowance
and history recycling. TrackUndo's private UndoEntry preparation now uses a
content-independent handoff after collection: scalar metadata, retain one
before-published root into an empty reference, at most two ID-vector swaps, and
logical collection retirement. Single-pass entries keep scalar passId only.
All preflight denials precede mutation; occupied entries are rejected. Retained
destination buffers move into the collection outputs rather than being freed.
Global entry-ID/cursor publication, redo/trim retirement, session-before root
retirement, snapshot-after attachment and capture-time admission remain open.
The protected publication capacity is a precondition of that publication, not a
measured grain: `GlobalUndoStack::entries` carries a reserved horizon
(`kUndoStackProtectedCapacity`) that `TrackUndoPublication::begin` requires and
that no stack rebuild may drop. A rebuild that shrinks the horizon turns the
stop's protected publication into a plain commit — the pass still commits, but
no `OverdubPassAdded` entry is recorded (see
[`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
§ Undo).

DEC-063 retirement helpers are structurally quantized but **not yet admitted to
active production dispatch**. LoopSnapshot::stepRelease visits at most Q ownership
slots or emptied nodes, with fixed tree depth five and at most 128 empty fixed
slots in any final node destructor; membership-chain tails never recursively
release. PersistedLoopSnapshotReleaseCursor visits one chunk, payload event, empty
row/backing buffer or geometry per unit. UndoEntryReleaseCursor invokes at most
one such child unit per outer unit, then drains ID storage. GlobalUndoStackTrimCursor
uses one decision, child release unit, survivor move or tail pop per unit; pressure
is checked only after complete entry release and compaction. Q=1/7/32 native
fixtures cover completion and survivor behavior. No helper allocates work storage.
The enclosing transaction must own inaccessible retired inputs beyond its no-fail
point, reserve the complete horizon separately from ranked grains, and retain its
cursors until drained. Destruction of an unfinished cursor is not an active grain.
Global stack visibility, redo/canonical reclamation ordering, admission credits,
attachment routing, allocator timing and command dispatch still require proof;
these helper checks do not establish realtime eligibility or a stop-duration bound.

Legacy stop admission rejects event counts above staging capacity, physical
closure-reservation overflow, zero Q, and declared total credits above UINT16_MAX.
`requiredOrderGrains` returns UINT16_MAX for invalid size/quantum, and that sentinel
cannot admit a stop. Valid existing production limits and MIDI behavior remain.

`beginFrozenCanonicalTail` requires this runtime's completed prefix and frozen
capture, idle row/pass owners, and an available apply worker. Its fixed setup
binds pending changes and appends capture without copying the sealed prefix.
`stepFrozenCanonicalTail(Q)` executes only normalization, row preparation,
companion setup, or one per-pass companion grain. Transition turns never execute
the next child. A conservative coordinator bound is `tailBound(C,H,Q) +
max(1,ceil(R/Q)) + perPassBound(C,P,H,R,Q) + 4`, including fixed setup/transitions.
No arrays or active allocations are added. Generation mismatch/source change
latches denial; cleanup only discards still-owned generations. Live fields and
pending-change contents must remain valid/immutable as specified by the child
owners. The typed result can feed existing ranked mapping/compilation, whose
bounds are additional. Production stop routing remains excluded: the existing
3072 order-grain budget does not reserve this work or bound service denials.

Empty selected history skips the normalization apply stage and copies raw to
effective storage directly. Empty companions skip the apply stage and merge
immutable pass inputs directly, preserving origins. Both paths remain Q-bounded
and do not acquire/cancel the shared apply worker. Normalization and tail bounds
omit Happly for H=0. The executable per-pass bound is
`1+P*((R ? A : 0)+ceil(C/Q)+4)`; ranked aggregate bounds now call that API.
Standalone empty apply batches still copy, bounded by `ceil(C/Q)` regardless
of unused prefix history size. Nonempty-row bounds and production gates remain.

Selection adds C prewarmed SealedPass descriptors and 2048 row pointers;
`fixedExternalBytes()` includes them. The selected-input uint64 bound adds
`ceil((C*(C+1)+R)/Q)+C+4` to the earlier bound with P=C, conservatively charging
each full scan and per-pass rounding. All loops remain allocation-free in
active grains. Production lifetime/source binding, capture-generation freezing,
atomic publication and aggregate capacity/stop admission remain unintegrated.

`RuntimeCompanionPasses::step(Q)` implements DEC-060's unpublished per-pass
preparation. Caller-owned frozen pass descriptors remain in canonical merge
order. Each call starts one pass, steps one borrowed apply child, or copies at
most Q output events in the pairwise stable raw-tick merge. It never stacks
apply and merge children. Each event retains pass/event provenance; source or
borrowed-apply generation mismatch denies preparation without exposing a prefix.
Only the borrowed generation is cancelled. Two C-event and two C-origin arrays
are prewarmed external-first, `2C*(sizeof(MidiEvent)+sizeof(Origin))` bytes;
Origin is two uint16 fields. C and pass count are each capped at 2,048. Aggregate
capacity refusal publishes nothing and cannot justify omitting accepted input.

The canonical sequence mode performs a Q-bounded prior-Delete scan per row to
preserve the sequence's present-identity guard, resetting that history for each
pass. If G(C,Q) is the single-row apply bound, P passes and R rows can use
`1 + P*((R+1)*(G(C,Q)+ceil(R/Q)+2)+ceil(C/Q)+4)` calls, evaluated in a uint64 counter. This component bound does
not prove the production stop reservation or latency. Gathering immutable pass
inputs, mapping their provenance to the published base, and production transfer
remain open. No active-path allocation or canonical mutation is introduced.

`RuntimeCompanionApply::step(Q)` runs one copy/find/write/reconstruct/overlap/
Delete/compact/order/permutation phase per call on a private prewarmed event
candidate. Content-sized local loops and each borrowed child consume at most Q
input slots. Output/provenance stay hidden until the full row or frozen batch
completes under the original source identity. Capacity/stale-source failures
publish nothing. Provenance remains the original source index, or UINT32_MAX
for a synthesized Off. Another fixed C-event storage plus C uint32 provenance
entries are prewarmed (C <= 2,048); active apply allocates nothing.

The conservative per-row bound is
`ceil((32C² + 64C + orderWork(C))/Q) + 32C + 64` calls. R frozen rows can use
`(R+1)` times that bound in a wide counter. This does not extend production stop
credits or establish acceptable latency. Bridge mapping and aggregate stop
admission must be proved before the production owner can select this work.

`RuntimePlaybackViewFill` can construct a full-loop view and its pass-origin
table together from a completed unedited baseline merge of effective pass
inputs. One Q-bounded input visit copies both event and origin. Fill needs
`max(1, ceil(N/Q))` calls, including an empty source's receipt. The table and
source identity are marked ready only at completion; baseline reuse/staleness
or destination interference denies an unfinished fill. Published destinations
are refused. Raw fills and reset clear the origin receipt. Source working
buffers may retire after copying; view ordering leaves event/origin indexes
aligned. Each of three views prewarms C two-uint16 origins (24,576 bytes total
at C=2,048); no active allocation or extra traversal is introduced. The typed
mapper begin requires the published view's matching origin receipt. Canonical
chunk/history gathering and production source binding remain open.

The per-pass overload of `RuntimeCompanionBridgeMap` uses an immutable
published-base origin table and independently guards base/preparation source
identities and preparation generation. It validates base event fields, rejects
duplicate coordinates, and searches origins in bounded quanta without NoteId
inference. B base rows and A output rows require at most
`3B + A + B(B-1)/2 + AB` charged visits, or `ceil(visits/Q)+4` calls. Every input
and comparison consumes credits. It reuses the fixed mapping storage and adds
only constant metadata. SysEx validation compares pointer/length, not payload
bytes. The producer must construct genuine provenance when preparing the view;
validation cannot recover historical pass membership from identical event
values. Invalid/stale data or capacity denial exposes no partial mapping.
Production generation binding, aggregate reservation, and transfer remain open.

`RuntimeCompanionBridgeMap::step(Q)` prepares unpublished overlay records and
canonical ranks from a completed, immutable companion candidate. Clear-base,
visit-candidate, and suppress-missing passes charge every input visit, including
unchanged rows, for at most Q visits per turn. With B base rows and A candidate
rows, completion is bounded by `ceil((2B+A)/Q)+2` calls. C records plus two
C-entry uint16 rank arrays are prewarmed external-first (C <= 2,048); there is
no active allocation. Results are hidden until complete; source changes and
apply generation changes invalidate even a previously completed map. Overflow
publishes nothing and requires the owner's reserved recovery policy. Canonical
ranks now survive bridge compilation/merge separately from source provenance.
Ranked full-loop compilation copies map metadata within existing clear/record
quanta and adds a borrowed-index base ordering child bounded by `orderWork(B)`
visits plus a constant transition turn. It writes only bridge-owned indexes,
never the immutable source order/phases. Two uint32 rank arrays and two base
uint16 index arrays add `4C + 8B` prewarmed bytes per bridge. Mapping reuse/source
changes or candidate record interference deny incomplete compilation. Completed
bridges own all rank data. Combined capture and aggregate stop admission remain
open.

Canonical companion payload preparation now consumes at most the declared
pending-change input quantum in `RuntimeCaptureCompanions::step`. Add rows
consume input credits too. Its fixed `EditPass` backing storage is allocated
by runtime prewarm; no pass IDs or canonical revisions change during preparation.
Legacy sealing shares the same Hide/Shorten payload encoding. Actual companion
pair/wrap/overlap resolution and the complete atomic transfer remain unproven.

Task-2.3 bridge preparation now has a native-verified implementation in
`RuntimePlaybackBridgeBuild::step(entryQuantum)`: at most the declared quantum
of clear/record/closure/order/index entries, with at most three 128-entry ledger-summary
comparisons per record, one such scan per closure/index entry, and a fixed
128-entry closure-proof check. Cross-wrap closing paths and safe changed-pair
instances are compiled into four cycle-qualified indexes for each stream. It preserves
the current view and selected bridge, and an invalid
candidate stays unpublished. The prepared cursor performs constant work per
consumed event; completed-phase selection uses bounded binary searches over
fixed-capacity indexes. The selected full-loop bridge is consumed by production
playback, including its wrap tail. These helpers are not yet wired to production
capture-stop admission, so this does not reclassify the complete row-27/28
owners or establish device grain cost. See
[bridge progress](../../../openspec/changes/resumable-midi-boundary-playback-maintenance/BRIDGE-PROGRESS.md).

### Draw-source resolve (row 3)

**Row 3 goal:** `resolveDisplayNotesByDrawSource` (production paint path) measures
elapsed resolve time against `Diagnostics::kDisplayResolveBudgetMicros` (5000 µs).
When the budget is exceeded and the prior frame matches the current track/slot,
return the last composed frame and increment `DisplayResolveOverBudgetCount`.
Full rebuild and slide gather are skipped; reuse/slide-incremental paths unchanged.
Overrun does not publish partial gather state. Seed: [`122537`](../../../captures/session_20260912_122537.log)
post stages 1–3 (`DFRAME` resolve max 2.6 ms); pre-fix [`033502`](../../../captures/session_20260912_033502.log)
252 ms pathology required RC4 first.

### Snapshot candidate construction (`LoopSnapshotConstruction::step` and session-exit publish)

**Row 21 goal:** Advance one in-flight candidate by one bounded construction
unit and then return. The unit is one persistent-cursor bar and at most
eight membership pages of sixteen references. The step may copy or sort
that page and move the cursor. It must not walk the whole snapshot, rebuild
a pass, or call `storePublishedSnapshot`. Per-bar membership capacity is the
explicit `kSnapshotMembershipMaxRefsPerBar` design envelope (derived page
count), applied equally to event-bar and note-bar membership; pages are lazily
allocated, so the envelope costs nothing until content uses it. A failed
incorporation discards the partial candidate and leaves authoritative
pass-change progress unchanged, so the exact fragment can be retried.

**Row 22 goal:** Separate queue from publish. Wrap and session-exit only
enqueue bounded work (journal fragments with exact parent identity, or a
session-exit pass list) at stop / wrap commit. Idle then admits two units
(`kSnapshotIdleConstructionUnitsPerTurn` = 2). Each unit is one
journal-capped fragment start or one row-21 step. Live-record
start walks `capture.store` from the persisted store index through
`tick < includeUntil` and emits at most one journal-capped changeset (32
changes). It does not copy or scan the historical store. Admission
cardinality is two units per Snapshot idle turn. Publication is atomic and only when wrap journals,
queued session-exit passes, construction, and companion changes are all
complete. A failed or incomplete candidate is never selected; the previous
published root stays authoritative. Pass-change traversal progress is a
transaction: it becomes authoritative only after successful incorporation of
the fragment that produced it, and a failed fragment keeps its exact checkpoint
while any partially constructed candidate is discarded before retry. A
persistent representability failure keeps the previous root, blocks
publication, and emits a diagnostic rather than retrying the same impossible
fragment silently.

Phase 2 publication is admitted at NOTE_EDIT close and idle
`stepPendingSnapshotPublish`. Record and overdub session-exit **queue** at
stop (`queueSessionExitSnapshot`) and construct from
`Track::processDeferredIdleMaintenance`. Phase 3 wrap publication queues
bounded journal fragments from `Track::commitOverdubWrapAtSessionStart` and
steps them during PLAYING/OVERDUBBING after the first root (`DEC-047`). An idle
turn that applies Snapshot construction does not also run a visual-cache slice.
`STOPPED` first-paint (`visualCacheNeedsFirstPaint`) does not suppress that
Snapshot turn when session-exit, in-flight, or wrap-journal work is pending.
First-record construction uses the overdub live-enqueue setup (`DEC-045` 4b.2):
RECORDING may start from `capture.store` after the playhead has left bar 1.
Wrap-ahead admission consumes only bounded journal fragments and never starts
pass reconstruction. Construction remains **not** admitted to
`handleMidiInput()`, clock dispatch, track-tick, or the
`CommitReason::OverdubWrap` call stack; the wrap path queues the fragment only.

Selected structural caps:

| Cap | Value |
|-----|-------|
| Membership page capacity | `kSnapshotMembershipPageCapacity` = 16 |
| Bar-membership design envelope | `kSnapshotMembershipMaxRefsPerBar` = 4096 refs/bar (event-bar and note-bar alike) |
| Membership pages per bar | `kSnapshotMembershipMaxPages` = 256 (derived from the envelope) |
| Journal changes | `kSnapshotWrapJournalMaxChanges` = 32 |
| Journal affected bars | `kSnapshotWrapJournalMaxAffectedBars` = 32 |
| Journal membership entries | `kSnapshotWrapJournalMaxMembershipEntries` = 64 |
| Journal bytes | `kSnapshotWrapJournalMaxBytes` = 4096 |

| Property | Row 21 goal (`step`) | Row 22 goal (queue / idle publish) |
|----------|---------------|----------------|
| **Unit cap** | One persistent-cursor bar and at most 8 membership pages of 16 references | Queue: journal-capped fragments with exact parent identity. Idle: two units per turn; each unit is one fragment start or one row-21 step |
| **Maximum indivisible step** | Select/update or copy/sort one membership page (≤ 16 refs) and advance the cursor. No full-span walk, pass reconstruction, or publish | Start at most one journal-capped fragment (32 changes). Live-record start consumes only unconsumed `capture.store` events from the persisted index. Session-exit length mismatch first clones one snapshot header when applied live-record ticks fit the sealed length. Non-fit discard remains a content-sized destructor |
| **Progress** | Persistent bar / membership cursor, `complete()`, `failed()`. Authoritative pass-change progress advances only when the fragment is incorporated (DEC-047 transaction) | Ordered fragments and pass-change cursor; previous published root stays selected until `freeze()` succeeds |
| **Completion** | All admitted changes applied and `failed()` is false | Idle stores a complete root only after wrap journals, session-exit passes, construction, and companions are done |
| **Overrun / failure** | `failed_` plus `failureReason()`; `freeze()` returns null; the partial candidate is discarded and the exact fragment retries from unchanged authoritative progress | Keep the previous published snapshot; do not publish a prefix; compatibility `LoopPass` records stay committed; a persistent representability failure parks publication with a diagnostic |

`T_RT_reserve` remains the 3.6 device gate for construction-attributable MIDI
gap and deadline lateness ([DEC-053](../../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)).
`T_RT_reserve_ceiling` is the 10,000 µs policy cap. Per-turn `reserve_us` is
the measured interval from snapshot-idle start to the next timing-critical
`handleMidiInput()` entry. Admission compares the classified two-unit cost
to `min(T_RT_reserve_ceiling, remaining_us − T_RT_safety_margin)` and yields
when the cost does not fit. That reject is **parked**
(`kSnapshotIdleReserveYieldEnabled` is false) after
[`115043`](../../../captures/session_20260908_115043.log) `remaining=0`
starved live construction until STOPPED. Yield helpers and
`DIAG,snapshot_idle,yield` stay compiled. `60% × T_clock` is a diagnostic
proxy only. STOPPED samples are excluded from this gate. Do not freeze an
observed unit as the ceiling.

Capture-serial `snapshot_idle` now
emits child durations for `startNextPendingConstruction`, nested
`startNextPassChangeFragment`, `LoopSnapshotConstruction::step`,
`finishPendingConstruction`, and `storePublishedSnapshot`, plus a `probe_us`
overhead sample. Session-exit length mismatch first clones one snapshot
header when applied live-record ticks fit the sealed length; non-fit
discard remains a content-sized destructor. Rows 21 and 22 stay
**structurally bounded** until that attribution shows whether the 79 ms
class sits outside the classified unit or the classification is wrong. Do
not reclassify from the parent `snapshot_idle` span alone.

A local timer is not the bound. Measured first-record idle `step(1)` vs
`MI`/`MO` note gaps: [`233308`](../../captures/session_20260825_233308.log)
tables in
[`loop_pass_snapshot_effective_state_refinement.md`](../../Plans/loop_pass_snapshot_effective_state_refinement.md)
and
[`runtime_scheduling_owner_boundary_admission_refinement.md`](../../Plans/runtime_scheduling_owner_boundary_admission_refinement.md).
First-record construction uses a constructed
empty parent object, not a null parent. Overdub and NOTE_EDIT publication
require an already published parent; they do not invent content from
`materializeToEventVector()`. Playback uses the stale-snapshot fallback until
idle publish stores a current root.

### Playback snapshot read (row 2)

Phase 3 playback preparation uses the existing row 2 owner.
`Loop::tryGatherPublishedSnapshotEventsInWindow` walks only the bars that
intersect the already selected playback window and copies those bar
membership events. A published root behind `playbackRevision` still supplies
that window; unpublished overdub session / pending-publish passes and their
companions overlay through windowed `CommittedEventRange` on those lists
only. Compatibility gather of every committed chunk list runs only when no
published snapshot exists. Snapshot identity is recorded on
`PlaybackMergedMidiEvents` for freshness (current root only). This does not
add a new interval owner and does not convert row 2 to a bounded class:
short-loop full gather and the no-snapshot fallback remain content-sized.
`playbackEventPhase` stays the clock-facing scalar.

### Deferred loop save — LSV1 dual-write substeps (row 14)

**Row 14 goal:** `StorageManager::processDeferredSaveState` →
`stepDeferredLoopPersist` (and synchronous `writeLoopPersisted` /
`measureLoopSlotFileBytes`) advance one deferred slice per turn under the
existing **300 µs / 3000 µs** between-step caps. LSV1 does not add a new bound
class.

| Substep | Owner | When | Indivisible unit |
|---------|-------|------|------------------|
| `lsv1_preamble` | `writeLoopSnapshotBodyFormatV1SavePreamble` | `loopUsesLoopSnapshotBodyFormatV1ForSave` at `DeferredLoopWriteStage::Header` | Body header + `LoopMetadata` + `PersistedLoopSnapshot` section frame (oracle payload streamed next) |
| `legacy_oracle_header` | `writeDeferredLoopHeader` | Every save | Loop header scalars + capture-pass count |
| `capture_pass_header` / `capture_pass_chunk` | `writeDeferredCapturePassHeader` / `writeDeferredCapturePassChunk` | Per pass, per chunk | One pass header or one PSRAM chunk batch (unchanged) |
| `edit_tail` | `writePersistedEditsTail` | `DeferredLoopWriteStage::EditTail` | One edit/geometry tail write |
| `lsv1_loop_snapshot_tail` | `stepLoopSnapshotBodyFormatV1SaveTail` (`DeferredLoopWriteStage::LoopSnapshotTail`) | After `edit_tail,done` when LSV1 save | One classified grain (`maxGrains = 1`): membership page gather, ≤16 sort swaps, 512-byte payload IO slice, or section header |

**Save-time migration (no eager owner):** legacy body on disk → load adopts passes →
`queueLoadedSnapshotPublish` + idle construction → `publishedSnapshotIsCurrent()` →
next successful deferred save emits LSV1. Pre-hydration slots keep writing legacy
until a current published root exists.

### Deferred slot load — LSV1 authority substeps (row 15)

**Row 15 goal:** `DeferredJobScheduler::runFrame` → `LoadLoopJob` advances one
phase slice per turn under **250 / 1200 / 5000 µs** between-step caps. LSV1 parse
is resumable; oracle stepping reuses `stepPersistedLoopSnapshotParse` grains.

| Substep | Owner | When | Indivisible unit |
|---------|-------|------|------------------|
| `sd_read` | `stepLoadLoopJob` Reading | Until payload + SAVE token | One `LoadLoopBudget::ReadChunkBytes` read (or token verify) |
| `body_probe` | `probeLoopSnapshotBodyFormat` | Enter Parsing | First-four-byte + version probe; reject unsupported/truncated LSV1 |
| `legacy_parse` | `stepPersistedLoopSnapshotParse` | Legacy body | One parse grain (unchanged) |
| `lsv1_scan_section` | `stepLoopSnapshotBodyFormatV1LoadParse` ScanSections | LSV1 body | One section header (+ inline `LoopMetadata` or handoff) |
| `lsv1_oracle_parse` | `stepLoopSnapshotBodyFormatV1LoadParse` PersistedOracle | LSV1 body | One `stepPersistedLoopSnapshotParse` grain inside oracle section |
| `lsv1_snapshot_payload` | `stepLoopSnapshotBodyFormatV1LoadParse` LoopSnapshotPayload | LSV1 body | One validated `LoopSnapshot` section → candidate ref (never publishes) |
| `commit_publish` | `applyLoadedLoopSlotBodyCommit` | Committing | Legacy: `rebuildSlotFromLoopContent`. LSV1: `storePublishedSnapshot` + `clearUndoHistoryForSlot` (depth 0); **no** rebuild |

LoopSnapshot body-format validation caps (`kLsv1MaxSnapshotObjects`, membership
ref limits, section payload cap) are **format guards** inside the parse substeps,
not new interval-path owners.

**Boot sync load** (`CurrentSetBootLoad`) remains legacy-only; runtime
`LoadLoopJob` carries LSV1 authority (5.3.3).

---

## Rejected

| Proposal | Status |
|----------|--------|
| `RuntimeWorkBudget::admit(WorkClass)` | **Rejected.** Local class budgets do not bound nested or indivisible work. |
| Per-job memory-pool declaration as a RAM1 fix | **Rejected.** RAM1 pressure is compile-time FlexRAM bank allocation, not runtime pool consumption. |
| Interval reservation | **Not authorized** until the Owner-Boundary Gate exits (contract Stage A). |

### Protected stop continuation (2026-09-14; not production-admitted)

`TrackUndoPublication` stages or restores one redo entry per unit. Its successful
joint canonical/audible/UndoEntry commit is the no-fail boundary. Bounded physical
redo release follows; canonical reclamation precedes the first feedback trim
check. Trim still rechecks the actual memory predicate after each full entry
release. Competing stack readers/writers and snapshot attachments must be
excluded or deferred before this helper can be enabled in production.

`Loop::DisabledPassReclaim` admits at most 4,096 canonical IDs. Gathering,
bottom-up merge sorting, reference visits and stable compaction are resumable;
reference membership lookup has at most 13 binary-search iterations. Releasing
one chunk, payload event or empty row is a unit. Each step validates stack and
canonical storage/revision identity. Removal of disabled content increments
storage identity once and leaves audible revision unchanged. The owner must
finish prepared-history recycling first and audit persistence invalidation.

`Loop::SessionClose` charges each session pass ID, each nested companion ID and
empty companion row, each live/source event and source note, plus six phase/final
visits. It retains outer capacity and changes final session scalars only after
draining. It requires exclusive post-publication ownership and retained snapshot
exit work (or no snapshot box); it cannot release an unpublished graph. Total
admission/reclamation/queue/attachment credits and device duration remain open.

Validation: full native suite **2,075 passed, 1 skipped**; capture-serial firmware
build passed (RAM1 code 425,864 bytes, variables 91,808, locals 6,496; flash code
945,392 bytes). These are functional/size results, not hardware grain timings.

Boot memory regression (2026-09-14 device): boot allocates capture staging
without full per-slot views/bridges/companion workspaces. Capture-only ordering
uses staging readiness; full view selection retains the full-bundle guard.
An upgrade with accepted/frozen/open staging state is rejected before reset.
Eight eager full bundles exhausted the device before SD load. The staging-only
boot left 4,401 KB PSRAM free and completed selected-loop load. Full concurrent
bundle admission remains open; this is not a change to the 6,240 ranked budget.
Allocation-only runtime construction and slot prewarm are flash-resident;
final RAM1 code is 425,784 bytes with 6,496 bytes local-variable headroom.

**Superseded 2026-09-17 by DEC-065:** boot no longer reserves either staging role.
`TrackManager::prewarmPlaybackRuntime` now establishes only the runtime slot, the
per-track stop-descriptor ring, the transfer horizon and the canonical copy
directory; staging is reserved by capture admission - the record arm through
`prewarmViews`, or `Track::startOverdubbing` on its own cold turn. "Capture-only
ordering uses staging readiness" therefore reads as "capture ordering uses
*admission* readiness": `captureStagingIfPrewarmed()` is null until an operation
admits itself, and every capture-ingress site already fails closed on that null
rather than assuming a boot reservation.

### 2026-09-14 stop-phase correlation telemetry

`RuntimeTimingTelemetry` retains its capture-only observation role. Existing
late-send one-shots now retain device send/due/deadline stamps. Main-loop
`Track::processDeferredIdleMaintenance` captures entry slack and elapsed time
around each existing stop grain. A fixed per-Track aggregate counts calls,
unknown cadence and service-deadline crossings, retaining the maximum-duration
call's timestamps; phase/identity completion flushes one `DIAG,stop_phase` row.
No new MIDI service site, admission decision, per-grain allocation, or ISR
formatting is introduced. Integer storage is fixed; capture text is flash-resident.
Crossing a deadline in wall time is not equivalent to preventing an interrupt
from servicing it. The logging cost is outside the measured stop call and must
still be included in full active MIDI-gap validation. Unknown cadence, absent
phase summaries and overflow cannot establish a bound. The retained first-late
sample is not an exhaustive event trace. See the active change's
`FOCUSED-STOP-HITL.md` for the schema and still-open attribution requirements.


### 2026-09-15 retained bridge bases and rolling emission views

`LoopPlaybackRuntime` owns two immutable bridge source bases independently of its
three rolling emission views. Both use `RuntimePlaybackView` storage and cold
prewarm; native fixed-external accounting increases by 192,512 bytes per full
runtime. Source bases survive preparation cleanup and cannot be reused while
their bridge is selected. This extends the existing DEC-058 owner, not canonical
pass ownership or the persistence format.

`serviceBridgeWindow` occupies one existing deferred maintenance turn: setup,
coverage fill at `kCanonicalPrefixInputQuantum`, order, or lifecycle promotion.
The existing bridge stream supplies immutable ordered source rows. Indexed
bridge emission covers an incomplete refill without a new MIDI-service site,
full gather, sort, or delayed launch. `acknowledgeThroughPhase` and window seek
perform bounded binary searches over prepared indexes; due-event emission keeps
the existing cursor/ledger policy. No wall-clock deadline bound is claimed.

Second-source closure inheritance reuses compiler Inherit/CarryOffs/mask work.
`requiredRetainedClosureGrains` separately overbounds copied Off visits and the
possible merge-sort width increase. This does not change the provisional 6,240
baseline or create a runtime admission ceiling. A changed live ledger restarts
only unpublished bridge compilation from an already complete canonical mapping;
refresh turns need explicit device measurement before any real-time reservation.

Boundary-only bridge reconciliation reuses stable records and both sorted
permutations. For Q>0, a conservative entry-turn bound is
ceil(L/Q) + ceil(R/Q) + 2*ceil((B+I)/Q) + 4, where L is the bounded ledger
count, R existing records, B immutable base events and I prepared insertions.
Each entry includes at most the fixed 128-entry ledger scan. No ordering or
canonical-copy work recurs. Admission's affected-ledger comparison visits at
most 128 current and 128 captured entries, with fixed pitch/identity lookups.
An affected-ledger/deadline change can require another reconciliation; real-time
reservations must still prove completion against actual MIDI service cadence.
Two 128-bit summaries per bridge add 64 fixed bytes per two-bridge runtime.
No new active allocation or source-capacity policy is introduced.

2026-09-16 canonical prework clarification: the existing VisualCache canonical
owner also advances the shared canonical-base merge/fill/order continuation at
Q=32. Prefix completion yields before base work; completed bases yield the owner.
Wrap append/source spans retain priority. No additional MIDI service call site,
allocation or new quantum is introduced. Stop resumes the same continuation;
prewarmed storage defines estimate capacity, independent of tail size. Device
reservations and remaining stop latency require the open HITL checkpoint.

2026-09-16 continuous canonical preparation: the existing Q=32 child also uses
otherwise-unused wrap-append turns, with existing global priority checks and
one child per turn. PLAYING and OVERDUBBING share the same continuation. Proven
append/snapshot-identity reuse resets selection metadata while retaining owned
completed passes. Final companion merge performs one additional scratch event
write per consumed addition; per-pass descriptors are recorded at the existing
pass transition. Successful cleanup swaps canonical scratch/output pointers
and rebinds metadata in constant work, then reselects descriptors/history in
later Q-sized turns. Scratch provenance is owner+generation matched. An extra
2,048-entry Pass descriptor array is prewarmed externally (16 KiB on device),
with no additional event buffer. These are implementation bounds, not measured
real-time reservations; task 5.3 must remeasure the augmented merge grain.


### Task 2.3 capture-range storage foundation (2026-09-16)

`LoopEventStore::sealCaptureRange` exchanges pre-reserved ownership directories,
seals at most one tail through the existing fixed-pool persistence owner, and
updates scalar receipts. No event/chunk traversal, directory growth or pool
allocation occurs at the cut. `CaptureRange::read` checks/copies one row;
`releaseGrain(Q)` releases at most Q chunk references via the existing pool owner.
Readers are invalidated before release; pool-lifetime mismatch cannot release a
new occupant. Cold `reserveCaptureTail` retains the existing vector allocator's
failure policy; cold destruction can release the full range and is excluded from
active scheduling. This is a structural storage bound, not a measured Track
punch deadline or full note-lifetime reservation. No production Track route is
connected to it yet. Queue capacity, simultaneous memory, undo/snapshot ownership
and task 2.3/5.3 acceptance remain open.


### Stopped-interval Track receipt (task 2.3 integration)

Stop admission saves fixed identity/geometry fields and swaps the pending-note
map into the existing worker's job. It traverses no pending-note nodes. Reserved
closure resolves the saved slot and phase. Following history recycling, each
admitted cleanup turn erases at most one job-owned pending-note node and returns;
Loop cleanup cannot stack on that turn. The number of these additional cleanup
turns is the number of pending notes at punch-out, bounded by accepted reservation
capacity. This adds post-commit cleanup turns; the provisional preparation-grain
estimate is not an end-to-end stop deadline. Allocator node-release latency and
device timing remain unverified. This does not classify the remaining Loop
preview/session cleanup as bounded or prove concurrent live capture safety.


### Reserved canonical copy (task 2.3 integration, 2026-09-16)

The production CanonicalRewrite phase now writes at most Q=32 ordered retained
rows into a separate prewarmed output store using physical chunk credits. After
copy completion, that phase releases at most one raw source chunk per turn.
These are separate turns. Canonical output admission reserves eight 256-event
chunks cold. Post-commit credit replenishment reserves one physical chunk and
returns; an eligible idle VisualCache turn likewise reserves one and returns.
`ChunkReservation::reserveChunks(N)` is proportional to newly requested credits;
only cold setup requests eight at once. A failed growth rolls back only newly
acquired credits. Cold teardown can release all remaining credits and is excluded
from active scheduling.

Active pass transfer preflights source chunk count against the prewarmed row
capacity; it cannot enter the allocation fallback. Already-owned output transfers
at a full pool. Copy, raw release, credit refill and retained preview cleanup need
separate device timing; these structural bounds do not close a deadline gate.
The added per-runtime reservation directory uses 1,024 payload bytes, plus 1,024
payload bytes of prewarmed output chunk IDs and eight reserved EventChunks. This
is single-head output admission, not the still-missing live-plus-queued budget.


### Pending capture and playback reset (2026-09-16)

A runtime borrows the admitted job's PendingStopAdmission until cleanup releases
it. Reset with that borrower returns false after scalar STOPPED retargeting and
existing generation/owner epoch checks; it does not discard source, copy output
or worker state. Track invalidation preserves playback generation and blocks
emission until recovery. The existing terminal STOPPED path retries cache reset
with no borrower. This is a lifetime bound, not a deadline proof or FIFO support.
Prewarm capture telemetry emits one bounded record per prewarm call, using pool
counters and the existing internal-heap sample, with no PSRAM header traversal.


**2026-09-16 overlap capture diagnostic stack evidence:** observed stack-boundary
fault at _svfprintf_r in the dense automated capture. Foreground-only observation
scratch (two fixed NoteId sets and line buffer) now resides in RAM2 and is cleared
on use. Compiled snapshotOverlapHoldCandidates local frame is 76 bytes vs 1,372
previously; both exclude register saves and do not bound the full call chain or
interrupt stack. RAM1 static headroom stays 5,600 bytes. Two post-change dense
runs avoid reboot; focused Stop-window silence passes. No full deadline/stack
safety guarantee follows. See active FOCUSED-STOP-HITL.md for exact evidence.


**2026-09-16 descriptor ownership and dense evidence:** Track stop head now uses
cold-prewarmed stable metadata slots (four entries, 616 ARM bytes per track,
external-first). Advancing a retired head is constant-time after existing bounded
cleanup; there is no per-job duplicate worker/staging graph. Concurrent admission
is still blocked by the existing staging/session owners. Local code/ring admission
tests do not close that gate. Dense timing review records a CanonicalOrder grain
crossing a service deadline and zero correlated emission rows; no audible deadline
PASS is established. See active FOCUSED-STOP-HITL.md.

### Record-arm reservation: buffers, bytes, and lifetimes (2026-09-17)

Step B of the memory-exhaustion RC. Device element sizes are **measured**, not
assumed: compiling the headers with the `teensy41-capture-serial` toolchain
(`arm-none-eabi-g++`, the env's own flags) resolves `sizeof(MidiEvent) = 20`,
`sizeof(EditPass) = 40`, `RuntimeCompanionPasses::Pass` / `SealedPass` = 8,
`Origin` = 4, `NoteUtils::DisplayNote` = 16,
`RuntimeCompanionOverlapWalk::Action` = 16, `PlaybackBridgeRecord` = 28,
`PlaybackPassEventOrigin` = 4, `MidiEventVec` = 12. Native/host sizes are larger
(8-byte pointers), so the host numbers must never be used for device bounds.
Every buffer below then reconciles **exactly** with the measured
`EXTERNAL_POOL_WRAP` tag record, which is itself evidence the sizes are right.

`LoopPlaybackRuntime::prewarmViews` (`src/Track/TrackPlaybackRuntime.cpp:389-416`)
is the arm's dominant reservation, and it is **larger than its own tag shows**:
its inner prewarms each open their own accounting tag, so no single tag is
"prewarmViews total".

| buffer | element x capacity = device bytes | written | read | required lifetime |
|---|---|---|---|---|
| three rolling views + two bridge bases (`include/RuntimePlaybackView.h:352-358`) | `RuntimePlaybackView` x 5 = 350 KB (`RuntimePlaybackView.cpp:184`, `:690`) | view build/fill during playback and stop | playback cursor emission, bridge selection | published/borrowed by playback, so it outlives preparation |
| two bridges (`FixedPlaybackBridgeStorage`) | 2 x 184,320 = 360 KB (`RuntimePlaybackView.cpp:692`, `:222`) | bridge compile | playback emission of the selected bridge | selected bridge is read until replaced |
| `frozenCaptureCompanions` | `EditPass` x 2048 = 80 KB (`RuntimeCaptureCompanions.cpp:24-34`) | companion preparation at punch-out | `RuntimeCaptureCompanions::step`, `beginFrozen` guards | stop-only content; capacity needed from arm |
| `canonicalPassInputs` | 8 arrays = 147,456 B = 144 KB (`RuntimeCanonicalPassInputs.cpp:29-56`) | `beginCurrent`/`step` during **live overdub canonical-prefix service** and during stop | `frozenCompanionPasses` merge, `canonicalPassInputs.matches*`, commit preflight | **capacity needed during RECORDING**, not only at punch-out |
| `frozenCompanionPasses` | `events[2]` 80 KB + `origins[2]` 16 KB = 96 KB (`RuntimeCompanionPasses.cpp:29-48`) | per-pass apply/merge | view build, bridge map, committed normalization | capacity needed during RECORDING (same live service) |
| `frozenCompanionBridgeMap` | `records` 56 KB + 3 x `uint16[2048]` 12 KB = 68 KB (`RuntimeCompanionBridgeMap.cpp:65-79`) | `CompanionBridge` phase, after merge completes | bridge compile and `refreshFrozenCompanionBridge` re-entry | from map begin through compile |
| `frozenCompanionApply` | `FixedPlaybackEventStorage(2048)` 59,392 + `provenance_` 8192 = 66 KB (`RuntimeCompanionApply.cpp:14-21`) | apply step machine (Copy..Permute) | merge (`RuntimeCompanionPasses.cpp:170-184`) | one apply run; capacity from arm |
| `frozenCompanionReconstruction` | `DisplayNote` x 4096 = 64 KB (`RuntimeCompanionReconstruction.cpp:15-25`) | Reconstruct phase | overlap walk (`RuntimeCompanionApply.cpp:366`) | one apply run |
| `frozenCompanionPairLookup` | 2048 x (2+2+1) = 10 KB (`RuntimeNotePairLookup.cpp:23-35`) | Find/Pair and reconstruction wrap lookup | Pair step, `SpanWalk` | one apply run |
| `frozenCompanionSpans` | `flags[2048]` = 2 KB (`RuntimeCompanionSpanWalk.cpp:14-22`) | SpanWalk initialize/scan | reconstruction projection | one apply run |
| `overlaps` + `deleteLookup` | 0 external bytes (`Action` inline, delete lookup borrows) | - | - | - |
| `preparedCompanionHistory_.rows` | `EditPass` x 2048 = 80 KB (`LoopPreparedCompanionHistory.cpp:68-74`) | companion preparation at punch-out | `publishPreparedCompanionHistory` swaps it into `passes.editPasses` (`:295-301`) | becomes canonical history, so capacity is permanent |
| `passes.editPasses` canonical reserve | `EditPass` x 2048 = 80 KB (`LoopPreparedCompanionHistory.cpp:54-62`) | canonical history over time | all canonical consumers | permanent; the two vectors form a rotating pair |
| `captureStopJobs_` ring | 4 descriptors per track (`include/Track.h:544-549`), ~80 KB over 8 tracks boot aggregate | punch-out admission, retired in grains | deferred stop worker | whole take; an admission limit, not a grain allocation |
| `captureStagings[2]` | `FixedPlaybackEventStorage(2048)` 59,392 + 128 reservations = 61,952 each (`RuntimePlaybackView.cpp:369-371`) | live ingress during RECORDING; the frozen role from punch-out | transfer/commit and the stop worker | admission-provisioned, both roles (record arm or overdub start, DEC-065) |
| transfer horizon | `overdubPasses` + chunk-id pool + session-exit receipts ~1.1 KB/track (`LoopCapture.cpp:2205-2229`) | live overdub ingress grows `overdubPasses`; pool consumed at seal | pass seal, atomic transfer | **needed during RECORDING** |

Reconciling the measured tag vector (tag order 1,2,3,5,6,10-16):
`tag 2 = 790 KB` is views+bases 350 KB + bridges 360 KB + `frozenCaptureCompanions`
80 KB - it does **not** include the seven inner prewarms, which are tagged 10-16
(450 KB). So `prewarmViews` costs **1,240 KB**, not 790 KB, and the earlier
"~340 KB is `viewSet->prewarm()` plus `frozenCaptureCompanions`" note
double-subtracted that 450 KB. `tag 1 = 80 KB` and `tag 5 = 7 KB` are boot
aggregates over all tracks (`src/TrackManager.cpp:59-80` prewarms every enabled,
non-empty slot with `prepareCapture=false`); `tag 3 = 1334 KB` is the boot
aggregate of both staging roles over every enabled slot (~116 KB per slot), not a
per-arm or per-slot figure. The arm's incremental cost is therefore
`790 + 450 + (two 80 KB history vectors once) + 0` for staging, stop jobs and
horizon, which are already boot-resident.

**Termination of the transport-start 81920.** A record request with the clock
stopped is parked as `pendingRecord` (`TrackManagerCaptureQueue.cpp:53-77`) and
released by `handlePendingRecordStart` at transport start, which reaches the same
arm. Playback start never touches the `EditPass` horizon, and boot prewarm
deliberately uses `prepareCapture=false` - which is why the first record command
pays the two 80 KB reserves. There is no separate transport-start allocation.

**Exclusivity verdict (the design input for step C).** The apply group cannot be
merged into one buffer. `RuntimeCompanionApply::begin` binds `candidate_`,
`pair_`, `deletion_`, `spans_`, `reconstruction_` and `overlaps_` at once
(`RuntimeCompanionApply.cpp:98-108`); `Reconstruct` borrows `spans_` and `pair_`
(`:338`); `SpanWalk` borrows `pair_` (`RuntimeCompanionSpanWalk.h:20`); the
overlap walk reads reconstruction's live `notes_` output (`:366`); and the merge
reads `apply_->data()` while writing `frozenCompanionPasses`' alternate buffers
*and* `canonicalPassInputs.normalizedScratch`
(`RuntimeCompanionPasses.cpp:170-184`). All of them are live inside one run, and
the same workers are additionally used by live canonical-prefix service while
overdubbing (`TrackDeferredMaintenance.cpp:855-877`), so their capacity is
required during RECORDING, not only from punch-out. The only plausible disjoint
pair is `frozenCompanionBridgeMap.records_` (68 KB) against the apply group: the
map is built in the `CompanionBridge` phase after the merge completes and apply is
released. The counterexample is that `frozenCompanionBridgeMap.begin` stores
`prepared_`/`baseOrigins_`/`base_` borrows that must stay valid through compile
(`TrackPlaybackRuntime.cpp:1099-1112`, `:1185`) and can be re-entered through
`requiresBridgeRefresh` (`TrackOverdubLifecycle.cpp:847-849`); and 68 KB > 66 KB,
so even then the arena is sized to the maximum, not the sum. Reducing this
working set therefore has to come from capacity shaping or from not provisioning
a full view set per armed slot - not from aliasing the workers.

Lifetime classes, which the step-C design must respect: (a) live-ingress capacity
required for the whole take - `liveCaptureStaging`, `Loop::capture.store`,
`pendingNoteChanges_`, `overdubPasses`; (b) capacity required from arm because a
MIDI-tick service consumes it while overdubbing - `canonicalPassInputs`,
`frozenCompanionPasses`, `frozenCompanionApply`, `reconstruction`, `spans`,
`pairLookup`; (c) stop-only content whose capacity must already exist at arm -
`frozenCaptureCompanions`, `bridgeMap`, the two 80 KB history vectors; (d)
published/borrowed artifacts that cannot be repurposed even after preparation -
the views, the selected bridge, `bridgeBases`, `preparedCompanionHistory_.rows`
(becomes canonical history) and the transfer-horizon rows (become committed chunk
refs).

### Explicit stopped heap diagnostic

`MemoryMonitor::emitStoppedHeapStats` is a synchronous, explicit session-capture diagnostic (DEC-084 — explicit stopped heap inspection), never a scheduled runtime/admission operation. The serial entry rejects transport, external clock, active track/capture/stop work and deferred save/load/prewarm before inspecting allocator state. Internal traversal caps at 4096 nodes; external traversal is bounded by pool size / 12 header positions (plus allocator trailer validation). It can block main-loop service: the host must keep MIDI clock/transport quiet throughout. Completeness and elapsed microseconds are returned; no allocation probes or metadata mutations occur.

The diagnostic clock guard also requires one second without a received MIDI clock pulse, tracked before ClockManager’s stopped-transport early return. This capture-only timestamp bookkeeping performs no allocation or heap inspection. Diagnostic timestamps are initialized during ClockManager setup.

### Shared capture identity and record-length prerequisites (2026-09-27)

`Track::beginCaptureInterval` adds one scalar identity increment after the existing successful
state transition. Record eligibility is checked before destructive preparation. Accepted-stop
identity validation adds fixed slot/LoopId/mode checks, with no allocation on stale/unallocated
owners. These added units are structurally bounded; they do not reclassify all start admission.
`Loop::captureMaximumTick` is a scalar read plus the store's constant-time empty check; accepted
append maintains the maximum with one comparison, reusing the former append-order field.
`prepareRecordStop` uses it for Record length calculation. Remaining record normalization,
closure, validation and publication work retains its existing bound classification until the
connected S4f.18–S4f.21 changes are verified. No claim that the full record-stop edge is bounded.

### Deferred capture publication persistence request (2026-09-27)

`requestLoopSlotPersistAndSaveState` runs once after a successful atomic transfer, using the
validated job slot. It reuses StorageManager's dirty/admit/request path without SD dispatch.
Track resolution is capped by `NUM_TRACKS`; queue admission uses the existing 256-entry table;
`getInternalHeapFreeBytes` reads the arena-tail scalar. No job/Track fields or content-sized
work are added. The reused helper is FLASH-resident. This does not reclassify storage allocation,
serialization, the entire atomic-transfer turn, or prove device MIDI timing. Evidence and
integration limits: [persistence execution packet](../../../openspec/changes/runtime-working-set-ownership/S4F20-PERSISTENCE-EXECUTION.md).

### Frozen record source window consumer (2026-09-27)

`RuntimePlaybackViewSource::fromCaptureRange` borrows the sealed directory without copying
chunks. `RuntimePlaybackViewFill::step` checks extent/generation and reads at most its supplied
row quantum, including rows outside coverage. `RecordCoverage` projects overflow NoteOn ticks
and rejects other overflow rows before copying into prewarmed window storage. Projection and
range reads are constant-time per row; fill performs no allocation or full-source normalization.
Matching-window overflow or an invalid read denies the unpublished build. Ordering remains a
separate existing bounded grain after copying; the frozen source is no longer borrowed then.
Total window scanning is still linear in source extent. This consumer does not prove onward
playback deadlines or make the current inline record-stop edge bounded. Held-note closure,
pass preparation and record admission integration remain open. Evidence:
[source execution packet](../../../openspec/changes/runtime-working-set-ownership/S4F18-SOURCE-EXECUTION.md).


### Record held-note identity receipt (2026-09-27)

Accepted Record NoteOn input stamps `PendingNote::recordNoteId`. A physical or
synthetic Off with a pending Record key uses that scalar rather than walking raw
capture rows to discover an identity. Lookup uses the existing channel/pitch map,
whose domain is MIDI key/channel identities, independent of take length. No new
allocation or source scan is introduced by the receipt. ARM PendingNote grows
from 1,052 to 1,056 bytes per existing held-note node; Track/job descriptors and
static RAM are unchanged. The host layout uses existing padding (1,072 bytes).
This does not reclassify append/dedup/preview work or the rest of finalization;
raw-event callers without a pending key and overdub retain existing resolution.
Evidence: [closure execution packet](../../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-EXECUTION.md).


### Job-owned Record closure output consumer (2026-09-28)

`Track::stepDeferredRecordClosure` validates the saved slot/LoopId, mode, raw-range
borrow generation, admission/runtime binding, and canonical output cursor. It appends
at most one terminal Off from the job's pending-note map into the existing reserved
canonical output, then erases that entry and consumes one closure credit. Resource
denial retains the obligation. It never searches raw content or resolves the selected
slot. The output cursor stores a source-owner pointer and generation, adding eight bytes
per allocated playback runtime (6,344 to 6,352 bytes on ARM); Track/job sizes are
unchanged. No new allocation operation or content-sized buffer is introduced. Map entry retirement uses the
existing one-entry cleanup pattern. Output uses pre-reserved chunks/directory capacity.
This consumer is not yet dispatched by production Record admission. It does not prove
normalization, pass construction, publication, first-bar/onward deadlines, or expand the
current overdub-specific admission contract. Evidence:
[Record output execution packet](../../../openspec/changes/runtime-working-set-ownership/S4F18-OUTPUT-EXECUTION.md).


### Prepared canonical chunk-directory transfer (2026-09-28)

`LoopEventStore::beginCommittedChunkTransfer` checks the exclusive prepared source,
prewarmed destination and owner generation in scalar work. Nonempty source
`reservedAppendReady()` also proves persistence prewarming; a cold source denies.
`stepCommittedChunkTransfer` performs at most Q tail-reference moves or directory
swaps. Each move seals one fixed chunk, moves its existing reference without a
release/retain walk, and updates scalar source metadata. Completion never performs
an unbounded reverse. Destination is incomplete/unpublishable until Complete;
source and destination own disjoint reference sets throughout cancellation.
No event rows are copied, source/destination directory capacity never grows, and
immutable raw CaptureRange input is not touched. Cursor footprint is 64 B ARM /
96 B native, caller-owned. The worker integration below places it in the existing job ring. The legacy
`detachChunksToCommittedChunkIds` wrapper still drains synchronously, using this
primitive only for fully prewarmed stores. This does not reclassify the entire
legacy stop path or close Record normalization/admission/playback deadlines.
[Transfer execution evidence](../../../openspec/changes/runtime-working-set-ownership/S4F18-TRANSFER-EXECUTION.md).


### Resumable job-owned pass staging (2026-09-28)

`Loop::beginPreparedCapturePassFromActiveAdmission` consumes one prewarmed directory
into a job receipt with scalar preflight and metadata. It rejects the live producer,
a busy candidate/cursor, insufficient admission and an unprewarmed source; a denied
source bind returns the directory to its owner. `Track::processDeferredOverdubStopGrain`
performs admission in its own turn, then calls `stepPreparedCapturePass` with quantum
one: at most one fixed-chunk seal/reference move or order-restoring pair swap per
PrepareTransfer turn. The Loop producer slot remains independent. The partial receipt
owns references but `preparationPending` denies transfer and companion borrowing;
completion only clears it after the ordered directory is ready. No whole-take work,
new allocation or capacity growth belongs to these steps.

The 64-byte ARM cursor is held per job in the existing externally allocated four-entry
ring: 2,728 → 2,984 bytes. Track (384), Loop (984) and playback runtime (6,352) sizes
remain unchanged; the receipt flag fits padding. Retirement resets the cursor before
ring reuse. `PendingStopAdmission::retireCompletedPreparation` also retires unused
undo estimates after collection and protected publication preparation finish, in
scalar work; busy/denied collection retains its obligation, and refused protected
publication retries the retained entry in a later turn. This is not a device
latency measurement or completion of Record normalization/admission/playback.
[Staging execution evidence](../../../openspec/changes/runtime-working-set-ownership/S4F18-STAGING-EXECUTION.md).

### Capture evidence outcome accounting

`DebugSessionCapture` Record-family accounting adds fixed scalar increments and
bounded tag-prefix checks to the existing append/drain paths. It does not change
the ring's displacement loop or its fixed 1 MiB capacity. Critical rejection
latches a count/timestamp; serial reporting occurs only on a safe drain budget
(`maxRecords > 8`), with USB capacity checked first. The explicit stopped-only
`emitIdentityPathStats` request adds two fixed-format rows. No input-edge serial
write or dynamically growing history is introduced. See the runtime change's
[S4f.22 evidence-accounting packet](../../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f22--record-evidence-loss-accounting-2026-09-29).

### Record-family ring reserve

The Record receipts (`RECA`/`RECS`/`RID`) are unevictable by ordinary traffic and by
their own replacement while the reserve lasts. `appendCaptureRecord` keeps the last
`kRecordReserveBytes` (32 KiB) of the fixed 1 MiB ring out of the ordinary append
ceiling, so a take's start/stop/stage/identity row appends into free space instead of
displacing an earlier one. Ordinary rows still displace an ordinary head at that
ceiling, so the ring cannot wedge; a refused ordinary append is reported as timing
shed exactly as before. Record rows may use the whole ring, and a reserve that is
itself exhausted falls back to the pre-existing critical-to-critical displacement,
which stays fatal and counted. Ring capacity, allocation policy and the
timing-critical drop-only path are unchanged.

### Record REVT drain during playback

`Track::processDeferredRecordRevts` is sliced at 32 events per turn when the
transport is playing, and that slice takes the bounded chunk walk only. The
full-merge fallback and the stored-MIDI verification still run only on a fully idle
transport: flattening the active passes is the unbounded work the idle gate existed
for. The chunk ids are copied at queue time, so the playing slice adds one bounded
chunk read plus at most 32 capture-ring appends per turn and no new resident state.
