# S4c.4/S4c.5 — checkpoint parity, and the fixtures the sibling must own

**Status:** specification only. No firmware, no `src/`/`include/` change, no build, no device action. The
publication unit stays as it is, and **no playback-root dependency is removed here** (`S4c.4`'s own clause) — this
document states what the sibling `windowed-playback-generation` must accept and prove, and answers the two
questions `S4C0-PUBLICATION-INVENTORY.md` §4 handed it.

## 1. S4c.4 — what "checkpoint parity" must mean under a windowed authority

A **checkpoint** is any representation a consumer keeps outside the playback path and later becomes authoritative
again: an **undo/redo entry** (`TrackUndo`) and a **persisted body / library observation** (`StorageLoopIo`,
`WorkspaceSave`, `LibraryObservationSession`). Parity is the requirement that such a checkpoint represents the
**same content, for the same identity**, as the authority it was taken from.

**Today's answer, read from the code rather than assumed.** The checkpoint's root is the **published** root:

| fact | where |
|---|---|
| the persisted body is serialized from the published root | `StorageLoopIo.cpp:1317`, `WorkspaceSave.cpp:671`, `:699` (`const ImmutableLoopSnapshotRef loopSnapshot = loop.publishedSnapshot();`) |
| the library observation's identity and presence test also read it | `LibraryObservationSession.cpp:200`, `:222` |
| a **restore installs the loaded body as the published root** and marks the coverage outcome **current** | `StorageLoopIo.cpp:1363` (`loop.storePublishedSnapshot(std::move(loadedLoopSnapshot), 0, kSnapshotCoverageCurrent)`) |

That last row is the known divergence the inventory recorded as open question 3: the body is serialized from
`publishedSnapshot()` **with no reconcile against the committed passes**, and a restore then asserts
`kSnapshotCoverageCurrent`. It is recorded here as the *premise the sibling inherits*, not as a defect to fix in
this change.

**What the sibling must therefore state (requirements, not a design):**

1. **Which representation a checkpoint represents.** Until the sibling proves otherwise, the published root stays the
   **immutable checkpoint authority for undo and save** — `design.md` Revision 11's own carve-out ("a published root
   survives only as the immutable checkpoint authority for undo and save"). A windowed authority may not quietly
   become the thing a checkpoint is judged against.
2. **The rule when the checkpoint lags the committed passes.** Parity is judged against the **current** revision
   (S4c.1 AC-4's single definition of current). A lagging checkpoint must be **named** in the S4c.1 AC-3/AC-5
   vocabulary; it must never be silently presented as current — which is exactly what
   `storePublishedSnapshot(…, kSnapshotCoverageCurrent)` does today at the restore edge, and what the sibling has to
   resolve *for its own unit* rather than inherit.
3. **Undo parity and save parity are one requirement on two consumers.** An undo must restore geometry **and**
   content identical to what its entry named; a save's body must be equivalent to the root it serialized. Neither
   may consult the window's lag: the **one-pass-late rule is a playback rule** (AC-5), not a checkpoint rule.
4. **Explicit non-goal.** Nothing here removes a root dependency, and none may be removed on the strength of this
   document: `S5`'s demotion of inactive roots is gated on the sibling's parity proof
   (`IMPLEMENTATION_CONTRACT.md` §4: reactivation must prepare whatever the *current* playback authority requires
   off-clock, "including a whole-loop root until F").

## 2. S4c.5 — the fixtures the sibling must own

Each entry says what it proves, and who owns landing it. **Ownership: the sibling lands them**; this change records
the list and the acceptance shape.

| # | fixture | what it must prove |
|---|---|---|
| F-1 | **window identity and immutability** | a window is identified by `(slot, revision, interval)` and no consumer read straddles a replacement (AC-2) |
| F-2 | **readiness per affected tick, inside the deadline** | after a change at a tick, the covering window is ready before the playhead returns — the §Sizing deadline (16–31 ms at 1/16–1/32-beat quantisation) is the oracle, not a bar count (AC-1) |
| F-3 | **one pass late, and only that** | a missed deadline keeps the *previous* version authoritative for exactly one pass, never torn, never held, never timing-deferred, and a longer lag is **named** (AC-5) |
| F-4 | **nothing allocates or blocks on the edge** | the merge/switch/clock edges only *read* readiness; the off-clock owner is named, following the S4b.6/S4b.7 `SlotProvision` precedent (AC-6) |
| F-5 | **checkpoint parity, undo** | an undo taken under the windowed authority restores geometry and content identical to its entry's root — including the **lagging** case, where the outcome must be named rather than "current" (§1.2) |
| F-6 | **checkpoint parity, save** | the persisted body is equivalent to the root it was serialized from, and a restore of it is *either* current *or* named as stale — today's `kSnapshotCoverageCurrent`-without-reconcile is what this fixture has to falsify |
| F-7 | **content parity across a window boundary** | no tear at the boundary: the content served by successive windows concatenates to the same events as the whole-loop path |
| F-8 | **length-independent root residency** | the acceptance question the inventory left open: **a window is served without the whole root being resident**. This is a *residency* statement per slot (windows resident, root demoted) **plus** content parity for the demoted state, and it must be **swept** over loop lengths — `design.md` row 2 ("64 bars ≈ 4 bars") is the shape, and its "window size is a tunable" is the reason a single bar count cannot be the oracle |

**Two ownership rules that keep the list from duplicating work:**

* **F-7/F-8 extend the existing content-verification pipeline; they do not build a second one.** The `S5 gates` line
  already owes "content parity for a demoted/re-activated slot (existing content-verification pipeline)", and that
  pipeline is the repo's own (`.agents/skills/hitl-content-verification`,
  `docs/Guides/HITL_CONTENT_VERIFICATION.md`). The sibling feeds it the windowed/demoted cases.
* **F-1…F-8 do not restate `test_playback_prewarm`.** That suite pins the *current* reserved-capacity formula
  (`fixedExternalBytes()`, which [S2C1](S2C1-PLAYBACK-WINDOW-LEASE.md) measured); the sibling's fixtures are new
  because the **unit** changes, not because the old suite is wrong.

**What today's nearest acceptance is, so F-8 is a delta and not a blank page:** S1.5/S2.5 measure
length-independence *at today's topology* (one sounding slot per track; comparable resident working sets across very
different loop lengths), and S2.4/S2C23's class budget charges the `P` term per holder. F-8's own residue is the
**root's** residency — the term `design.md` Revision 11 prices at ~325 KB per loop — which no fixture of ours can
move until the sibling owns that representation.

