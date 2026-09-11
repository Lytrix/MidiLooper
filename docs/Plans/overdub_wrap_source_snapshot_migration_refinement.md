# Overdub Wrap Source Snapshot Migration Refinement

**Status:** 3.5 firmware in progress on `feature/overdub-wrap-snapshot-publication`; 3.6 native wrap-journal parity in tree; `T_RT_reserve` policy recorded as [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary); device recapture open.

**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)

**Related residual:** OpenSpec `4b.3` remains a separate post-stop timing-attribution item. This plan does not close it.

## Goal

Move overdub-wrap effective-note handling toward the immutable `LoopSnapshot`
end state. Do not optimize compatibility caches or extend compatibility replay
as a normal source.

The target is a complete immutable snapshot for every effective-state
consumer:

```text
settled overdub wrap
    → existing Loop semantic mutation
    → SnapshotNoteChange records
    → bounded LoopSnapshotConstruction
    → complete provisional O: snapshot
    → all consumers read selected snapshot content
```

## Identity and version contract

- `NoteId` is the stable logical identity of a note.
- `LoopSnapshotNoteRef` identifies one immutable version of that logical note.
- `SnapshotNoteChange` is transient construction input only. It is never an
  authoritative representation of loop content and is not retained as a
  second effective-state model.
- A Shorten, Move, or Length update creates a new immutable note object with
  the same `NoteId` and changed geometry only for this migration slice.
- A Hide operation removes the note reference from the successor snapshot.
- A new captured note receives a new `NoteId`.
- The selected snapshot contains only the latest reference for each effective
  `NoteId`; older snapshot roots retain older versions for undo/redo.
- Candidate construction is copy-on-write from the selected snapshot.
  Unchanged note objects and unchanged membership structures remain shared.
- No numeric per-note version field is introduced unless a later design
  decision proves that reference identity and snapshot identity are
  insufficient.

## Owners

- `Track::commitOverdubWrapAtSessionStart` owns wrap sequencing.
- `Loop` owns snapshot allocation, candidate construction, publication,
  selection, and lifetime.
- Existing capture finalization, pairing, overlap, and NOTE_EDIT geometry
  owners supply settled mutations.
- Existing playback, display, NOTE_EDIT, analysis, validation, persistence,
  undo, redo, clear-slot, and load owners retain their derived representation
  and runtime policies.
- Compatibility pass replay and materialized data remain only as parity or
  rollback sources until each consumer migration gate passes.

## Implementation contract — 2026-08-27

The active implementation slice is limited to OpenSpec tasks `3.5` and `3.6`.
Stages 6–8 are follow-on work and are not activated by this refinement.

- This slice supersedes the post-root successor-idle restriction in `DEC-045`.
  The supersession requires a new decision-log entry, an updated
  `CURRENT_WORK.md` phase, and a replacement device gate. The existing
  `4b.2c` evidence remains historical evidence for the prior policy.
- Capture/pairing state assigns `NoteId` incrementally to each logical
  NoteOn/NoteOff pair before pass promotion. Pass promotion must not scan the
  capture content to assign pair identities. `EventId` remains the identity
  for non-note events only.
- Existing semantic owners provide an explicit, transient per-wrap mutation
  journal. The journal carries new notes, geometry replacements, hidden-note
  removals, and affected event replacements/removals. It is not an effective
  state authority.
- The journal has structural caps for change count, affected-bar count,
  membership-entry count, and bytes. A wrap is split into ordered bounded
  fragments when required. A fragment cannot publish a partial snapshot.
- Pending fragments retain their exact parent snapshot identity. Multiple wraps
  are applied in order to the latest complete unpublished candidate. A failed
  fragment remains queued for retry; it is never silently discarded.
- Candidate construction uses fixed-capacity immutable membership pages and a
  persistent bar cursor. `begin()` must not enumerate all bars up front, and a
  construction unit must not copy, scan, or sort an unbounded membership list.
  The existing apply state now persists membership-copy, mutation, append, and
  event-sort progress so one admitted unit copies, scans, appends, or sorts at
  most one 16-reference membership page.
- The event index is effective content for this slice. Hide removes the paired
  NoteOn/NoteOff event references by `NoteId`; geometry changes replace their
  immutable note event objects while preserving `NoteId` and canonical order.
  Non-note event objects retain their logical `EventId`.
