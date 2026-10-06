# G2 execution packet — 3.12 (with 5.5 and 2.11): the paged path's serving envelope, measured per due window

Slice of task **3.12** (admission limit and the bounded legacy compatibility path), carrying the two gates that name the
same missing measurement: fixture **5.5** (the legacy paged path's envelope) and fixture **2.11**'s restored-over-`L`
dimension. Written before the edits, per the
[G2 execution packet](../../../../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
requirement. Test-only: no `src/` or `include/` production file changes.

## 1. What is owed, exactly

`tasks.md` § 5.5 asks to *"verify page preparation, page handoff and MIDI emission each meet their deadlines for a
restored loop whose requested interval exceeds `L`, including over-capacity same-tick events, loop wrap and cross-page
note closures; assert complete, ordered, exactly-once output with no tick delay, no inline rebuild and no truncation;
record that segmentation proves a **storage** bound, not throughput"*. § 2.11 asks for the same serving case from the
admission side (*"a loop restored from SD that predates the limit (served through bounded pages with cross-page
closures)"*), and 3.12's own text makes the production consumer its last clause.

The inventory of what already exists (before this slice):

| dimension | state |
|---|---|
| the ring's storage behaviour (over-capacity same-tick, `RingFull` → reuse, wrap-crossing window, >3-page pair) | **measured** — `test_window_paged_legacy` (3.12d), storage level |
| the production consumer entry + trigger (pages prepared per turn, `NeedsPage`, `StaleRevision`, release) | **measured** — 3.7a I3b fixtures |
| the emission adapter (ring → walk, pass re-arm, admission) | **measured** — 3.7a I3c-a / I5 |
| the interval's content emitted whole, two passes, on the real clock path | **measured** — 3.7a I5 (single-list content, no closures) |
| a halved service rate breaching within the pass | **measured** — 3.7a I4 |
| **complete/ordered/exactly-once *per due window*** (the "no tick delay" claim) | **absent** |
| **cross-page note closures on the emitting path** (a pair whose On and Off are pages apart) | **absent** — the I5 content has no Offs at all; 3.12d covers it at the storage level only |
| **the emitted stream exceeding `L`** as a joint proof that the ring served it | **absent** |
| the per-turn page cap and the ring's depth read as a bound *over a whole drive* | **absent** |
| "segmentation is a storage bound, not throughput", stated with its numbers | **absent** |

## 2. Scope

One fixture in `test/test_runtime_workings_set_ownership`'s file (the production-path suite): a **retained-source
(restored-library) shape** — dense content that predates `L`, pinned bodies retained for the current revision, no
whole-loop root — whose interval **exceeds `L`** and is served **through the ring**, driven by the production per-track
clock entry (`Track::playMidiEvents`) with one deferred-lane turn per frame, over **two whole passes** so the loop wrap
is crossed.

**The oracle is per frame, not per drive.** For every frame (from the transport's first), the rows the MIDI edge
received in that frame must equal, in order, the interval's own effective events whose tick is due in that frame
(`(prevPhase, curPhase]`, with the pass-boundary split; the fresh start is `tick ≤ cur`). That single comparison is the
measurement of five separate clauses: nothing is withheld to a later tick, nothing is emitted early, nothing is dropped
or duplicated at a page boundary, the order (including Off-before-On at equal ticks) holds, and the whole interval is
emitted exactly once per pass.

**Bounds read, declared not invented:** one page (`kWindowPageCapacity` = 128 events) per deferred turn, the ring's
depth (`kWindowPageCount` = 3), `L` (`kWindowAdmissionLimit` = 2,048). **No duration is claimed**: the end-to-end
thresholds belong to the runtime change's **S7.5a — envelope/timing oracle**, and this fixture's numbers are per-frame
*sets*, not microseconds — which is precisely the task's own sentence (segmentation proves a storage bound).

**Out of scope, named:** the device leg (6.4/6.5) and any `ExecutionBounds.md` row reclassification; the cross-*interval*
closure (the reserve's case, not this fixture's); same-tick accumulation's arithmetic (already covered by the accounting
fixture and agnostic to content shape); and 2.11's "an edit that moves events into a full interval", whose reading is
recorded in the evidence rather than faked into a fixture (an edit's rows do not pass the capture-edge gate this owner
wired).

## 3. Transition oracle (pinned before the edits)

| # | Transition | Oracle |
|---|---|---|
| O1 | The demand for an interval **over `L`** on the retained-source shape | the window lane's traversal completes **inside** its slice ceiling and the switch comes from the **admission's capacity refusal** (17 of 18 slices), so this is the over-`L` case and not a spent budget |
| O2 | Each frame of a two-pass drive | the MIDI edge's rows for that frame == the interval's due events for that frame, in order, at their own ticks |
| O3 | The whole drive | 2 × the interval's events, exactly once each, no drop, and the wire boundary agrees with the sink element-wise |
| O4 | A note pair whose On and Off are more than a page apart | both emitted, once per pass, in order — the closure survives the pages that carried its On being retired |
| O5 | Every deferred turn | at most one page produced (the declared cap) |
| O6 | The whole drive's storage | the ring's published-page high-water ≤ 3 while the interval's content is far larger (a storage bound, not throughput) |
| O7 | The whole drive's serving | no inline rebuild (`playbackOrderHotRebuildCount()` unchanged), the merged store stays empty, the paged delivery is what emitted (`emitting`, `dryTurns == 0`, `passesUnstraddled == 0`, ≥ 2 boundaries) |

## 4. Fixtures and red-proofs

* Fixture: `test_3_12_the_paged_path_serves_a_restored_over_l_interval_at_each_due_tick`, plus the content helper
  `prepareDensePairedInterval(notes, loopBars)` (1,050 note pairs = **2,100 events > 2,048** inside the two-bar span,
  one arithmetic-progression tick schedule whose Offs land on other notes' On ticks, so the same-tick tie is exercised
  by construction).
* Red-proofs (planned, each restored byte-identically, sha256-checked, anchor occurrence asserted):
  * **RP-A** the ring's drained-page release removed (3.12d's M-RING) → the ring cannot advance and the per-frame oracle
    fails (missing rows);
  * **RP-B** the ring's cursor re-feeding an offset at a page boundary → the per-frame oracle fails (a repetition).
  A mutation that the fixture does not catch is recorded as a non-red-proof with its mechanism, not claimed.

## 5. Exit criteria

1. focused `test_runtime_working_set_ownership` green with the new fixture;
2. full `pio test -e native` green, no suite ERRORED/FAILED;
3. the firmware build **unchanged** (`pio run -e teensy41-capture-serial` SUCCESS with the RAM1 line byte-identical — a
   test-only slice must not move the image);
4. both red-proofs measured red with byte-identical restores;
5. `tasks.md` § 3.12/5.5/2.11 recorded with what is now measured and what stays owed, and the runtime docs updated.
