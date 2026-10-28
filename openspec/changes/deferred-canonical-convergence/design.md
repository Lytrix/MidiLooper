## Context

See proposal.md — Why. The concrete defect: a destination-Playing overdub stop admitted before its predecessor Record-return publication has landed pins `job.causalBase` against a stale `{playbackRevision, snapshotIdentity}`. When the Record return later commits the record pass and publishes its snapshot, `causalBaseCurrent` flips false mid-build, and `stepCanonicalPrefix` restarts (`prefix_restart`), which cascades into `record_return_superseded` → `fill_order_denied`. DEC-171 already converts that into a fast escape (raw commit, ~76–100 grains), but the canonical/companion representation is skipped, not recovered.

The existing machinery we reuse, unchanged:

- `stepCanonicalBase` / `stepCanonicalPrefix` / `stepCanonicalWindow` and the `viewBuild` / `frozenCompanionPasses` builders already produce the full canonical/companion result (proven by the healthy "overdub 3" route in `session_20261027_214639.log`).
- `causalBase` / `causalBaseCurrent` (`TrackOverdubLifecycle.cpp` ~2897) already names the source identity and already drives the raw-only escape at `CompanionBridge`.
- The raw commit path (`retireSupersededPreparation` → `AtomicTransfer` → `causal_base_superseded`) already preserves the immutable committed representation and lets the FIFO drain.

What is missing is only a place to *park* the derivation after the raw retire — the current lifecycle skips, it does not defer.

## Goals / Non-Goals

**Goals:**
- Capture-stop may retire raw the moment its immutable committed representation has transferred; canonical convergence is independent of interaction completion.
- One slot-level, identity-qualified, coalescing deferred obligation that rebuilds the canonical/companion representation through the latest committed generation once an authoritative source exists.
- Keep the healthy `causalBaseCurrent == true` inline path byte-for-byte equivalent (no behavior change there).

**Non-Goals:**
- Not moving every canonical build off the stop path — only the post-supersession case.
- Not changing the raw committed representation, the pass/undo/companion data model, or the `causalBase` admission logic.
- Not a general background-builder queue; the obligation is scoped to deferred canonical convergence.

## Decisions

### 1. Obligation lives on `LoopPlaybackRuntime` (one per slot), not a separate allocation

The obligation drives the runtime's own canonical builders (`canonicalPassInputs`, `frozenCompanionPasses`, `viewBuild`), so it belongs with that machinery. A separate per-slot allocation would add a lifetime, a failure mode, a pointer indirection, and extmem admission/accounting for no benefit. The runtime is already externally allocated, so the cost is a measured `sizeof` growth (see Decision 4), not RAM1.

### 2. Key the obligation to `committedPassRevision()`, not `causalBase`

The obligation keys to the target committed generation the slot owes a representation through. `committedPassRevision()` is the correct identity: it is `playbackRevision`, and every `++playbackRevision` site is a committed-state inventory change (pass commit, wrap commit, edit-pass save/delete, pending-change seal, pass disable/reclaim, prepared-companion swap — verified across `LoopCapture.cpp`, `LoopEditPasses.cpp`, `LoopPendingNoteChange.cpp`, `LoopPreparedCompanionHistory.cpp`), each accompanied by `notifyCommittedContentChanged()` (or, for the companion-history path, scalar invalidation). Its advancement exactly means "the canonical/companion representation is no longer complete for the committed state." No non-convergence event bumps it, so keying to it cannot cause needless discard/rebuild loops.

`causalBase` keeps its narrow role on the stop job: `causalBaseCurrent == false` is a **terminal verdict on that binding** (inline derivation is invalid), never a future-ready key. The future builder re-resolves a fresh authority.

**Self-invalidation audit (prerequisite, resolved before task 2.1):** `committedPassRevision()` is advanced by `publishPreparedCompanionHistory` (the prepared-companion swap commits edit passes, `LoopPreparedCompanionHistory.cpp`). The deferred convergence must therefore be scoped to the **derived** canonical window (`stepCanonicalBase`/`stepCanonicalWindow`), whose adoption (`view.published = true`) does not bump `playbackRevision`; the companion-history *commit* is a committed-state change (note-edit-session owned), not part of the derived artifact. Task 1.8 enumerates every `++playbackRevision` site and proves the convergence publication/adoption cannot self-invalidate its own target; if the audit finds the adoption *does* advance the generation, the target identity is re-scoped (e.g. to the raw-pass commit revision) rather than using `committedPassRevision()` directly.

### 3. State machine — Owed / Building / Ready, coalescing forward

```
Idle
  ↓ raw stop commits w/o canonical artifact
Owed(targetCommittedGeneration, coverage{loopLengthTicks})
  ↓ publishedSnapshotIsCurrent() && committedPassRevision() >= targetCommittedGeneration
Building(buildSourceIdentity, targetCommittedGeneration)
  ↓ candidate complete && buildSourceIdentity still authoritative
Ready
  ↓ atomic adoption/publication
Idle
```

