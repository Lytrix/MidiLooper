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
- `History` (undo states and slot currents): built in E4, see "E4 — history" below.
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
- **Q-OVL-4, ratified 2026-11-04 as OVL-9:** OVL-3 is evaluated against the later note's effective wrap-aware, repeating span.
  An earlier note starting inside it is hidden even if it was also sounding when the later note started; Hide
  absorbs Shorten. Legacy resolves a hold once at its NoteOn and skips such notes at its NoteOff. For a later note
  that wraps or repeats, that would leave both sounding on the same pitch, against the same-pitch exclusivity lemma
  and the "overlap against effective repeating spans" instruction. For non-wrapping later notes the two cases exclude
  each other, so nothing changes.

**Mutations:**
- Sending the flagged NoteOff fails 6 tests.
- Dropping the once-per-loop source-count decrement is an equivalent mutant: a held-loop source note keeps its pitch
  counted as sounding either way, so the decrement only keeps the counter bounded.

### OVL-9 (Q-OVL-4) fixtures — owner's cases, L = 768, hand-specified

| Test | Case |
|---|---|
| `test_qovl4_case1_later_note_covers_earlier_start` | B 50–750 covers A's start at 700: A hidden every cycle, cold start included, no carry, no NoteOff |
| `test_qovl4_case2_earlier_note_shortened_by_next_later_start` | B 50–600: A starts at 700 and ends at 817 when the next B starts at 818, every cycle |
| `test_qovl4_case2_adoption_keeps_and_shortens_transmitted_note` | revision adoption with an already-transmitted A, case 2: the new revision carries the same instance and shortens it at 817 |
| `test_qovl4_case1_adoption_closes_transmitted_note_at_switch` | adoption, case 1: A is closed at the switch (768) and never restarts |
| `test_fixture_unwrapped_endpoint_smaller_phase_is_later` | a next-cycle end of 99 is later than a same-cycle 699; 699 wins |
| `test_fixture_lookahead_never_sends_a_terminal_early` | a carried natural NoteOff at 2000 is not sent at 1999, and at 2000 goes out before the NoteOn |

Revision adoption warms the new revision's composer by one silent cycle, so its carry reflects its steady state.
Without a warm-up, a cold composer carries nothing and every transmitted note closes at the switch. Restart
checkpoints will make this warm-up cheaper; it is bounded by one cycle of composition today.

## E3 slice 4 — edit hides (as built)

**Edit run** (`RunKind::kEdit`, a NOTE_EDIT commit):
- HideNote rows come first (status `kHideNoteStatus` = 0xF4, never sent; `noteId` = target).
- Replacement notes follow and keep the target's `noteId` (DEC-245 lineage).
- A row of an earlier run is hidden iff a later edit run hides its `noteId`, so a second edit replaces the first
  edit's replacement.

**Hide table:**
- The composer builds a fixed table at `begin`: noteId → latest hiding pass, heapsorted, O(H log H), H ≤ 4,096
  (W: hidden note ids per manifest).
- Lookup per NoteOn is O(log H).
- A source with more hides than the table is refused by name at `begin`.
- Since E3.3 the table lives in the shared edit-index store, not in the composer (see E3 slice 6).

**EDIT-1 (ratified by owner 2026-11-04):**
- An edit run is not a derived occluder. Its notes neither shorten nor hide earlier passes and are not counted as
  occluding sources. NOTE_EDIT already writes its own overlap results (DEC-029, DEC-030, DEC-034) as explicit hides and
  replacements, so derived occlusion would apply them twice.
- Later overdub passes occlude edited notes like any earlier content.
- A later-pass note hidden by an edit still counts as a source occluder: its sealed effect on earlier passes persists
  (OVL-8).
- A later edit of the same NoteId replaces the earlier replacement (the latest hiding pass wins).

**Tests:** `test_content_engine_compose_edit` (hand-specified), zero heap:
- hide;
- replacement;
- lineage;
- EDIT-1;
- a later overdub over an edit;
- OVL-8 with an edit-hidden note;
- undo (manifest without the edit run);
- hide capacity refusal (`IndexResult::CapacityExceeded`);
- two successive edits of one NoteId plus an unrelated earlier same-pitch note (owner regression);
- a source with edit runs and no index store is refused.

Mutations caught: ignoring hides fails 6 tests; letting edit runs occlude fails the EDIT-1 test.

## E3 slice 5 — restart checkpoints (as built)

**Capture:** `Composer::capture(Checkpoint&)` stores the carry state at a window boundary:
- up to 64 nonzero (pitch, pass) source counters;
- up to 16 admitted notes;
- stamps for identity, loop length, position and cycle.

The run cursors are not stored.

**Resume:** `Composer::resume(source, …, Checkpoint)` checks the stamps, runs `begin` (hide table, edit flags, tick-0
table), restores the counters and admitted notes, and recomputes the three cursors per run by binary search on tick.
A mid-loop start (undo, redo, slot switch, revision-adoption warm-up) therefore replays at most from the nearest
earlier checkpoint instead of the loop start.

**Refusal:** a checkpoint whose sounding source set exceeds 64 entries is refused by name; the caller uses an earlier
one, or the loop start.

**Tests:** `test_content_engine_checkpoint` (4):
- 30 random multi-pass sources (held-across-the-end notes, held-loop notes, edit hides), with a checkpoint at every
  window boundary over 3 cycles. Every resumed composition gives byte-identical windows and carry lists from that
  point on, with zero heap.
- An over-capacity refusal.
- A source mismatch refused.
- A resume landing directly on its window with the carried note.

Mutations caught: counters not restored, admitted notes not restored, wrap cursor reset.

**Sizes (ARM):**
- A checkpoint is 404 B. At 8 checkpoints per slot (every 8 bars for 64 bars) that is 3,232 B per slot and 206,848 B
  for 64 slots, against 262,144 B budgeted.
- **Budget finding (resolved by E3.3):** a composer was 43,184 B because each one held a 4,096-entry hide table
  (32 KB). The hide table now lives in the shared edit-index store; a composer is 10,428 B.

**Not yet built:** the owner of the per-slot checkpoint table (which sweep captures, and when a checkpoint is retired
after a source change). That belongs to the worker/scheduler milestone (E5).

## E3 slice 6 — shared edit-hide index (E3.3, as built)

**Owner:** `EditIndexStore<Pool, Runs, Slots = 18, Capacity = 4096>` (`include/ContentEngine/EditIndexStore.h`), one
fixed object shared by every compose cursor. A composer takes a pointer to it at `init` and no longer carries a hide
table.

