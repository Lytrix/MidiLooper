# Design — root-bearing loop persistence

## Decision review (DECISION_LOG search)

**Searched:** `docs/DECISION_LOG.md`, `openspec/changes/*`, `openspec/specs/*`, `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`,
`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`, `docs/Plans/` — terms: `decideLoopPersistBodyFormat`,
`hasSettledPublishedSnapshotForSave`, `loopSettleDeferrals`, `AwaitSettled`, `markLoopSlotMaterialDirty`,
`admitLoopSlotPersist`, `root`, `rootless`, `LSV1`, `writeLoopPersisted`, `PassesAuthoritative`.

**Findings**

| ID | What it fixes here |
|---|---|
| DEC-073 | A root is current by **coverage** of the committed-pass revision, not by its own revision stamp. This is why a post-commit root is legitimately "stale" for one publication window. |
| DEC-074 | RC2: an LSV1 body admits only a settled root; the format is pinned once at `Header`; the fallback is the passes-authoritative legacy body; `AwaitSettled` is bounded. **Extended here** (the bound and the waiting predicate, not the admission). |
| DEC-179 | A parity-divergent body restores its passes into an empty slot; the restore never reports `committed` without content. Unchanged. |
| DEC-180 | A save may embed a root only when it is proven equivalent to the passes being serialized; a rootless slot paints from `PassesResolve`. **Unchanged** — this change only gives the publisher the chance to supply that root before the format is pinned. |
| DEC-192 | A rootless *playing* slot is recovered by the deferred Snapshot owner. Unchanged; that is playback, this is persistence. |
| DEC-193 | Playback is investigated as an image-boundary regression. Untouched. |

**Reusable pattern:** the existing three-outcome decision (`Lsv1SelectedRoot` / `AwaitSettled` / `PassesAuthoritative`),
the existing deferral field on `CurrentWorkspaceSaveJob`, and the existing per-slot dirty/`admit`/`request` triple the
stop path already uses.

**Reuse decision: YES.** No new owner, queue, lease, format, or lifetime.

**New constraints introduced:** (1) a persistence turn may now admit work by itself when a recorded rootless slot
becomes settleable; (2) the settle wait is wall-clock **and turn** bounded, so the save path may be parked for up to
`kLoopPersistSettleWaitMs` per loop write when a publication is owed; (3) a synchronous drain never waits, so a
clear/`saveState()` gesture keeps its latency.

## Model

```
stop commit ──► markLoopSlotMaterialDirty + admitLoopSlotPersist + requestDeferredSaveState
                     │
                     ▼
        deferred loop write, Header stage
                     │
     decideLoopPersistBodyFormat(loop, budgetRemaining)
        ├─ settled root            ──► LSV1 body (root embedded)          [unchanged]
        ├─ publication owed, in budget ──► AwaitSettled (writes nothing)  [NEW: owed, not only current]
        └─ otherwise               ──► legacy body, rootRewritePending=1  [unchanged + record]
                     │
                     ▼
        publication lands (existing publisher)
                     │
     serviceRootlessSlotRepersist (persistence owner turn)
        └─ slot flagged ∧ root settled ──► mark dirty + admit + request save, clear flag
                     │
                     ▼
        second loop write, Header stage ──► LSV1 body (root embedded)
```

`rootRewritePending` is true exactly while the card's body for that slot has no root. It is set at slot finalize from
the pinned `loopWriteUsesLsv1` decision, so it cannot disagree with what was written.

## Invariants

1. A body that embeds a root always passes the loader's equivalence gate (DEC-180); the writer comparison is untouched.
2. A deferral writes no bytes and leaves the loop write stage unchanged (DEC-074).
3. The re-save is admitted only for a slot whose body was written without a root **and** whose root is now settled
   (`hasSettledPublishedSnapshotForSave()`), so a re-admission can only improve the body; it cannot loop without a new
   publication or a new content change.
4. The flag is cleared when a slot is written root-bearing and when the slot no longer holds committed passes.
5. No new work on the interval path: both additions are on the persistence/idle lane.

## G2 execution packet — slice 1: the settle wait covers an owed publication

- **Scope / dependency closure.** `decideLoopPersistBodyFormat` (`src/StorageLoopIo.cpp`),
  `deferUnsettledLoopPersist` + `resetDeferredLoopWriteState` (`src/StorageManager/WorkspaceSave.cpp`),
  `CurrentWorkspaceSaveJob::loopSettleDeadlineMs` (`include/StorageSession.h`). Requires only existing state.
  Non-goals: the writer's equivalence gate, the loader, the publisher's construction. Commit boundary: decision + wait
  + its two fixtures.
