# Slice 3.12b — the bounded legacy compatibility path (pages + resumable cursor)

**Change:** `windowed-playback-generation` · **Task:** 3.12 (**second increment; the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.12
**Contract:** DEC-108 §14; the spec's *Legacy dense content is served through bounded pages*; fixture **2.11**'s paged
half and the shape fixture **5.5** will measure.

**What the task asks of this clause:** content that exceeds `L` — and any interval that outgrows one page — served
through **multiple fixed-capacity immutable event pages** consumed by a **resumable cursor** that preserves source
revision, ordering and **exactly-once** emission across page boundaries, prepared **off-clock** from bounded preallocated
storage, with no truncation, no tick delay and no inline rebuild.

---

## What landed

| File | Change |
|---|---|
| `include/WindowPagedStream.h` (new) | `WindowEventPage` (a preallocated fixed-capacity buffer + revision + sequence), `WindowPageCursor` (the consumer's `(sourceRevision, nextSequence, offsetInPage)`), and `WindowPagedStream` — a **ring of three pages** with `produce(...)` (one bounded slice per call, through the **existing** `CommittedEventRange::appendWindowBudgeted` + `WindowTraversalCursor`) and `consume(...)` (one bounded slice per call, continuing exactly where it left off). Typed outcomes: `Produced` / `SourceComplete` / `RingFull` / `StaleRevision` and `Emitted` / `NeedsPage` / `StaleRevision` / `Exhausted`. |

**The design points that carry the requirement, and why each is the honest shape:**

- **Bounded memory, unbounded content.** The ring is the bound (`kWindowPageCount = 3` × `kWindowPageCapacity = 128`),
  and a **drained** page returns to it; that is why segmentation proves a *storage* bound rather than throughput (task
  5.5's own phrasing). Nothing allocates after `reset`.
- **No inline rebuild, and no second traversal.** Production reuses `CommittedEventRange::appendWindowBudgeted` with its
  own resumable `WindowTraversalCursor` — the task 3.8 audit's "reuse the resumable machinery" applied to this clause
  rather than a parallel enumerator.
- **Exactly-once and ordering from positions alone.** The consumer's `(sequence, offsetInPage)` is the only position, so
  re-entering continues instead of repeating, and a page is never written after it is published.
- **No blocking state.** A consumer whose next sequence is unpublished answers `NeedsPage` (the caller produces); there
  is no wait to fall into, so the clock path can never be delayed by unprepared content.
- **Generation-qualified.** A cursor carrying another revision is refused by name (`StaleRevision`) on both sides, so a
  returning slot cannot resume an earlier operation's stream — the same rule as the shared-view identity (3.11).

## The oracle, case by case

| Case | Observed |
|---|---|
| content larger than the ring (700 events; six pages at 128) | every event is emitted, **in source order**, with the page high-water mark ≤ the ring (3) — the ring turns over and each event appears exactly once |
| the ring's bound | `pagesPublished()` never exceeds `kWindowPageCount`; a producer with no free page is told `RingFull` rather than allocating |
| a consumer that arrives before preparation | `NeedsPage` — never a block, never a delay |
| a cursor from another revision | `StaleRevision` from **both** `consume` and `produce` |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **57/57** (was 56; +1 fixture) |
| `pio test -e native` (full) | **2688 cases: 7 skipped, 2681 succeeded, 0 failed** (baseline 2687 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** on a **clean rebuild** — `variables 96,192` **unchanged** / `code 391,928 + padding 1,288` / locals 34,880, floor 840, **448 B headroom**. The new header has **no production caller**, so it adds nothing to ITCM or DTCM, and the S3.0 pin is untouched |

## Red-proofs

`shasum -a 256 -c` against the pre-mutation hash, `OK` after both restores:

- **M15 — the drained page is not returned to the ring**: the consumer stalls (*"a consumer never waits for
  preparation: it asks for a page"* fails) because production hits `RingFull` with no page ever reusable.
- **M16 — the cursor does not advance on drain**: the same page would be emitted again; the same assertion fails for the
  same reason (the stream cannot progress), which is the observable end of "exactly-once is the cursor's property".

**A test gap that the first M15 attempt exposed, recorded rather than skipped.** With 300 events the fixture filled
exactly three pages and drained them once: the **ring never had to turn over**, so M15 left the suite green. That is a
fixture defect, not a code property — a mutation that cannot trigger is not evidence. The fixture now commits **700**
events (six pages) so reuse is required, and only then did both mutations red-prove. The same discipline as 3.6b's
no-op mutation: measure the mutation, and fix the *test* when it turns out not to discriminate.

## Not claimed

- **No production consumer**: the paged stream exists and is exercised directly; the lane that would serve over-`L`
  intervals through it (and the placement/charging of the ring in the per-slot account) is **not** wired. Fixture 5.5's
  timing half — page preparation, page handoff and MIDI emission each meeting their deadlines — is therefore **not**
  measured here, and this slice makes no timing claim.
- The ring's size (3 pages of 128) is a **declared, unmeasured** choice; task 6.3's sweeps are what may move it.
- No over-capacity same-tick, wrap or cross-page-closure case yet: those are fixture 5.5's, with the timing envelope.
- **Task 3.12 stays open**: the MIDI-edge wiring of the admission limit (3.12c) and fixture 5.5's envelope remain.
