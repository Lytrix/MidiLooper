# Slice 3.6c — the interval index over pinned bodies

**Change:** `windowed-playback-generation` · **Task:** 3.6 (final clause — this increment checks the task off) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.6
**Contract:** DEC-108 item 14(c); spec *One pass late is the only stale authority* (the index/retention paragraph) and
*A window is served without whole-root residency*.

**What the spec asks for:** later intervals stay constructible from the retained revision **through an index over
pinned bodies, with no resident whole-loop root**; lookup is **interval-directed** (total work does not grow with
unrelated loop content) and **includes edit-moved events at their effective destinations**; index updates cover both
the old and the new affected ranges and publish **atomically with the source revision**, never modifying an index a
reader holds; and the index's representation change, ownership, **resident-memory bound** and construction/update
lifetime are stated explicitly — with `TickIndex`'s whole-history event-copy storage **not** inherited and
`CommittedEventRange` used as the body-reading primitive.

---

## What landed

| File | Change |
|---|---|
| `include/PassReclaim.h` | `RetainedIntervalIndex`: **one bit per bar** ("any pinned body carries content here"), plus the revision/generation stamp, the indexed bar count and the folded-span count. `kRetainedIndexMaxBars = 512` (16 words) is the resident bound. |
| `include/TrackPlaybackRuntime.h` | `retainedIndex` on the per-slot runtime — the S3.0 pin moved per ABI again (target 6,488 → **6,512**, +24 B; the host stayed 6,544). |
| `include/Track.h`, the owner TU | `buildRetainedIntervalIndexForSlot` (typed results incl. `BarsUnsupported`), `queryRetainedIntervalForSlot` (typed results incl. `RevisionMismatch`), and the build wired into `retainPlaybackSourceForSlot` so the index is **published with the source revision**. |
| the reference fixture | `test_retained_interval_index_is_interval_directed_and_refuses_stale_revisions`. |

**The representation decisions, stated as the spec requires.**

- **Descriptor, not a copy.** One bit per bar; no events are copied. `TickIndex`'s whole-history storage is not
  inherited; `CommittedEventRange` remains the body-reading primitive, and the fixture demonstrates both halves: the
  index directs, the pinned chunk lists answer.
- **Interval-directed by construction.** A query counts the interval's bars that the pinned bodies cover and the bars
  that are **skipped without touching a chunk** — the property that keeps lookup cost independent of unrelated loop
  content. The fixture's deep interval reports 0 covered, 2 skipped, and the body primitive agrees the interval is
  empty.
- **Edit destinations are folded in.** An `EditPass` carries no chunk bodies (a moved note's events stay in the capture
  bodies), but its `[startTick, endTick]` **destination span** does — folding it is what stops the index skipping a bar
  whose only effective content is a moved note. Applying the edit (ordering, effective content) stays the
  effective-content resolver's job. Over-marking is the chosen error direction: it costs inspection, never a missed
  event.
- **Atomic publication with the source revision.** The build fills a *candidate* and publishes it with a single
  assignment, with the revision stamped inside the candidate — so no reader can see one revision's bits under another's
  stamp, and an index a reader already holds is never mutated.
- **Update strategy: a full rebuild per admitted revision**, not a dual-range patch. That covers **both** the old and
  the new affected ranges by construction; the cost is bounded by the bar ceiling. Recorded as the explicit
  representation choice the spec asks for.
- **Resident bound and refusal by name.** `kRetainedIndexMaxBars = 512` bars (16 words per bitmap). Beyond it the build
  **and** the query refuse (`BarsUnsupported`) and **no index is left behind**, because an index that answered part of a
  loop would present the complete scan as window-bounded — the spec's own clause, which puts that scan's accounting in
  admission.
- **Lifetime** is the revision the index describes: a query for another revision is `RevisionMismatch`, never a stale
  answer.

## The oracle, case by case

| # | Case | Observed |
|---|---|---|
| W-25 | an interval over a bar whose pinned bodies carry content | `Indexed`, 1 covered, 0 skipped |
| W-26 | an interval two bars deep where the pinned bodies carry nothing | `Indexed`, **0 covered, 2 skipped** — and the body primitive confirms the interval is empty from the pinned lists alone, with no published root consulted |
| W-27 | an active edit whose destination is that otherwise-empty bar | after the rebuild, the destination bar is **covered, not skipped** |
| W-28 | a query for another revision; a 513-bar loop | `RevisionMismatch` (0 covered, 0 skipped — nothing answered); `BarsUnsupported` for the build and `NoIndex` for the query afterwards |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **54/54** (was 53; +1 fixture) |
| `pio test -e native` (full) | **2685 cases: 7 skipped, 2678 succeeded, 0 failed** (baseline 2684 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** on a **clean rebuild** — `variables 96,192` (**unchanged**, no re-base) / `code 391,928 + padding 1,288` / locals 34,880, floor 840, **448 B headroom** |
| the S3.0 pin | re-based per ABI again: target 6,488 → **6,512** (measured with a temporary compile-time probe, removed before the commit); host unchanged at 6,544 |

## Red-proofs (three mutations, each restored byte-identically)

`shasum -a 256 -c` against the pre-mutation hash, `OK` after every restore:

- **M7 — the edit destinations are not folded**: *"the edit's destination bar is indexed, not skipped"* fails
  (`Expected 1 Was 0`).
- **M8 — the revision guard dropped** (a stale index answers as current): the `RevisionMismatch` assertion fails
  (`Expected 2 Was 0`).
- **M9 — every bar reported covered** (the scan the index exists to avoid): *"no pinned body covers this interval"*
  fails (`Expected 0 Was 2`).

## Task 3.6 — checked, with its limits named

All four clauses of 3.6 are green: the **retained source** in explicit roles with one release predicate (3.6a), its
protection as read by the reclamation consumer **and** on the direct reset path, the **interval index over pinned
bodies** with the representation/bound/lifetime stated and interval-directed, edit-destination-aware lookup (3.6c), and
the **lag rule** with the named second-miss breach (3.6b). Two limits are recorded rather than smoothed over:

1. **The consumer path is still 3.7's.** The index and the retention are seams: nothing in production requests a window
   yet, so the end-to-end "a consumer builds a later interval from the pinned revision" path is not exercised by a
   production caller. The fixture drives the two halves directly (index answer + body enumeration from the pinned lists).
2. **The envelope roll-up of these figures is RWO's.** The per-slot charge is stated and pinned here (6,512 B target:
   +96 B sources, +24 B lag, +24 B index), which is what DEC-108 item 4 asks this change to charge; the roll-up into
   RWO's budget artifact belongs to that change's own terms.

## Not claimed

- No event-level effective-content resolution in the index (the resolver's job, cited not re-proven); no
  inspected-work figure beyond the bar counts the query reports; no deadline/timing measurement (F-2 stays open); no
  device evidence, no flash, no stimulus.
