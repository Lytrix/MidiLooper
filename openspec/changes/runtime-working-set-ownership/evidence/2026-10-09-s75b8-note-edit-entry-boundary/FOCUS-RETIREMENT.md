# S7.5b.8b — bounded focus retirement prerequisite

## Frozen execution packet (before production edits)

Authorized dependency of S7.5b.8b — bounded entry/focus preparation. Existing `NoteEditFocus` owns baseline and overlap scratch maps. Extend that owner, not a manager/queue/lease. Branch `feature/unified-rolling-playback-path`, HEAD `067be7f3d7bf0500a49da589c02d78c0904f9cda`, accumulated unrelated dirty work preserved.

Production API: `beginClear()` resets only fixed focus scalars (inactive, invalid mover, default commit/last spans and moving range), retaining both maps. Destructive `stepClear()` returns true for one retired global-head map node or one EMPTY map bucket-table compaction, false for observational completion. Baseline map drains/shrinks before overlap map. Existing synchronous `clear()` shares scalar reset but retains its current whole-map behavior. No resident state/token, no asynchronous acceptance receipt: caller must exclude live use/borrowers and qualify session/focus before every grant. Partial maps must not be consumed as edit authority. Existing current-state and undo retirement pattern reused; target GCC11.3.1 empty-map destructor still scans bucket_count, so shrink empty maps above2 buckets with max_load_factor(1), rehash(0). Global-begin erase has before-begin predecessor in pinned target/native layouts; no collision-chain walk. Compaction may perform fixed small allocation; allocator duration/failure, portable STL complexity and device timing remain unqualified.

Authority/history: decision-log search on focus/current-state ownership and bounded reclamation; DEC-029 — current-state ownership and DEC-030 — sticky overlap participation remain unchanged. No overlap-resolve/ledger/playback behavior, new exclusion, owner move or formal reassessment trigger. Existing reopen/reset still call synchronous clear; this slice does not wire entry/main or claim stale-session rejection.

## Oracle fixed before behavior

Existing `test_note_edit_focus` harness links real focus/current-state implementations. Baseline116/116 PASS before fixture edits. No new test seam. New methods absent pre-link: missing API compilation is NOT behavioral RED.

1. Scalar acceptance: seeded active mover77, nondefault last/commit/range, baseline3 + overlap2 with both tables explicitly large. Begin clears only scalars; both map nodes, capacities and content unchanged. Withheld grants preserve inventory; repeated begin retains payload.
2. Exact mixed grants: baseline3 nodes → baseline EMPTY-table compaction → overlap2 nodes → overlap EMPTY-table compaction =7 true grants. Verify independent cardinality pairs after each turn and retained fields; eighth/repeated calls false without mutation. Scalars remain reset throughout.
3. Empty tables: zero nodes with large baseline/overlap bucket arrays. Two distinct true compaction grants, then false; no hidden map destructor scan. Default fresh focus completes immediately after begin.
4. Collision:8 baseline keys deliberately in one actual bucket (IDs spaced by observed bucket_count), plus overlap1. Eight one-node grants, separate baseline shrink, one overlap grant, separate overlap shrink then false. Surviving values retain exact pitch/ticks.
5. Isolation/reuse: neighbor focus and authoritative current-state seeded independently. Draining target leaves neighbor/rows unchanged. Seed a successor only AFTER completion, then begin+drain again; no stale-token claim. Synchronous clear regression preserves previous scalar and whole-map semantics.

Drain bounds: mixed8 including terminal, collision12 including terminal; test fallback at most32. Counterexamples: begin performing whole clear, step draining2 nodes, omitted empty-table compaction. Each control must independently fail assertions and be restored byte-identically before review/broad checks.

## Verification recipe

`pio test -e native -f test_note_edit_focus`; controls same suite. Restored focused plus `test_note_edit_entry_boundary`, `test_note_edit_session_undo`, `test_loop_event_store`. One focused independent review. `pio test -e native`; `pio run -e teensy41-capture-serial`; strict OpenSpec and `git diff --check`. Recheck parent withheld-owner expected RED and restore ordinary native artifact. Logs beside packet. Full task/feedback/hardware unchecked; no upload/device/SD/stimulus/marker/instrumentation or timing claim.

## Completion record

- Baseline116/116 (`focus-clear-baseline.log`); new5 plus116 prior pass **121/121** (`focus-clear-focused.log`). Restored combined focus+store+entry+undo **260/260** (`focus-clear-final-focused.log`). Fixtures/oracle unchanged.
- Isolated controls fail: whole-map begin4, two-node grant3, omitted table compaction4 (`focus-clear-control-acceptance.log`, `focus-clear-control-quantum.log`, `focus-clear-control-buckets.log`). Each header restoration checked byte-identically via `cmp`; final SHA-256 `e99ecf70a09eaed86ee9ce573dade4724ac361cbc5b5cd2e7ccbb46a36e6d60e`. Missing API before link is not behavioral RED.
- Full native **2,962 total /2,955 passed /7 skipped /0 failed**,112 PASSED suites (`focus-clear-full-native.log`). Default capture SUCCESS (`focus-clear-capture-build.log`): RAM1 variables96,192/code390,552/padding2,664, locals34,880; RAM2 163,168, EXTRAM9,312 unchanged, no rebase.
- One focused independent review APPROVE, no findings. Packet/implementation/dependency pattern and5 registrations inspected; commands/hash were supplied evidence, not independently run by reviewer. Approval covers component prerequisite only.
- Parent flagged oracle still55 passed /1 withheld-owner publication assertion failure, exit1 plus SIGHUP/harness ERRORED (`focus-clear-parent-oracle.log`); ordinary artifacts restored afterward with260/260. Full entry remains open.
- Device/HITL NOT RUN: no new serial-observable path enabled; no upload/device/SD/stimulus/marker/instrumentation. Content-traversal bounds do not qualify allocator duration/failure or device timing.
- Closeout: runtime/current-work, deliverable, tasks/design, guide and bounds updated; no full task/feedback/hardware checkoff. Existing owners and policy retained, no new architecture decision or exclusion. Next cleanup dependencies: apply-owned nested EditPass rows, scalar ID buffers/derived-cache ownership and qualified composed departure; source admission/cancellation/graph retirement, caller intents and atomic session/FSM/main grants still open.
- Final checks performed: strict OpenSpec valid (`focus-clear-validation.log`), `git diff --check` clean, header `cmp` equal;112 full-native PASSED suites confirmed. Session-closeout template applied: runtime/task/design/deliverable and guide/bounds recorded above. No decision-log append needed for this implementation-only extension of existing owners/policy.