- Pair `NoteId`s and non-note `EventId`s are runtime-only in this slice.
  Compatibility pass storage remains unchanged; reboot identity
  persistence/reconstruction belongs to the later storage phase.
- The global overdub undo entry remains pass-based while construction is
  pending. Snapshot-identity attachment belongs to the later undo migration.
- The device gate closes only against the [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
  `T_RT_reserve` contract: `T_RT_reserve_ceiling` 10,000 µs, per-turn
  `reserve_us` to the next timing-critical `handleMidiInput()` entry, and
  yield/retry without prefix publication. `60% × T_clock` is diagnostic only.
  Do not freeze an observed unit.

### Admission starvation finding — 2026-08-27

Capture [`194515`](../../captures/session_20260827_194515.log) showed the
18 wrap fragments drain and publish a new root, but `queued=1,exit=1`
remained for more than 28 seconds. The existing active-phase admission only
recognized an in-flight construction or wrap journal, so it stopped calling
`Loop::stepPendingSnapshotPublish` after `journals=0`. The stopped-track
maintenance gate also returned while the transport clock remained running.
The fix extends those existing admission gates to recognize all pending
snapshot work while retaining `stepPendingSnapshotPublish(1)`. Native and
capture-serial build gates pass; device recapture must verify a final
`pending=0` row.

### Construction slice finding — 2026-08-27

Capture [`200115`](../../captures/session_20260827_200115.log) showed a
63.667 ms session-exit construction slice and a following 64 ms `midi_gap`.
The previous “one bar” unit could copy and scan all eight membership pages in
one admitted step. `LoopSnapshotConstruction` now retains page-level progress
through copy, mutation, append, and event sorting while keeping publication
atomic. The capture-serial build remains RAM1-safe at 7008 locals; native
verification passes, but the device `T_RT_reserve` recapture remains open.

### Device recapture — 2026-08-27

Capture [`202635`](../../captures/session_20260827_202635.log) confirms the
admission fix and journal publication: 18 wrap fragments drain from
`journals=18` to `journals=0`, the root advances to `root_rev=5`, and the
session eventually reaches `pending=0` at `root_rev=13`. The page-progress
build still fails the interruption gate: `snapshot_idle` reaches 79.169 ms
(`construction=4`), with later 74.453 ms and 41.000 ms slices. Corresponding
construction-period `midi_gap` values reach 79.486 ms and 74.944 ms, and
deadline-lateness markers reach 78.024 ms (NoteOn) and 20.948 ms (NoteOff).
The 79.169 ms slice is `startNextPendingConstruction` for a queued session-exit
pass, not a membership-page step. That start now reuses the existing journal
caps and a persistent `CommittedEventRange` cursor so one admitted unit builds
at most one bounded pass-change fragment. Publication stays atomic until every
queued fragment completes. Device `T_RT_reserve` recapture remains open.

### Premature wrap publication — 2026-08-27

Capture [`204136`](../../captures/session_20260827_204136.log) published
`root_rev=8` as soon as wrap journals reached `journals=0`, while
`queued=3` session-exit passes were still pending. Playback switched to
`playback_build,snapshot` for that incomplete root, so
`tryGatherPublishedSnapshotEventsInWindow` skipped the unpublished overlay
and the first record pass disappeared. Wrap-journal completion now keeps the
candidate unpublished while queued passes, construction progress, or
companion changes remain; `storePublishedSnapshot` waits for that same
complete gate. Device recapture remains open.

## Work stages

1. **Measure the existing wrap phases**
   - Attribute capture finalization, companion sealing, snapshot change
     creation, candidate construction, and consumer refresh separately.
   - Keep probes capture-gated and disabled in normal builds.

2. **Snapshot correctness**
   - Map Shorten to `Replace` with the same `NoteId`.
   - Map Hide to `Remove`.
   - Map new overdub notes to `Add` with new identities.
   - Carry the settled pair `NoteId` from capture/pairing state; emit event
     replacements/removals for note geometry changes. Assign `EventId` only to
     non-note event objects.
   - Verify dirty bars include both old and new geometry, including wrap
     tail/head membership.
   - Verify `SnapshotNoteChange` is transient input and never a content
     authority.
   - Verify copy-on-write sharing, self-contained roots, latest-reference
     selection, and older-snapshot stability.

3. **Bounded candidate construction**
   - Extend the existing `LoopSnapshotConstruction` progress path with
     persistent bar-cursor progress; do not populate all dirty bars in
     `begin()`.
   - Replace vector-backed unbounded membership updates with fixed-capacity
     immutable membership pages and bounded page fragments.
   - Consume the bounded per-wrap mutation journal rather than reconstructing
     complete pass content in `startNextPendingConstruction`.
   - Execute it through the existing bounded/resumable maintenance mechanism
     (`Loop::stepPendingSnapshotPublish` through the Ring B idle cursor).
   - Do not introduce a new scheduler, runtime budget owner, or source-cache
     authority.
   - Construct candidates copy-on-write from the selected snapshot; never
     mutate the selected or historical root.
   - Define journal caps, membership-page capacity, maximum indivisible step,
     persistent progress, completion state, overrun guard, and retry policy in
     `ExecutionBounds.md`.

4. **Publish and retain the previous snapshot**
   - Extend the existing `Loop` publication path from
     `Track::commitOverdubWrapAtSessionStart`; the wrap path only queues the
     bounded journal and does not construct a candidate.
   - Allow ordered successor construction during `PLAYING`/`OVERDUBBING`
     under the superseding phase policy after the first root exists.
   - A candidate is never selected while construction is incomplete.
   - Select only after construction reaches the frozen complete state, and
     publish that selection atomically.
   - Keep the previous complete snapshot selected while construction is
     pending or fails, and retain the failed journal for retry. “Provisional”
     means private and unselected until completion.
   - Keep wrap revisions provisional `O:` state; create one global
     `OverdubPassAdded` undo transition at overdub stop.
   - Keep that pending undo transition pass-based in this slice; do not attach
     snapshot identities until the later undo migration.

5. **Parity and MIDI-interruption gate**
   - Compare the complete published candidate against the materialization
     oracle without making the oracle the runtime source.
   - Measure maximum MIDI gap attributable to snapshot construction,
     maintenance time per slice, and total construction wall time.
   - Score the [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
     `T_RT_reserve` contract before checking off 3.5/3.6. Policy ceiling is
     10,000 µs. Pass only when construction-attributable MIDI gap and deadline
     lateness remain within `min(ceiling, reserve_us − T_RT_safety_margin)`;
     total wall time alone is not sufficient. Capture-gated `reserve_us`
     telemetry is required; `T_RT_safety_margin` is not frozen.
   - Extend the existing snapshot/take-survival native tests with repeated
     same-`NoteId` updates, wrap NoteOn/NoteOff replacement, Hide, equal-tick
     ordering, and non-note events. Wrap-journal fixtures (2026-09-08):
     `test_wrap_journal_repeated_same_note_id_replace`,
     `test_wrap_journal_replaces_wrap_note_on_off_pair`,
     `test_wrap_journal_hide_removes_published_note`,
     `test_wrap_journal_equal_tick_non_note_event_order`,
     `test_skipped_overdub_stop_retains_in_flight_wrap_journals`,
     `test_queue_session_exit_keeps_wrap_journals_without_new_pass`. Focused
     native **83/83**. Device wrap-retain recapture
     [`101815`](../../captures/session_20260908_101815.log): `seal=skipped`
     still, but journals retained, `fragment` advanced, `root_rev=3`
     matches `playback_rev=3` (`pending=0`). Prior fail class
     [`094930`](../../captures/session_20260908_094930.log) /
     [`094228`](../../captures/session_20260908_094228.log). Do not check
     off 3.5/3.6 until the `T_RT_reserve` device recapture against DEC-053.
   - Run native, firmware-size, and capture-serial device verification before
     beginning consumer migration.
   - Device `snapshot_idle` must report wrap-journal state separately from
     pass-id `queued`/`applied`/`construction`: `journals` (remaining
     fragments), `fragment=i/n` (front fragment), and `parent_rev` (published
     revision when the front journal parent is the selected root; `0` when
     the parent is an unpublished candidate). Capture
     [`173826`](../../captures/session_20260827_173826.log) cannot close this
     gate because those fields were absent and `root_rev` stayed `1`.

The first implementation slice ends after Stage 5. It proves snapshot
correctness, bounded construction, atomic publication/retention, parity, and
MIDI interruption behavior on device. It does not migrate overdub source
resolution, display, NOTE_EDIT, analysis, validation, persistence, playback,
or undo/load. Stages 6–8 begin only after that gate.

6. **Migrate overdub source resolution**
   - Overdub source resolution reads pitch/bar `NoteRef` memberships,
     deduplicates by `NoteId`, and derives geometry from the selected immutable
     references.

7. **Migrate all remaining consumers**
   - Display, NOTE_EDIT, analysis, validation, playback, persistence,
     undo/redo, clear-slot restore, and load use snapshot content and
     snapshot identity while retaining their existing representation owners
     and bounded policies.
   - Consumer-owned `DisplayNote` and playback caches use snapshot identity as
     their source freshness key.
   - `LoopSnapshot` does not own consumer windows, projected intervals,
     playback cursor/order, ledgers, or derived caches.
   - Select bounded canonical spans before `IntervalProjection`.

8. **Retire compatibility dependencies**
   - Remove normal consumer dependence on maintained LCR, pass replay, and
     full materialization only after all individual migration gates pass.
   - Do not remove compatibility sources before parity and rollback gates pass.

## Required verification

- Snapshot self-containment: queries do not walk parent snapshots.
- Same-`NoteId` geometry replacement creates a new immutable reference.
- The pair `NoteId` is carried from settled capture/pairing state and is not
  assigned by a promotion-time scan. `EventId` is retained for non-note event
  objects only.
- Hide removes paired note event references by `NoteId`; geometry changes
  replace immutable note event objects while preserving `NoteId` and canonical
  order.
- Unchanged references and membership lists remain shared.
- Older snapshots retain their original geometry.
- Wrap tail/head lists deduplicate one logical `NoteId`.
- The selected snapshot never exposes stale or duplicate note versions.
- Failed or over-budget construction leaves the previous snapshot active and
  retains the bounded journal for retry.
- Multiple pending wraps apply ordered bounded fragments against exact parent
  identities without exposing an incomplete candidate.
- Construction performs no upfront full-span bar enumeration or unbounded
  membership-vector copy.
- Before consumer migration, the snapshot candidate matches the existing
  materialization oracle for note identity, geometry, wrap membership, hidden
  notes, pair-`NoteId` event membership, event ordering, and non-note
  `EventId`s.
- The first implementation slice ends after this parity gate and does not
  migrate playback, display, NOTE_EDIT, analysis, validation, persistence, or
  undo/load.
- The device gate records per-turn `reserve_us` (idle start → next
  timing-critical `handleMidiInput()` enter), construction-attributable MIDI
  gap, maintenance time per slice, and total construction wall time. Parent
  `snapshot_idle` wall time is not the acceptance metric. [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary).
- All later consumers match the materialization oracle while using the
  snapshot as the normal source.
- Pair `NoteId`s and non-note `EventId`s are runtime-only in this slice;
  persistence and reboot reconstruction are later-phase requirements.
- The global overdub undo entry remains pass-based while snapshot construction
  is pending.
- Run `pio run -e teensy41-capture-serial` first and require RAM1 locals of at
  least 4000, then run `pio test -e native`.
- Run the existing capture-serial HITL baseline after native and firmware
  gates pass.

## Architecture gate

- **Owner:** `Loop` snapshot publication; existing semantic owners supply
  mutations; existing consumers retain derived-state ownership.
- **Primary invariant:** the selected complete snapshot contains only the
  latest immutable reference for each effective `NoteId`.
- **Ownership change:** NO new owner; this slice extends `Loop` publication
  and existing capture/pairing owners. Normal consumer authority migration is
  later.
- **State transition change:** YES, because settled overdub wraps publish
  provisional `O:` snapshot identities.
- **Execution-bound change:** YES, because fixed-capacity membership pages,
  persistent bar cursors, and bounded mutation-journal fragments are required
  through the existing Ring B maintenance cursor; no new scheduler or runtime
  budget owner is introduced.
- **Behavior-preserving:** YES for musical semantics and consumer output;
  snapshot freshness and source ownership intentionally change.
- **Reuse:** YES — extend existing `Loop` publication and existing consumer
  owners; do not create a compatibility cache authority or new manager.
- **Phase authority:** `DEC-047` supersedes the post-root successor
  restriction in `DEC-045` only for this `3.5`/`3.6` slice.
- **Scope:** OpenSpec `3.5`/`3.6` capture publication, bounded mutation
  journal, fixed-capacity membership construction, event identity/parity,
  firmware-size, and HITL verification. Direct-consumer migration and
  snapshot-identity undo/load remain later phases. No unrelated post-stop
  `4b.3` fix.