**Identity and pinning:** a slot holds the sorted hide index (noteId → latest hiding pass) of one manifest identity.
`acquire(identity, source, count, lease)` at `begin` returns:
- `Ready`: an existing slot with the same identity is reused and pinned again, or a slot is built;
- `NotNeeded`: the source has no edit run, so no slot is taken (most manifests);
- `CapacityExceeded`: more hides than one slot holds; no slot is taken;
- `Busy`: every slot is pinned.

`end()` (and the next `begin`) releases the lease. A composer given a source with edit runs and no store refuses it.

**Residency and reclamation:** residency is bounded by the slot count, never one index per historical manifest. Only
unpinned slots are evicted, least recently used first, so an index in use is never reclaimed. An evicted identity is
rebuilt from its runs on the next `acquire`, identically (heapsort plus collapse to the latest hiding pass, O(H log H)).

**Bound:** 18 slots ≥ 18 compose cursors (16 playback + 2 display); each cursor pins at most one slot, so `Busy` is
unreachable at production capacity (static check in `EngineCore.h`). It remains a named outcome for smaller stores.

**Sizes (ARM):** composer 10,428 B (18 cursors = 187,704 B ≤ 221,184 B; static check); store 590,124 B, the plan's
576-page edit-index reserve (static check). The store is a fixed object; placing it inside the page reserve, and
building indexes in worker grains instead of inside `begin`, belong to E5.

**Tests:** `test_content_engine_edit_index` (7): shared pinned slot, no slot without edit runs, exhaustion with pinned
slots never evicted, LRU eviction, capacity, identical rebuild after eviction, composer budget; zero heap.

## E5 slice 1 — restart-checkpoint ownership (as built)

**Owner:** `CheckpointTable<Checkpoint, Slots = 64, PerSlot = 8>` (`include/ContentEngine/CheckpointTable.h`): per slot,
8 checkpoints at a fixed spacing (8 bars), stamped with the manifest identity swept. Production size 208,128 B against
262,144 B budgeted (plan § 4.1; static check in `EngineCore.h`).

**Filling (`CheckpointSweep`, the sweep worker's job):** a cold composition's cycle 0 does not carry notes held across
the loop end; steady playback (cycle 1 on) does, and a cold start wants the steady state. The sweep therefore:
1. composes cycle 0 and stores its captures as *provisional* (never resumed);
2. at the end of cycle 0 stores the position-0 checkpoint as usable (it carries the steady state);
3. continues into cycle 1, storing usable captures, until one equals the provisional capture at the same position.

A checkpoint is the composer's whole state (resume equivalence), so equal state at a position means equal state from
there on: the remaining provisional captures are promoted without composing further. Cost: one loop plus the windows
until the carried notes have ended, at most a second loop. The plan's soft-job estimate (§ 7.3, one loop per committed
slot) holds for loops whose held-across-the-end notes end within the first spacing; the worst case is two loops.
`step` composes one window; grain-sliced composition is a later E5 slice.

**Retirement and pins:** a new sweep for a slot retires its table at once; a sweep whose identity is no longer the
slot's ends `Superseded` and writes nothing. The table holds no page references, only the identity stamp, and manifest
identities are never reused, so a stale checkpoint cannot match another manifest. The table therefore needs no manifest
pin (plan § 5.4 lists one; holding it would only delay reclamation).

