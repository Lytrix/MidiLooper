# Slice 3.14c — the refresh's source question, verified

**Change:** `windowed-playback-generation` · **Task:** 3.14 (**third increment; the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.14
**Contract:** DEC-108 §13(g); the design's *Runtime continuity dependency and undo/redo integration* — "a request through
the existing immutable publication owner and the cross-generation reconciliation above".
**Follows:** [`…-s314-undo-audible-handoff`](../2026-10-02-s314-undo-audible-handoff/EVIDENCE.md) and
[`…-s314b-committed-pass-source`](../2026-10-02-s314b-committed-pass-source/EVIDENCE.md), which named this as the
remaining item: *"taking either [source] silently is what this line of work has avoided throughout."*

**This increment changes no production code.** It answers the source question with the code's own terms and pins the
measured consequence in the fixture, because the last two increments both ended at the same open decision.

---

## The question, and both candidate answers

To make the undo/redo edge *request* a refreshed window through the immutable publication owner, the request needs a
bounded content source for the affected interval. Two candidates were visible in the code:

| Candidate | What the code says about it |
|---|---|
| `RuntimeCompanionPasses` (the build's no-caller-buffer source) | *"DEC-060: **caller freezes** active passes in canonical merge order … and keeps their descriptors/events and rows immutable **until done**"*; `ready(source)` requires `phase_ == Complete && valid() && source == expected_`; `begin(...)` requires `inputs.ready()` / `rows.ready(capture)`; `matchesOwners` compares the *owner pointers and generations*. Verdict: **an operation-scoped preparation**, valid only for the exact source identity it was prepared against. |
| `RuntimeCanonicalPassInputs` | Its own docs: *"Borrow live counters; **invalidate before pass/history mutation or release**"*, *"Owners and immutable pass content must outlive discard; **advance revision/epoch before mutation or release**"*, and `FrozenSource` *"does not retain chunks"*. Verdict: **the same**, by construction. |

**Both are rejected, on the code's own terms rather than on a preference:** the undo edge's act *is* a pass/history
mutation — the very thing those structures document as invalidating them. Building a refreshed window from either would
read content the same gesture has just invalidated, which is the class of silent-wrong this change has refused
throughout. So the no-allocation candidate is not available at this edge, and the enumerated-scratch candidate is the
only one whose input is *by definition* the post-change content.

## And the enumerated-scratch candidate is gated, measurably

Driving the production route in the fixture (a served window, then a real `TrackUndo::undoForLoop`) shows why the edge
does not enumerate either — **the published root lags by design after an undo**:

- the served window stops being `Ready` and the lane owes a rebuild **in the owner's own vocabulary** (asserted);
- `Track::requestPlaybackWindow` then answers **`SourceNotCurrent`** — a **named** refusal, never a lagging root served as
  current. That is DEC-073's protection ("a root from an older committed-pass state can never be stamped current by the
  selection itself"), which is exactly the behaviour task 4.1 is asked to land as *its* lagging-checkpoint outcome.

So the refresh's requester is the lane that **owns a current root** — i.e. after **4.1** (undo parity, lagging root named)
the **3.7** consumer/traversal lane can request it with the interval and the phase it already knows. Asking the gesture
edge to enumerate would (a) need a content-sized cold allocation, (b) source from structures the gesture invalidated, or
(c) request against a root that is deliberately not current — and the third of those is not merely unavailable, it is
**refused by name**, which is the system telling us where the requester belongs.

## What the edge therefore owes — and already does

- **Currency:** the pass state change advances `playbackRevision`, and the refresh advances the generation
  (`invalidatePlaybackMergedMidiEvents(true)`), so the served window is *stale by revision* — asserted as "not `Ready`".
- **Closures:** the settlement (`closed` / `suppressed` / `preserved`) runs **before** the refresh, from the change's own
  identities, so an unchanged sustained note is never closed or retriggered and a removed one is closed (3.14a/3.14b).
- **The ledger survives** the cache invalidation, because it *is* the closure-obligation set.
- **Nothing is silenced**, and nothing is enumerated at the edge.

## The oracle, case by case (the new assertions)

| Case | Observed |
|---|---|
| a served window, then a real undo | readiness is **not** `Ready` — the lane owes a rebuild, named in the owner's vocabulary |
| the refresh request at the edge | **`SourceNotCurrent`**: the lagging root is named, never served as current |
| after the refusal | the owner's lane owes nothing extra in flight (`hasPendingPlaybackViewMaintenance()` false) — a refusal changes nothing |
| the undo's own precondition | the loop's revision **advanced** (the currency the handoff relies on) |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **64/64** (the 3.14 fixture was **extended** again, not added) |
| `pio test -e native` (full) | **2695 cases: 7 skipped, 2688 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` (clean rebuild) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` (48 B above the floor), byte-identical to the five previous slices: **this increment adds no production code at all**, and no size pin moved |

## Red-proofs

Three mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hash of all seven files):

| # | Mutation | Observed failure |
|---|---|---|
| M45 | a lagging root is served as current (the not-current guard dropped) | `Expected 6 Was 0` — *"a lagging root is named, never served as current"* |
| M46 | the undo no longer advances the revision (the currency the handoff relies on) | *"fixture precondition: the undo advanced the loop's revision"* |
| M47 | the edit-batch site stops settling (silence removal alone) | `Expected 1 Was 0` — *"exactly one closure goes out, for the identity the batch owned"* |

M45 and M46 deliberately attack **other owners' code** (`requestPlaybackWindow`'s not-current guard; `setEditPassState`'s
revision bump) because those are the facts the handoff's correctness rests on — a fixture that only fails when *its own*
slice is undone would not be testing the claim it states.

## Not claimed

- **No production code changed here.** This increment is a verification and a naming: its deliverable is the *reason*
  the edge does not enumerate, plus the fixture assertions that pin the consequence. Recorded as such rather than
  presented as an implementation.
- **The refresh is still not performed at the undo edge**, now with the evidence for why: the requester belongs to
  **4.1** (a current root for the restored revision, with the lagging case *named*) and then **3.7** (the consumer). That
  is an ordering statement backed by two measurements, not a deferral for convenience.
- Also owed, unchanged: the between-grains matrix (repeated undo/redo, track/slot switching, stale completion while real
  preparation grains are paused — needs 3.7's serving path), retained-source lifetime and eventual release (3.6a's
  release predicate — cited), displayed provenance, and preview-session undo/redo (3.13's preview discipline / fixture
  2.12 — cited).
- No timing claim, no device evidence, and the RAM1 headroom remains 48 B above the floor.
- **Task 3.14 stays open** on the items above; its **audible** requirement and the red-proof the task demands ("restore
  the blanket silence") are complete.
