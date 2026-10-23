# Slice 3.6b — the lag rule (F-3, AC-5): one pass late, and only that

**Change:** `windowed-playback-generation` · **Task:** 3.6 (second increment; the **interval index over pinned bodies**
remains) · **Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.6
**Contract:** DEC-108 items 4, 13(e); RWO `S4C1-READINESS-CURRENCY-CONTRACT.md` **AC-5** / the sibling's **F-3**;
spec *One pass late is the only stale authority*.

**What AC-5/F-3 asks for:** a missed deadline keeps the **previous** version authoritative for that traversal — never
torn, never held, never timing-deferred; a **second** missed traversal is reported as a **named contract breach**, not
as successful one-pass-late operation; a consumer is **never blocked**; and the allowance is **not reset** when a
further edit arrives.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` | `PlaybackLagDecision { Current, PreviousOnePass, BreachSecondMiss }` — **total by construction**, with no waiting/blocking outcome — and `PlaybackLagState`: the interval being traversed, the served and desired revisions, the consecutive misses, and the once-per-episode breach report. |
| `include/TrackPlaybackRuntime.h` | `PlaybackLagState lagState` on the per-slot runtime (the S3.0 pin re-based **per ABI** again: 6,464 → **6,488** on the target, unchanged 6,544 on the host, where the state fitted existing padding). |
| `include/Track.h`, the owner TU | `resolvePlaybackLag` (**the rule, pure**), `advancePlaybackLagTraversal` (**the traversal boundary**), `playbackLagDecisionForSlot`, and the miss/breach reads. |
| the reference fixture | `test_playback_lag_rule_serves_the_previous_revision_once_and_names_a_second_miss`. |

## The oracle, case by case

| # | Case | Observed |
|---|---|---|
| W-20 | the served window carries the desired revision | `Current`; no miss counted, no breach |
| W-21 | a complete current window is published for the boundary | `Current` — the replacement is published *before* the following eligible traversal; through the seam this also **ends the episode** |
| W-22 | the first traversal with no replacement ready | `PreviousOnePass`; the published window is **byte-for-byte unchanged** across the miss (no tear, no hold, no defer) and the previous revision is still what is served |
| W-23 | a second consecutive miss, with a further edit in between | `BreachSecondMiss` reported **once per episode** (a third miss keeps breaching, `breachReports` stays 1) and the further edit did **not** reset the allowance (misses 1 → 2) |
| W-24 | a different interval | a clean episode: misses reset to the new interval's own count and the report flag clears |
| — | totality | the pure resolution answers `Current` / `PreviousOnePass` / `BreachSecondMiss` for every input (`missedTraversals = 250` included): there is no outcome that waits, so a consumer cannot be blocked |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **53/53** (was 52; +1 fixture) |
| `pio test -e native` (full) | **2684 cases: 7 skipped, 2677 succeeded, 0 failed** (baseline 2683 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** on a **clean rebuild** — `variables 96,192` (**unchanged**, no re-base) / `code 391,928 + padding 1,288` / locals 34,880, floor 840, **448 B headroom** |

## Red-proofs (three mutations, each restored byte-identically)

`shasum -a 256 -c` against the pre-mutation hash, `OK` after every restore:

- **M4 — a further edit resets the allowance** (the AC-5 violation, guarded on the *previous* desired revision):
  *"and the further edit did not reset the allowance"* fails (`Expected 2 Was 1`).
- **M5 — a published replacement is ignored**, so a servable boundary is treated as a miss:
  *"a published replacement makes the boundary servable at the current revision"* fails.
- **M6 — the once-per-episode guard dropped**:
  *"a third miss keeps breaching without re-reporting"* fails (`Expected 1 Was 2`).

## Recorded rather than smoothed over

1. **The first M4 attempt was a no-op, and that is worth keeping in the record.** The mutation was inserted *after*
   `lag.desiredRevision = desiredRevision;`, so its comparison could never differ and the fixture stayed green. A
   mutation that cannot trigger is not evidence; it was re-placed before the assignment and only then red-proved. The
   same discipline applies to any mutation whose condition is derived from the state the code just wrote.
2. **`Current` means "this boundary may serve the current revision", not "adoption happened".** The decision is the
   lag rule's; the swap itself is the consumer switch (3.7) and the adoption edge (3.4). The packet says so in the
   non-goals so the distinction is not read out of the fixture.
3. **The breach is a report, not a decision** (the S4b.7 hold terminal's shape): it is named once per episode through
   the existing row emitter (`reportPlaybackViewMaintenanceRow(..., "lag_breach")`), the previous revision keeps being
   served, and nothing partial is published. Under a native build without `SESSION_CAPTURE` the emitter is a no-op, so
   the fixture pins the **count and flag**, not the row text.
4. **The builder/reader clauses are still conservative, not yet precise.** 3.6a's release predicate holds all retained
   sources while any build is outstanding, and the lag rule likewise decides from the published window and readiness
   rather than from per-builder progress. Both are named as owed refinements; neither is claimed as finished.

## Not claimed

- **The interval index over pinned bodies** — the remaining clause of 3.6 (later intervals constructible from the
  retained revision with no resident whole-loop root; both affected ranges; atomic publication with the source
  revision; inspected-work budgeting). Nothing here claims interval-directed lookup.
- **Task 3.6 is not checked**: two of its clauses are green, the index is not implemented.
- No production caller: the traversal boundary is a seam, like the window lane and the adoption edge.
- No timing/deadline measurement (F-2 / task 2.2 stays open), no envelope claim beyond the per-slot figure, no device
  evidence, no flash, no stimulus.
