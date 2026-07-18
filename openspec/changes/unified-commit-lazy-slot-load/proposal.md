## Why

Cold boot restores every SD loop slot before the instrument is interactive (~10.5 s for 26 slots in `session_20260718_020628.log`). Scheduling drains are SD-bound; batch `ioRead` did not fix wall time. Separately, runtime vocabulary still says **Publish** for what the architecture now treats as **Commit** — the operation that makes immutable loop state runtime-visible.

This change freezes the agreed Commit-centered model and delivers audible-first boot plus on-demand slot hydration without inventing a parallel load subsystem.

Architecture authority (frozen): [`docs/plans/unified_publish_pipeline_deferred_lazy_loading_architecture.md`](../../../docs/plans/unified_publish_pipeline_deferred_lazy_loading_architecture.md) and [review resolutions](../../../docs/plans/unified_publish_pipeline_review_resolutions_refinement.md).

Brownfield: [`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md), [`openspec/specs/timeline-passes`](../../specs/timeline-passes/spec.md), [`openspec/specs/multi-loop-slots`](../../specs/multi-loop-slots/spec.md), [`docs/Guides/BOOT_LOAD.md`](../../../docs/Guides/BOOT_LOAD.md).

## What Changes

- **Commit semantics** — All runtime-visible loop changes occur through **Commit** (build → commit → committed state → optional derived). Producers (record, overdub, edit, load, import, paste, undo) share semantics, not necessarily `commitCapturePass()`.
- **Vocabulary rename** — Public Publish/Published identifiers for committed loop truth become Commit/Committed with action+scope names (`hasCommittedPasses`, …). No new **Flat** identifiers; no bare `…ToCommitted`.
- **Hydration lifecycle (architectural)** — `UNLOADED → HEADER_READY → COMMITTED → DERIVED_READY`. Storage of that lifecycle is implementation-flexible. **COMMITTED** is sufficient for playback, editor, and display (including piano roll from committed passes). **DERIVED_READY** is optional performance.
- **Audible-first boot** — Sync-commit audible boot set only; interactive UI at COMMITTED; do not auto-enqueue remaining SD slots.
- **On-demand deferred load** — Runtime priorities: (1) audible, (2) explicitly requested. No speculative adjacent prefetch. Load while PLAYING is deferred (MVP may queue until transport idle).
- **MVP load path** — Existing sync `loadLoopSlotFromCurrentSetSd` remains acceptable for audible boot; deferred `SlotLoadSession` / `LoadLoopJob` advance is incremental, not a prerequisite for audible-only boot.

**BREAKING:** Public C++ identifiers currently using Publish/Published for committed-pass APIs (rename pass). Serial/HITL marker strings unchanged unless a task explicitly updates them.

## Capabilities

### New Capabilities

- `loop-commit-semantics`: Commit as the architectural operation; committed vs derived; producer parity; naming rules
- `lazy-slot-hydration`: Audible-first boot, on-demand deferred slot load, hydration lifecycle, ready gate

### Modified Capabilities

- `timeline-passes`: Align “published” prose/APIs with **committed passes** / Commit vocabulary; clarify that Commit does not introduce a `PassState::Committed` label (reconcile with existing “shall not use committed as pass state label” wording)
- `multi-loop-slots`: Slot select / focus may request load of unloaded slots; boot does not require all slots COMMITTED for interactive ready
- `playback-runtime-prewarm`: Boot prewarm / ready assumptions limited to audible (or already COMMITTED) slots, not full-set restore

## Impact

- Firmware: `Loop.*`, `Track.*`, `StorageManager.*`, `SlotLoadSession.*`, `main.cpp` boot ready, `BootLoopSlotRestore.h`, display paths that gate on `hasPublishedEvents`
- Guides: `BOOT_LOAD.md`, rename cross-links in storage/loop guides
- Tests: `pio test -e native`; device gate boot CAP vs `020628`; select unloaded slot while stopped
- Open product TBD (non-blocking): exact post-rename public function names for boot sync restore / session advance; whether a dedicated load queue type is needed beyond pending restore + `processDeferredLoopSlotRestore`

## Non-goals

- v7 SD chunk index / format bump
- New `*Executor` / `*Manager` types without naming review
- Renaming `RevisionCommit`, seal/mid_pass, or USB MIDI “publish”
- Interactive load-while-PLAYING (later phase)
- Speculative adjacent slot prefetch
- Mandatory rename of historical `unified_publish_pipeline_*` plan filenames
- Evolving architecture inside OpenSpec — describe the frozen model only