- **Coalescing (universal):** while `Owed`/`Building`, any `committedPassRevision() > targetCommittedGeneration` sets `targetCommittedGeneration = committedPassRevision()`, discards any in-flight candidate, returns to `Owed`. Never a second obligation. This covers raw overdub commits, undo/redo, edits, wraps, etc. with one rule.
- **Identity re-resolution:** if `committedPassRevision()` or `publishedSnapshotIdentity()` changes during `Building`, discard the candidate and re-resolve. No stale-prefix/new-suffix mixing.
- **Denied vs Pending:** a terminal `Denied` (or invalidated source) discards the candidate and returns to `Owed`; an ordinary non-terminal `Pending`, even temporarily unchanged, keeps `Building`. No arbitrary grain timeout is introduced here (a no-progress watchdog, if added later, would be the only `Building → Owed` demotion).

### 4. Grow `LoopPlaybackRuntime`, re-base the I5 pin explicitly

The obligation state (state enum + `targetCommittedGeneration` + `buildSourceIdentity` + coverage) is added to `LoopPlaybackRuntime`. The `sizeof` static_asserts (`7768` target / `9896` host) are re-based to the compiler-measured value (padding may make the delta 8, 16, or more — measured before finalizing). The rebase is recorded as an explicit architectural cost: "`LoopPlaybackRuntime` grows because it now owns one bounded deferred-convergence obligation per slot; RAM1 unchanged; no new event buffer or queue."

### 5. Serviced by a slot-level owner turn, never the stop grain

`Owed`/`Building`/`Ready` are advanced by a slot-level deferred-owner turn (routed alongside the existing VisualCache/Snapshot owners in `TrackDeferredMaintenance`), never inside `processDeferredOverdubStopGrain`. This is what makes the FIFO-liveness guarantee structural rather than incidental: the stop grain returns after the raw commit and never waits on convergence.

**Standing demand, not edge-triggered:** `Owed` itself makes the slot eligible for service. Snapshot/root publication may enqueue or nudge the owner, but correctness does not depend on catching that transition — a later owner turn observes an already-ready authority and begins the build. This avoids the "obligation exists but nothing bootstraps the prerequisite" failure class.

### 6. The inline gate — two paths

The `causalBaseCurrent` check gates **both** the before-bind and the mid-build cases:

- **Before bind:** at the top of the `CanonicalPrefix` branch, `causalBaseCurrent == false` → never bind the stale source; raw commit (existing `causal_base_superseded` route); mark the slot `Owed(committedPassRevision())`; retire the stop job.
- **During build:** the measured race is a build that *started* while `causalBaseCurrent == true` and then had its authority superseded. The existing `stepCanonicalPrefix` restart path and DEC-171's terminal-denial escape already detect this; the change is that the escape, and any `stepCanonicalBase` terminal `Denied` caused by causal-authority change, must also commit raw **and mark `Owed`**, not merely commit raw. This is the path the device reproduction exercises.

### 7. Exclusive canonical-builder ownership — inline stop wins

The deferred obligation shares `canonicalPassInputs`/`frozenCompanionPasses`/`viewBuild`/`stepCanonicalBase` with the inline stop path, so two owners must never drive them concurrently.

- **Inline priority:** a capture stop never waits for optional convergence. If deferred convergence is `Building` and a new inline stop needs the builder, the deferred candidate is discarded, the obligation returns to `Owed` (coalescing to the latest generation), and the inline stop owns the builder.
- **Deferred defers:** if an inline canonical build is active, a deferred owner turn does nothing and the obligation remains `Owed`.

**Inline satisfaction:** a successfully adopted inline canonical result from an authoritative source whose committed revision covers `targetCommittedGeneration` clears the obligation through that generation — no duplicate later publication. This is exactly the measured device sequence (OD1/OD2 raw, OD3 healthy inline) and avoids rebuilding state OD3 already produced. If another commit raced ahead, coalesce and remain `Owed`.

### 8. Slot replacement cancels the obligation

The obligation belongs to the current slot/loop incarnation, not merely "revision N". Slot/loop clear, reset, replacement, or load cancels any outstanding obligation and invalidates its candidate. This is already guaranteed by the existing per-slot runtime lifecycle: `LoopPlaybackRuntime::reset()` (and the destructor path in `TrackPlaybackRuntime`) discards the canonical machinery, so the obligation state is reset alongside it — no new identity field is needed; the obligation discard is folded into `reset()` and proven by a fixture rather than tracked by an extra generation.

## Risks / Trade-offs

- **[I5 pin grows]** → measured, re-based in the same commit, RAM1 budget unchanged (runtime is extmem).
- **[Coalescing loses an intermediate generation's separate artifact]** → intended: the newest committed generation subsumes older owed work, matching the one-authoritative-presentation-per-slot model. The intermediate raw content is already committed and playable.
- **[Convergence can fail forever]** → the obligation stays `Owed`; committed raw content remains authoritative/playable (already guaranteed by `loop-commit-semantics` "derived is disposable"). The obligation must never re-block interaction — asserted by fixture 4/6.
- **[Autonomous wakeup missing]** → the deferred owner must be driven by the same signal that publishes the snapshot (snapshot-publication / root-current transition), not by a user interaction. Fixture 7 covers this.