**Cold start (`coldStart`):** resumes from the nearest usable checkpoint at or before the position, else warms up a
whole cycle from the loop start (the plan's worst path), renumbers the cycle (`Composer::rebaseCycle`) and replays to
the position. With a usable checkpoint it replays fewer windows than one spacing.

**Composer change:** removing an admitted note now preserves admission order (it was swap-with-last). The carried
state is then a function of the position in a steady cycle, not of how many cycles were composed; without it a cold
start's carry list was a permutation of uninterrupted playback's, and checkpoints never converged.

**Tests:** `test_content_engine_checkpoint_table` (5), zero heap:
- 25 random multi-pass sources (held-across-the-end, held-loop, edit hides): a cold start equals uninterrupted
  playback window for window, from an empty table, at every step of a running sweep, and at every window of three
  steady cycles from a complete table (replay < one spacing);
- cold captures stay provisional until steady (hand-specified held note across the end);
- a new sweep supersedes the old one;
- lookup, unusable entries, identity, slots;
- production budget.

Mutations caught: promoting cold captures, no warm-up, swap removal, no convergence check, ignoring supersession.

**Owner review 2026-11-05:** both departures approved — the two-loop sweep (a work bound, not waiting) and the table
without a manifest pin (completed checkpoints are value snapshots); an active sweep job still holds a source lease.

## E5 slice 2 — job leases, grains and convergence (owner review 2026-11-05, as built)

**Lease (`SourceLease`):** a sweep or cold-start job pins its manifest while it reads it, holds the run list with sealed
row limits, and builds the manifest's edit index in grains before composing. Done, superseded, refused and cancelled
all release the pin (and the composer's index lease). A slot that drops the manifest mid-sweep leaves it alive until
the sweep ends; then the manifest is freed and its runs go to reclaim (`test_pin_keeps_source_alive_until_the_sweep_ends`).

**Publication guard:** a checkpoint is published only at a window boundary and only while the slot's table still
belongs to the sweep; the check runs at the start of every grain, so a sweep replaced between the grains of one window
(after composing, before publishing) ends `Superseded` and writes nothing (`test_replacement_between_grains_publishes_nothing`).

**Grains:** `Composer::openWindow` / `composeRows` compose a window incrementally and equal `composeNext`. The sweep and
the cold start (`ColdStart`, nearest usable checkpoint, else one warm-up cycle) advance one grain per `step`; neither
waits for playback.

**Convergence within two cycles (proof).** A composer's state at a window boundary is (source counters, admitted notes
in admission order, cursors); cursors are a function of the position, and resume equivalence shows the captured part is
the rest. Source counters are a function of the position in every cycle (the cold pre-pass counts wrap-held notes at
tick 0). Every admission decision is a function of the source: hides (OVL-2/6, OVL-9, Q-OVL-3, edit index), shortening
(OVL-1) and the Q-LEN-1 effective end (≤ S + L − 1) depend only on source rows. The admitted set therefore depends on
history only through OUT-POLY refusal (more than T = 16 sounding). Below saturation, the admitted set at position p of
any cycle ≥ 1 is exactly the instances started in (p − L, p] whose effective end is ≥ p; cycle 0 lacks only those
started in the previous cycle, all of which end by position L − 2. Hence state(cycle 1, p) = state(cycle n, p) for every
n ≥ 1, and cycle 0 agrees with it from the first checkpoint position after the last carried end. The sweep detects this
exactly: equal captures at one position mean equal state from there on (determinism), so it publishes the remaining
provisional captures; otherwise it composes all of cycle 1 and `periodic()` compares the end-of-cycle state with the
position-0 state. Covered by hand-specified sources for Q-LEN-1 held-loop notes (> 2L and exactly L), a wrap hold
shortened in the next cycle, Q-OVL-3, OVL-9 across cycles, an edit hide of a wrap-held note with its replacement, and a
revision transition (`test_convergence_within_two_cycles_per_mechanism`, `test_revision_transition_sweeps_the_new_manifest`),
plus 25 random multi-pass sources.

**Q-OUT-2 (resolved 2026-11-05 as source-ordered refusal; see "OUT-2" below). Original finding:** Above OUT-POLY (more than 16 notes sounding), first-come
admission makes which notes a cycle drops depend on the notes carried in. Uninterrupted playback itself then settles
only after 2–4 cycles (probe: 25 of 40 saturated sources differ between cycle 1 and cycle 3). The sweep flags these
(`periodic()` false), never promotes cold captures, and a cold start reconstructs the cycle-1 state deterministically,
within T (`test_saturated_composition_is_flagged_aperiodic`). Options: (a) accept a cycle-1 reconstruction under
saturation; (b) make OUT-POLY refusal history-independent (for example by source order), which makes saturated
playback periodic from cycle 1 too.

## E5.3 — edit index in its reserved pages, built in grains (as built)

`EditIndexStore::init` takes the 576-page edit-index partition from the pool once, tagged `kOwnerIndex`; it is never
returned or added to, and content cannot receive those pages (`test_partition_taken_from_pool_and_never_borrowed`). Init
order: pool, index store, then the reservation ledger with only the compaction partition as its fixed reserve
(`kLedgerFixedReservePages`). The store object is 1,760 B (headers and page lists).

`reserve` takes and pins a slot in state `Building` (reading no rows); `buildStep` advances gather, heapify, sort and
collapse by at most 256 units; capacity is found while gathering; `Building` is a named outcome for a second requester.
Incremental equals synchronous for 200 random sources. Jobs prepare the index through `SourceLease`, so no composer
`begin` builds synchronously (`syncBuilds() == 0`). Full capacity (4,096 hides): 57 grains, worst 415 µs on the
device (the plan estimated 16 grains of "256 entries"; the heapsort phases cost more units).

## E5.2 — worker scheduler (as built)

`WorkerScheduler<MaxJobs>`: one grain per main-loop turn. Hard jobs EDF; a soft grain only when every hard job keeps
remaining × g within slack − B_max; display ≥ 1 in 4 and persistence ≥ 1 in 16 while pending; overruns and missed
deadlines (live or completing past the deadline) reported; fixed job table, `submit` refused by name when full.
Decision cost with 32 jobs: 4.7 µs worst on the device.

Tests (`test_content_engine_scheduler` 8): EDF, shares, slack deferral, overrun / full table, the plan's sustained worst
at U_e = 50 % (no miss, shares kept, service gap ≤ g + other + B_max), the same at measured figures (g = 800 µs, 47-grain
windows), misses reported at U_e = 5 %, and real composer + sweep jobs composing every window of 4 tracks before its
boundary, byte-identical to direct composition.

## E5 invariant — checkpoints are not transmission proof (owner 2026-11-05, tested)

Checkpoints reconstruct musical source state. Physical sounding-note ownership stays with the terminal scheduler (per
track) and `OutputHub` (per destination). `test_content_engine_restart_backpressure`: composer → player / terminal
scheduler → hub with a 40 ms/message DIN port and a USB port; play M1, cold-start M2 mid-loop from its checkpoint, adopt
M3, stop. Every transmitted NoteOn gets exactly one NoteOff per port; no NoteOff for a note never transmitted; nothing
sounds after the stop. At the restart 5 carried instances, and at the adoption 4, had never reached the DIN wire
(655 DIN expiries); none received a DIN NoteOff.

## E5 — device grain measurements (`teensy41-engine-bench`, image `7a6bab95…`)

Production placement (pool and index partition in PSRAM, tables and windows in RAM2), DWT cycles, interrupts enabled,
60-run manifest with a 4,096-hide edit run; densest spread window 2,773 NoteOns. Worst / average per grain:

| Grain | Worst | Average |
|---|---|---|
| compose, 128 row units (spread) | 655 µs | 514 µs |
| compose, 128 row units (clustered within 9 ticks) | 486 µs | 303 µs |
| sweep step | 613 µs | 527 µs |
| compose seek (`begin`) | 655 µs | 447 µs |
| checkpoint resume | 786 µs | 769 µs |
| checkpoint capture | 70 µs | 67 µs |
| index build, 256 units | 415 µs | 191 µs |
| scheduler decision (32 jobs) | 4.7 µs | 3.1 µs |

**Fix found by measuring:** the first image measured 9.7 ms per compose grain. Selecting the next row re-read every
run's stream heads from PSRAM, and the OVL-2/6 look-ahead read every later pass per NoteOn. Stream heads (position,
class, pitch, next tick) are now cached in RAM, edit-run cursors skip their leading hide rows, and `composeRows` counts
look-ahead reads as work (8 reads = 1 row unit). Behaviour is unchanged (all engine suites and the oracle).

**Findings for the plan's bounds (§ 7):**
- g = 786 µs (worst measured, checkpoint resume) replaces 500 µs. Superseded by E5.8: g = 810 µs.
- A window is 44–47 grains when its rows are spread (2,773 NoteOns + NoteOffs), not 23: the plan counted START rows,
  composition merges NoteOffs too. C1 at the measured figures: 8 × 47 × 0.786 ms = 296 ms per 1.6 s; the simulation
  meets every deadline at U_e = 50 %.
- When later passes cluster inside the OVL-6 range the look-ahead is O(rows of later passes within 9 ticks) per NoteOn:
  124,360 reads and 155 bounded grains for the clustered window. Eight such tracks at once need U_e ≥ 63 %. Removing it
  needs a per-composer onset frontier (about +2 KB per cursor, beyond the 221,184 B composer budget): owner decision.
- Seek and resume exceed 500 µs (binary searches over 60 runs); splitting them per run group is possible if g must be
  lower.

