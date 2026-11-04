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
| CE-B1 | Span windows (START + carry / TERMINAL entries) with a fixed terminal set of **T = 16** sounding output instances per track (OUT-POLY, owner-ratified end-state constant). `entries(W) ≤ startRows(W) + 16 = 2,816 + 16 = 2,832 ≤ 3,072` (`static_assert` in `EngineConfig.h`; tight in `test_span_window_bound_is_tight`). The 3,072-entry buffer is kept pending E3 qualification. | **Bound settled with OUT-POLY**; the E3 composer and its carry list are not built yet | `test_engine_window_bound` (8), `test_content_engine_terminals` (12) |
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

## CE-B1 — representation comparison (executable: `test_engine_window_bound`)

| Option | Counter-example (20 stacked passes, 2,560 source rows in one window) | Worst case in W | Cost |
|---|---|---|---|
| Row windows (rev 3) | 4,992 rows → **overflows 3,072** | ≈ 2 × source starts | — |
| (1) effective-count admission | admits 12 of 20 passes; **refuses 1,024 notes** a user played | holds 3,072 | changes accepted-input behavior by stacking depth |
| (2) bound stacked passes per window | same 12-pass limit | holds 3,072 | same refusal, coarser |
| (3) split buffers | needs 5,632 rows per window | holds | **+983,040 B PSRAM** (24 windows) |
| **Span windows + terminal set** | storage never refused; ≤ 2,832 entries; output beyond 16 sounding suppressed by OUT-POLY | **startRows + 16 ≤ 3,072, tight at 2,832** | 0 extra PSRAM; terminal set 16 × 12 B per track |

Properties shown on the reference model, compared with the expanded-row stream:
- **Exact stream:** DEC-244 order at every tick (NoteOffs fired from the terminal set first, then START entries in pass
  and id order).
- **Identity:** each NoteOff carries its own NoteOn's pitch and channel (Q-OVL-2).
- **Wrap notes:** the end lands in the next cycle's window as a TERMINAL entry.
- **Held notes:** one START with an open end, nothing in between, one TERMINAL.
- **Cross-window shortening:** the generated NoteOff is the TERMINAL entry.
- **DEC-148 cold start:** TERMINAL entries for notes never sounded are ignored.
- **Output admission:** a NoteOn is admitted only while fewer than T admitted notes sound; the same rule in composer
  and consumer keeps them identical.

Consequences that need the owner's acknowledgement:
- T = 256 admitted sounding notes per track is a new **named output limit**. Legacy sends notes beyond its 128-entry
  ledger, which only overflows its bookkeeping. Under the span model, note 257 sounding at once is refused at output,
  and the stored content is unchanged.
- A cold start inside a window whose composition refused notes may under-fill polyphony relative to a from-zero
  playback (the composer decided admission). This only applies while the T limit is binding.
- A writer's still-open note in a composed prefix gets its end from the writer's NoteOff at run time (late terminal,
  O(1) in the terminal set). This path is modeled in the design but not yet in the reference test.

## Effective polyphony (CE-B1 remaining proof) — analysis

1. **Lemma (tested, `test_same_pitch_notes_of_different_passes_never_overlap`):** under OVL-1..3, two effective notes
   of the same pitch from different passes never sound at the same tick. The later pass either shortens the earlier
   note (it was sounding) or hides it (it started inside).
2. **So at any tick** each pitch is sounded by **one** pass. Polyphony(t) = Σ over pitches of that pass's sounding notes
   on that pitch.
3. **Per pass**, sounding notes at one position are ≤ 128, its writer's open-note cap. A run that spans owed-commit
   cycles holds ≤ 8 segments, so at most 8 × 128.
4. **Different pitches can come from different passes**, so polyphony ≤ 128 × (passes contributing) and is **not**
   ≤ 256. Constructed: 3 passes × 128 notes on 3 pitches = 384 sounding
   (`test_polyphony_exceeds_t_and_out_poly_refuses_by_name`).
5. **Undo / redo:** a manifest is a subset of passes; the lemma and the bound hold per manifest.
   **Cold start:** only notes starting at or after the start position sound, so it is never above the steady state.

Conclusion: no admission rule on *input* can guarantee ≤ T at every future tick, because a later pass's notes on
other pitches add to sounding content recorded earlier. The bound therefore needs a narrowly specified output policy.

## OUT-POLY — output polyphony (owner decision 2026-11-04, ratified end-state constant)

- **T = 16** simultaneously sounding MIDI output note instances per track (`Config::kMaxSoundingPerTrack`). It is not
  configurable, and it will not be raised for hypothetical polyphony.
- It limits **sounding output only**: never source recording, immutable storage, undo history or piano-roll rendering.
- When 16 instances already sound, the next output NoteOn is **suppressed deterministically in DEC-244 order**
  (composer and consumer apply the same rule), counted (`refused` per window, `refusedFull` per track).
- No NoteOff is ever sent for an unsounded instance. Every emitted NoteOn keeps closure ownership until its NoteOff is
  sent: composed end, live NoteOff, transition closure, or occupy.
- Physical-port backpressure delays NoteOffs, which stay owned and are counted late. It never drops them
  (`test_port_backpressure_delays_noteoffs_and_drops_none`). The port bandwidth policy for NoteOns is CE-C1.

## RAM2 recalculation (OUT-POLY T = 16)

| Item | Revision 3 / earlier | Now |
|---|---|---|
| Terminal sets | 256 × 12 B × 8 = 24,576 B (comparison table, never in the § 4.2 total) — **removed** | `TerminalScheduler<16>` measured **352 B per track**: 192 B instances (16 × 12) + 128 B per-pitch occupancy counts + counters → **2,816 B** for 8 tracks; instances alone 1,536 B, as the owner computed |
| RunTable | 69,632 B | 35,912 B (measured, 64-B descriptors) |
| ManifestTable | 42,624 B | 43,208 B (measured) |
| **RAM2 statics after cutover** | 225,581 B | **195,261 B** (225,581 − 33,720 + 584 + 2,816) |
| Heap left for libraries | 298,707 B | **329,027 B** |
## Live / composed closure ownership (`TerminalScheduler`, tested)

| Case | Rule | Test |
|---|---|---|
| Writer note still held when its START window was composed | START has an open end; the writer's NoteOff (`liveNoteOff`) closes it **now**; a later composed TERMINAL is suppressed | `test_live_noteoff_closes_open_instance_and_suppresses_later_terminal` |
| Cross-window | open end in W, TERMINAL in W+2 | `test_composed_terminal_closes_across_windows` |
| Duplicate terminals (old and new revision) | first closes, rest suppressed | `test_duplicate_terminal_suppressed` |
| End learned after its tick | fires at the current tick (`lateTerminals`), never in the past | `test_late_terminal_fires_at_current_tick` |
| Revision transition | close instances the new source does not carry (DEC-148 closure); ignore carried entries never sent | `test_revision_transition_reconciles_carry_set` |
| Live overlap (logical Shorten end = live NoteOn − 1) | the sounding instance's NoteOff is sent **now**, before the live NoteOn the caller passes through; the pitch is occupied (count) and composed NoteOns on it are suppressed until every live note on it is released | `test_live_occupy_closes_now_and_suppresses_until_released` |
| Full set | new instance refused by name; every sent instance still gets its NoteOff | `test_full_set_refuses_new_start_and_never_drops_a_noteoff` |

Windows must carry a **carry list** (every instance sounding across the window start, with its end or open) so a
transition can reconcile. The carry list replaces the TERMINAL-only list at the same bound (≤ T entries).
