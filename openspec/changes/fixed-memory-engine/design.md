# Design — fixed-memory content engine

The architecture baseline is [plan revision 3](../../../docs/Plans/2026-11-04-fixed-memory-convergence.md), approved
by the owner on 2026-11-04. This file records the **contract clauses that tests settle** after that approval. It
replaces nothing in the plan except where a clause below says so. Clauses are amended individually; there are no
whole-plan revisions (owner, 2026-11-04).

## Clause status

| Id | Clause | Status | Settled by |
|---|---|---|---|
| CE-A1 | Writer reservations track **outstanding unwritten obligations**, not a standing 160-row block: a NoteOn reserves its own NoteOff from free capacity before it is written; a NoteOff consumes its reservation; capacity is replenished only by being free. Reservation is converted to **pages per run** (data pages + directory pages to append the run's outstanding rows to its current page); the pool invariant is `free pages ≥ Σ_runs pagesNeeded(run, outstanding rows)` plus the fixed reserves. | **Settled (host)** | `test_content_engine_core`: long sequential take, 128 open, closing runs, Stop, exhaustion, random fuzz |
| CE-A2 | New input is refused only at the first row of a new note or message; an outstanding NoteOff can never fail. | **Settled (host)** | same |
| CE-B1 | Window effective-row bound. Revision 3's `2,816 + 128 ≤ 3,072` is **not** a bound: generated NoteOffs come from shortened notes whose source NoteOff lies outside the window, and their number is bounded by source notes crossing the window edges, not by sounding polyphony. | **Open — counter-example recorded** | `test_engine_semantic_oracle` `window_effective_rows_exceed_source_plus_polyphony` |
| CE-C1 | Physical output: per-port admission and overload policy. | Open | — |
| CE-D1 | Scheduling: measured grain costs; input-drain responsiveness separate from window deadlines. | Open (needs device measurements) | — |
| CE-E1 | Display tiles hold **identity stamps only, no pins**. Tile content is self-contained. Only an in-progress sweep cursor pins its manifest, and it is counted as one of the 18 compose cursors. ManifestTable worst case: 64 + 99 + 32 + 24 + 24 + 18 + 3 = **264 ≤ 288**. | **Settled (design)** | — |
| CE-E2 | Live occupancy is a **count per pitch** (u8, 128 per track), not a bit: two held notes on one pitch, for example on two channels, release independently. | **Settled (design)** | — |
| CE-E3 | Derived occlusion reproduces the ratified overlap rules: same-pitch overlaps, equal tick, wrap-crossing, channel ignored, min-length hide. | In progress (E0 fixtures, draft) | `test_engine_semantic_oracle` |
| CE-E4 | C1 owed-pitch dependencies. | Open (E0) | — |

## Storage core (E1) as built

- `ContentEngine::PagePool<Pages, RowsPerPage>`: fixed pages, LIFO free stack, **per-page owner tag**. The tag exists
  so that conservation can be checked by ownership, not only by count.
- `ContentEngine::RunTable<Runs, Pool>`: global run descriptors with 16 inline pages and directory pages drawn from
  the pool (each directory page holds 512 page ids). Refcounts, free list, page reclamation in bounded grains.
- `ContentEngine::ManifestTable<Manifests, MaxRuns>`: global manifests with monotonic identity, refcounts, and reader
  pins as a subset of refs. Releasing the last ref releases each run once.
- `HistoryRing` and `SlotTable`: not built yet; they land with undo/redo semantics (E4).
- `ContentEngine::RowReservations`: the CE-A1 ledger.
- Every table has an explicit `init()`. RAM2 and EXTMEM are not zeroed at reset, and the core tests run `init()` over
  poisoned storage.

## Measured E1 sizes vs plan revision 3 budget

| Region | Plan § 4 | Measured (host, 64-bit layout of the same fixed arrays) | Delta |
|---|---|---|---|
| Page data | 4,194,304 B PSRAM | 4,194,304 B | 0 |
| Pool metadata | 8,192 B PSRAM (free stack) | 16,386 B (free stack + owner tags) | **+8,194 B PSRAM**, taken from the non-content allowance: 804,092 → 795,898 B on capture builds |
| Run descriptor | ≤ 128 B | 64 B (16 inline + 8 directory page ids) | RunTable 35,912 B vs 69,632 B budgeted: **−33,720 B RAM2** |
| Manifest | ≤ 148 B | 148 B | ManifestTable 43,208 B (incl. free list) vs 42,624 B: +584 B RAM2 |

The arrays are fixed-width integers, so the layout is the same on the Teensy. The device census at E10 is the gate.

## CE-B1 — what is known

- The exact identity, executable in `test_ceb1_effective_row_identity_holds`:
  `effective(W) = source(W) − hidden rows(W) − suppressed source NoteOffs(W) + generated NoteOffs(W)`.
- `generated NoteOffs(W)` counts notes shortened inside W. Each one is an effectively sounding note (carried in at the
  window start, or started inside W) that a later pass's NoteOn occludes. Run order serializes them, so one window can
  hold 128 per earlier batch, and batches are limited by passes, not by polyphony.
- Candidate repairs, to be decided by test, not by assumption:
  1. admit by an **effective** per-window count kept by the writer (a NoteOn that shortens notes adds one row per
     shortened note to that window's count, computed from the live occupancy and ledger, which are O(1));
  2. bound the passes that can stack inside one window;
  3. split a window's emission across two buffers when generated rows overflow.
