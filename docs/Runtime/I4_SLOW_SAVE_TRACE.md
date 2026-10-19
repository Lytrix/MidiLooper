# I4 — slow checkpoint save trace

Read-only code/device investigation, 2026-10-12, requested by owner (“trace it”). No firmware, test, host, OpenSpec, decision, plan, budget or stimulus edits; no new device command/Save/reset/upload/load/slot action or Git delivery. Runtime documentation records findings only.

## Scope traced

Stopped Save creating S0001_v0001 in `captures/i4-checkpoint-t8s4-02`; Git26125cebb08980342ba56ed2b77e5397783ac8e5+dirty, installed default capture HEX8ed5a9dc97ae56dc0612808d55f353526025c57d54f856ad1067ebea4e518c6b. User confirmed responsive OLED and visible completion, but rejected duration. Source receipts and prior qualification: [upload/checkpoint evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/UPLOAD-CHECKPOINT.md).

## Quantitative attribution — VERIFIED

Device timestamps, not host arrival order: Save confirm127960846us; dispatch127963081us; complete172617380us. Dispatch delay2.235ms; total44.656534s.

| Disjoint interval | Elapsed seconds | Evidence |
|---|---:|---|
| Confirm → first LSV1 tail begins | 2.935337 | first tail_gather_begin130896183us |
| Inside 41 paired LSV1 tail intervals | 40.289918 | tail_gather_begin → loop_snapshot_tail,done, per tail |
| Gaps between those tails | 1.075127 | previous done → next begin |
| Last tail done → revision complete | 0.356152 | last done172261228us |
| Total | 44.656534 | arithmetic reconciles exactly |

Tail intervals are90.22% of end-to-end latency, **not** a measured CPU-time or SD-time percentage. Every pair is ordered, non-overlapping and within this Save interval.34 two-bar tails consume15.302176s (median0.451155s);20-,18-,25-bar tails consume5.827477s,5.851049s,8.121458s respectively (19.799984s together).

41 LSV1 tails cover152 bars,20,566 tail service calls and256,129 snapshot-tail payload bytes. Bytes exclude legacy oracle/preamble/other revision chunks and are not whole-file size. At least152×128=19,456 pitch/bar bucket visits are required by the gather loop,94.60% of tail call count; additional membership pages/phase transitions/events/sort/write account for the remainder. The exact count of empty buckets is not emitted; do not call every visit empty.

`PERS,work` values are **not durations** for these tail markers: `emitLoopSnapshotTailStepTelemetry` puts `tailStepCount` in the first numeric field and progress signatures in the next two. `st` is total tail service calls, not gather-only operations or microseconds. Routine telemetry is decimated (tail step512, wrapper step128); intermediate phases1–5 are absent. No exact wall-clock gather-versus-write breakdown is claimed.

## Current execution path — VERIFIED

1. Save row → `beginOverlaySaveRowCommit` → `requestCommitRevision`; overlay exits. main's `serviceDeferredPersistenceTurn` promptly admits existing StorageManager work.
2. `processDeferredSaveState` dispatches Snapshot and calls one `stepRevisionCommitJob`. In-progress revision breaks the scheduler loop after that step, even if nominal slice budget remains.
3. `beginRevisionCommitSnapshot` → `prepareRevisionCommitLayout` scans track/slot metadata and calls `measureLoopSlotFileBytes` for each included loop. Measurement uses a counting StorageIo sink but invokes **full** `writeLoopPersisted`, including LSV1 `gatherLoopSnapshotPayload` plus temporary metadata/oracle/snapshot buffers. It is not a metadata-only size lookup.
4. WriteLoopSlotChunks → `stepDeferredLoopPersist` → EditTail → `beginLoopSnapshotBodyFormatV1SaveGather` → repeated `stepLoopSnapshotBodyFormatV1SaveTail(...,0,1)`.
5. `gatherOneNoteMembershipPage` visits one pitch/bar membership bucket/page per call; even a null bucket only advances its cursor once. It scans all128 pitches across every bar. `maxGrains!=1` is explicitly rejected by the tail step. main's normal seven-owner rotation limits each call's opportunities; no workspace-priority bypass exists for revision-only work.
6. WritePayload emits at most512 bytes, but `writeLoopSnapshotPayloadByteSlice` resets local streamPos to0 and calls the payload serializer again, skipping to the saved byteOffset. Prefix traversal repeats for each byte slice; the byte cap does not bound traversal cost. Gathered note/event lookup also uses linear scans. These costs are code-verified; their individual durations are not measured here.
7. Footer CRC/readback, Validate, CatalogUpdate and Complete follow the last tail. Only0.356s remains after last tail done; those final stages cannot account for the preceding40s.

