# Slice 3.14b — the committed-pass identity source (the session case's stuck-note risk)

**Change:** `windowed-playback-generation` · **Task:** 3.14 (**second increment; the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.14
**Contract:** DEC-108 §13(g); the design's *Runtime continuity dependency and undo/redo integration*.
**Follows:** [`evidence/2026-10-02-s314-undo-audible-handoff/EVIDENCE.md`](../2026-10-02-s314-undo-audible-handoff/EVIDENCE.md),
which removed the blanket silence for the four **edit-batch** sites and named this as the remaining half: *"until that
exists, undoing an overdub session can leave a note sounding whose content the undo removed"*.

---

## What landed

| File | Change |
|---|---|
| `include/Loop.h` | `overdubSessionPassIdAt(index)` / `overdubSessionCompanionIdsAt(index)` — the pass a **session step just changed** and the companion edit batches it changed with it. Out-of-range answers are **named** (`kInvalidPassId` / an empty list), never clamped to a neighbouring step. |
| `src/Track/TrackPlaybackWindowBuild.cpp` | The settlement is now one shared core — `settleLedgerAgainstPresence(ledger, affected, identitiesAreRemoved)`, a single O(open-notes) ledger walk — used by **both** reconcilers; `collectCommittedPassIdentities(loop, passId, out)` reads a committed pass's NoteOn identities from its **own bodies**, a row at a time (`chunkUsedCount` + `readSealedChunkEvent`), bounded and allocation-free; and `Track::reconcileAfterCommittedPassStateChange(slot, passIds, passCount, identitiesAreRemoved)`. |
| `src/TrackUndo.cpp` | The generic refresh is replaced by `refreshPlaybackAfterCommittedPassStateChange(track, slot, passIds, passCount, identitiesAreRemoved)`, and **both** overdub-session sites now settle: the step's committed pass (its bodies) **and** its companion edit batches (their added events), each with the direction's own flag. |

**The design points that carry the requirement:**

- **The stuck-note risk is closed.** A session step's removed identities are now the ones its disabled pass actually
  carries, read from that pass's own bodies — so a note the undo removed **closes**, and the ledger cannot keep a
  phantom sound alive with no note-off coming. (The pass keeps its chunk references while disabled — "keep disabled
  capture passes on the timeline so redo can re-enable them" — which is exactly what makes the removal settleable.)
- **One core, two sources.** Whether the identities come from an edit batch's added events or a committed pass's bodies,
  the classification is the same single walk: closed / suppressed / preserved. A divergence between the two cases would
  now be a divergence of *input*, not of rule.
- **The step's pass is named by the session's own cursor**, and the two directions read it at opposite moments —
  `undoOverdubSession` decrements first, `redoOverdubSession` reads before incrementing. That asymmetry is stated in
  the accessor's doc and handled defensively: the redo site's "one before the cursor" index answers `kInvalidPassId`
  when the live-undo branch (which never moves the cursor) was the one that ran, so no neighbouring pass is ever
  settled by accident.
- **Nothing about undo semantics moved:** the state change, the revision bump and the stack discipline are untouched;
  the settlement still runs where the undo already mutates the timeline (off-clock), allocation-free.

## The oracle, case by case

| Case | Observed |
|---|---|
| a committed pass whose own body carries a sounding note | `closureCount == 1`, exactly **one** NoteOff on the wire for that note, and its ledger obligation discharged |
| a sounding note that pass does not carry | **preserved**, still in the ledger |
| the re-establishing direction | `closureCount == 0`, the re-established note **suppressed** (not retriggered), and **nothing** emitted |
| a refresh with no affected identity | emits nothing and leaves the soundings alone |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **64/64** (the 3.14 fixture was **extended**, not added) |
| `pio test -e native` (full) | **2695 cases: 7 skipped, 2688 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` (clean rebuild) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom**. Byte-identical to the four previous slices: FLASH-placed cold additions, no ITCM growth. **No size pin moved** — the `Loop` accessors add no state, and the build's own `static_assert(sizeof(Loop) == 984)` is what proves it |

## Red-proofs

Six mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hash of all seven files):

| # | Mutation | Observed failure |
|---|---|---|
| M36a | the blanket silence restored on the edit-batch path | `Expected 1 Was 3` — a NoteOff for *every* sounding note |
| M37 | both refreshes clear the ledger again | *"a sounding note the batch never named is untouched"* |
| M40 | the committed-pass identity source is empty | `Expected 1 Was 0` — *"the pass's own sounding closes"* |
| M42 | the re-issuing direction closes instead of suppressing | `Expected 0 Was 1` — *"a re-establishing change closes nothing"* |
| M43 | the presence filter inverted | `Expected 60 Was 64` — the wrong note closed |
| M44 | the committed-pass refresh clears the ledger | *"and it leaves the soundings it did not reconcile alone"* |

**Two harness bookkeeping notes, recorded rather than left implicit.** (1) When the ledger walk moved into the shared
core, the previous slice's M38/M39 anchors no longer matched, and the generic refresh M36b targeted was deleted: their
successors here are **M42/M43** (the core) and **M44** (the committed-pass refresh), so every claim those mutations
covered is still red-proved — on the code as it now stands. (2) M44's first attempt reported "green" because its anchor
was in the wrong **file** (the refresh lives in `TrackUndo.cpp`, not the window-build TU), i.e. the mutation was never
applied; re-aimed at the right file it fails as it must. A "passing" mutation run with an unmatched anchor is not
evidence, and the `shasum` oracle plus the occurrence-count assertion are what make that visible.

## Not claimed

- **The refresh is still not a request through the immutable publication owner.** Today's refreshes invalidate the
  cached representation and settle the closures; the *request* needs a bounded content source for the affected
  interval. The two candidate sources are now both visible in the code: the **owner's own companion passes**
  (`RuntimeCompanionPasses`, no caller buffer — but whether it is authoritative for a slot at undo time is **unverified**),
  and the **existing request path's caller-owned enumerated scratch** (3.2's shape — correct but a cold content-sized
  allocation on a gesture). Choosing between them is a design decision the packet must state before the code takes it;
  taking either silently is what this line of work has avoided throughout. Named as **3.14c**.
- Also owed: the between-grains matrix — repeated undo/redo, track/slot switching and stale completion while real
  preparation grains are paused (needs 3.7's serving path); retained-source lifetime and discharge (3.6a's release
  predicate — cited, not restated); displayed provenance; and preview-session undo/redo (3.13's preview discipline /
  fixture 2.12 — cited).
- No timing claim and no device evidence; the RAM1 headroom remains 48 B above the floor.
- **Task 3.14 stays open** on the items above.
