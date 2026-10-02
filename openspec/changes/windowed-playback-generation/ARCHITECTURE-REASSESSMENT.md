# Architecture reassessment — `windowed-playback-generation`

**Status:** reassessment recorded 2026-10-02, one session, companion to **DEC-108**. Produced under
[`docs/ARCHITECTURE_REASSESSMENT.md`](../../../docs/ARCHITECTURE_REASSESSMENT.md) because its **Required output when a
trigger fires** fields are all answerable here. It authorises no code: the change's `tasks.md` §1–§6 remains the
implementation plan, and every bound below is **unproven** until its own gate passes.

## Reason triggered

Three formal triggers fire together:

| Trigger | Why |
|---|---|
| **New owner introduced** | The window-build/publication owner becomes `Track::stepPlaybackViewMaintenance`, which **does not exist in code** today (`ExecutionBounds.md` rows 27–29 call it a declaration; the `CoverageServiceContract` struct and `TrackPlaybackRuntime::beginCoverageBuild` machinery do exist). |
| **Ownership transfer required** | Steady-state window publication moves off the incidental path it sits on today (`Track::ensurePlaybackMergedEventsForSlot`, reached from the `SlotProvision` turn, `src/MidiButtonActions.cpp:59`, `src/main.cpp:234`) to that owner. `SlotProvision` keeps requesting preparation and reading readiness. |
| **Timing model changes** | Readiness becomes a completed-publication predicate with a tempo-derived deadline, retained previous versions and a bounded service contract on the turn that produces them — replacing "build inline on the live turn when no pair/view is available". |

Supporting evidence, all read at HEAD this session: `docs/Authority/Architecture/ExecutionBounds.md` rows 2, 27–29 and
the `CoverageServiceContract` row; `src/Track/TrackPlaybackWindowBuild.cpp` (window build, readiness, fallback);
`src/Track/TrackPlaybackHotPath.cpp:287-314` (the three consumption paths); `src/Loop/LoopSnapshotLifetime.cpp:49-76`
(DEC-073 currency); `include/TrackPlaybackRuntime.h:317, 322` (the working window) and
`src/Track/TrackPlaybackRuntime.cpp:2246-2249` (the size pins).

## Current architecture

Active playback is served by **three representations**, chosen per slot per turn
(`src/Track/TrackPlaybackHotPath.cpp:287-314`):

1. **prepared pair** — admissible short loops (`fixedView == nullptr && !shouldAvoidFullVisualRebuild(...)`), consumed
   through `resolvePreparedPairConsume(...)` against `{playbackRevision, playbackGeneration, publishedSnapshotIdentity}`;
   the previous pair is already held while a replacement prepares (`PreparedPairConsumeDecision::held`).
2. **fixed view / bridge** — whole-take admissible slots, published only while every track is stopped
   (`publishPreparedPlaybackPairFromWorkingWindow`, and the fixed-view publish gated on `!anyTimingCriticalTrackActive()`).
3. **merged window** (`PlaybackMergedMidiEvents`, `LoopPlaybackRuntime::mergedMidiEvents`) — the fallback
   (`useMergedFallback`), i.e. the long-loop windowed path and the cold first audible turn. It is documented as the
   off-clock preparation's **working window**; the live turn may build it, and `ExecutionBounds.md` row 2 classes that
   build **Unbounded** (2 bars on long loops, the whole loop on short).

Owner of the state: `Loop` owns passes, the published root and `playbackRevision`; `Track`/`LoopPlaybackRuntime` own the
per-slot working window, the prepared pair and the immutable views; `TrackManager` owns transport orchestration and the
commit binding; `StorageManager`/`TrackUndo` consume the published root as the checkpoint authority. `activeLoopIndex`
stays the sole authority for which slot is active (DEC-105). `committedPassRevision()` **is** `playbackRevision` by
contract, and coverage — not the revision stamp — decides currency (DEC-073).

## Proposed evolution

**Extend, do not replace.** Per DEC-108: keep prepared-pair and fixed-view playback for admissible loops; introduce
**immutable interval publication for demoted/windowed loops** inside the existing view set (window-sized, no restored
whole-take reservation); treat a whole-loop view as coverage that spans the whole loop; give the window the pair's
existing retention semantics as a **pinned source revision** rather than a copied buffer; make readiness a completed
publication with matching identity and sufficient coverage (empty interval = valid silence); and move the steady-state
build/publication onto `Track::stepPlaybackViewMaintenance` under the declared `CoverageServiceContract`.

Ladder position: **no new owner is invented** — the owner is the one `ExecutionBounds.md` already declares as a required
implementation contract, and its admissibility condition ("maximum phase advance, maximum consecutive denied turns,
required grain count, lead margin") is adopted rather than relaxed. What is new is the implementation and its proof.

## Migration impact

- **Files (at Apply, none now):** `src/Track/TrackPlaybackWindowBuild.cpp`, `src/Track/TrackPlaybackHotPath.cpp`,
  `src/Track/RuntimePlaybackViewBuild.cpp`, `src/Track/TrackPlaybackRuntime.cpp` (size pin), `include/RuntimePlaybackView*`,
  `include/TrackPlaybackRuntime.h`, `src/TrackManager/TrackManagerTransportTick.cpp` (the commit binding's readiness
  term), `src/TrackManager/TrackManagerSlotMatrix.cpp` + `src/main.cpp` (the owner's turn wiring).
- **Tests:** new fixtures first (identity/immutability, deadline, one-pass-late, edge allocation, empty interval, stale
  completion, repeated edits, boundary closures, root-demoted playback); `test_playback_prewarm` must stay green and is
  **cited, not restated**.
- **Device/HITL:** a new windowed stimulus profile through the existing content-verification pipeline, canonical
  stimuli preserved; no SD format change and no destructive operation.
- **RAM1/envelope:** the size pins are re-based deliberately with the reason recorded at the assert (currently
  `sizeof(LoopPlaybackRuntime) == 6368`, `sizeof(Loop) == 984`, `sizeof(TrackPlaybackRuntime) == 32`); source retention
  is charged to the envelope; the `padding_floor` is 840 B.
- **Compatibility:** no persistence or undo format change; the published root keeps its checkpoint authority, so S5's
  root demotion stays gated on this change's parity proof.

## Recommendation

**One preferred path: implement DEC-108 as written, owner first.** Land `Track::stepPlaybackViewMaintenance` with the
declared `CoverageServiceContract` quantum and its proof fields before any behaviour change, since the admissibility
condition is what `ExecutionBounds.md` gates active windowed playback on; then interval publication, retention and the
readiness term, each behind its fixture. **Confidence: medium.** The owner, the readiness change and the retention
semantics are grounded in existing declarations, code and decisions; the **capacity, scheduling and retention bounds are
unproven**, and the change must not claim `N`, a supported density, or a cleared reservation until they are measured.

**Explicitly not decided here:** the product limit beyond the provisional 2,048-event window capacity; whether smaller
window spans are adopted; the supported `N`; and the removal of the live merged-window build (gated on its replacement
being verified).