## Initial synchronous span — VERIFIED boundary, attribution limit

Existing `DIAG,loop_rem,persist_save` ends at130792916us with2830063us duration. Its start is127962853us, immediately around dispatch127963081us. Scheduler routing identifies this first call as Snapshot/layout, which includes the synchronous sizing path above. The **whole call** is2.830063s; exclusive per-function allocation, serialization and SD metadata time is unknown. This is not the44.7s admission delay and not the spinner itself delaying the save.

## Ownership/lifetime map

StorageManager storageSession owns revision job, file handles, cursors, workspace epoch transition and completion/provenance. Shared existing save scratch gathers one loop's payload; published snapshots are read through existing immutable refs. main owns when the worker is offered, not storage mutation. Display reads status only. No ownership/lifetime change investigated or approved.

## Deferred-work / state-transition map

REQUEST→Snapshot→Write (each included loop's header/pass/edit/snapshot tail)→Validate→CatalogUpdate→Complete. In-progress state survives every owner rotation. Alternate serial commit and dirty-Yes routes reuse the same job; workspace saves reuse the same lower-level tail serializer. This case was STOPPED with no concurrent capture/load; behavior under changing sources is not established by this timing run.

## Resource/timing consequences

The sizing path allocates length/content-scaling temporary maps/sets/vectors, then actual writing gathers again. Partial slice serialization re-traverses prefixes. Existing `ExecutionBounds.md` row14 remains empirically bounded with budgets checked **between** steps; current maxGrains1 and512-byte write contracts are authoritative constraints, not arbitrary speed knobs. No new RAM1/PSRAM measurement or budget change in this trace; previous buildpadding904≥840 and captured heap remain separately recorded evidence.

## Existing validation

Native admission/commit/load fixture and guard matrices previously134/134; full native3070passed/7skipped; hardware live/drained checkpoint oraclePASS. Those tests verify correctness, not seconds-scale completion. This investigation streamed the existing log through bounded filters, paired41 intervals and checked exact arithmetic/non-overlap/schema against production emitters. No new native/device tests run, no performance PASS or deadline adopted.

## Hidden dependencies / assumptions / unknowns

- VERIFIED: whole-workspace revision serialization, not merely track8/slot4;41 LSV1 tails observed, not proof that no additional legacy-only slots were included.
- VERIFIED:19,456 obligatory pitch/bar visits; serial one-grain routing; repeated full sizing and payload-prefix traversal.
- ASSUMPTION: empty-bucket and repeated-prefix work are strong optimization targets; no counterfactual speedup claimed or implementation chosen.
- UNKNOWN: exact CPU versus SD time, intermediate phase timing, metadata scan's exclusive share, exact empty-bucket count, behavior/speed at maximum content or during playback.
- CONFLICTS: no new authority conflict. Increasing grain count/budget without preserving declared unit and MIDI service bounds would need its own contract; no such change proposed here.

## G0 — implementation evidence verdict: PASS

Dominant runtime path and amplification mechanisms are established for the recorded save. This is discovery, **not implementation approval** and not a claim that all44.7s is attributable to one micro-operation. Main cause at path level: repeated full-workspace LSV1 serialization with fine-grained full pitch/bar enumeration; initial synchronous sizing and replay-based byte slices are additional concrete costs. No firmware/device changes were made.
