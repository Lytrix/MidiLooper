# 3.12 / 5.5 — the falsified emission clause repaired: the emission selection **refuses by name** at the declared closure capacity

Repair slice of task **3.12** (admission limit and the bounded legacy compatibility path) carrying the gate that
fixtures **5.5** and **2.11** name, and closing the clause that
[`evidence/2026-10-06-s312e-paged-serving-envelope/`](../2026-10-06-s312e-paged-serving-envelope/EVIDENCE.md) measured and
**falsified** one increment earlier. Packet: [`EXECUTION.md`](EXECUTION.md). Firmware + tests.

## 1. The candidates, and which one the measurement settles

3.12e's record named two repairs and attributed no mechanism: *a producer turn that publishes more than one page while the
ring has room*, or *a named refusal of the emission selection at densities the declared service cannot carry* (I3c-b's
lesson). This slice measured the mechanism first, on the same drive, with temporary probes (since removed):

| figure | value |
|---|---|
| frames whose consumed-ordinal delta ≠ their due set | **0** — every frame consumed exactly its own due set |
| frames whose emissions fell short of that due set | **20**, 370 events (8.8 % of 4,200) |
| the kind of every missing row | **`NoteOff`** (150 sampled mismatches, *all* `type=128`); no `NoteOn` was ever missing |
| the missing `NoteOff`'s own `NoteOn` | **already sent**, at its own tick, in an earlier frame |
| the active-note ledger's occupancy during the loss | pinned at **128 = `ActiveNoteLedger::kMaxOpenNotes`**, `overflowed == 1`, from frame 3 of pass 1 onward |

So the walk was never the loser (it consumed its due set every frame, which also explains 3.12e's withdrawn probe:
doubling the lane's turns changed nothing, because the producer's page rate was never the constraint). What bound the
drive is the **emission service's declared closure capacity**: the interval's own content holds ≈ 157 ledger identities
open at once (1,050 note-ons over a 1,336-tick on-span with a 200-tick note span), the ledger therefore stopped
*recording* note-ons once its table was full — its documented, never-evict policy emits the On anyway — and then refused
every note-off for an unrecorded identity as an **orphan**. The measured result is not a late delivery and not a refusal:
it is **a note-on emitted that nothing can close** (a stuck note), i.e. *wrong content*.

**Candidate A is falsified as a repair**: no producer turn, however many pages it publishes, can change a per-frame
ledger occupancy that the interval's own musical concurrency fixes. **Candidate B is the repair**, and it is what this
slice implements.

## 2. What was implemented

1. **The criterion.** `Track::beginPagedPlaybackForSlot` now reads the interval's own **closure profile** — the running
   note-on/note-off balance of the content the retained gather has already traversed for this interval, plus whether that
   content owes closures at all — and refuses the switch when the peak exceeds `ActiveNoteLedger::kMaxOpenNotes` **and**
   the content closes notes. Both terms are deliberate:
   * the balance is **prefix-safe** (a prefix's maximum can only be ≤ the whole traversal's), so a refusal decided on a
     partially gathered interval — the slice-ceiling route — can never refuse content the whole would have carried;
   * the `owesClosures` term exists because note-on-only content exceeds the capacity while losing **nothing** (a
     table-full note-on is still emitted and there is no closure for it to orphan). That is the pre-existing I3b/I5
     fixtures' shape, and the peak alone over-refuses it — measured, not assumed: see **RP-E** below, where dropping this
     term makes ten of them fail.