## OUT-2 — source-ordered refusal (owner decision 2026-11-05, as built)

**Rule.** The composer keeps every *effective* source instance that is open: started, not hidden, and not yet ended by
its NoteOff, an OVL-1 shortening, a Q-LEN-1 re-strike or the window look-ahead. That includes instances refused for
output. Instances started in an earlier loop cycle are marked *carried*. A NoteOn is **selected** iff, when the DEC-244
merge reaches it, fewer than T = 16 open instances *occupy a voice*. An instance occupies a voice if it was selected in
the current cycle, or if it was carried in, selected or not.

**Proof of a one-cycle repeating selection.** It does not rely on sorting the NoteOns.
1. *Effectiveness and ends never depend on a selection.* Hides use the raw source counters (OVL-8) and the edit index.
   OVL-1 shortening, Q-LEN-1 re-strikes and NoteOffs end every open instance, selected or not.
2. *The carried-in set is a function of the source.* Every effective span is shorter than L (Q-LEN-1: end ≤ S + L − 1).
   An instance open at the start of cycle c therefore started in cycle c − 1, and by (1) the set is exactly the
   source's effective instances that start in a cycle and remain open at its end. The same holds after a cold cycle 0,
   because which instances started in cycle 0 are effective does not depend on what cycle 0 inherited.
3. *In-cycle decisions are a deterministic function of (2) and the source.* Within the cycle the merge order is fixed
   (DEC-244). The voice count at each NoteOn is |carried-in instances still open| plus |instances selected earlier in
   this cycle and still open|. The first term is fixed by (2); the second is determined, by induction over the merge,
   by earlier decisions of the same cycle.
4. Hence cycle c ≥ 1 makes the same decisions as cycle 1. A cold start that seeds the carried-in set (checkpoint, or
   one warm-up cycle) makes them in its first cycle too; only the physical state differs (DEC-148: carried notes it
   never sent are not sounding).