## 3. Reconciliation the S4c gate asked for (P5's wording vs the tree)

`ARCHITECTURE-REVIEW.md`'s P5 gate text says "the five gather sites/checkpoint consumers". The tree has **nine call
sites of two entry points across six files** — `DisplayNoteResolveCommitted.cpp` ×2, `DisplayColdHelpers.cpp` ×2,
`TrackPlaybackWindowBuild.cpp` ×2, `LoopVisualCache.cpp` ×2, and the intra-wrapper call in `LoopMaterialization.cpp`
(whose `…WithCapture` wrapper *is* the second entry point's definition; `include/Loop.h` declares both). The
inventory attributes them **by consumer group** (playback window build; display, which the visual cache belongs to),
so the count is reconciled here rather than asserted at check-off: the gate's "five" is a consumer-group count, not
a call-site count.

## 4. What this document does not claim

* **No shipped windowed publication** — the unit, the coverage vocabulary and the revision identity are unchanged.
* **Phase F's own artifacts/DEC/reassessment must exist before its implementation**, and **S7/archive waits on the
  sibling's gates** (this change's S7 gate text names that dependency).
* **No root dependency is removed**, and no `S5` demotion may lean on this document as its permission.

## 5. Verification of this document's own citations

Every code path was checked live at the line named: `StorageLoopIo.cpp:1317` and `:1363`, `WorkspaceSave.cpp:671`
and `:699`, `LibraryObservationSession.cpp:200` and `:222`; the `S5 gates` line's content-parity clause; the
content-verification pipeline's two locations; `design.md` Revision 11's checkpoint carve-out and its ~325 KB figure;
`IMPLEMENTATION_CONTRACT.md` §4's reactivation clause; and the nine gather call sites of
`Loop::tryGatherPublishedSnapshotEventsInWindow[WithCapture]`. `openspec validate --strict` valid.
