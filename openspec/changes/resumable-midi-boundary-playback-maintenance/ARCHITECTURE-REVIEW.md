# Architecture review — `resumable-midi-boundary-playback-maintenance`

Phase gate for [`.cursor/rules/OpenSpec-Phase-Gate.mdc`](../../../.cursor/rules/OpenSpec-Phase-Gate.mdc).
This change predates that rule and had no review artifact; the parent
[`ARCHITECTURE-REASSESSMENT.md`](ARCHITECTURE-REASSESSMENT.md),
[`PREFLIGHT.md`](PREFLIGHT.md) and
[`OWNERSHIP_TRANSFER.md`](OWNERSHIP_TRANSFER.md) carry the original review for
phases 1–2. This file adds the **per-phase gates for the remaining phases**, starting
with task 2.4.

Authority order: [`docs/Authority/README.md`](../../../docs/Authority/README.md).
Design: [`design.md`](design.md) § implementation sequence. Bounds:
[`ExecutionBounds.md`](../../../docs/Authority/Architecture/ExecutionBounds.md).

---

## North star (unchanged)

> Active-transport work is bounded/resumable or excluded; playback consumes
> immutable, prepared runtime views; accepted capture is never lost; overdub stop
> remains a resumable active service; and the active MIDI path must no longer fall
> back to content-sized synchronous rebuilds.

Steps 1–2 of [`design.md`](design.md) § implementation sequence are complete
(`LoopPlaybackRuntime` owns the derived playback views; the fixed active/ready/build
views, selected/candidate bridges and the incremental builder are proven against the
complete in-order oracle on native fixtures). The phase gated here is **step 3** =
**task 2.4**.

---

## Phase gate — task 2.4 (active clock path consumes prepared views)

| Question | Answer |
|---|---|
| **Owner module** | `Track::playCommittedLoopMidi` (`src/Track/TrackPlaybackHotPath.cpp`) consumes; `Track::ensurePlaybackMergedEventsForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp`) prepares off the clock path; `LoopPlaybackRuntime` owns the prepared derived views and the selected bridge. |
| **Primary invariant** | The active MIDI tick consumes an already-prepared immutable source and never performs a content-sized build; a missing source is *requested* and prepared on an eligible non-clock turn. |
| **Ownership change?** | **No.** `LoopPlaybackRuntime` keeps the derived views; `Loop` stays canonical authority. Task 2.4 changes *when* the existing builder runs, not who owns it. |
| **State transition change?** | **No.** No `TrackState`, `OVERDUB_STOPPING`, slot-switch or transport transition changes. |
| **Execution bound change?** | **No new bound is introduced.** The change removes an unbounded content-sized operation from a gated interval (`ExecutionBounds.md` interval-path class, DEC-043); the off-path owner that already exists (`ensurePlaybackMergedEventsForSlot`, called from the button edge, the slot-switch commit and the deferred first-commit prewarm) keeps its current bound. |
| **Behavior-preserving?** | **Not bit-for-bit by construction.** Emitted MIDI is intended to stay identical; task 2.5 owns the byte-identical differential evidence. The phase must not change audible semantics on any path where a prepared source is already available, and the native suite plus the new instrumentation test are the gate. |
| **Reuse decision** | **YES** — extend the existing prepared-source consume path (`runtime.selectedFullLoopBridge` / `runtime.publishedFullLoopView` / the prepared `mergedMidiEvents` + `playbackOrder` pair) and the existing off-path preparer `Track::ensurePlaybackMergedEventsForSlot`. No new scheduler, no new view owner, no second builder. |
| **Phase scope (files)** | `src/Track/TrackPlaybackHotPath.cpp`, `src/Track/TrackPlaybackWindowBuild.cpp`, `include/Track.h`, `src/Track/TrackDeferredMaintenance.cpp` (request satisfaction), `include/Utils/Diagnostics.h` + `src/Utils/Diagnostics.cpp` (instrumentation), a native test suite, and docs. |

### Architecture-checkpoint questions (capture stop / commit / deferred FSM / MIDI interval path)

1. **Which owner answers the question?** `Track::playCommittedLoopMidi` decides what
   the tick consumes; `LoopPlaybackRuntime` answers "is a prepared source available
   for this exact identity"; `Track::ensurePlaybackMergedEventsForSlot` answers "how
   is a missing source prepared". The change adds no new decision owner.
2. **What is the single source of truth?** The prepared runtime view/bridge identity
   (`playbackRevision`, `playbackGeneration`, published snapshot identity, generation)
   is the consume authority; `loop.playbackOrderDirty` plus the merged-window identity
   is the preparation authority. The tick reads both and never writes content.
