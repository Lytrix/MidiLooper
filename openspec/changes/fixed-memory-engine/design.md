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
| CE-C1 | Physical output port, one per destination (DIN, USB device, USB host; independent policies, fixed memory). NoteOffs need **no FIFO slot**: they are owed by the port's sounding table (≤ 8 tracks × 16 = 128) and sent with priority once due, never before their own NoteOn; a NoteOff whose NoteOn is still queued cancels both. Essential state (Bank Select, Program Change, sustain-family, channel mode) and RPN/NRPN go through the **ordered** queue with notes and are never dropped by the automation policy (state coalesces only where no NoteOn of its channel depends on the earlier value; overflow waits in a per-(channel, state) table, NoteOns held back meanwhile). Continuous automation **coalesces** per channel/controller to its latest value, unchanged values are not re-sent. Transmit scheduling: due NoteOffs → ordered queue, with continuous guaranteed 1 slot in 4 while both are pending (shares of actual transmissions). Lateness measured per class. | **Partly settled (host).** Bandwidth policy beyond this is not final: the 16/64 occupancy cap of `e85fe039` is replaced, not adopted | `test_content_engine_output` (8) |
| CE-D1 | Scheduling: measured grain costs; input-drain responsiveness separate from window deadlines. | Open (needs device measurements) | — |
| CE-E0 | **Sounding-instance identity is (noteId, run)**, not noteId alone, and not a window-level stamp. An edit run's replacement row keeps the hidden note's id, so across a revision switch an old instance and the replacement share the id. `runKey` = run descriptor (10 bits) + pass order (6 bits) keeps the instance at 12 B. | **Settled (host)** | `test_instance_identity_includes_run` |
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

## E3 composer slice (as built)

`Composer<Pool, Runs>` merges a source's runs (a manifest in pass order, each read to a row limit, so a writer's open
prefix composes like a sealed run) in DEC-244 order and writes `SpanWindow`s in sequence. Effective content is derived
on the fly:
- an earlier-pass NoteOn is hidden by a later pass sounding on its pitch, or by a later pass starting on its pitch
  within `[on, on + 8]` (OVL-2 equal start, OVL-6 minimum length);
- a later-pass NoteOn shortens admitted earlier-pass notes on that pitch to one tick before it (OVL-1). A NoteOn on a
  window's first tick is caught by looking one tick past the previous window's end, so the generated end is never
  late.

The window carries the admitted set (≤ 16). `WindowPlayer` drives the track's `TerminalScheduler`: on a source change
it closes uncarried instances; it applies TERMINAL entries; at each tick it sends NoteOffs before STARTs.

Fixed scratch: 128 × 64 source counters, 16 admitted, one cursor per run. Zero heap in the integrated run.

**Named preconditions of this slice:**
- each run's rows are tick-monotonic within one loop cycle;
- no wrap-held notes;
- no owed-commit runs spanning cycles;
- cold start mid-loop recomposes from the loop start (checkpoints not built).

**Authority split:**
- The composer simulates OUT-POLY only to bound its TERMINAL entries. The scheduler decides what actually sounds.
- Live occupancy suppression or a revision switch can make the scheduler sound fewer instances than the composer
  admitted. That is safe: TERMINAL or carry entries for instances that are not sounding are ignored.
- A switch to a source that does not admit an instance still sounding closes it at the switch boundary. That
  instance is cut early at the switch, never left hanging.
- The composer never consults port admission.

## CE-C1 — output port (owner review of e85fe039, 2026-11-04)

The 16/64 non-note occupancy cap of `e85fe039` was an admission constraint, not a transmitted-bandwidth share (72 %
measured against a "75 %" cap). It is replaced. Its regression scenario is kept as
`test_sustained_din_saturation_regression`.

