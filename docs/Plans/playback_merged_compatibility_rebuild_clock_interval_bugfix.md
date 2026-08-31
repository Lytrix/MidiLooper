# Remove Rank 6 full compatibility gather from the clock

**Status:** Firmware in tree. Device recapture open. Do not check off OpenSpec `3.5` / `3.6`.  
**Date:** 2026-08-31  
**Kind:** bugfix  
**Parent:** [`overdub_wrap_derived_rebuild_clock_interval_bugfix.md`](overdub_wrap_derived_rebuild_clock_interval_bugfix.md)  
**Evidence:** [`005527`](../../captures/session_20260828_005527.log) T4/S0 compatibility climb `8.429 → 12.560 → 17.381 → 20.277 → 27.828 ms`. After wrap split, [`135510`](../../captures/session_20260831_135510.log) `playback_build,compatibility,7547,0,8448` at `57711324`.

**Does not start:** Ranks 1–3, 5, 7–8. Snapshot consumer migration. `T_RT_reserve`. Incremental `rebuildPlaybackOrder`. OpenSpec `3.5` / `3.6` checkoff.

---

## Invariant (one sentence)

When `ensurePlaybackMergedMidiEventsBuilt()` has an existing full-loop merged representation and detects exactly one revision introduced by the latest overdub seal, it may incrementally admit that sealed pass and the complete companion set for that same seal. Incremental admission is valid only when the pass identity, companion identities, and representation geometry can all be proven compatible. Otherwise the existing full gather remains the correctness fallback.

The incremental path must not call `gatherCommittedEventsForDerivedView()` or `tryGatherPublishedSnapshotEventsInWindow(0, loopLength)`. Success is the **absence of a full-loop `playback_build,compatibility` row for that single-revision overdub admission** — not a reduction in duration.

---

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — gather owner stays `ensurePlaybackMergedMidiEventsBuilt`; occupy still reads `Entry.noteId` |
| Musical wrap/stop semantics? | NO — sealed pass and companions still become audible; only the representation update is incremental |
| State transition change? | YES — user-approved. Full compatibility gather no longer runs on the clock tick after a **proven** single overdub seal |
| New owner / scheduler? | NO |
| Unbounded interval owner? | NO for this path — newly sealed pass + companion targets. ExecutionBounds row 2 stays **Unbounded** for first-build / multi-revision / provenance-miss / long-loop window slide |

Reuse: YES — extend `ensurePlaybackMergedMidiEventsBuilt`. Do not add a Manager, helper type, or `cancel*` / `abort*`. Do not put this on `handleMidiInput` or clock dispatch.

---

## Code-backed eligibility (not `delta == 1` alone)

`commitCapturePass` increments `playbackRevision` once. `sealPendingNoteChangesToEditPasses` increments it **again** when companions are sealed. A Rank 6 overdub stop with overlap companions therefore has revision delta **2**. Inferring incremental-admit from `playbackRevision == builtFromRevision + 1` would miss the climb this slice exists to remove.

The one-shot overdub-seal receipt is the provenance:

```text
priorRevision     = playbackRevision before commitCapturePass
playbackRevision  = Loop.playbackRevision after commit + companion seal
passId            = lastCommittedPassId()
companionIds      = return of sealPendingNoteChangesToEditPasses
```

```text
incremental-admit eligibility =
    existing full-loop merged representation of receipt.priorRevision
    AND loop.playbackRevision == receipt.playbackRevision
    AND receipt.passId == lastCommittedPassId()
    AND that pass is the overdub pass introduced by this seal
    AND receipt companion ids are complete and applicable
    AND loop geometry matches the existing merged window

otherwise:
    existing full gather
```

An empty companion list is complete when that seal produced no companions. Missing receipt, pass-id mismatch, stacked seals (`merged.builtFromRevision != receipt.priorRevision`), or companion rows that cannot be applied is incomplete → full gather.

Companion IDs are a one-revision publication receipt for the latest seal. They are consumed exactly once by the corresponding incremental admission or discarded when the existing full-gather fallback supersedes them.

---

## Return-value contract

`ensurePlaybackMergedMidiEventsBuilt` returns `true` only when a **full** rebuild happened. [`playCommittedLoopMidi`](../../src/Track/TrackPlaybackHotPath.cpp) then runs `reconcilePlaybackLedgerAfterFullLoopRebuild`. Incremental admission leaves a nonempty current stream and returns `false`. `false` does not mean “nothing usable.”

`playCommittedLoopMidi` must not `runtime.reset()` the merged stream on a **revision-only** stale. That reset would destroy the existing representation incremental admission requires. Generation change still resets.

---

## Cross-stage S correctness

```text
After wrap:
    merged/source state covers (prev, S]

After next-tick incremental admission:
    merged state additionally contains pass events after S

After any later full rebuild:
    merged state == authoritative content exactly once
```

---

## Observability

Capture-gated. Incremental success emits `playback_build,incremental` (not `compatibility`). Order rebuild on the same tick emits `playback_order` so gather disappearance is not confused with work moving to sort.

Device proof:

```text
overdub seal
    ↓
playbackRevision after that seal
    ↓
next playCommittedLoopMidi
    ↓
incremental admission
    ↓
NO full-loop compatibility gather
```
