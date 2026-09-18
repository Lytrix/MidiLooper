## Why

`feature/resumable-midi-boundary-playback-maintenance` added ~39,000 lines across 56 new files
(`RuntimeCompanion*`, `RuntimePlayback*`, `LoopSnapshot*`, `TrackPrepared*`, `TrackUndo*`, and the
LSV1 body-format TU). A code-tidying review of that added code found four recurring classes of
problem, none of which is gated by any existing check:

- **Dead code the branch itself superseded** — unreferenced public API (`Loop::clearFailureInjectionForTest()`,
  `RuntimePlaybackViewLifecycle::buildIsPending()`, `RuntimeCompanionOverlapWalk::actionReady()`),
  write-only state (`PendingSnapshotPublishBox::applyingCompanions`, `TrackUndoPublication::units_`,
  `MidiBatchComposition::Summary::nonClockCount`), vestigial on-disk writer fields
  (`telemetryTailEntered`, `sortSwapBudget`, the `PayloadWriteSubphase` cluster), four unreachable
  `logVisualCache*` emitters duplicating live inline ones, and a committed 247 KB zip of an
  already-tracked directory. In a firmware whose binding constraint is RAM1/ITCM and whose capture
  ring is size-limited, dead weight is a real cost, not just clutter.
- **One resumable-worker pattern re-expressed thirteen times** — thirteen per-worker `Phase` enums and
  five differently-named step-result enums for the triad the repo already names
  `…StepResult { MoreWork, Completed, Failed }`, plus duplicated allocation, admission-guard,
  source-identity, `ceilGrains`, merge-comparator and ledger-equality blocks.
- **Two vocabularies for one concept** — `view` and `window`; `normalized*` as a third synonym for the
  authority's **merge**; `Kind` where NAMING prefers `Type`; a forbidden "lane" in a comment.
- **No enforced formatting** — the repo has no `.clang-format`/`.editorconfig`, and this one branch
  already produced three copyright-header forms and trailing whitespace in 24 of its 56 new files.

Why now: the subsystem is new, the host suite is green (**2,262 cases, 1 skipped, 2,261 passed**), the
firmware builds with RAM1 local headroom (`variables 96128 / code 392344 / padding 872 / free for local
variables 34944`), and each duplicate is a place a rule can silently drift — the DEC-072 "never consult
the Loop producer slot on the deferred path" rule already has to be re-applied by hand at seven sites
and is carried as a manual checklist item.

## What Changes

Six phases, in dependency order; **every phase is behavior-preserving** and no phase moves ownership.

- **P1 — remove dead code and dead artifacts.** Delete the unreferenced declarations, write-only
  fields, vestigial writer state and inert computations listed above; remove the duplicate include
  inside the anonymous namespace in `TrackDeferredMaintenance.cpp`; remove the committed
  `resumable-midi-boundary-playback-maintenance.zip` and the stale scratch reproductions/patches from
  the change directory.
- **P2 — name the constants and enums.** Add `static_assert` links between the duplicated membership
  envelope (`kLsv1MaxMembershipRefsPerBar` vs `kSnapshotMembershipMaxRefsPerBar`), replace raw `2048`
  and `768u` with their existing owners, name the closure `flags` bits, the bridge cycle-mask bits, the
  atomic-preflight denial bits, and give the DEC-063 retirement cursors a `Phase` enum.
- **P3 — de-duplicate into existing owners.** One `RuntimeWorkerStepResult` and one step vocabulary;
  one body for the atomic-transfer pair; one `ceilGrains`; one merge comparator; one ledger-entry
  comparison on `ActiveNoteLedger`; one external-first alloc/free pair on the existing allocator
  header; one undo-trim predicate; one `installRetainedCurrentView`; one bar-membership comparator.
- **P4 — converge naming on `docs/Authority/NAMING.md`.** Rename `normalized*` to `merged*` in the
  touched files; `*Kind` → `*Type`; fix abbreviations; resolve `view` vs `window` by **recorded
  decision**, not by rename.
- **P5 — split two files along real ownership seams.** `RuntimePlaybackView.cpp` (capture admission
  out), `RuntimePlaybackBridge.cpp` (read side out), and the idle-telemetry block out of
  `LoopSnapshotPublish.cpp`. Each split is gated on a firmware size check; files with no clean seam
  are explicitly left intact.
- **P6 — make formatting and docs consistent and enforceable.** Normalize the three copyright-header
  forms and trailing whitespace across the 56 added files, add an `.editorconfig`/`.clang-format`,
  move the session journal out of `tasks.md` into `docs/Runtime/CURRENT_WORK.md`, and cut the 131
  measured duplicated long sentences from the change documentation.

Explicitly **not** breaking: no public behavior, wire format, persisted layout, serial protocol or
timing changes; no renamed repo-wide symbol; no new top-level domain noun.

## Capabilities

### New Capabilities