| Owner requirement | Implementation | Test |
|---|---|---|
| Essential state not suppressed by the automation quota | ordered queue + per-(channel, state) overflow table; coalesce only without a dependent NoteOn between | `test_sustain_release_survives_saturation`, `test_bank_program_noteon_order_under_saturation` |
| Coalesce continuous CC; dedupe; final value reached | per (channel, controller / bend / pressure / poly AT) latest-pending table + dirty FIFO | `test_saturated_cc_sweep_reaches_final_value` |
| Priority-aware, causal scheduling | due NoteOffs → ordered (state before dependent NoteOns, FIFO) → continuous 1-in-4 share | `test_bank_program_noteon_order_under_saturation`, regression |
| Distinguish occupancy, dequeue share, transmission | shares counted on transmissions (`sent[class]`) | regression |
| NoteOn and NoteOff lateness separately | `maxNoteOnLateMicros`, `maxNoteOffLateMicros`, sums | regression |
| 8 × 16 held notes on one DIN port without consuming the FIFO | sounding table; the ordered queue is empty while 128 notes are held | `test_128_held_notes_share_one_port_without_holding_fifo` |
| Note burst behind full automation | 32 NoteOns out in ≤ 44 slots although ~1.3 s of automation is pending | `test_note_burst_behind_full_automation` |
| Independent ports | one object per port with its own message time | `test_ports_are_independent` |

**Same 10 s DIN saturation (3 notes/tick, 2 CC/tick):**

| | `e85fe039` | Now |
|---|---|---|
| NoteOns sent (each with one NoteOff) | 3,759 | 4,323 |
| Continuous automation | 2,955 sent | 1,442 sent (14.3 % of transmissions); 17,758 values coalesced |
| Notes cancelled unsent (NoteOff due before the NoteOn could start) | — | 3,357 |
| Longest wait | 57.5 ms (queue) | NoteOn 19.5 ms (mean 12.4 ms); NoteOff 9.3 ms (mean 2.9 ms) |

**Still not final:**
- the 1-in-4 continuous share and the ordered note space (48 of 64) are declared values;
- RPN/NRPN beyond 64 overflow messages are counted as dropped (`parameterDropped`);
- the USB message time is an estimate until measured on the device.

## CE-C1 memory ownership correction (owner, 2026-11-04)

**Previous `OutputPort<>`, measured on ARM: 29,560 B per port.**

| Part | Bytes | Share |
|---|---|---|
| Continuous controller matrix: pending 8,256 + last-sent 8,256 + dirty FIFO 8,256 (16 ch × 258 lanes, dense) | 24,768 | 83.8 % |
| Sounding notes (128 × 16 B) | 2,048 | 6.9 % |
| Essential-state overflow (pending 544 + status 272 + order 544) | 1,360 | 4.6 % |
| Ordered TX queue (64 × 12 B) | 768 | 2.6 % |
| RPN/NRPN overflow (64 × 8 B) | 512 | 1.7 % |
| Counters + scalars | 104 | 0.4 % |

Three ports therefore came to 88,680 B, mostly three copies of a full controller matrix and three copies of
128 sounding notes.

**Designs compared (ARM bytes, 8 tracks / 16 tracks):**

| Design | 128 voices | 256 voices | Complexity | Worst case per operation |
|---|---|---|---|---|
| A. three dense ports (previous) | 88,680 | 94,824 | low | continuous O(1) direct index; NoteOff scan 128 |
| B. per-port sparse lanes, per-port sounding table (ports × tracks × 16) | 20,496 | 26,640 | low | lane scan ≤ 256; NoteOff scan 128 |
| C. one global pool for notes, essential transactions and lanes, tagged by destination | ≈ 20,000 (est.) | ≈ 22,000 | high: per-entry destination state, shared ordering for essential state, cross-port eviction | similar scans, plus per-port filtering of a shared ring |
| **D. chosen: shared logical notes (tracks × 16, 2-bit state per port) + per-port sparse lanes + per-port dense essential overflow** | **16,400** | **18,448** | moderate | lane scan ≤ 256; NoteOff scan over shared notes (128 / 256); note find ≤ 128 / 256 |

