# Slice 3.6a — the retained source revision, its roles, and the one release predicate

**Change:** `windowed-playback-generation` · **Task:** 3.6 (first increment; the index and the lag rule remain) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.6
**Contract:** DEC-108 items 4, 12(a), 13(a) and 14(c); RWO `S4C1-READINESS-CURRENCY-CONTRACT.md` **AC-5** (F-3's
precondition: the previous revision must be *held*, not merely named).

**What the spec asks for:** a missed deadline keeps the previous revision of the affected interval authoritative, so
the system holds "an immutable description of that revision's pass membership and order … with **actual body
ownership** and a **generation-qualified identity**"; the retained form is **pinned bodies plus reconstruction
through the effective-content resolver**, never a frozen per-interval copy and never merely a count; the protection
must be **read by the reclamation consumer** and **honoured on the direct reset/replacement paths**; sources are
budgeted in explicit **serving / candidate / retiring** roles; and a source is released only by **one release
predicate** (the loop's revision advanced past it, and no window, builder, obligation or reader references it).

---

## What landed

| File | Change |
|---|---|
| `include/PassReclaim.h` | `RetainedSourceRole { None, Serving, Candidate, Retiring }` and `RetainedPlaybackSource` — the identity (`revision`, `generation`), **descriptor pins** (`SlotPassReferences`) and **owned bodies** (`CommittedChunkIdList`). `kMaxRetainedPlaybackSources = 2`, the bound that covers the lane's own sequencing (one build at a time). |
| `include/TrackPlaybackRuntime.h` | The per-slot holder: `std::array<RetainedPlaybackSource, 2> retainedSources` plus `hasRetainedPlaybackSource` / `retainedSourceForRevision` / `freeRetainedSourceSlot`. |
| `src/Track/TrackPlaybackRuntime.cpp` | The S3.0 pin re-based **per ABI** (see the finding below): 6,368 → **6,464** on the target, 6,544 on the host. |
| `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp` | `retainPlaybackSourceForSlot` (typed outcomes `Admitted` / `AlreadyRetained` / `NoRuntime` / `NotServable` / `RoleCapacityReached`), the reads, `tryReleaseRetainedPlaybackSource` (**the one release predicate**) and `mergeRetainedPlaybackPinsInto`; `noLiveOwnerForSlot` gains the retention term **by name**. |
| `src/TrackManager/TrackManagerMemoryPressure.cpp` | The reclamation consumer merges each track's retained pins into the set it hands to `Loop::reclaimUnreferencedDisabledCapturePasses` / `…EditPasses` — "a pin registered only alongside undo references is not sufficient protection" — and the pass itself is now `PRESSURE_RECLAIM_MEM` (FLASH), which the RAM1 guard measured as a **net win** (see below). |
| the reference fixture | `test_retained_source_is_admitted_with_roles_and_survives_both_destruction_paths`. |

## The oracle, case by case

| # | Case | Observed |
|---|---|---|
| W-14 | admission of a revision | `Admitted`; identity `(revision, generation)` recorded; role `Serving`; the pass is **pinned** and its bodies **owned**; re-admission is `AlreadyRetained`, refreshes the role/generation and **does not re-own** the bodies |
| W-19 | the role set is bounded | two entries (`Serving` + `Retiring` after the newer source is admitted); a third admission is `RoleCapacityReached` and the still-needed source is **not** discarded |
| W-15 | the pass-level reclamation consumer | a `Disabled` pass **survives** with the retained pins in the set and **is reclaimed** in the same shape with an unreferenced set (the negative control); the production pass over every track and slot does not reclaim it either |
| W-16 | the one release predicate | refuses while the retained revision is the loop's current one; refuses while **a window still serves it** (even after the revision advanced); releases once no clause holds — including when the content it described is **gone** |
| W-17 | `Track::noLiveOwnerForSlot` | **false** while a source is retained (the new named term), **true** once released |
| W-18 | the **direct reset/replacement path** | `Loop::resetPassTimeline` is **not vetoed** (the gesture still works), and the retained bodies **survive** it — then are returned to the store by the release predicate, and only then |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **52/52** (was 51; +1 fixture) |
| `pio test -e native` (full) | **2683 cases: 7 skipped, 2676 succeeded, 0 failed** (baseline 2682 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** on a **clean rebuild** — `variables 96,192` (**unchanged**, no re-base) / `code 391,864 + padding 1,352` / locals 34,880, floor 840, **512 B headroom** |

## Red-proofs (three mutations, each restored byte-identically)

`shasum -a 256 -c` against the pre-mutation hashes, both files `OK` after every restore:

- **M1 — the descriptors are not pinned** (`pinPassesFromLoopPasses` dropped): *"the retained revision's pass is
  pinned for the reclamation consumer"* fails.
- **M2 — the bodies are named but not owned** (`LoopEventStore::retainChunkRefs` dropped): *"and the retained bodies
  survive it, because the source owns them"* fails (`Expected 38 Was 37` — the reset freed them).
- **M3 — the "a window still serves it" clause dropped** from the release predicate: *"a served window keeps its
  source retained"* fails.

## Findings recorded rather than smoothed over

1. **The direct reset path is protected by ownership, not by a veto — and that is the spec's own clause.** The
   approved plan for this slice was "the reset calls the one release predicate and refuses while still
   referenced". Implementing the retained source exposed a better mechanism already in the tree:
   `LoopEventStore::retainChunkRefs` ("pins borrowed ids for a deferred reader"), so the retained source **owns** its
   bodies and `Loop::resetPassTimeline`'s `releaseChunkRefs` only drops the loop's own reference. Every one of the
   eleven call sites is therefore safe **with no veto anywhere**, gestures (record start, clear, load) keep working,
   and `RetainedPlaybackSource::ownedChunks` is released by the one predicate. The veto was *replaced by* ownership,
   not skipped: W-18 asserts the reset is not refused **and** the bodies survive.
2. **The reclamation consumer's set already contains the undo stack, and that overlap is not the protection.** A pass
   disabled by `setCapturePassState` is an undo-recorded action, so the track's undo stack already references it and
   the manager's pass protects it even without the merge — measured: removing the merge alone does **not** change that
   outcome. What the merge covers is a pass disabled **without** an undo reference, and the fixture pins the **set
   that decides** (the merged `SlotPassReferences`) rather than the coincidence. The fixture says so in place.
3. **The S3.0 size pin is ABI-dependent, and the target's figure is the charged one.** The new holder contains
   pointer-sized pin lists, so `sizeof(LoopPlaybackRuntime)` grows by **+96 B on the target (32-bit)** and +176 B on
   the host. The pin is now per ABI (`#if defined(__IMXRT1062__)`), exactly like `TrackMem.h`'s own pattern; the
   first build of this slice failed on the target **only** because a single literal cannot hold both, and the native
   suite had passed. The charged envelope figure is 6,464 B per slot; DEC-108 item 4 requires that cost charged.
4. **A §-free RAM1 win came out of the same slice.** The first target build after the pin was fixed grew ITCM code by
   336 B (`padding 1,016 → 680`, under the 840 floor) with `variables` unchanged. The cause was the reclamation pass's
   enlarged body in ITCM; marking that cold pass `PRESSURE_RECLAIM_MEM` (the macro already existed in the TU) moved it
   to FLASH and the clean rebuild now reports `code 391,864 + padding 1,352` — **336 B less ITCM code and 336 B more
   headroom than the 3.5 baseline**. No re-base of `scripts/ram1_budget.json` was needed or made.
5. **The builder clause of the release predicate is deliberately conservative.** While a build is outstanding its
   frozen source is not inspectable from the predicate, so retained sources are held rather than risk releasing one a
   builder is reconstructing from. The finer per-builder term is **named as owed**, not assumed away.

## Not claimed

- **The interval index over pinned bodies** (later intervals constructible from the retained revision with no
  resident whole-loop root, both affected ranges, atomic publication with the revision, inspected-work budgeting)
  is the **next increment** of this task.
- **The lag rule / F-3** (one pass late and only that, the named second-miss breach, the allowance not reset by a
  further edit, the consumer never blocked) is likewise the next increment. This slice builds its **precondition**:
  a revision can now be held, protected against both destruction paths, and released by one predicate.
- **Task 3.6 is not checked**: only its first clause is green.
- No retention **page/index** cost claim and no envelope claim beyond the per-slot figure above; no device evidence,
  no flash, no stimulus.
- The release predicate's per-builder term and the "future-window obligation" clause are named, not yet expressible
  from the predicate's inputs.
