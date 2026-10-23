# Reuse before invention — the resumable machinery this change uses, and the scheduler it does not add

**Change:** `windowed-playback-generation` · **Task:** 3.8 · **Date:** 2026-10-02
**Inherited criteria:** RWO `S4C1-READINESS-CURRENCY-CONTRACT.md` **AC-7** ("reuse the resumable machinery; do not add
a scheduler — *the mechanism is the existing one: the body format's bounded write/parse, the resume-arming pattern,
and the single persistence drain budget. A second publication scheduler is a recorded failure mode*"); DEC-108
item 12's closing clause; `design.md` row 28.

**How to read this note.** Every row below was checked by reading the cited code in this session — **VERIFIED** means
the symbol and the behaviour named are at that path; **INTERPRETATION** marks a mapping *this change* makes (a
reader can disagree with the mapping without the citation being wrong). Nothing here is closed by assertion: the
windowed lane's own evidence lives in the slice records under `evidence/`.

---

## The named entry points

| # | What the contract names | What exists today (VERIFIED) | How this change uses it |
|---|---|---|---|
| 1 | **the declared build contract** | `CoverageServiceContract` (`include/RuntimePlaybackViewBuild.h:56`), its `isDeclared()`/`requiredGrains()`/`canCompleteBeforeHandoff(...)` (`:64-68`), and the owner's declaration `Track::playbackViewCoverageContract()` with the grain quanta `kPlaybackViewFillQuantum = 64` / `kPlaybackViewOrderQuantum = 48` (`include/Track.h:292-296`) | The window lane **declares** its contract (task 3.1) and admits a request only through `canCompleteBeforeHandoff` — the same struct, the same margin arithmetic; no second contract type was introduced. |
| 2 | **the body format's bounded write/parse** | `StorageIo`'s bounded `write`/`read`/`peek` function objects (`include/StorageLoopIo.h:23-27`), the format's write/read entry points (`writeCapturePassSlotFileHeader` / `readCapturePassSlotFileHeader` `:72`/`:74`, `writePersistedEditsTail` `:76`), and the **bounded batch** accessor `getLastPersistedCapturePassWriteMaxBatchEvents()` (`:82`) | **INTERPRETATION:** the lane's *build* is bounded the same way — a fixed quantum per turn with a persistent cursor (`RuntimePlaybackViewBuildGrain` fill/order) rather than a whole-body write — so the "bounded unit, resumed next turn" shape is inherited, not reinvented. The lane writes no persistence bytes; the format's own bounds are untouched. |
| 3 | **the resume-arming pattern** | `PersistedLoopSnapshotReleaseCursor` (`include/StorageLoopIo.h:49-55`): `begin(...)`, `step(size_t maximumUnits)`, `complete()`, `completedUnits()` — a cursor that carries progress across calls | **INTERPRETATION:** the lane's owed-window obligation is the same shape: the build's cursor *is* the obligation, so an interrupted build resumes rather than restarts (task 3.1's "a build that is begun and unfinished is the obligation; no request record is involved"). |
| 4 | **the single persistence drain budget** | `SyncDrainBudget` (`include/PersistenceSyncDrainBudget.h:49`), `PersistenceSyncDrainBudget::buildSyncDrainBudget(input, sliceBudgetUs)` (`:115`), `buildSyncDrainBudgetForSession()` (`include/StorageManagerInternal.h:194`) | **Unchanged, and deliberately not shared:** the lane allocates no persistence work and consumes no drain slice; the one drain budget stays the persistence path's. |

## The audit the task asks for: no second publication scheduler

**VERIFIED — there is one deferred-service enumeration and the lane added a *value*, not a scheduler.**
`DeferredIdleOwner` (`include/Utils/MidiServiceDrain.h:13`) enumerates `Snapshot`, `VisualCache`,
`VisualCacheWrapAppend`, `Persistence`, `LoadAndDisplay`, `SlotProvision` and — added by this change in task 3.1 —
`PlaybackViewMaintenance`. The lane's dispatch rides the **existing** deferred frame (`src/main.cpp:463`), and its
eligibility is **state-driven** (`MidiServiceDrain::playbackViewMaintenanceNeeded(runtimePresent, buildUnfinished)`,
`:220-224`), so there is no queue, no timer, no thread and no request record of its own.

Three consequences worth stating plainly, because each is where a second scheduler would normally sneak in:

- **No self-scheduling.** The lane never asks to be run again; it is serviced when the frame's owner enumeration
  reaches it. `stepPlaybackViewMaintenance` serves at most one grain and returns.
- **No new owner for publication ordering.** The *ordering* across holders is still the deferred machinery's
  (task 3.10's fairness work), and the publication itself happens inside the completing turn (task 3.2) — one
  authority change, not a background publisher.
- **No second persistence path.** Nothing in this change writes or parses persisted bytes; the retention it adds is
  in-memory (pinned descriptors + owned bodies) and is released by one predicate (task 3.6a).

## What this change did add, and why none of it is a scheduler

| Added | Why it is not a second scheduler |
|---|---|
| `CoverageServiceContract` **declaration** for the lane | It is the existing struct, filled in by the owner; the contract type and its margin arithmetic are unchanged. |
| One `DeferredIdleOwner` value (`PlaybackViewMaintenance`) | Recorded as the deliberate shape choice in the G2 packet (on the `SlotProvision` precedent, task 3.1); it participates in the one enumeration rather than forming its own. |
| A build cursor inside the per-slot runtime | The cursor is progress state for one owed unit, the `PersistedLoopSnapshotReleaseCursor` pattern; it schedules nothing. |
| A retained source + interval index (tasks 3.6a/3.6c) | State with a lifetime and one release predicate; both are read by a caller, neither re-arms itself. |
| `PlaybackLagState` (task 3.6b) | Per-interval accounting for a decision (`resolvePlaybackLag` is a pure resolution); it drives no service. |

## Limits of this note

- It is a **code-reading audit**, not a runtime proof: the "one enumeration" claim is about the code's shape, and the
  behavioural half (that the lane is actually serviced fairly among holders, and that no holder starves) is **task
  3.10's**, where DEC-108 §13b's fairness proof belongs.
- The persistence rows (2 and 4) are cited from the storage side; this change does not exercise them, and it claims
  nothing about their bounds beyond what their own suites already pin.
- No code changed for this task: the verification record is therefore the **last** full-suite/build state
  (`windowed-playback-generation` 3.6c: focused **54/54**, full native **2685 / 2678 succeeded / 7 skipped /
  0 failed**, `teensy41-capture-serial` **SUCCESS** with `variables 96,192` / `code 391,928 + padding 1,288`), not a
  fresh run — and no red-proof applies to a note.