**D, per part:**

| Part | Bytes |
|---|---|
| Shared notes (128 × 16 B; 256 voices: 4,096) | 2,048 |
| Per port (× 3), total | 4,784 |
| — ordered TX queue | 768 |
| — essential overflow (pending + order; status derived from the slot) | 1,088 |
| — RPN/NRPN overflow (64 × 4 B) | 256 |
| — 256 automation lanes (8 B each) + dirty FIFO (u16) | 2,560 |
| — counters | 80 |
| — scalars | 32 |

**Why D over C:**
- Automation lane state is inherently per destination: last-sent and pending values differ per port. A shared lane pool
  would save only the 2-byte lane key and would need headroom, so it costs more bytes and more management.
- Essential-state overflow must never drop, so it is sized densely per destination. Dense (1,088 B per port) is
  smaller than a guaranteed shared pool (816 entries × 8 B).
- The logical note identity is the one thing worth sharing, and sharing it meets "tracks × 16, not
  ports × tracks × 16".

**Broadcast:** one key routed to several ports is one logical note with a 2-bit state per port (queued / playing /
off pending). Each port sends, cancels and terminates it independently (`test_broadcast_note_shares_identity_with_per_port_states`).

**Automation lanes:**
- Only lanes in use take entries; a clean lane is evicted for a new one.
- Only when all 256 lanes of a port have pending values is a new lane's value suppressed (counted
  `automationLanesFull`). This is an automation-only workload limit: ≤ 256 lanes with values waiting at once per port.

**Unchanged:** semantics and numbers. The CE-C1 tests and the saturation regression give identical results.


## CE-C1 — expired-note cancellation (owner decision 2026-11-04, final)

- A NoteOn that has not begun physical transmission when its scheduled end is reached is never transmitted on that
  destination. This covers two cases:
  - its NoteOff arrives while the NoteOn is still queued;
  - the transmitter reaches it at or after `endMicros` (start ≥ end, so the exact deadline counts as expired).
- The instance's port state and queue entry are released. The logical note is freed once no port holds it.
- Once a NoteOn has started on the wire, its NoteOff obligation is kept, even if the NoteOff arrives mid-transmission.
- Per destination: the same note can expire on saturated DIN and play normally on USB.
- Counted as `expiredBeforeTransmit`, apart from:
  - `suppressedNoteOn` (queue or note-table refusal at admission);
  - polyphony suppression (`TerminalScheduler::refusedFull`, OUT-POLY);
  - lateness (`maxNoteOnLateMicros` / `maxNoteOffLateMicros`).
- Recorded events and composer output are untouched.
- The 3,357 expirations in the 5× DIN saturation fixture are accepted overload behaviour, not lost recorded MIDI.

| Test | Shows |
|---|---|
| `test_noteon_not_started_by_its_end_is_cancelled_exactly_at_deadline` | a start equal to the end expires; end + 1 starts; nothing owed for the expired note; an off-by-one mutation fails this test |
| `test_noteoff_during_noteon_transmission_is_kept` | NoteOff during the NoteOn's transmission is kept and sent after it |
| `test_expiry_is_per_destination` | expires on DIN, plays and ends on USB |

## E3 slice 2 — notes held across the loop end (as built)

**Row timeline:**
- A held note's NoteOff goes into the closing run at `loop + position`, so each run's rows stay tick-monotonic.
- The composer reads every run as two streams: the main stream (ticks < loop) and the wrap stream (ticks ≥ loop,
  played at `tick − loop` in the next cycle).
- `nextCycle()` continues with all carried state.
- Windows carry `cycleBase`, and the player works in timeline ticks.

**Cold start:** `begin` counts wrap-held source notes as sounding at tick 0, so occlusion at the loop start is exact
from the first cycle. It admits none of them: they were never sent, so their ends are ignored (DEC-148).

**Rules added or corrected:**
- **Q-OVL-3 (ratified):** a later pass starting on the pitch at tick 0 hides a wrap-held earlier note entirely. This
  is decided at its NoteOn, by checking the run's wrap section for its NoteOff (bounded by the notes held at the wrap).