2. **The refusal, by name, in three places.** `LoopPlaybackRuntime::PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity`
   (the lane's own conclusion), `PlaybackViewPendingReason::ClosuresExceedDeclaredCapacity` (why the demand is still
   owed), and `Track::PagedEmissionDeliveryState::ClosuresExceedDeclaredCapacity` (the selection gate's answer). The ring
   is **never established**: no page is produced, nothing is bound, so `admittedForEmission()` — the hot path's one
   cached boolean — is false and no tick can take the ring.
3. **The demand is not dropped.** Both switch routes coalesce it back into `pendingViewRequest_` with the new reason, and
   `committedPlaybackRetryCanProgress` refuses to re-attempt it exactly as it does for `WindowExceedsRetainedCapacity`
   (a refusal with a name is an answer, not a demand to re-gather every turn).
4. **No new cost.** The probe is a per-event counter over content the gather already traversed, in the
   runtime's own external state and in flash: **no allocation, no RAM1 variable, no ITCM instruction** (the build's line
   below is byte-identical to 3.12e's).

## 3. The fixtures

The 3.12e drive is split into its two halves, sharing `prepareDensePairedInterval(notes, loopBars, noteSpanTicks)`, whose
one degree of freedom is the note span that decides the interval's closure high-water against the same over-`L` content:

* **The acceptance half** — `test_3_12_the_paged_path_serves_a_restored_over_l_interval_at_each_due_tick`, 1,060 pairs
  (**2,120 events > `L`**) with a **132-tick** span: closure high-water **109 ≤ 128**. The clause now holds strictly and
  is asserted with the numbers: over two whole passes, *per frame* the rows the MIDI edge received equal the interval's
  own due set, in order (`skipped == 0`, `shortFrames == 0`), `emitted == 2 × the interval` exactly, the sink and the wire
  boundary agree element-wise, and `ledger.overflowed()` is false. The four serving properties 3.12e established are
  re-asserted unchanged: one page per turn, the ring's high-water ≤ 3 while the interval is over 5× its whole capacity
  (a **storage** bound; no duration is claimed), no inline rebuild, every wrap straddled.
* **The reproducer, now the refusal's own acceptance test** —
  `test_3_12_a_restored_over_l_interval_beyond_the_declared_closure_capacity_is_refused_by_name`, the 3.12e content kept
  whole (1,050 pairs, 2,100 events, **200-tick** span, high-water **164 > 128**). It asserts: the traversal completes
  inside the gather's ceiling (17 turns, so the over-`L` fact came from the admission, not a spent budget), **no page was
  ever produced**, `pagedActive == false`, the ring's ordinal space was never written, the lane's outcome / the pending
  reason / the selection gate's answer are all the new name, the delivery is unbound and inadmissible, the demand is still
  owed with its position promise untouched — and then, over the same two whole passes that used to emit 3,830 rows: **zero
  rows** at the MIDI edge, zero on the wire, no merged fallback built, and **`ledger.overflowed() == false`**. That last
  figure is the point: the harm class cannot arise from a path that served nothing, so the refusal is a repair rather than
  a smaller loss.

**A fixture-construction fact worth recording** (it is why the carried shape's span is 132 and not, say, 100): the
capture store's hot-stop rule removes completed pairs shorter than `noteMinLengthTicks` (48) using a *newest-open-on* stack
pairing, so a dense same-pitch schedule whose note-ons can land inside an earlier same-pitch note's span is mis-paired and
**both** its events are dropped at the seal. With 1,100 pairs and a 100-tick span that removed 418 pairs (the interval's
effective content was **1,364**, not 2,200 — the gather then completed inside `L` and the demand became a *window*). A
40-line model of the pairing reproduced exactly that figure (1,364 / 418) and then located a span that survives the store
whole while keeping the high-water inside the capability (132 → 109, with a pair still 176 positions apart in the event
order, so the cross-page closure claim stays non-vacuous). The fixture is the *declared service's* shape either side of the
capacity; the capture pipeline's own minimum-pair rule is orthogonal to it and is not changed here.

## 4. Red-proofs (guarded: sha256-verified copy, restore verified against the pristine hash)

| id | mutation | measured |
|---|---|---|
| **RP-D** | the closure preflight disabled in `beginPagedPlaybackForSlot` | **RED** — `test_3_12_a_restored_over_l_interval_beyond_the_declared_closure_capacity_is_refused_by_name`: *"Expected 0 Was 1. no page is produced before the switch is decided"* (the ring was established again, i.e. 3.12e's loss returns) |
| **RP-E** | the `owesClosures` term dropped from the criterion | **RED** — **10** fixtures fail, all of the note-on-only paged shape (`test_3_7a_i3c_a_paged_emission_never_produces_and_names_its_states`, `…_a_whole_loop_interval_is_refused_by_name`, `…_i5_the_boundary_judgement_precedes_the_selection_in_one_turn`, `…_i5_a_rootless_demoted_slot_is_served_from_the_ring_pass_exact`, `…_i4_the_paged_delivery_names_a_halved_service_rate`, …): the peak alone over-refuses content the service demonstrably carries |

Both restores were verified byte-identically against the pristine `sha256=2150eedd273c1a93edb239d426338a795f8307dd2f2655ba11d3bcc2104bfede`.

## 5. Verified

* focused `test_runtime_working_set_ownership` **164 / 164** (baseline 163, +1 fixture); ring suite `test_window_paged_legacy` **3 / 3**;
* full `pio test -e native` **2,842 / 7 skipped / 2,835 succeeded / 0 failed** (baseline 2,841 / 2,834);
* `pio run -e teensy41-capture-serial` **SUCCESS** with the RAM1 line **byte-identical** to 3.12e's
  (`variables 96,192`; `code 392,248 + padding 968`; locals free 34,880) — the refusal costs no RAM1 and no ITCM;
* both red-proofs measured red with byte-identical restores; `git diff --check` clean; `openspec validate --strict` valid.

## 6. Named, not claimed

* **The criterion is a balance, not a ledger.** It counts a note-off as closing one identity, so content whose note-offs
  do **not** close its own note-ons (orphan-off content that mis-pairs) is not detected here while the peak stays inside
  the capacity; the emission service's own refusal/`overflowed()` report is the backstop owed for that shape, and 3.12e's
  measurement did not exercise it.
* **The refusal is conservative in one direction, deliberately.** Content whose *excess* note-ons are unpaired (a
  wrap-crossing note whose closure lies outside the interval) is refused although the ledger might still carry every
  closure it owes. Over-refusal is the safe direction for "emit no content the service cannot close"; it is stated here
  rather than smoothed over.
* **Channel remapping is not modelled**: the balance is per event, not per identity, so a multi-channel content whose
  playback channel collapses to one lane is not distinguished. Named residual; the two fixtures' content is single-channel.
* **For this content the slot is not served at all.** Rootless, demoted, merged store empty: the refusal means silence for
  the demand's interval **by decision** (the I3c-b trade against stuck notes), and it is why **3.7b** (fallback removal)
  cannot be closed for such content without either raising the declared capacity or giving the closure a different owner.
* **No duration claim, no device leg, no `ExecutionBounds.md` row change, no fallback removal (3.7b).** The end-to-end
  thresholds remain **S7.5a**'s.
* **`kMaxOpenNotes` itself is unchanged.** DEC-108 §14 item 12(c) leaves the reserve's size to measurement and the sweeps
  that could move it are **6.3**'s; this slice makes the *existing* declared capacity the emission selection's own
  criterion rather than pre-empting that decision.
