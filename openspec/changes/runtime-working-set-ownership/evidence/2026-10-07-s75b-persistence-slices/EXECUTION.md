# S7.5b.1 — resumable persistence payload writes (G2 execution packet)

## Identity and scope

- **Task:** `runtime-working-set-ownership` S7.5b.1 — persistence payload writes do not starve playback.
- **Owner:** `StorageManager::stepDeferredLoopPersist` / `writeDeferredCapturePassChunk`; existing `CurrentWorkspaceSaveJob` continuation and persistence scheduler. No new owner, queue, transport gate, schema, or SD format.
- **Evidence/source:** `captures/s64-37c-01/host_midi_hitl_windowed_post_stop_drain_20261006_224412.log`; lines around `RECS,stage,save_request` and later `PERS,work,...payload_slice`; DEC-081 — audible playback never gates persistence.
- **Observation:** payload slice ~30.3 ms, `persist_save` maximum 31.7 ms. These are repro evidence only, not approved device timing limits.
- **Non-goals:** parking saves until transport stops; changing record finalization ownership; claiming that a smaller synchronous SD call has a verified physical-latency bound.

## Oracle before production edit

Fixture: a real committed capture pass with >512 payload bytes; invoke `stepDeferredLoopPersist` through its production work-item path, with MIDI/main-loop service opportunity between each writer turn.

1. **Initial:** Header/PassHeader work reaches `CapturePassChunk`, committed chunk ID and byte offset 0 are owned by `CurrentWorkspaceSaveJob`.
2. **Each turn:** write at most the declared byte quantum from the same immutable serialized chunk; offset advances only by successfully written bytes; pass/chunk cursor remains on that chunk until complete. The scheduler returns between turns.
3. **Completion:** each byte appears exactly once and in order; CRC equals the independent whole-stream CRC; next chunk starts at byte offset 0; the final persisted body parses and reconstructs the exact pass events.
4. **Failure:** short/failed write terminates through the existing named persistence failure path; no successful result or dirty-clear is published; no cursor falsely acknowledges unwritten bytes.
5. **Red-proof:** removing the quantum/cursor reintroduces one whole-chunk write and fails the asserted maximum write size. A mutation that advances the cursor before the write must fail byte/CRC parity or the injected-write-failure case.

## Bounds and controls

- Reuse `test_storage_drain_restore`'s real persistence work-item harness and in-memory SD boundary; add only a maximum-write observation/injection at that I/O boundary if needed.
- The independent oracle is the pre-write committed chunk stream and CRC, not the production writer's own output as expected content.
- Preserve LSV1/legacy body selection, chunk ordering, finalization, and DEC-081's concurrent persistence policy.
- Full native and both capture builds; strict OpenSpec validation; no upload without explicit authorization.

## Status

- **Baseline regression:** before the writer change, the new production continuation fixture failed exactly at the write bound: `Expected 8192 to be less than or equal to 512`; all sibling focused cases passed.
- **Implemented/native:** `test_storage_drain_restore` **16/16**. The same production `stepDeferredLoopPersist` now completes in multiple calls, each File write is ≤512 bytes, production parser round-trip reproduces every event, serialized byte count and segmented CRC match, and an injected failed slice retains offset 512 and the same chunk cursor. Red behavior and repair are both demonstrated.
- **Broad/build:** full `pio test -e native` **2,858 cases / 2,851 succeeded / 7 skipped / 0 failed**. `teensy41-capture-serial` SUCCESS, RAM1 `96,192 variables / code 392,248 + padding 968` (128 B over floor); qualification SUCCESS, `96,192 / code 392,312 + padding 904` (64 B over floor). Strict OpenSpec validation and `git diff --check` pass.
- **Device attempt (2026-10-07): BLOCKED before S7.5b oracle.** Authorized upload/run on Track 5/slot 1; `windowed_content_parity` record leg could not clear the slot. Two attempts returned `Clear refused: deferred save temporarily blocked by waiting_commit_gate; slot left untouched, save still owed`; drain refused after 0 iterations. No new record, save, or playback-during-save was exercised. Capture: [`session_20261007_000314.log`](../../../../../captures/s75b-device-20261007/session_20261007_000314.log); metadata pins git `339c94bfffd00d97166e15831f452759fedca783`, firmware SHA-256 `b8f63af3a1bbab57d07ec015a207c1e23d886db3f0ef22f08892d587feafcdbd`. Temporary stimulus fork: `STIM-WINDOW-CONTENT-01-TEMP-s75b`. This is not a device pass or timing measurement; SD busy-time/MIDI deadline evidence remains open.