- **Defect fixed (found while writing these tests):** the OVL-2 / OVL-6 look-ahead hid an earlier note whenever a
  later pass started on its pitch within 8 ticks, even if it had already ended. Hiding now requires the earlier note
  to still sound at that later start (its NoteOff after it), including across the loop end.
- **OVL-6 across the loop end (owner decision 2026-11-04):** the minimum-length rule uses the earlier note's entire
  surviving duration in unwrapped time, pre-wrap portion included. Q-OVL-3 still hides a wrap-held note entirely at
  tick 0.

**Evidence:** `test_content_engine_compose_wrap` (6) compares 3 cycles from a cold start against the reference:
- wrap-held note;
- head shorten;
- tick-0 hide;
- a later wrap-held note hiding earlier notes in its head;
- min-length only while sounding (in-cycle and across the wrap);
- 40 random multi-pass trials.

Four targeted mutations each fail their test: no cold pre-pass, no tick-0 hide, hide regardless of sounding, and the
wrap stream ignored.

**Still not in the composer:** owed-commit runs spanning more than one cycle; edit hides; checkpoints for mid-loop
starts.

### E3 slice 2 closure checks (owner review 2026-11-04)

**Independently specified fixtures** (hand-written expectations, not the reference model):

| Fixture | Case |
|---|---|
| `test_fixture_min_length_boundaries_7_8_9` | remnants 7 (hidden) / 8 / 9 within a cycle |
| `test_fixture_min_length_across_loop_end` | the same boundaries across the loop end |
| `test_fixture_end_abutment_in_cycle_and_across_loop_end` | abutment within a cycle and across the loop end |
| `test_fixture_tick_zero_hides_wrap_held_note` | tick-0 hide over 3 cycles |
| `test_fixture_successive_iterations_of_one_note` | a note one tick shorter than the loop, restarting each cycle |

The 3-cycle reference comparison and the four mutations are kept.

**Bounded look-ahead (composer worker, not the Clock path):**
- Per NoteOn the composer reads at most the source rows of all passes in the next 9 ticks (across the loop end
  too) for the hide check, plus its own run's rows in that span to find its NoteOff.
- The run's wrap section (≤ notes held at the wrap) is read only when a later pass starts a note on that pitch at
  tick 0. A per-pitch table built at `begin` makes that test O(1).
- Measured on 8 stacked 128-note chords: 136 rows at most per NoteOn, against 2,048 rows within 9 ticks
  (`test_lookahead_work_is_bounded_on_dense_chords`, `Composer::stats()`).
- Theoretical worst case per composition grain is 128 × (R8 + 128). R8 is the rows within any 9-tick span, ≤ source
  admission per window. A device grain measurement (E0.7) decides whether R8 needs its own admission limit.

**No phantom NoteOffs:**
- Every engine stream in these tests is checked: each NoteOff closes a started, open instance, and no
  `(noteId, run)` has two open instances.
- Overlap counters never emit; only admitted instances and the scheduler do.
- `duplicates == 0` across cycles, because absolute ticks keep successive iterations apart.

## E3 slice 3 — no multi-cycle runs (as built)

**Writer change:** every wrap starts a new run, so a run never spans more than one loop cycle. This removes
"multi-cycle runs" from storage instead of composing them.
- The DEC-069 owed commit stays a manifest-order concern: a revision publishes when its runs have sealed, in order.
- Several runs may be closing at once. Each holds at least one open note, so there are at most 128 per writer, and
  each holds one descriptor.
- With no descriptor left the writer enters FinalInterval (named).
- The sealed queue is sized for a Stop that seals every closing run plus the active one; a 100 k-step fuzz found the
  old 8-slot queue overflowing.
- `CommitOwed` is gone from `WriterResult`.

**RAM2 correction:** a writer measures 2,612 B on ARM, mostly the 128 × 16 B open-note table, against the 1,024 B
budgeted.

