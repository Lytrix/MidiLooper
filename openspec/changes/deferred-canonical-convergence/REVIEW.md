# deferred-canonical-convergence — implementation record

Executed 2026-10-27 on `feature/unified-rolling-playback-path` (working branch; the tree also carried the
uncommitted DEC-165…DEC-171 slices, which this change did not modify).

## 2.0 — Gate: self-invalidation audit (`committedPassRevision()` is the right target identity)

Enumerated every `++playbackRevision` site in `src/` (15 sites; `Loop::bumpPlaybackRevisionWithoutNotifyForTest`
is test-only):

| Site | Committed-state change |
|---|---|
| `LoopCapture.cpp` `reclaimDisabledCapturePass` (record + overdub) | pass disable/reclaim |
| `LoopCapture.cpp` `setCapturePassState` (record + overdub) | undo/redo pass state |
| `LoopCapture.cpp` `shiftCommittedEditPasses` | edit-pass geometry move |
| `LoopCapture.cpp` `commitPendingCapturePass` | raw pass commit |
| `LoopCapture.cpp` `seedRecordPassFromStore` | record pass seeded |
| `LoopCapture.cpp` `transferPreparedCapturePass` | prepared pass commit (the stop's own commit) |
| `LoopEditPasses.cpp` `adoptPersistedSnapshot` / `restorePassesSnapshot` | load/replacement |
| `LoopEditPasses.cpp` `saveNoteEditPass` (non-deferred) | edit-pass save |
| `LoopEditPasses.cpp` `disableEditPasses` / `enableEditPasses` | edit-pass state |
| `LoopPendingNoteChange.cpp` pending-change seal | companion/pending-change commit |
| `LoopPreparedCompanionHistory.cpp` `publishPreparedCompanionHistory` | prepared-companion swap |

Each is an inventory change (an accompanied `notifyCommittedContentChanged()` or, for the prepared-companion
swap, scalar invalidation). **None is reached by the deferred convergence's own work.** The deferred build
calls only `stepCanonicalBase` → `stepCanonicalPrefix` (pass inputs) → `stepCanonicalWindow`, whose adoption
path is:

```cpp
if (!canonicalWindowIsBridgeBase && canonicalWindowIndex != set->lifecycle.ready &&
    !set->publishCompletedBuildAsReady(PlaybackViewHandoffMode::Preparation)) return Pending;
view.published = true;
```

`RuntimePlaybackViewLifecycle::publishCompletedBuildAsReady` only moves `ready`/`build` role indices, and
`view.published` is a boolean on the derived view. Neither touches `Loop::playbackRevision` (nor
`canonicalStorageGeneration`). The companion-history *commit* that does bump the revision is note-edit-session
owned and is not part of the derived artifact.

**Verdict: GREEN.** `Loop::committedPassRevision()` (currently an alias of `playbackRevision`) is the target
identity, and the deferred adoption cannot advance its own target. No re-scoping was needed.

## 2.2 — `sizeof(LoopPlaybackRuntime)` measurement (I5 pin re-base)

Compiler-measured with the size probe (not estimated):

- target ABI (`teensy41-capture-serial`): **7,768 → 7,808** (+40 B)
- host ABI (`native`): **9,896 → 9,944** (+48 B)

The delta is the packed 2+2 state byte, the target committed generation, the loop coverage and the bound
`PlaybackViewSourceIdentity` (24 B on the target ABI's 4-byte pointers, 32 B on the host's 8-byte pointers).
The runtime is `extmem_malloc`-allocated per slot, so RAM1 is unchanged: build `SUCCESS` at
`variables 96,192 / padding 2,744` (floor 840).

## 1.x — RED-first ordering, stated honestly

The obligation state and its read-only test access (2.1) were added before the fixtures, because a fixture
cannot observe an obligation that has no state. The state is **inert scaffolding**: nothing marks or services
it until 3.2/3.3/4.1–4.9. Each fixture therefore still fails on the unwired tree for its own behavioral
reason, which is what the mutation table below proves (M1/M3/M4/M5/M6/M7/M8 disable the wiring, not the
scaffolding, and each kills the fixture that names that behavior).

## 5.1 — Mutation red-proof (each restored byte-identically, sha256 verified)

Anchors: `TrackOverdubLifecycle.cpp a793b0be…`, `TrackDeferredMaintenance.cpp 0b7f504d…`,
`TrackPlaybackRuntime.cpp 1ce8eb7e…` (restored hashes match byte-for-byte after every run).

| # | Mutation | Fixtures that failed |
|---|---|---|
| M1 | `causalBaseCurrent` supersession gate disabled | **11** (1.1, 1.2, 1.2b, 1.3, 1.5, 1.5b, 1.6, 1.7, 1.8, 1.9, 1.10) |
| M2 | terminal-denial escape's `Owed` mark removed | `test_denied_base_escapes_immediately_without_the_stall_bound` |
| M3 | deferred owner turn disabled | **12** (every deferred-convergence fixture; 1.4 fails on the coalesced-target invariant) |
| M4 | inline preemption removed | 1.8 `test_deferred_convergence_yields_the_builder_to_an_inline_stop` |
| M5 | reset cancellation removed | 1.10 `test_deferred_convergence_is_cancelled_by_slot_reset` |
| M6 | inline satisfaction removed | 1.9 `test_deferred_convergence_is_satisfied_by_the_inline_build` |
| M7 | coalescing removed | **8** (including 1.3 and 1.5) |
| M8 | mid-build identity re-resolution removed | 1.5b `test_deferred_convergence_discards_a_candidate_on_a_source_identity_change` |

## Verification

- `pio test -e native -f test_shared_capture_finalization`: **128/128** (12 new fixtures + the obligation
  assertion added to the terminal-denial escape control)
- `pio test -e native` (full): **3,540 cases / 7 skipped / 3,533 succeeded / 0 failed**
- `pio run -e teensy41-capture-serial`: **SUCCESS**, RAM1 `variables 96,192` / `padding 2,744` (floor 840)

## Three corrections the suite forced (recorded, not smoothed over)

### The gate must be phase-wide, not `CanonicalPrefix`-only
The first cut gated only at `CanonicalPrefix`. A second obligation admitted while the builder still held a
**completed** base reused it instantly, and the authority then moved after the phase had left `CanonicalPrefix`
— the job reached `CanonicalTail` with a Ready base for a stale source and spun there forever (DEC-070's bound
is retargeted-only). The gate now applies to every base-dependent phase (`CanonicalPrefix`, `CanonicalOrder`,
`CanonicalRewrite`, `AudibleOrder`, `CanonicalTail`, `PrepareTransfer`), one transition per grain, with the
established abandoned-base route doing the retirement. The late companion phases (`CompanionHistory`,
`UndoCollection`, `CompanionBridge`) already have their own proven superseded retirement
(`CompanionBridge` → `AtomicTransfer` → `causal_base_superseded`), so there the gate records the obligation and
performs no phase surgery.

### Admission must not require an idle builder
`canonicalPassInputs` legitimately keeps a completed build (the inline path reuses it). Requiring `idle()` at
deferred admission would deadlock every obligation after the first completed one; the builder's own admission
reuses a matching source and restarts (identity-mismatched) for a newer one. Fixture 1.2b pins the second
obligation.

### Two existing contract fixtures needed a real correction, not a re-encode

`admitOverdubStartWorkingSet` computes the prepared-companion capacity, and the old superseded route restored
that horizon after the companion preparation had begun. The new cause-based escape can retire **before** any
preparation begins, so `restoreSupersededPreparedCompanionHistory` answers "nothing to restore" (its documented
refusal for a horizon that is already `Prewarmed`) and the caller's fallback discarded a prewarmed horizon,
breaking the next interval's "no STOPPED-only re-prewarm" contract in
`test_deferred_stop_records_and_pins_its_causal_predecessor_source` and
`test_superseded_predecessor_retires_without_touching_a_live_successor`. The fix treats "already prewarmed" as
the desired end state (skip the discard); the fixtures' assertions are unchanged.

## Citation conflict recorded (doc vs code)

The proposal/design cite "DEC-171's escape-on-denied" for the terminal-denial fast escape, but
`docs/DECISION_LOG.md` **DEC-171** is the boot loop-slot restore retry fix; the escape's decision is not logged
under its own number (the code comments in `TrackOverdubLifecycle.cpp` say `DEC-171`). This change's DEC entry
describes the behavior by name (`base_build_done,outcome=denied` / terminal-denial escape) rather than
re-litigating the number, and DEC-172 is the ownership-boundary decision this change actually introduces.

## Not claimed

No device/HITL run: the change is observable over serial only as timing, and no new stimulus/marker was added.
The device acceptance of the content-fidelity outcome (early overdubs recovering their canonical/companion
representation) remains open behind the existing upload gate.

## Device capture 2026-10-27 (`captures/session_20261027_231625.log`) — the change did NOT fix the symptom

The owner flashed the first implementation (`sha256 a22375e7…`) and captured a rapid-overdub session. Result:
**no user-visible change**, and the log explains exactly why.

### What the log shows

- `84.491` — the new supersession gate fires once: `Deferred stop stalled: causal base superseded; stop
  continues without it` → raw commit.
- `89.078` onward — **every later stop** logs `canonical base unavailable; companion path skipped` and
  `Overdub stop: companion path skipped (no canonical base); raw pass only`. Ten `atomic_transfer,…,
  causal_base_superseded` commits; no `base_build_done,…,outcome=complete` for any of them. The ring keeps
  draining and no start is refused by it — but no stop after the first derives anything.
- `129.685` — one overdub start refused by the *session-exit publication obligation* (`state=populated`), a
  different, pre-existing family.
- 39 `win_frozen_readiness,…,build_drop` and 0 `fill_order_denied`/`base_build_done,…,outcome=denied` in this
  session: the later jobs never even attempted a base.

### Defect 1 (introduced by this change, now fixed): abandonment was Track-level

`retargetStallPhase_` is a **Track** field; the DEC-165 comment calls it "the per-job stall phase", but it is
not per job. The gate wrote `kCanonicalBaseAbandonedPhase` there on the first supersession, and every later
job — which starts at `CanonicalOrder` and never passes a bounded phase that would clear it — then took the
`CanonicalTail` cause escape: skip the base, skip the companion, commit raw. **One supersession poisoned the
whole session.** Fixed by moving abandonment to `PendingStopAdmission::canonicalBaseAbandoned` (per job,
reset at admission/retirement) and using it in `stopCompanionPathIsUnreachable` and the `AtomicTransfer`
condition. Fixture 1.11 pins the cross-job guarantee (a healthy stop after an abandoned one still derives its
companion).

### Defect 2 (this change's premise was wrong): supersession does not stale the base

`stepCanonicalPrefix` resolves the **live** source (`baseSource` = live `playbackRevision`/`playbackGeneration`/
`publishedSnapshotIdentity()`); the recorded `causalBase` is only the *supersession reference* that decides
whether a successor **window** may be published. Abandoning the base on `!causalBaseCurrent` therefore threw
away work that was still valid. The phase-wide gate is removed; supersession keeps the established
`CompanionBridge` retirement (skip only the bridge). The obligation is now recorded **after the raw commit** in
the superseded transfer branch (final committed generation), never as a speculative pre-commit target.

### Defect 3 (pre-existing, exposed by the fix): `reset()` is not a slot-incarnation boundary

`LoopPlaybackRuntime::reset()` is also the ordinary playback-state invalidation
(`resetPlaybackStateForSlot → invalidatePlaybackMergedMidiEvents → releaseEligiblePlaybackRuntimeSlots`). Task
4.9's "fold the discard into `reset()`" therefore erased the obligation that the stop had just recorded, before
any owner turn saw it. Cancellation now lives in the owner turn and is keyed to a restarted timeline
(`committedPassRevision() < target`, which no legitimate generation advance can produce), while a playback reset
keeps the obligation (fixture 1.10 asserts both sides).

### Defect 4 (the design gap this capture proves): the deferred artifact is the wrong one

Even with defects 1–3 fixed, the deferred build re-derives the **canonical bridge-base window**
(`stepCanonicalBase`) — a derived view used as the successor bridge's base. It does **not** rebuild the
**companion / note-edit representation** (`Loop::preparedCompanionHistory_` → `editPasses`) that the display
resolves the overdubbed notes from (DEC-070: "the overdubbed notes stayed audible but vanished from the display,
which resolves them from the note-edit representation"). The device's raw-only stops lose exactly that
representation, so a window rebuild cannot produce the user-visible fix. This is the design's own fallback
condition from task 2.0: the companion-history commit *does* advance `committedPassRevision()`, so the target
must be re-scoped (raw-pass commit revision) and the obligation must own the **companion** derivation, not only
the window.

**Status: the change is not complete and must not be archived.** The corrected mechanism (per-job abandonment,
post-commit obligation, reset semantics) is verified natively; the capability's requirement
("build the canonical/companion representation through the target generation") is not met for the companion.
Tasks 6.1–6.3 record the follow-up.

## Fix 2026-10-27 (after the capture): a window-only denial keeps the companion path

The capture's later jobs were already poisoned by the Track-level latch, but the *earlier* device evidence
(`214639`: `base_build_done,where=window,reason=fill_order_denied`) shows the genuinely measured failure shape:
**the prefix is ready and only the canonical window is denied**. The window is the successor *bridge's* base;
the tail extends the prefix and the companion/note-edit representation the display reads is derived there. The
DEC-171 fast escape treated a window denial like a prefix denial and abandoned the base, which is what lost the
companion.

**Landed:** the terminal-denial escape now splits by latch — prefix `Denied` → abandon the base (raw + deferred
obligation, unchanged); window `Denied` with a **ready prefix** → record
`PendingStopAdmission::canonicalBridgeUnavailable`, drop only the denied window, and keep the companion path.
`CompanionBridge` retires the ranked estimate for that cause too, `AtomicTransfer` skips the bridge refresh, and
`TrackPreparedStopCommit::advanceAtomicTransferWithoutBridge[WithUndo]` publishes the pass **and its prepared
companion history** with no bridge (the shared helper is also the Stopped commit's, now parameterized by expected
destination). Fixture 1.12 pins the split; mutation M9 (window denial treated as abandonment) reds exactly it.

What this means for the change's scope: the capability's user-visible requirement is met **for the measured
device shape** without rebuilding the companion after the fact. The deferred obligation now covers genuine
**prefix** denials, where the tail cannot run and a post-hoc companion rebuild would still be needed; that
remains open (tasks 6.1b/6.2) and its target would have to be re-scoped to the raw-pass commit revision.

### Verification after the split

- `test_shared_capture_finalization` **130/130** (1.12 added)
- full native **3,542 cases / 7 skipped / 3,535 succeeded / 0 failed**
- `teensy41-capture-serial` **SUCCESS** at RAM1 `variables 96,192 / padding 2,744` (floor 840)
- 9 mutations red-proved, each restored byte-identically: post-commit mark (13), owner turn (10), inline
  preemption, inline satisfaction, incarnation cancel, coalescing, identity re-resolution, per-job abandonment
  (4), window-denial split (1)
- image `sha256 9e72497a…`, no source newer than the HEX

## Fix 2026-10-28 — correction 3: owner-supersession is the display-loss cause, not the bridge

The owner capture on the correction-2 image (`captures/session_20261027_235228.log`) confirms base builds now
complete — but every atomic transfer committed `causal_base_superseded` raw-only (9 events), discarding the
*completed* companion derivation. Root cause: a record-return pass commit moves `playbackRevision` between the
companion derivation reaching `Phase::Ready` and the commit, and every commit-time Ready/validity verifier
(`preparedCompanionHistoryReady`, `preparedCompanionCaptureVerified`, `preparedCompanionHistoryMatches`,
`canCommitPreparedCaptureAndCompanions`) dies on that bump because
`RuntimeCanonicalPassInputs::valid()` value-pins `current_->playbackRevision == expected_.playbackRevision`.

**Landed:** a supersession-tolerant check — `supersessionReady()` plus
`stateAllowsBinding()`/`includesFrozenOverdubBinding()`/`frozenCaptureBinding()` (skip only the
playbackRevision equality; generation + snapshotIdentity retained) — and a lossless publish route
`commitPreparedCaptureAndCompanionsSupersession` replacing `matchesCanonicalOwner` with a **store-unchanged
proof** (`historyCount == passes.editPasses.size()` && `storageGeneration == canonicalStorageGeneration`),
guarded by `PendingStopAdmission::supersededCompanionExchange`. The discard route stays as fallback and is
the only route that marks the obligation.

**Fixture finding (corrects the earlier row-growth framing):** the healthy route's single edit row in fixture
1.11B comes from the **bridge rewrite** lane, which the no-bridge route intentionally retires; a clean
single-note capture legitimately derives ZERO companion rows from the pending-changes lane (both fixtures
show an empty change lane at stop). So fixture 1.13 pins the **route**, not row growth.

**Verification:** fixture 1.13 green (routeSeen + deferredIdle + companion prewarm restored); mutation M10
(route disabled) reds exactly it, restore byte-identical `d98d102c…`; full native **3,543 / 7 skipped /
3,536 succeeded / 0 failed**; build **SUCCESS** at RAM1 `variables 96,192 / code 390,568 + padding 2,648`.
HEX `sha256 7f55b52b…` — **flashed 2026-10-28 10:45** on owner instruction, boot proved separately (`DIAG,boot_reset,crash=0`, `BOOT,load,ok`); owner re-capture pending. Tasks 6.1b/6.2 (genuine prefix-denied
raw retire) remain open; the change is still not archivable.