3. **What happens on denial / no source?** The tick **requests** preparation and holds
   that source for the turn instead of building. The request is satisfied by the
   existing off-clock preparer on the next eligible main-loop turn. No path may
   silently fall back to a content-sized build while a track is timing-critical; the
   instrumentation counter makes a violation fail a native test.

### What this phase deliberately does NOT do

- It does not remove the merged/order oracle: the complete in-place builder stays
  reachable as the **STOPPED-state oracle** (and for the structurally merged
  NOTE_EDIT preview path), and it is still what task 2.5 differentially compares
  against.
- It does not remove the compound load-frame compatibility path; task **5.4** owns
  that removal and is explicitly blocked until 2.4/2.5/5.3 pass.
- It does not add the "immutable source-change bridge" beyond what already exists in
  the runtime; if the empirical instrumentation shows an active path that cannot
  consume a prepared source during a source change, that path is recorded as its own
  slice rather than absorbed silently.

## Amended sequence (owner decision 2026-09-17)

The detector changed 2.4 from a scheduling refactor into a **coherency** problem: `mergedEvents` can
advance while `playbackOrder` is still the previous one, so the active path needs a *coherent pair*
and a source to keep consuming while the replacement prepares. The agreed order is therefore:

```text
2.4 slice 1   DONE   active-path rebuild detector + reachable-path audit
2.5a          NEXT   byte-identical differential harness (oracle = today's synchronous path)
2.4 slice 2          immutable PreparedPlaybackPair / last-prepared receipt;
                     active path consumes only a coherent published pair;
                     a source change requests replacement preparation;
                     no merged/order mismatch is ever visible to playback
measure replacement-preparation latency against the playback horizon
IF it fits:          2.4 complete (no second ordering algorithm)
ELSE:                2.4 slice 3 -- bounded incremental order update for the proven
                     expensive delta (likely the admitted sealed pass)
```

Target model for slice 2:

```text
pair P0 prepared + published -> active playback consumes P0
source changes               -> request preparation of P1; keep consuming P0
off-clock                    -> build mergedEvents P1, playbackOrder P1, reanchor P1
P1 complete and coherent     -> atomic pair publish; playback switches to P1
```

Counters to keep/add for the measurement (alongside `playbackOrderHotRebuildCount`):
`prepared_pair_request_count`, `prepared_pair_publish_count`, `prepared_pair_hold_ticks`,
`prepared_pair_rebuild_units`, `prepared_pair_max_latency_ticks`.

### Slice 2 invariant (pinned by the owner, 2026-09-17)

> Playback never observes `mergedEvents` and `playbackOrder` from different generations. A pair becomes
> consumable only when both members and their cursor/reanchor state are complete for the same source
> generation.

The reanchor/cursor seed is part of the pair's identity, not an after-publish fixup:

```text
PreparedPlaybackPair
    generation
    mergedEvents
    playbackOrder
    anchor / cursor seed
    source identity
    ready
```

Publication is one atomic authority change: `P0 playing` -> source generation changes -> request P1,
keep consuming P0 -> P1 fully prepared -> atomic `P0 -> P1`.

**Measurement caveat.** "Continue P0" is safe only while P0 remains semantically valid for the
audible interval. If newly accepted material must become audible at a particular quantized edge, hold
time is bounded by that **latest legal publication/audibility edge**, not by the generic one-bar
maintenance budget. Report both, and only then decide whether slice 3 (b) -- bounded incremental
order maintenance -- is required.

### 2.5a is a parity-oracle prerequisite, not 2.5 completion

2.5 (the full differential matrix) still completes **after** 2.4. The landed increment builds only
the oracle that the 2.4 conversion is validated against; the task docs say so explicitly so the
numbering cannot read as 2.5 leapfrogging 2.4. Fixture order agreed with the owner: (1) overdub seal
admitted into merged events, (2) same-tick NoteOff/NoteOn ordering, (3) the reanchor case, (4) wrap
and loop-edge note, (5) source change mid-window, (6) long-loop window, (7) record-stop gap. The
negative control stays permanently.

### Verification for this phase

- New instrumentation counter (`Diagnostics::Counter::PlaybackOrderHotRebuild`),
  incremented by every content-sized build (merged gather/assign, order
  resize/sort/phase fill, full reanchor) that runs while
  `anyTimingCriticalTrackActive()`.
- A native test that drives the active route and asserts the counter stays zero for
  the prepared-source paths, and that a missing source produces a *request* rather
  than a build.
- `pio test -e native` stays at zero failures; `pio run -e teensy41-capture-serial`
  stays within the RAM1 local allowance (the tree sits one 32 KB FlexRAM bank from
  the edge).