- **Transition oracle.** Initial: loop with committed passes, a publication owed for that state, root one revision
  behind. Sequence: (a) `decideLoopPersistBodyFormat(loop, budgetRemaining=true)` → `AwaitSettled`, no bytes written,
  `loopWriteStage` still `Header`, no SD handle held; (b) the window expires (clock **or** the turn cap) with the
  publication still owed → `PassesAuthoritative`; (c) publication drains inside the window → next turn decides `Lsv1SelectedRoot` and the
  body embeds the root. Final: one body, format pinned once.
- **Outcomes at the boundary.** `AwaitSettled` = waiting (request survives, stage unchanged); `PassesAuthoritative` =
  accepted-without-root (now recorded by slice 2); `Lsv1SelectedRoot` = accepted-with-root. No new enum.
- **Controllable interleavings.** Native: the mocked clock (`nativeMillisSet` / `nativeMillisAdvance`) drives the
  window; `requestCommittedPassesRepublish` / `drainPendingSnapshotPublish` (production) create and settle the owed
  publication. No test-only production seam added.
- **Independent invariants.** The pinned body format is read back through `probeLoopSnapshotBodyFormat` (an
  independent reader, not the writer's own decision). Deferral must leave the temp file closed
  (`currentWorkspaceSave.loopFileOpen == 0`) so no SD handle survives the wait.
- **Verification recipe.** `pio test -e native -f test_storage_loop_io` (new
  `test_dec74_a_publication_owed_for_the_current_state_is_waited_for_within_its_window`), plus the existing
  `test_loop_persist_body_decision_awaits_settlement_within_its_budget` and
  `test_write_loop_persisted_requires_a_settled_published_snapshot` unchanged. Then
  `pio test -e native -f test_storage_drain_restore`. Build: `pio run -e teensy41-capture-serial`, RAM1 compared
  against the recorded `96,192` variables / `1,560` padding.

## G2 execution packet — slice 2: a rootless body is re-persisted once its root settles

- **Scope / dependency closure.** `currentSetLoopSlotRootRewritePending` (`include/StorageManagerInternal.h`,
  `src/StorageManager/Internal.cpp`), the record/clear at slot finalize (`src/StorageManager/DeferredSaveJobStages.cpp`),
  `StorageManager::serviceRootlessSlotRepersist` (`src/StorageManager/PersistenceAdmit.cpp`, declared in
  `include/StorageManager.h`), and its call in `src/main.cpp` beside `serviceDeferredPersistenceTurn`. Non-goals: the
  publisher, the loader, the playback lane, boot restore. Commit boundary: record + owner turn + its fixture.
- **Transition oracle.** Initial: slot with committed passes, no current root, `rootRewritePending=false`. Sequence:
  (a) production save writes the slot → body reads back legacy (passes-authoritative), flag `true`, no second save
  queued; (b) `serviceRootlessSlotRepersist` with no settled root → no admission, flag stays `true`; (c) the
  publication drains → `serviceRootlessSlotRepersist` → one `LoopPersist` admission for exactly that slot, flag
  `false`, one `PERS,request`; (d) the re-written body reads back LSV1 and the reloaded slot reports a current root.
  Final: two bodies written, the last one root-bearing, no further admissions.
- **Outcomes at the boundary.** Not-settleable = record retained (waiting); settleable = admitted exactly once (the
  flag is cleared before the admission); slot without committed passes = record dropped. `false`/`void` do not conceal
  the outcome: the flag and the queue depth are observed directly.
- **Controllable interleavings.** `test_storage_drain_restore` already drives the production save worker to idle and
  reloads through the production loader; the publication is stepped with the production helpers. A capture-active case
  must show the admission is parked, not lost (the existing deferred save's own park rule).
- **Independent invariants.** Body format read back through the loader (`Applied` + a current root) rather than the
  writer's decision; exactly-once admission (queue depth returns to zero and the flag stays cleared across further
  turns); slot identity preserved (`loopId`, length, note content unchanged).
- **Verification recipe.** `pio test -e native -f test_storage_drain_restore` (new
  `test_a_rootless_saved_body_is_rewritten_with_its_root_once_the_publication_settles`), then full
  `pio test -e native`, then the build + RAM1 comparison. Device leg (optional, owner-authorized): record/stop, power
  cycle, confirm the slot restores with `root_rev` non-zero and plays without a new take.

## Non-goals and failure behaviour

- A publication that never settles is not repaired here: after the window the body is written passes-authoritative and
  the record stays set; nothing re-requests the publication (that is DEC-192's territory).
- A body written passes-authoritative because the root is *uncovered with nothing owed* never re-arms.
- No SD write at boot: the flag is memory-resident and is only set by a write this boot.

## Validation

Native only for the behaviour; build for RAM1. No device claim is made by this change (HITL evidence would be a
boot-time `root_rev` read on an owner-authorized image).
