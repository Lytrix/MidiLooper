# Task 3.13 (I10) — fixture 2.12's **`then save`** clause, in the storage harness

Execution packet for the next increment of slice **3.13** (*"the preview contract — immutable deferred publication"*),
whose **one remaining owed item** besides the display consumer is *"fixture 2.12's **save** clause in the storage
harness (why this task stays open)"*. Authority:
[IMPLEMENTATION_READINESS.md §G2 execution packet](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), `tasks.md` fixture **2.12**
(*"… mutate or exit the session **between build grains**, then **save**: assert the preview is identified as preview, is
invalidated by its own lifecycle, publishes no mixed or stale content, and **never counts as committed currency**"*),
`G2-EXECUTION-PACKET.md` §"Slice 3.13", **DEC-073** (the persistence admission), **DEC-108** §13 and **DEC-114**.

## Sequencing, recorded rather than smoothed

The probe that decided this increment's shape — *can one TU link the window lane **and** the storage harness?* — was run
**before** this packet was written (it cost one compile: the two storage TUs link in
`test_windowed_preview_integration` with **no** additional stub, and the suite's nine fixtures kept passing), and the
first fixture draft was written while fixing the oracle's own inputs (the checkpoint's **settle** requirement, below).
This packet therefore records the oracle **as it was pinned in practice**: the trigger, the two rules under test and
the red-proof set were fixed before the *final* fixture form, and every deviation is in [`EVIDENCE.md`](EVIDENCE.md).
That is a deviation from "packet strictly before the edit" and it is named here rather than written around.

## Why this increment, and what it is the next step of

`tasks.md` names exactly one item as *why 3.13 stays open* besides the display consumer: fixture 2.12's **`then save`**
clause, whose substance is the last clause in 2.12's list — **a preview is never committed currency**. The structural
half is already shipped: a preview window carries `PlaybackWindowOrigin::Preview` in **both** identities, and the
capture admission counts **committed** windows only (3.7a/I3). What has never been measured is the **save**: the one
consumer that writes a slot's body to storage, at which a preview reaching the writer would persist uncommitted content.
This increment measures it where the clause says — **in the storage harness** — with the window lane linked in the same
TU, which is the combination that has never existed.

## Identity and scope

- **Slice:** 3.13 / increment **I10** (fixture 2.12's save clause). **DEC-114's direction is unchanged.**
- **Owner and production chain:** the preview lane's own publication (`Track::requestPreviewWindow` + the deferred lane
  the fixtures of this suite drive) and the **save**'s production entry for a slot body,
  `StorageLoopIo::writeLoopPersisted(io, loop)` — the function the workspace save calls per slot, whose source selection
  is `Loop::hasSettledPublishedSnapshotForSave()` → `writeLoopSnapshotBodyFormatV1(io, …, *loop.publishedSnapshot())`.
- **Primary file:** `test/test_windowed_preview_integration/test_windowed_preview_integration.cpp` (the two storage TUs
  are linked into it; the linkage is the increment's one enabling change). **No production line is expected to change**;
  a preview reaching the body would be a finding, not a tolerance.
- **Preconditions already present:** 3.13's preview contract and its integration half, 3.7a/I3's production callers,
  `PlaybackWindowOrigin` in both identities, the committed-currency predicate
  (`Track::committedPlaybackWindowServesSlot`), and the storage harness's own round trip
  (`writeLoopSnapshotBodyFormatV1` / `readLoopSnapshotBodyFormatV1`).
- **External blockers:** none. No device leg (the lane's rows are not on the shipping env) and no upload.
- **Non-goals (explicit):** no change to the save format, to the persistence admission, to the preview contract, to the
  identity vocabulary, or to `ExecutionBounds.md`; no device claim; no timing claim; the **display consumer** of a
  preview window stays 3.13's other owed item and is not touched here; no fallback removal (3.7b).
- **Commit boundary:** one commit on the working branch `feature/windowed-playback-generation` — the linkage, the
  fixture and the evidence in the same commit; no PR without an explicit request.

## Oracle fixed before the final fixture form

The oracle is **which content the body carries**, read back through the storage harness's own reader — never "a save
happened".

| Case | Initial state | Ordered actions | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-2.12-save** the save serialises the checkpoint, never the served preview | the suite's long loop (64 bars, six committed notes) with the session's projection established **first** (a root-current session rematerializes with an empty store and has no preview content at all) and served through the preview lane; then a published root, and the loop's queued publications settled (`Loop::hasSettledPublishedSnapshotForSave()`) | (a) read the served window and assert it is the session's projection; (b) read the **checkpoint** the save will embed (the published root at save time) and assert it differs from the projection; (c) read the committed-currency predicate while the preview serves; (d) `writeLoopPersisted` into memory; (e) read the body back with `readLoopSnapshotBodyFormatV1`; (f) read the served window again | (a) the served content equals `sessionMidiEvents()`; (b) the checkpoint is current and settled and its content differs from the projection; (c) `committedPlaybackWindowServesSlot == false` and `committedPlaybackWindowEventCountForSlot == 0`; (d) the write returns true; (e) `read.ok`, **not** `legacy`, and the embedded snapshot's content equals the **checkpoint's** and differs from the **projection's**; (f) the preview is still the served window at its own revision with `origin == Preview` | a body that is the legacy/passes-authoritative one (the checkpoint is not what was written), a body carrying the session's projection, a preview counted as committed currency, or a save that disturbs (cancels, adopts or re-requests) the served preview |

**Preserved invariants (asserted, not assumed).** The save neither cancels nor adopts the preview; the preview's
identity keeps `origin == Preview` across the save; the checkpoint's own admission (`hasSettledPublishedSnapshotForSave`)
is a **precondition read by the fixture**, not something the fixture bypasses; and nothing about the preview is written
into the body.

**The clause's nature, stated:** the save's source selection has **no preview input** at all
(`writeLoopPersisted` reads the published root and the pass set), so "a preview is never saved" holds **by construction**
at that layer. The two rules whose failure *would* let a preview reach a save are therefore the ones this increment
red-proves: the **committed-currency predicate** (origin) and the **body-format/source selection**.

## Controls and coverage

- **Rule reuse:** the lane is driven through its own production seams (`requestPreviewWindow` + the deferred turn, as
  this suite's fixtures do), the save through `writeLoopPersisted`, and the comparison through the storage harness's own
  reader plus `LoopContentResolution::resolveWindow` (the snapshot content resolver).
- **Boundedness:** one session, one mutation, one settle (bounded by 10,000 steps, asserted to finish), one save, one
  read-back.
- **Untestable before link:** none — the whole case is native, and the fixture needs no device, no SD card and no clock.
- **Allocation:** the fixture's own buffers plus the snapshot the reader returns (a native test leaks it deliberately;
  the storage suite's `releasePersistedLoopSnapshotChunks` is not linked here — recorded, not hidden).

## Red-proofs (planned, each restored byte-identically and sha256-checked)

| ID | Mutation | Must fail |
|---|---|---|
| **RP-I10-origin** | `committedServedPlaybackWindowView` ignores the window's origin (a preview counts as committed content) | W-2.12-save (c) |
| **RP-I10-route** | `loopUsesLoopSnapshotBodyFormatV1ForSave` returns false (the slot's checkpoint is no longer embedded) | W-2.12-save (e) |

A mutation that does not fail its fixture is recorded as a non-red-proof with its mechanism named, never reworded.

## Verification recipe

- Focused: `pio test -e native -f test_windowed_preview_integration` (baseline: **9/9**).
- Broad: `pio test -e native` (baseline: **2,851 cases / 7 skipped / 2,844 succeeded / 0 failed**).
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; the approved line is
  `variables 96,192` / `code 392,248 + padding 968` (floor 840). Test-only, so it must be **byte-identical**.
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`,
  `git diff --stat -- src include` (empty for this increment).
- Device: **NOT RUN** (nothing device-visible is claimed).
- Evidence path: `evidence/2026-10-06-s313-212-save-clause/`.