**Bound of 16, no stealing.** At any instant take the selected instance X that started last in the current cycle.
Every other selected instance sounding then is either carried in (counted at X, since a carried instance that is
still open was open at X's start) or selected earlier in this cycle (also counted). So at most 16 sound. If no instance
of this cycle sounds, the carried selected ones are a subset of the previous cycle's bounded set. Decisions are made
only at a NoteOn, so a selected instance plays its whole effective span.

**Why not the alternatives.**
- Counting every open effective instance, selected or not, is also periodic, but it starves: a 20-note held pad
  re-struck every loop would be silent, because each re-strike sees 19 others open
  (`test_twenty_note_pad_keeps_sixteen_voices`).
- Counting only selected instances (the previous first-come rule) depends on which notes the previous cycle selected.
  That is the 2–4-cycle settling the owner rejected.

The carried-in term is what breaks the cycle-to-cycle dependency. The cost is that, right after the loop start, refused
notes that are carried in can hold back new notes until they end. That cost is deterministic and the same every loop.

**Memory and limits.**
- **Open-instance table:** 48 entries (12 B each), replacing the 16-entry admitted list. The composer is 12,184 B, within
  12,288. More than 48 effective instances open at once are refused untracked and counted (`Stats::openOverflow`); the
  bound still holds, and the sweep reports `periodic()` false
  (`test_open_table_overflow_is_named_and_bounded`).
- **Checkpoints:** ≤ 56 source-counter pairs and ≤ 32 open instances, with their selected and carried flags (500 B;
  64 × 8 tables = 257,280 B of 262,144). A boundary with more open instances is an unusable checkpoint, and the cold
  start uses an earlier one or warms up.
- **Sweep:** the end-of-cycle-0 capture is provisional, because cycle 0's selections lacked carried-in instances. The
  end of cycle 1 is the steady position-0 state.

**REDs (owner list) → tests.** `test_content_engine_selection`:
- More than 16 cyclically overlapping notes with different starts and durations: identical over 8 cycles.
- Wrap-held notes competing with tick-0 notes: 4 of 10 selected every loop; a cold start agrees.
- A cold start (warm-up and checkpoint) at every window agrees with play after 2, 4 and 8 cycles.
- Bound, no phantom NoteOffs and no repeated instances through the terminal scheduler.
- The 20-note pad, and the 48-entry overflow.

`test_content_engine_restart_backpressure`: after an adoption with a saturated 40 ms/message DIN port, an isolated cold
start with no port decides every later window identically.

Mutations caught: counting every open instance (starvation), counting only selected ones (history), no carry at
`nextCycle`, a loop-end capture without the carry flag, and shortening only selected instances.

## E5 — clustered look-ahead: onset-pitch mask (measured optimisation)

Each run descriptor (RAM2, 64 → 80 B; +8.7 KB over 544 runs, not composer memory) keeps a 128-bit mask of the pitches
it starts notes on, set by `RunTable::append`. The OVL-2/6 look-ahead skips any later pass whose mask lacks the
NoteOn's pitch. Host counts for the densest clustered window: 124,360 → 9,440 reads, with each pass on its own pitches.
In the adversarial case (pitches rotating per cluster, so the mask rarely skips) it is 67,008 reads.

**Feasibility gate** (`test_feasibility_gate_clustered_eight_tracks`): it builds the adversarial window with the
production composer, counts its grains (100 at 128 units), and simulates eight such tracks at the measured g = 786 µs and
U_e = 50 %. Result: 0 missed deadlines. The gate is ignored and reported RED whenever it misses. Device confirmation:
E5.8 below.

## E5.8 — device re-measurement with OUT-2 and the mask (image `59c8a6f8…`)

Same bench and placement as E5. Worst / average per grain:

| Grain | Worst | Average |
|---|---|---|
| compose, 128 row units (spread) | 719 µs | 581 µs |
| compose, 128 row units (clustered, 45 grains) | 785–800 µs (5 runs) | 562 µs |
| compose, 128 row units (clustered rotating, 100 grains) | 603 µs | 391 µs |
| sweep step | 684 µs | 583 µs |
| cold-start step | 664 µs | 574 µs |
| compose seek / `begin` | 681 / 420 µs | 451 / 410 µs |
| checkpoint resume, bind grain | 408 µs | 402 µs |
| checkpoint resume, seek grain | 697 µs | 686 µs |
| checkpoint capture | 68 µs | 67 µs |
| index build, 256 units | 398 µs | 191 µs |
| scheduler decision (32 jobs) | 4.7 µs | 3.0 µs |

**Resume was a 1.1 ms grain.** The first re-run reported resume at 0.1 µs: the bench ignored `capture`'s result, and
the densest spread window opens more than 48 instances (`openOverflow` = 1), so every capture was refused and resume
timed its own rejection. Timed from a capturable boundary it took 1,106 µs: binding the source (band seeks and the
index pin for 60 runs) plus seeking every stream cursor. `resume` is now `resumeBind` + `resumeSeek`, and `ColdStart`
runs them on two grains. The bench asserts capture and resume, and times `begin` separately.

**Result.** The worst grain is the clustered compose, 800.3 µs; the gate and the measured-load test use g = 810 µs.
At U_e = 50 % the eight-track adversarial clustered case (100 grains per window) misses no deadline, so the feasibility
gate is met on device figures. C1 for the spread case: 8 × 47 × 0.81 ms = 305 ms per 1.6 s.

**Open-instance ceiling (owner 2026-11-05).** The first bench source kept 37–59 notes open on one track (20-tick notes,
all passes starting within 8 ticks), which overflowed the 48-entry open table. The owner ruled that unrealistic: at
most 20 open notes per track is the test ceiling, enough to exercise the T = 16 limit. The spread source now staggers
pass onsets across each 32-tick slot with 10-tick notes (same 2,773 NoteOns, about 19 open), and the bench asserts no
overflow. Re-measured (image `4285a123…`): compose 710 µs; a mid-loop checkpoint resumes in two grains (bind 418 µs,
seek 358 µs), so cold start takes 3 grains instead of a 94-grain warm-up; worst grain still the clustered compose
(782–800 µs), g = 810 µs unchanged. The clustered benches still exceed 48 open; their look-ahead runs before the
overflow check and their open table stays full, so their grain costs remain an upper bound.

## W — one writer at a time (owner decision 2026-11-05, as built)

Only one track records or overdubs at a time; a second Record / overdub gesture while one is active is refused by
name (to implement with the track admission, E5.10). The 8-writer reserves are released:

| Capacity (`Config`, derived and asserted) | 8 writers | 1 writer |
|---|---|---|
| Reserved writer rows | 1,280 | 160 |
| Content rows (W) | 133,888 | 132,768 |
| Run descriptors | 544 | 520 (517 worst) |
| Manifest slots | 288 | 256 (249 worst) |
| Content quota pages | 3,264 | 3,222 |
| Page pool | 4,096 pages | 4,054 pages |

Measured (`teensy41-engine-bench` ARM build): RAM2 statics −6,752 B (run and manifest tables, reservation ledger),
PSRAM −43,200 B (pool and free stack). Plan budgets: writers 20,896 → 2,612 B RAM2; RAM2 statics 225,581 → 212,193 B,
heap for libraries 298,707 → 312,095 B; PSRAM allowance for non-content users +43,092 B (847,184 capture /
1,895,760 shipping). Committed content (131,072 rows, 512 runs), the NOTE_EDIT reserve and the edit-index partition
are unchanged. Device bench image `ab57a6d4…` unchanged in grains.

## E5 — reclaim worker (as built)

A run whose last reference drops (`RunTable::release`) joins the run table's reclaim queue in release order; the queue
links reuse the free-list array, because a reclaiming descriptor is not free. `reclaim(pool, maxUnits)` returns at
most `maxUnits` units, oldest release first: one data page (from the end), one directory page, or one emptied
descriptor freed. `reclaimPendingUnits()` is exact, so `ReclaimJob` (background class) declares
⌈units / 64⌉ grains for the queue as it stands and ends after them; runs released meanwhile go to the next
submission, so the scheduler never sees an overrun. Pinned manifests keep their runs out of the queue, and a run listed
by two manifests waits for both. The per-run `reclaimStep` API is gone (callers use the queue).

Device grain (image `3fb399d1…`): 64 units worst 29 µs, average 14 µs (60 released runs of 40 pages, directory-listed).
Tests: `test_content_engine_reclaim` (5): order and unit budget, pins and shared runs, scheduler job beside display
with a mid-job release, 20,000 random allocate/release/reclaim steps with conservation, no heap. Mutations caught:
budget + 1, LIFO queue, descriptor not counted, job running past its declared grains.

## E4 — history: per-slot undo, shared depth, oldest step forgotten (owner decisions 2026-11-05, as built)

`ContentEngine::History` owns the undo states and slot currents.
- **Per slot.** A slot's history is a chain of committed states (one manifest reference each) and a current pointer;
  undo / redo move the pointer in O(1) and act on that slot only. Below the first state lies the empty slot.
- **Shared depth 99.** At most 99 states are held across all slots (`Config::kHistoryDepth`).
- **Commit.** Drops the slot's redo branch, then, when full, forgets the oldest step in the set: the oldest-committed
  state that is no slot's current. An undo state raises its slot's floor (the empty slot is no longer reachable); a
  redo state takes its slot's whole redo branch (a redo chain has no holes). A commit is never refused for history.
  Since 99 > 64 slots, a full table always holds such a state.
- **Memory.** Each dropped state releases its manifest; runs no longer listed reach the reclaim queue (E5.11). The
  object is 1,576 B (RAM2), replacing the planned 512 × 8 B ring.

Tests (`test_content_engine_history`, 7): undo to the empty slot and redo; redo drop on commit with manifest release;
slot independence; the 100th state forgets the oldest; cross-slot eviction of a redo branch; 60,000 random steps each
on 8 slots / depth 20 and 64 slots / depth 99 against an independent reference model (currents, depths, held count,
released manifests, conservation); no heap. Mutations caught: evicting the newest, floor not raised, no redo drop on
commit, a redo chain left with a hole, a dropped state not releasing its manifest.

Not in this slice: history reconstruction on load (DEC-088), with persistence (E8).

### E4.2 — overdub session undo (DEC-038, as built)

Each non-empty wrap of an overdub session is an ordinary `commit`. `beginSession(slot)` records the session start as
the commit seq of the slot's current state (an index could be evicted; a seq cannot dangle). While the session is open:
- undo on its slot stops at the session start (`canUndo` refuses at a state with seq ≤ start); the O: level is
  `sessionDepth()`, the session's states at or below current;
- a new wrap after a session undo drops the session's redo (ordinary commit rule);
- other slots undo and redo normally.
`endSession()` (Stop) drops the session's redo and folds the session's states below current into it: one step, so U:
undoes the whole session (DEC-038.7). With every wrap undone it leaves no step. Session states share the 99: a long
session forgets its oldest wraps (and the pre-session state) like any oldest step, and undo stops at that floor.
The writer integration calls `beginSession` at overdub start, `commit` at each non-empty wrap and `endSession` at Stop.

Tests (`test_content_engine_history`, 11): bounded session undo and one step after Stop; fully undone session; new wrap
after undo with another slot undoing meanwhile; a 150-wrap session against the 99; the reference model now opens and
ends sessions at random (session depth compared every step). Mutations caught: no session bound, Stop keeping the
session's redo, no fold, a start off by one. 1,584 B.

### E4.3 — NOTE_EDIT session undo (owner 2026-11-05: limit 99, shared; as built)

The session layer now has two kinds, `SessionKind::Overdub` and `SessionKind::Edit`, each with at most one open
session; both may be open at once on different slots (W: one writer + one NOTE_EDIT), never two on one slot.
- **Start** is the commit counter at `beginSession` (not the current state's seq), so redo left on the slot from before
  the session is not a session step, cannot be reached while the session is open, and survives an empty abandon.
- **E: level** (`sessionDepth(Edit)`): each edit action is an ordinary commit; undo stops at the session start and redo
  never steps into pre-session redo.
- **Leave NOTE_EDIT** (`endSession(Edit)`): the same fold as Stop, one step (none if every edit was undone).
- **Abandon** (`abandonSession(Edit)`): every session state goes and the slot returns to its session-start state.
- **Restore point (owner 2026-11-05):** an open session (either kind) protects its start state — or the empty slot, if
  it started empty — from eviction. Eviction takes the oldest state that is no slot's current and no protected start,
  so a session longer than the 99 forgets its own oldest steps (undo then steps from the oldest kept one straight back
  to the start). Abandon therefore always restores the pre-session loop, and after Stop / leaving NOTE_EDIT one U:
  always reaches it. At most two starts are protected, so a full table still holds an evictable state (99 > 64 + 2).
- **Limit 99:** session steps share the 99 history states (the owner's limit), so no separate session reserve exists.
  Manifest worst case drops from 249 to 153 of 256 (slot currents and session steps are history states).

Not here: the edit writer itself (selection ≤ 512 notes, hide + replacement rows, seal in 18 grains) and restoring
focus / selection per step, which belong to the edit writer and `EditManager` integration.

Tests (`test_content_engine_history`, 16): edit fold and abandon; overdub + edit together; earlier redo left alone; a
120-edit session whose abandon restores the pre-edit state; a 130-wrap session started on the empty slot; the reference model opens, ends and abandons both kinds at
random. Mutations caught: abandon below the floor, abandon moving current without a session step, two sessions on one
slot, redo into pre-session redo, the start taken from the current state's seq; for the restore point: start not
protected, empty start not protected, a broken relink around the protected start (walks are bounded, so it fails
rather than hangs), abandon keeping the first step. 1,608 B.

## E4.5 — edit writer (owner decision 2026-11-05: hear each edit, rewrite one run; as built)

`ContentEngine::EditWriter` owns a NOTE_EDIT session's selection and edits (`include/ContentEngine/EditWriter.h`).
- **Admission:** `begin(slot, selection, n)` takes ≤ 512 notes of the slot's current manifest (noteId + geometry) and
  opens the History NOTE_EDIT session, which protects that manifest as the restore point.
- **E: steps** live in the writer's log, not in History: `apply` (one step of ≤ 512 changes: tick, length, pitch,
  velocity, channel, or delete) records the replaced values; undo swaps them back newest-first, redo oldest-first (a
  step may change one note twice). At most 99 steps and 1,024 changes; the oldest steps are forgotten when full. This
  supersedes E4.3's "E: steps are history states": a NOTE_EDIT session now holds one history state
  (`History::setSessionState`), replaced in place on every rebuild.
- **Audible at once:** every apply / undo / redo is new input (CE-A1). At acceptance it reserves a fresh edit run
  through the ledger (`RowReservations::reserve`, rows owed without a row written) — refused by name, nothing
  changed, only when content is full. The rebuild job (`step`, ≤ N rows per grain) writes HideNote rows for every
  edited or deleted note, sorts the replacements (one grain, heapsort), writes NoteOn / NoteOff rows in tick order
  (NoteOff first at a tick) keeping each noteId (DEC-245), seals, and publishes base runs + edit run as a manifest that
  becomes the session state; the previous state's manifest is released, its edit run reclaimed. With nothing edited
  the session state is removed (the slot plays its start, no run). A newer action supersedes a running rebuild:
  `RowReservations::forgive` drops its owed rows and its run is reclaimed.
- **Leave** keeps the state as one U: step (after a running rebuild, ≤ 24 grains); **abandon** forgives the rebuild and
  returns to the protected start.
- **Budget:** two edit runs (published + rebuilding) in W: content rows 134,304, content quota 3,247 pages, pool 4,079
  pages; run descriptors 518 ≤ 520; manifests 154 ≤ 256. Writer 20,984 B (RAM2) + E: log 20,480 B (PSRAM), see
  E4.5b. Rapid actions faster than reclaim can be refused by name near full content.

Tests (`test_content_engine_edit_writer`, 9): effective content is checked by composing the slot's current manifest
(production composer + edit index, 4 windows per loop) and every published edit run is checked for hides-first, tick
order; edit heard after its rebuild; delete with E: undo / redo; leave as one U: step; leave and abandon during a
rebuild; supersession; refusal at full content (nothing changed); the log forgetting its oldest steps; 3,000 random
apply / undo / redo / delete actions against a reference model (selection, E: depth and redo depth every action,
heard content every 7th); no heap. Mutations caught: redo newest-first, no hides, unsorted replacements, a superseded
rebuild not forgiven, no return to the start with nothing edited.

Device grains, first image (`e7ea4134…`, 512 notes moved): apply 3,486 µs, rebuild grain 922 µs, E: undo 1,196 µs —
above g. Acceptance did O(selection) work on PSRAM arrays and the sort was one grain.

### E4.5b — edit writer within g (as built)

- **Acceptance is O(changes of the action):** the edited and live note counts are kept incrementally, so the new
  run's size is known without a scan; the counts' change is computed from the action's notes before anything is
  written, then the replaced values go straight into the log (no scratch copy, no rollback). A step lists each note
  once (a duplicate is refused by name).
- **The rebuild is unit-bounded:** gather (one selected note, writing its hide row), heapify and extract (one sift
  each), write (one row). Sort keys are 32-bit (tick 21 bits, NoteOff-first bit, 10-bit index).
- **Placement:** selection, keys and steps stay in the writer (20,984 B, RAM2); the E: log (1,024 × 20 B) is
  caller-placed storage in PSRAM.

Device (image `cfb730eb…`, 512 notes moved, the action built in RAM): apply 532 µs, E: undo 453 µs, rebuild grain 158 µs
worst (24 grains at 128 units). Compose seek 692 µs and `begin` 429 µs again: the earlier rise was the 54 KB the first
image placed in PSRAM. Every measured grain is within g = 810 µs (worst: clustered compose 790–800 µs).

## E6.1 — playback windows per track (plan § 9.1, as built)

`ContentEngine::PlaybackTrack` (`include/ContentEngine/PlaybackTrack.h`) owns one track's three window buffers
(playing, next, transition) and two compose cursors (steady and transition; 16 of the 18 cursors for 8 tracks).
- **Grid:** windows start where the loop position is a multiple of W (the last one ends at the loop end); a boundary
  is such a timeline tick. Buffers are stamped with source identity and timeline start; `Complete` is immutable.
- **Select:** a source change at tick `now` (undo, redo, commit, edit publish, slot switch) targets the next boundary.
  The transition composer is positioned there by `ColdStart` (checkpoint or warm-up, E5) and composes that window;
  the track keeps playing the old source. Selecting the playing source again cancels the transition.
- **Succession** at a boundary: the complete window of the wanted source starting there; else the old source's next
  window (a late switch, counted); else nothing (an underrun, counted; sounding notes still end on time via the
  terminal scheduler). On a switch the transition cursor becomes the steady one and the old cursor's pin is released.
  The player reconciles carried notes at the identity change (B.3).
- **Catch-up:** a composer that missed its boundary moves its target to the next boundary and composes, then
  discards, windows that start before it; composing a window takes less than a window's time, so it catches up.
  (Without the target a late composer would chase one window behind forever — found while writing the tests.)
- **Work** is grains (`work(maxRows)`): the steady window first, then the transition (replay, then its window). The
  scheduler runs them as hard jobs with the boundary as deadline; `tick` composes nothing and reads no page.

Tests (`test_content_engine_playback_track`, 6): every emitted stream is compared with a reference that plays the
same schedule from two uninterrupted compositions through its own player and terminal scheduler — switch on time;
late switch (the old source plays the missed window, the switch lands one boundary later); underrun at start; steady
underrun with recovery one boundary later; a selection undone before its boundary; no heap. Mutations caught: never
taking the transition, the player not given the window, transition or steady target not moved, no discard before the
target, target forgotten after a cold start, steady keeping its stale window. Two branches the mutations showed to
be unreachable (freeing a stale complete window; resetting the steady target at a switch) were removed.

Not yet: the tick path (E_tick = 32 rows per tick, occupancy mask, port FIFOs), the scheduler wiring with boundary
deadlines, loop-length changes on a source switch, and the device measurement of `tick` cost.

## E6.2 — tick path, measured first (owner 2026-11-05: E_tick = 32 had no basis; measure)

`E_tick = 32` came into plan revision 3 without a derivation. The owner's point: the rows due between two ticks are not
bounded by the sounding notes — controller streams, several passes and generated NoteOffs all land there. Bench
`tick_*` (image `0629864c…`): one track's windows for a whole loop played tick by tick through `WindowPlayer`, the
terminal scheduler and `OutputHub` (each emit queued on 3 ports; port drains not timed). A tick is ~1.04 ms at 300 BPM,
192 PPQN.

| Workload | Most window rows in one tick | Emits | Worst tick |
|---|---|---|---|
| spread (2,773 NoteOns staggered) | 2 | 4 | 9.0 µs |
| clustered (59 passes start chords within 9 ticks; OUT-2 selects 16) | 10 | 10 | 20.3 µs |
| controller burst (admitted maximum: 2,773 CC rows on one tick) | 2,773 | 255 (buffer full) | 416.8 µs |

- Each emitted message costs ~2 µs (three port enqueues); consuming a row the emit buffer can no longer hold costs
  ~0.08 µs.
- **Defect found:** `WindowPlayer::tick` consumes every due row but emits only as many as its out buffer holds; in the
  burst 2,518 controller messages were lost. A NoteOn consumed this way would start in the terminal scheduler with no
  emit. The tick must stop consuming when it cannot emit and leave the rest for the next tick (`tick_spill`) — this
  is the mechanism E_tick bounds.
- Note rows stay small (≤ 10 per tick here: OUT-2 bounds what a tick starts), but controller rows do not: the burst
  needs ~2,773 × 2 µs ≈ 5.5 ms of enqueue, five ticks' time on one track.

E_tick follows from the tick time the owner gives the playback path: per-track cap ≈ (budget per tick ÷ 8 tracks) ÷
~2 µs per emit. For example 25 % of a tick (260 µs) gives ~16 emits per track per tick; a burst then spills over
~170 ticks (≈ 0.2 s late), with NoteOffs first.

### E6.2a — continuous-controller limit and spill (owner decisions 2026-11-05: limit, then 64; as built)

**Owner rule:** continuous controllers are limited, not lossless — they would overrun the tick and flood the ports.
Essential messages (sustain and the switch controllers CC64-69, Program Change, Bank Select, channel mode
CC120-127) and RPN / NRPN sequences are never limited. One classifier (`MidiClass.h`) serves the writer, the composer
and the output hub.
- **Recording:** the writer keeps at most `Config::kMaxContinuousPerTick` = 64 continuous values per tick
  (`WriterResult::Limited`, counted); the next tick has its own 64.
- **Playback:** the composer leaves continuous rows beyond 64 per tick out of the window (`Stats::continuousLimited`);
  rows merge in DEC-244 order, so the same ones are kept every cycle and after a cold start. Several passes can each
  hold 64 on a tick; the window still gets 64.
- **No loss in the tick:** `WindowPlayer::tick` consumes a row only if it can emit it; the rest wait for the next tick
  (`Stats::spills`). Due NoteOffs beyond the buffer stay due in the terminal scheduler (they were dropped before).
  `beginWindow`'s buffer must hold T.

Device: controller burst (2,773 continuous rows on one tick) — at 32 (image `3d2e90dc…`) the window kept 32 and the
worst tick was 15.4 µs (was 417 µs, with 2,518 messages lost). The owner then chose 64 (image `bd358d37…`): worst tick
29.7 µs; clustered 20.7 µs; spread 9.1 µs. A controller message costs ~0.5 µs on 3 ports, a note ~2 µs. 64 per tick on
8 tracks ≈ 238 µs, ~23 % of a 1.04 ms tick at 300 BPM.

Tests (`test_content_engine_tick_limits`, 5): classification; writer limit with essentials, RPN and pitch bend; the
composer limit over two cycles (same rows kept); player spill with every row played once in order; 16 NoteOffs through
a 4-message buffer, none lost. Mutations caught: no writer limit, no composer limit, the count kept across windows,
essentials limited too, sustain classed continuous, no spill, NoteOffs beyond the buffer lost.

### E6.2b — playback work under the scheduler (as built)

`ContentEngine::PlaybackJobs` submits each track's compose work to the `WorkerScheduler`, each job only while its work
is pending and never twice:
- **steady** — the window for the next boundary: hard, deadline = that boundary (µs from the Clock position and tempo),
  declared 100 grains (the measured worst window);
- **transition** — cold-start replay plus the new source's window: **background** (deviation from plan § 7.3, which
  lists the cold start as hard; for owner review). Succession already makes a late transition safe (the old source
  plays, a late switch is counted), and declaring a whole warm-up cycle as hard would let the slack check starve
  display.
`PlaybackTrack::work` is split into `workSteady` / `workTransition` for this.

**Scheduler change (found by the test):** background jobs were picked first-come-first-served, so a long background
job (compaction-sized, 3,000 grains ≈ 2.4 s) held up every playback cold start: tracks missed their start. Live
background jobs now take grains in turn (round robin).

Tests: `test_content_engine_playback_jobs` — 8 tracks on a simulated clock (tick every 1,040 µs, every grain the
measured worst g = 810 µs), a display job always pending and a 3,000-grain background job already running: every
track starts on time, half switch source mid-loop at the next boundary, 0 underruns, 0 late switches, 0 missed hard
deadlines, 0 overruns, display ≥ 1 grain in 4, and track 0's stream equals a reference from uninterrupted
compositions. `test_content_engine_scheduler` gains the turn-taking case. Mutations caught: background FIFO, deadline
at "now", a job that never ends, no transition jobs, no steady jobs.

### E6.2c — source-transition liveness (owner ruling 2026-11-05, as built; § 7.3 amendment proposed)

**Classes (ruling):** Hard — accepted Stop / edit sealing, publication obligations, steady playback windows; never
depends on spare capacity. **Transition** — preparing a newly selected source (Undo, Redo, slot switch, edit commit):
`JobClass::Transition`, ordered by target boundary, priority over soft work, and at least every second grain while
pending whenever the hard slack check allows (so it cannot starve behind repeated or long hard work while the hard load
is feasible). Soft — display, persistence, background (sweeps, reclamation, compaction).
**Late adoption:** a transition not ready at its boundary leaves the previous source playing with its pins held,
counts `lateSourceSwitches`, keeps composing toward the next boundary, and is adopted at the first boundary after it
is ready. **Latency:** `PlaybackTrack::Stats` reports acceptance-to-audible ticks (last, worst) and each adopted
transition's grains, separately from steady deadlines. **Stop** (`PlaybackTrack::stop`) closes every sounding note,
cancels a pending transition and releases both cursors' pins.

**REDs** (`test_content_engine_source_transitions`): simulated clock (tick 1,040 µs, every grain 810 µs); load = seven
dense playing tracks, each a hard 100-grain window job per boundary, plus display always pending; track 0 plays real
admission-dense music (~2,700 notes per window, ≤ 20 open notes) and its stream is compared with uninterrupted
compositions. The steady-window simulation of E6.2b is not used as evidence here.

| Case | Acceptance → audible | Transition grains | Named outcomes |
|---|---|---|---|
| (a) 8 dense + Undo, warm-up (no checkpoint) | 1,436 ticks (1.49 s) — first boundary | 729 | 0 hard misses |
| (a2) the same, Undo target's checkpoints present | 1,436 ticks — first boundary | 183 | 0 |
| (b) demand + edit commit with a 4,096-hide index build | 836 ticks (0.87 s) | 548 | 0 |
| (c) B superseded by C, C by D before ready | D at its first boundary | 640 | B, C leave no pin |
| (d) stop while a transition is pending | — | — | sounding notes closed, silence, no pin |
| (e) sustained: + a 10,000-grain hard obligation (always pending), a new source every 3 windows | worst 2,572 ticks (2.7 s) | ~640 each | 2 late of 10, latest adopted, 0 misses |
| (f) infeasible (7 × 400 grains per window) | — | — | 26 hard misses, 10 late switches; no phantom or duplicate note, no pin left |

Under this load a transition gets ≈ 0.64 grains per tick (≈ 985 per window). Mutations caught: no transition share,
transitions demoted to background (both: only 4 of 9 selections adopted within two boundaries in (e)), stop closing
nothing, a cancelled transition keeping its pin, latency measured from the target.

**Finding — Undo / Redo lose their checkpoints:** the checkpoint table keeps one manifest per slot (the current one).
An Undo or Redo selects another manifest, so as built it always takes the warm-up path ((a): 729 grains instead of
183).

**Trade-off against plan § 7.3** (which lists the cold start as hard with the next boundary as its response). Measured
here: next-boundary response holds when the transition's grains fit the transition share before that boundary. In W's
worst case — a 64-bar admission-dense loop, warm-up ≈ 3,009 grains (§ 7.2) — that is ≈ 3.1 windows of share: audible at
the 4th boundary after acceptance (≈ 5–6.5 s at 300 BPM), the previous source playing meanwhile. With a usable
checkpoint (≤ 190 + 58 grains) it is the first boundary, or the second if accepted in the last ~25 % of a window.

**Proposed amendment (owner to ratify):** § 7.3's "one cold start (gesture)" row becomes a Transition-class row:
acceptance-to-audible ≤ the first boundary by which ⌈G / ρ⌉ windows of transition share have passed (ρ ≈ 985 grains
per window under W's worst steady load; G = the transition's grains), i.e. ≤ 2 boundaries with a usable checkpoint and
≤ 5 boundaries on the warm-up path; the previous source plays until then (`lateSourceSwitches`). Two options would
bring Undo / Redo back to the checkpoint case: keep the previous manifest's checkpoints per slot as well (≈ 207 KB more
PSRAM: 64 slots × 8 × 404 B, inside the 801 KB allowance), or sweep a history manifest on demand when it becomes the
Undo / Redo target (no memory, latency unchanged).