| Item | Before | Now |
|---|---|---|
| 8 writers | 8,192 B | 20,896 B |
| RAM2 statics | 195,261 B | 207,965 B |
| Heap left for libraries | 329,027 B | 316,323 B |

**Q-LEN-1 — resolved (owner decision 2026-11-04: re-strike once per loop).** See the next section.

## Q-LEN-1 — re-strike once per loop (as built)

**Rule:** a note whose recorded duration is at least one loop length L replays with effective end
`min(recorded end, S + L − 1)`.
- Its NoteOn repeats at S + L, S + 2L, …, and each iteration ends one tick before its successor.
- The original late NoteOff is never sent against a newer iteration.
- Notes beginning near the loop end still sound into the next cycle; there is no clipping at tick 0.
- The recorded NoteOn and NoteOff stay unchanged.
- Live holds are the scheduler's live occupancy, not this rule: replayed iterations of a note whose key is still down
  are suppressed until the real NoteOff.

**Implementation:**
- **Writer:**
  - `begin(slot, kind, loopLength, startTick)`;
  - each run stores ticks relative to its own start;
  - a NoteOff whose note was held ≥ L carries `origin` flag `kOriginHeldLoop`. The flag is metadata, like
    `kOriginGeneratedClosure`; both flags now live in `PassEvent.h`.
- **Composer:**
  - a flagged NoteOff has no playback effect;
  - a NoteOn whose `(noteId, run)` is still admitted ends that previous iteration one tick earlier. This is also
    checked one tick past each window end and at the loop end, so the end lands exactly before the successor;
  - the source count of a held-loop note is +1 per NoteOn and −1 once per loop sweep;
  - the overlap look-ahead clamps the note's end to S + L − 1.
- **Player:** a TERMINAL for `pos − 1` (OVL-1 shorten or re-strike) can follow entries a lower pass wrote at `pos`.
  The player now looks one tick ahead for TERMINALs due now. This was a real defect, found by a 400-trial run, and
  affected shortenings as well; `test_fixture_shorten_terminal_after_lower_pass_entry_at_same_tick` fails without the
  fix.

**Hand-specified tests** (`test_content_engine_compose_wrap`):

| Test | Case |
|---|---|
| `test_qlen1_durations_L_minus_1_L_L_plus_1_and_beyond_2L` | durations L−1, L, L+1 and > 2L |
| `test_qlen1_note_beginning_near_loop_end` | a note beginning near the loop end |
| `test_qlen1_note_starting_at_tick_zero` | a note starting at tick 0 |
| `test_qlen1_later_pass_overlap_with_repeating_spans` | a later pass shortening a held-loop note; a held-loop later note hiding earlier notes, cold start included |
| `test_qlen1_live_hold_across_wraps_then_replay` | a live hold across three wraps, then replay |

Every stream is checked for orphan NoteOffs and duplicate instances. Separately, 2,000 random trials matched the
reference once it was corrected.

**Reference-model corrections found by the comparison (engine was right):**
- **OVL-7 "shortest end wins":** must compare ends in unwrapped time from the note's start.
- **Q-OVL-4 (owner to confirm):** OVL-3 is evaluated against the later note's effective wrap-aware, repeating span.
  An earlier note starting inside it is hidden even if it was also sounding when the later note started; Hide
  absorbs Shorten. Legacy resolves a hold once at its NoteOn and skips such notes at its NoteOff. For a later note
  that wraps or repeats, that would leave both sounding on the same pitch, against the same-pitch exclusivity lemma
  and the "overlap against effective repeating spans" instruction. For non-wrapping later notes the two cases exclude
  each other, so nothing changes.

**Mutations:**
- Sending the flagged NoteOff fails 6 tests.
- Dropping the once-per-loop source-count decrement is an equivalent mutant: a held-loop source note keeps its pitch
  counted as sounding either way, so the decrement only keeps the counter bounded.
