# Continuation after recovery — 2026-09-26

## Current result and deferred display fault

Recovery/undo implementation and evidence are committed in `f29539e3`. The device
completed automatic metadata repair and subsequent boot with restore admission
complete, undo hydrated and transport stopped. The host copy passed 64 slot
roundtrips, 122-action reload and 276 repair-interruption cases. Hardware content
parity and actual Undo/Redo gestures remain unverified; ready counters do not close
those tests. The recovery incident hold was deliberately cleared for the approved
installation; the standalone reader is no longer the installed firmware.

The user has explicitly parked the DMA display fault **until after this entire
`runtime-working-set-ownership` OpenSpec**, not merely until the next slice.
Do not investigate or repair DMA as part of the current implementation queue.
DMA counters advance but the panel held its first piano-roll frame; changing only
`MIDILOOPER_DISPLAY_DMA=0` yielded a visibly updating screen, confirmed by the user.
That localizes the failing route, not its exact cache/SPI/driver cause.

The installed image uses blocking SPI. The repository default remains DMA-enabled;
an ordinary default build/upload would re-enable the fault. The exact comparator
configuration, identity, build/upload logs and selected device observations are
preserved in `evidence/2026-09-26-workspace-loss/display-dma-deferred/`. To reproduce
the workaround build, from the repository root use:

```
pio run -c openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss/display-dma-deferred/platformio.ini -e teensy41-capture-serial
```

No upload is implicit. This profile preserves the capture-serial flags and changes
only the existing display switch plus isolated build directory. Blocking SPI is not
a realtime acceptance result. Applicable visual/MIDI timing requirements stay open;
record the transport in every result and do not equate its timings with the earlier
DMA baseline. Deferral does not waive any stage gate.

## Recommended next native slice: S4.1 — persistence identity during handoff

Prefer this bounded native investigation while hardware baseline work is unsettled.
Verified source: `DeferredSaveJobStages`' `DeferredFooterWriteStage::ActiveLoopIndex`
reads `trackManager.getActiveLoopIndex(footerTrackCursor)` at execution time.
`CurrentWorkspaceSaveJob` contains stage/track cursors and `stateSnapshot`, but no
captured active-slot array. This is explicitly listed as an S4.1 remainder.

**This read is not yet proven wrong.** The runtime footer represents workspace
selection, which may legitimately differ from the slot whose capture is being
persisted. Do not replace it mechanically with the outgoing capture slot.

Next G0/G2 packet should:

1. Trace stop A → slot-keyed persistence admission → workspace header/footer writes
   → handoff to B → load, distinguishing content identity from workspace focus.
2. Establish the existing contract's metadata consistency point and whether later
   selection dirties/requeues persistence. Use StorageManager as the existing owner.
3. Pause the real deferred worker before footer emission, perform A→B through the
   manager, resume and reload. Assert A/B content, loop identities and the specified
   restored focus; include failure/retry and no-switch controls. No test-only owner.
4. Only change code if that fixture demonstrates a contract violation. Preserve
   RAM1 floor 840, and run focused/full native plus capture-serial build.

This investigation requires no device reset, clear or recording and does not wait
on a DMA fix. It does not close S4.1's other release-transition work or the S3 gate.

## Other candidates and why not first

- S4f.6 — accepted-start execution: the formerly missing arm-and-wait and
  failed-then-retried exactly-once fixtures **already exist and are registered** in
  `test_runtime_working_set_ownership`. Do not recreate them. The switch now calls
  `clearQueuedRecordingTrack(trackIndex, captureSlot)`; whole-track clearing is an
  obsolete remainder. Named acceptance reporting and the combined query still need
  their own source audit.
- S1.6c.2 — snapshot reuse capacity acceptance: recover the intended N=56 baseline
  and target eligibility by a read-only census and content comparison first. The
  copied library had 60 nonempty slots, and user interaction after restoration
  changed live state. Do not silently clear/rebuild a fixture or count an N≈60 run
  with another display transport as the original experiment.
- Recovery hardware content/Undo/Redo verification is a useful separate device
  step; it must not be inferred from `undo_hydrated=1`. No new device action is
  authorized by this documentation/commit request.
- S4b — immediate repeated handoffs still needs the explicit finalizer-capacity
  decision. Retaining a request does not prove immediate execution.

## Verification and closeout

Production hashes still match the checkpoint tested with 2,461 native passes /
7 skips; isolated ABI recovery tests cover those recovery skips. Reader/audit tests
rerun: 16 pass. Both affected OpenSpecs validate. Snapshot spec hygiene preserves
previously omitted base scenarios and classifies two new requirements as ADDED;
no intended behavior changed in that reconciliation. No push or PR requested.
Capture remains open for the current diagnostic session; verify persistence idle
before any later close/reset. Exact captured CRLF bytes are preserved as evidence.
