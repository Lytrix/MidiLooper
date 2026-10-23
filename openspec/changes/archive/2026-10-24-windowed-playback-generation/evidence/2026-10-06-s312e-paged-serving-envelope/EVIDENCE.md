# 3.12 — the paged path's serving envelope, measured per due window: the storage bound holds, the emission clause is **falsified**

Test-only increment of task **3.12** (admission limit and the bounded legacy compatibility path), carrying the gate that
fixtures **5.5** and **2.11** both name. Packet: [`EXECUTION.md`](EXECUTION.md). No `src/` or `include/` file changed
(`git diff --stat` on the slice: the suite only), so the firmware image cannot move — verified, not assumed.

## 1. What was measured, and how

`test_3_12_the_paged_path_serves_a_restored_over_l_interval_at_each_due_tick`, driven through the **production per-track
clock entry** (`Track::playMidiEvents` with one deferred-lane turn per frame, 64-tick frames, two whole passes) on a
**retained-source (restored-library) shape**: dense content that predates `L`, pinned bodies retained for the current
revision, no whole-loop root, demoted (24 bars), and an interval of **note pairs** larger than `L` —
**2,100 events inside the two-bar span** the window path accepts (1,050 pairs, `L` = 2,048).

The content is built so the claims cannot be vacuous: the On/Off pairs share ticks with **other notes' Ons**, so
same-tick ties (and the Off-before-On rule at equal ticks) are exercised by construction, and a tick carries two events
throughout. The oracle is the interval's own effective content from the project's resolver
(`copyEffectiveCommittedEventsInRange`), read **per frame**, with the due set computed by the fixture's own phase
arithmetic (`(prevPhase, curPhase]`, with the pass-boundary split; the transport's first frame is `tick ≤ cur`).

## 2. What holds (asserted)

* **The demand is over-`L` for the right reason:** the window lane's traversal completes **inside** its slice ceiling
  (17 of 18 slices) and the switch to pages comes from the **admission's capacity refusal**
  (`WindowExceedsCapacity`), not from a spent budget — the restored-shape counterpart of the existing
  `…_an_over_capacity_admission_switches_to_pages` fixture.
* **No early emission:** every row the MIDI edge received was due in **its own frame**, at its own tick (per frame).
* **Order, no duplication, no invention:** the emitted stream is an **order-preserving subsequence** of the interval over
  both passes.
* **Storage bound — the task's own sentence:** the ring's published-page high-water stayed **≤ `kWindowPageCount` (3)**
  while the interval is **5.5× the ring's whole capacity** (`3 × 128 = 384` vs 2,100) → segmentation is a **storage**
  bound. No duration is claimed anywhere: the end-to-end thresholds are the runtime change's **S7.5a**.
* **No inline rebuild:** `playbackOrderHotRebuildCount()` unchanged across the drive, the merged store stayed **empty**,
  and the paged delivery is what emitted (`emitting`).
* **The wrap:** at least two pass boundaries judged, none straddled (`passesUnstraddled == 0`).

## 3. What is falsified (the finding)

**5.5's "complete, ordered, exactly-once output with no tick delay" does not hold for this shape.** Measured over two
passes (575 frames):

| figure | value |
|---|---|
| ordinals the walk consumed | **4,200** (= 2 × 2,100, exactly the interval twice) |
| rows emitted at the MIDI edge (sink) | **3,830** |
| **consumed but skipped** | **370 (8.8 %)** |
| frames whose emissions fell short of their due set | **20 short frames, 370 events** |
| dry turns in the final pass (per-pass counter; the boundary resets it) | **263** |

The mechanism, as far as the measurement pins it: the walk consumes the ring's ordinals in order and the adapter's own
catch-up rule expresses a late event as *"what the frame already passed"* — consumed, then **skipped** — so the events
whose page did not arrive before their tick window closed are **lost**, not served late, and not refused by name.

**Deliberately not attributed:** a plain per-turn page deficit. Doubling the lane's turns per frame in a withdrawn probe
changed **none** of these numbers (identical `lateFrames/lateEvents/emitted/pages/consumed/dry`), so the binding
constraint is not the producer's page rate per turn; finding it is the next slice's work, with this fixture as its
reproducer.

## 4. Red-proofs (byte-identical restores, anchors count-asserted, restore from a sha256-checked copy)

| id | mutation | measured |
|---|---|---|
| **RP-A** | `retireThroughOrdinal` never releases a drained page (3.12d's M-RING) | **RED** — my fixture: *"the declared rate lost events at this density: consumed 256 ordinals, emitted 256, skipped 0, over 2100 interval events x 2 passes, 46 short frames (3944 events), dry turns 0"*; also `test_3_7a_i3c_a_paged_emission_never_produces_and_names_its_states` and `test_3_7a_i5_…pass_exact` |
| **RP-C** | the emitting pass's own content bound dropped in `pagedEmissionStreamSize` (`servable = published`) — the I3c-c defect re-introduced | **RED** — `test_3_7a_i3c_a_a_paged_pass_emits_through_the_walk_and_straddles_its_wrap` (*Expected 5000 Was 4872. exactly two passes, nothing more: no next-pass event leaked into a pass*) and `test_3_7a_i5_a_rootless_demoted_slot_is_served_from_the_ring_pass_exact` (*Expected 2500 Was 964*) — **not** by this fixture, which is a bound on it, named below |
| **RP-B** | `pageForSequence` matching `page.sequence <= cursor` | **not red — recorded with its mechanism, not claimed:** the ring's sequences are monotone and a retired page is cleared, so the relaxed match finds the same page in every reachable state; the mutation is *equivalent* here rather than uncaught. |

## 5. Named, not claimed

* **The finding is pinned, not repaired.** The fixture asserts the loss (`skipped > 0`, `emitted + skipped == consumed`)
  with the numbers in its failure message, so the next slice's repair (or a **named refusal** of the emission selection
  at densities the declared service cannot carry — the I3c-b lesson that emitting wrong content is worse than not
  emitting from pages) has an acceptance test. 3.12 and 5.5 stay **open**; `ExecutionBounds.md` row 27 is **not**
  reclassified.
* **This fixture's own teeth have a measured bound:** RP-C (a changed *pattern* of loss that preserves order and the
  lateness class) is caught by the sibling I3c-a/I5 fixtures, not by this one — this fixture pins *lateness-class* loss,
  order, the storage bound and the serving path.
* **2.11's remaining dimension:** "an edit that moves events into a full interval" is **not** covered and is not faked
  here — reading it literally against this owner, an edit's rows do not pass the capture-edge gate
  (`Track::recordMidiEvents`) that 3.12c wired, so the clause needs the edit/interval-projection owner's terms (or a
  decision that it is already covered by `…_the_capture_admission_counts_committed_windows_only`), and 2.11 stays open
  on it. The rest of 2.11's list is covered by existing fixtures (below/at/above `L`, the closure at capacity, refusal
  by name, stop never refused, accumulation across passes) — the inventory is in the packet.
* **No device leg, no timing/threshold claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b).**
