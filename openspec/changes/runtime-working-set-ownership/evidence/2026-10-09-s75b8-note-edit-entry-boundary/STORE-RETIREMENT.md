# S7.5b.8b — store-wrapper retirement prerequisite

## Frozen execution packet (before production edits)

Scope: existing `LoopEventVectorCache::stepClear` component primitive, no resident cursor/token or new owner. Authorized bounded departure-cleanup dependency of S7.5b.8b — bounded entry/focus preparation. Baseline HEAD `067be7f3d7bf0500a49da589c02d78c0904f9cda`, branch `feature/unified-rolling-playback-path`, substantial unrelated dirty work preserved.

Existing extension: `LoopEventStore::releaseCaptureChunkGrain`; release reference and persistence `onChunkFreed` update fixed indexed scalars. Decision-log search: retirement / LoopEventStore / bounded; existing ownership and bounded reclamation remain unchanged, no formal reassessment trigger. See authority ExecutionBounds and active design.

Contract: destructive synchronous component grant, not async acceptance. Caller must exclude mutable borrowers and qualify departure/session/focus before every call. Return `Progress` after any retirement unit; return `Complete` only on a later observational empty turn. One call: drop flat-cache ownership WITHOUT syncing dirty values; OR replace a shared canonical header with a fixed empty header (other owner remains unchanged); OR retire exactly one unique canonical chunk via the existing grain; OR reset a drained header carrying directory/bar-index capacity. Never COW clone, flatten or enumerate content. Preserve nonnull getters. No per-wrapper resident state, no source/publication/main/caller integration, no instrumentation or device changes.

`MidiEvent`, chunk IDs and bar indices are trivial scalar payloads in target/native vector implementations: vector-buffer destruction does not traverse elements with observable destructors. Allocation/deallocation latency and failure remain under existing allocator policy; no allocation-free or portable arbitrary-EventVec claim. Stamped Capture stores reject recycled pool payload through existing lifetime semantics. Legacy zero-stamped stores retain existing live-pool precondition; this slice does not broaden their reset safety.

## Transition fixtures fixed before behavior

- Unique: append513 rows (256-row chunks), materialize bar index. Withheld calls preserve513/3chunks. Grants remove last chunk only:512/2,256/1,0/0; every call Progress, freed IDs/refcounts exact. Next resets retained empty header/capacities, Progress; following Complete, repeated Complete no mutation. No flat cache appears.
- Shared: two wrapper copies plus external snapshot of513 rows. First wrapper grant detaches only one header reference, Progress; source identity/content/chunk refs unchanged, no pool allocation/clone. Following Complete. Remaining source is explicitly drained after observation.
- Dirty: canonical513, cached extra row999. First grant discards cache/dirty only, canonical513/3 and refcounts unchanged; next removes one canonical chunk. Extra row is never synced.
- Empty: Complete immediately with same header and no materialization. Empty dirty cache gets its own Progress then Complete. Session-vector alias exercised.
- Reset: reserveCaptureTail stamps pool lifetime, append513; reset/reinitialize pool and append a new wrapper row. Old wrapper drains its three stale directory entries in three Progress turns without accessing/freeing recycled new chunk; header retirement then Complete. New row/refcount unchanged.

Fixtures include production header/source through existing `test_loop_event_store` harness. New API is absent before link: missing API build failure is NOT behavioral RED. Isolated controls: drain-all instead of one chunk; mutate shared source instead of detach; synchronize dirty cache before discard. Restore source byte-identically before review/checks.

## Verification recipe

`pio test -e native -f test_loop_event_store`; isolated controls same suite. One focused independent review. `pio test -e native`; `pio run -e teensy41-capture-serial`; `openspec validate runtime-working-set-ownership --strict`; `git diff --check`. Logs beside this packet. Full S7.5b.8b — bounded entry/focus preparation stays unchecked. Hardware/feedback/parent withheld-owner oracle remain open; no upload, device/SD/stimulus/marker/timing claim.

## Completion record

- Five new fixtures plus47 prior store cases pass **52/52**; restored combined store+entry+undo **139/139**. Logs: `store-focused.log`, `store-final-focused.log`.
- Controls independently discriminate: drain-all3 failures, shared-source mutation1, dirty-cache sync2 (logs `store-control-drain.log`, `store-control-shared.log`, `store-control-sync.log`). Wrapper SHA-256 `e29180497d9d3503054233c366262f8f1bf49829c0c0a84a325050c3e1e21cf3`, each source restoration checked with `cmp`; a misplaced temporary control line was corrected before execution, not counted as evidence.
- Full native **2,957 total /2,950 passed /7 skipped /0 failed**,112 PASSED suites (`store-full-native.log`). Capture SUCCESS: RAM1 variables96,192/code390,552/padding2,664, locals34,880; RAM2 163,168, EXTRAM9,312 (`store-capture-build.log`), unchanged from preceding prerequisite, no budget rebase. Unused template worker adds no linked firmware behavior yet.
- One focused independent review APPROVE, no findings. Reviewer inspected packet, implementation, dependencies and registrations; reported command results were supplied evidence, not independently executed. Approval is component-only.
- Parent flagged entry still55 passed/1 withheld-owner assertion failure plus harness ERRORED, exit1 (`store-parent-oracle.log`); ordinary native artifacts restored afterward. Missing-API baseline is not behavioral RED.
- Strict OpenSpec and whitespace checks recorded at closeout. No upload/device/SD/stimulus/marker/instrumentation or duration claim. Full task, feedback and hardware remain unchecked. Remaining work: composed current/focus/derived-cache cleanup, source admission/cancellation/graph retirement, caller intents and qualified atomic session/FSM/main grants.
- Session closeout: runtime/current-work, deliverable, task/design, guide and bounds updated. Existing owner/allocator/pool preconditions retained; no new architecture decision or policy exclusion, no decision-log append required. Rejected implementation shortcuts: COW clone would enumerate content; syncing dirty cache would rebuild canonical content; a detached retirement owner would duplicate ownership.
- Closeout checks performed: `openspec validate runtime-working-set-ownership --strict` valid; `git diff --check` clean; final checkpoint `cmp` equal. Parent harness signal was SIGHUP, not a clean failure-only completion. No new serial-observable path is enabled, so device/HITL is NOT RUN for this unused component prerequisite.
