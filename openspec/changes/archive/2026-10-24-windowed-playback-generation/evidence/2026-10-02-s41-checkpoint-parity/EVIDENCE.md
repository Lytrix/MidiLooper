# Slice 4.1 — checkpoint parity for undo, named (F-5)

**Change:** `windowed-playback-generation` · **Task:** 4.1 (**landed; the task stays open on one named item**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 4.1
**Contract:** the spec's *Checkpoint parity for undo and save* — "A checkpoint's parity SHALL be judged against the
loop's **current** revision, and a checkpoint that lags the committed passes **SHALL be named in the readiness/lag
vocabulary and SHALL NOT be presented as current**… Undo parity and save parity are one requirement on two consumers."

---

## What landed

| File | Change |
|---|---|
| `include/LoopSnapshot.h` | `CheckpointParity { NoCheckpoint, Current, OwesReconciliation, CoverageUnrecorded }` and the **pure** `resolveCheckpointParity(hasRoot, coveredPassRevision, currentCommittedPassRevision)`, placed beside the coverage constants it consumes. |
| `include/Loop.h`, `src/Loop/LoopSnapshotLifetime.cpp` | `Loop::checkpointParity()` — the same rule `publishedSnapshotIsCurrent()` applies, **named**, read from the three figures the loop already keeps. No new state. |

**Why this is the missing piece, and why it is not a second rule.** The parity rule already existed — `publishedSnapshotIsCurrent()` is
`coveredPassRevision == committedPassRevision()` — but it answered as a **bool**, and nothing *named* the lagging case,
which is what the requirement demands. The name cannot drift from the predicate: `storePublishedSnapshot` resolves the
`kSnapshotCoverageCurrent` sentinel into a real revision **as it stores it**, so the stored figure is always a real
revision or `kSnapshotCoverageUnset` — and for every reachable state `parity == Current` iff
`publishedSnapshotIsCurrent()`. The fixture asserts that agreement directly, so the naming is pinned to the rule rather
than to a paraphrase of it.

**The four cases, and what each is for:**

| Case | Meaning |
|---|---|
| `NoCheckpoint` | No root is selected: there is no checkpoint to judge, and nothing to present as current. |
| `Current` | The root was enumerated from the committed-pass state that is live now. |
| `OwesReconciliation` | The root is **behind** the committed passes. This is the case the requirement names: the readiness vocabulary's answer is that a rebuild is owed, and the outcome is never presented as current. |
| `CoverageUnrecorded` | No coverage was recorded — conservatively **not** current, which is also what DEC-073's demotion produces when a root is installed for a state that is not the live one. |

**Neither undo nor save consults the window's lag:** `checkpointParity()` reads the loop's own figures only, and the
fixture asserts that a slot whose **window lane is not ready** still reports a `Current` checkpoint — the window's lag is
the window's business. That is also why 3.14c's measured refusal (`SourceNotCurrent` after an undo) and this named parity
are consistent: both describe the *checkpoint's* own coverage, not the window's.

## The oracle, case by case

| Case | Observed |
|---|---|
| the rule, purely | `NoCheckpoint` / `CoverageUnrecorded` / `Current` / `OwesReconciliation` for their four input shapes |
| agreement with the existing predicate | for a root that exists, `checkpointParity() == Current` exactly when `publishedSnapshotIsCurrent()` |
| **through the production undo path** | the entry names a root whose coverage is the pre-undo state; the undo restores that root **with the coverage it carried** (DEC-073), the committed-pass revision advances, and the answer is `OwesReconciliation` — selected, with `publishedSnapshotIdentity()` **null**: never presented as current |
| DEC-073's demotion, named | a root installed with an explicit stamp that is not the live revision, and no coverage of its own, is `CoverageUnrecorded` and never current |
| the window's lag | with the slot's window lane not ready, a current checkpoint root is still `Current` |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **65/65** (was 64; +1 fixture) |
| `pio test -e native` (full) | **2696 cases: 7 skipped, 2689 succeeded, 0 failed** (baseline 2695 → +1) |
| `pio run -e teensy41-capture-serial` (clean rebuild) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom**, byte-identical to the six previous slices: a header-inline rule plus one cold `Loop` function, no ITCM growth, and **no size pin moved** (`sizeof(Loop) == 984` still holds — no state was added, which the build's own assert proves) |

## Red-proofs

Four mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hash of all five files):

| # | Mutation | Observed failure |
|---|---|---|
| M48 | the parity rule always answers `Current` (coverage ignored) | `Expected 2 Was 1` — the lagging case reported as current |
| M49 | `CoverageUnrecorded` not distinguished (the sentinel treated as a revision) | `Expected 3 Was 2` |
| M50 | DEC-073's demotion dropped (a root without coverage for the live state accepted as current) | `Expected 3 Was 1` — *"a stamp that is not the live revision is demoted, and named"* |
| M51 | the undo selects the root **without** the coverage it carried | `Expected 1 Was 2` — *"the undo restores the root its entry named, with the coverage it carried"* |

**A lesson repeated, recorded rather than glossed:** M50's first attempt reported "green" because its anchor sat in the
wrong **file** (`storePublishedSnapshotForTest` lives in `Loop.h`, not the TU the other anchors are in). This is the same
class of error as 3.14b's M44 — a mutation run whose anchor never matched is **not** evidence, and the
occurrence-count assertion plus the `shasum` oracle are what make it visible. Two occurrences is a pattern: the anchor
check should name the file's expected *path* as well as its count, and that is the harness improvement this slice
recommends.

## Not claimed

- **The *content*-level comparison is not made here.** "An undo restores exactly what its entry named" is verified as: the
  entry's restored **geometry** (S4b.4's existing fixtures) and the **checkpoint authority** it names, this root with this
  coverage, asserted here. A byte-level comparison of the restored content against the entry's named state is the
  content-verification pipeline's instrument — 4.3's own subject — and it is **owed** rather than assumed.
- **Save parity is 4.2.** The RC2 predicate it will need already exists (`Loop::hasSettledPublishedSnapshotForSave()`,
  "coverage-current and no snapshot request still pending"), but 4.2's own falsification of the
  `storePublishedSnapshot(…, kSnapshotCoverageCurrent)`-without-reconcile path is its subject, not this slice's.
- Nothing here removes a root dependency, and no window-lag interaction is introduced: the checkpoint authority remains
  the published root, judged against the loop's current committed-pass revision.
- **No production *caller* yet, stated plainly:** `Loop::checkpointParity()` is named and asserted; the consumers that will display it are save parity (4.2) and the readiness/lag vocabulary's reporting. Nothing in `src/` calls it today, so this slice lands an outcome that *can be named* rather than one already routed to a display.
- No deadline/timing claim, no device evidence, and the RAM1 headroom remains 48 B above the floor.
- **Task 4.1 stays open** on the content-level comparison above.