None. This is a pure refactor: it changes structure, names and dead weight only, and no requirement in
`openspec/specs/` changes. The change therefore sets `skip_specs: true` in `.openspec.yaml` rather than
inventing a requirement to satisfy validation.

### Modified Capabilities

None. Two near-misses were checked and confirmed to be implementation-only:

- `pass-reclaim` / `undo-memory-trim` — the shared trim predicate is extracted but the trim policy
  (absolute rail, minimum depth, memory pressure) is unchanged; the two current owners were verified
  to agree on every reachable case today.
- `playback-runtime-prewarm` — the prewarm requirement is unchanged; moving `CaptureAdmissionStaging`
  to another translation unit changes file layout, not capacities or behavior.

## Impact

- **Source:** `src/Track/Runtime{Playback,Companion}*.cpp`, `src/Loop/LoopSnapshot*.cpp`,
  `src/StorageLoopIoBodyFormatV1.cpp`, `src/Track/Track{PreparedStopCommit,UndoPublication,UndoRetirement,DeferredMaintenance}.cpp`,
  `src/Utils/*`, and their headers under `include/`.
- **Public headers:** several declarations are **removed** (see design § P1). All removed symbols were
  verified to have zero production callers; the test-only ones are listed with their replacement in
  design § P1 so the native suite can be re-pointed in the same commit.
- **Build resources:** every phase re-runs the RAM1 gate. P5 is the only phase that can move code
  between ITCM and FLASH; it is gated per split, because crossing a FlexRAM bank costs 32 KB of local
  headroom.
- **Tests:** `pio test -e native` is the gate for P1–P4 and P6; P5 adds a firmware size check per split.
  No HITL run is required for any phase (no observable behavior change); if a phase cannot show
  byte-identical behavior, it is dropped rather than HITL-verified.
- **Docs:** `docs/Authority/NAMING.md` (one clause, pending decision OD-1), `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
  (ownership sentence for the test-only reader), `docs/Runtime/CURRENT_WORK.md` + `DECISION_LOG.md`
  (journal relocation, `DEC` entries), and the change documentation under
  `openspec/changes/resumable-midi-boundary-playback-maintenance/`.
- **Deliverable tracking:** update [`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md)
  at closeout (no shipped feature; a maintainability deliverable).

## Non-goals

- **Changing diagnostic gating.** The review found `LoopSnapshotPublish.cpp` accumulating 13
  `micros()` probe scopes under the broad `SESSION_CAPTURE` gate while its only reader compiles out
  under `SESSION_CAPTURE_SNAPSHOT_IDLE`. Narrowing it changes observable capture-build output, so this
  change only **records the decision** (OD-2) and defers any narrowing to a follow-up.
- **Changing note-side bar application order.** `applySoonestBar` ignores its `playheadPhaseTick`
  parameter on the note side while the event twin sorts by playhead. That is a timing-model change;
  this change documents it and defers it (OD-3).
- **Renaming `view` → `window` across the codebase.** Decisions are recorded, renames apply only when a
  file is touched for another reason.
- **The uncalled `StorageManager` public statics.** A sweep found ~24 with zero references repo-wide;
  most predate this branch. Separate pass.
- **The synchronous gather fallback on the playback hot path** (`tryGatherPublishedSnapshotEventsInWindow`
  reached from `ensurePlaybackMergedMidiEventsBuilt`). Already tracked as OpenSpec task 2.4 / HITL 5.3;
  P3 must not "simplify" that fallback.
- **Formatting the 639-file branch diff.** Only the files this change touches.

## Open decisions

| ID | Decision | Default if unresolved |
|----|----------|-----------------------|
| **OD-1** | Is `view` or `window` the canonical term for the playback derived representation (`NAMING.md:43,146,411` vs the `RuntimePlaybackView*` family and a DEC capacity table)? Needs a `DEC-###` and a one-clause NAMING note. | Record the decision; **no rename**. |
| **OD-2** | Is `snapshot_unit` / snapshot-idle instrumentation meant to be on in the default capture env, or behind `SESSION_CAPTURE_SNAPSHOT_IDLE` like its siblings? | Keep the current broad gate; record a `DEC` and comment only. |
| **OD-3** | Should note-side `applySoonestBar` order by playhead (event-twin parity), or is ascending source order intentional? | Keep current behavior; document why in a comment. |
| **OD-4** | Fate of the test-only `readLoopSnapshotBodyFormatV1`: delete and re-point the native assertions at the stepped reader, or gate it to `PIO_UNIT_TEST_NATIVE` and correct the guide's ownership sentence? | Gate to native + correct the guide (removes its flash weight, no test churn). |
| **OD-5** | Merge order: this branch stacks on `feature/resumable-midi-boundary-playback-maintenance` (the tidied code is not on `dev`). Does the tidy-up PR target the feature branch, or wait and target `dev`? | Target the feature branch (reviewable unit). |
