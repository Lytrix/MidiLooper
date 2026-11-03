# C5.3b — device successor-lead sweep at 0/3/7 denials (DEC-246 amendment 26)

Date: 2026-11-02.

## Setup

- **Image.** `teensy41-capture-serial-revstore-succprobe`, HEX `299ca0e3af56f802`: the reader image plus the successor
  probe and the denial injector, measurement only. RAM1 is the reader baseline.
- **Stimulus.** `STIM-LANE-CADENCE-TEMP-01`, sweep mode, run with
  `host_midi_hitl.py run --scenarios lane_cadence --track 8 --loop-slot 4 --midi-channel 1 --boot-reset
  --lane-cadence-mode sweep --lane-cadence-deny 0,3,7`.
- **Content.** The dense HITL take on track 8 slot 4: 32 bars of 16ths, the manifest-routed slot. The rest of the set
  plays, with normal display and save activity. 300 BPM on external MIDI clock; no overdub.
- **Restart gate.** The first manifest window served 24.4 s after the restart.
- **Capture.** `captures/session_20261102_184609.log`; the points are in `captures/lane-cadence.json`.

## Method

- The denial injector withholds `deny` grants after every grain served to track 8.
- Each probe configures `!SUCC_PROBE <lead> 7 <deny>`, which resets the counters, and lets that first seam resolve.
  It then configures again, because a configure lands mid-window and its own successor is requested at once with
  whatever lead is left. Only the seams after that are judged.
- A failed point whose smallest **actual** request lead fell more than 48 ticks short of the configured lead is
  measured once more and flagged "short".
- The search halves the lead from one span (1,536 ticks) until it fails, then bisects at 48-tick resolution, then
  checks 20 and then 50 seams.

## Result

| deny | Lmin (50/50 seams) | max request → ready | max request → admit | notes |
|---|---|---|---|---|
| 0 | **288 ticks** (240 fails) | 232 | 112 | ready margin at Lmin: 48 ticks |
| 3 | **none ≤ 1,536** | 952 | 464 | every miss is on a seam whose actual lead was 744–760 |
| 7 | **none ≤ 1,536** | 952 | 464 | the same: actual lead 744 at the miss |

Points (lead, seams: handed/missed, actual minimum lead):

- **deny 0:**
  - ok: 1536 (10/0, 1512), 768 (10/0, 736), 384 (10/0, 368), 288 (10/0, 272), 288×20 (21/0), 288×50 (50/0).
  - fail: 192 (0/1, 184), 240 (1/1, 224).
- **deny 3 at 1536:** 4/1 and 1/1, both short (744 and 760). 186 and 78 grants withheld.
- **deny 7 at 1536:** 1/1 and 1/1, both short (744). 154 and 147 grants withheld.

## Reading

- **Uncontended (deny 0).** A dense manifest successor is ready within 232 ticks of its request. 288 ticks of lead
  hold 50 clean seams at 300 BPM.
- **Under arbitration (deny 3 and 7)** the failure is not the lead the requester asks for.
  - With the lead set to a whole span, the successor that misses was requested only 744–760 ticks before its
    handoff: about half a span after N began.
  - Its preparation under the same denials takes up to 816–952 ticks.
  - So with the current two-view rotation the successor cannot even be requested early enough. The earliest request
    point under contention is about 770 ticks into N, which is below the preparation time.
  - The cause of that delay is **not yet named**. The most likely candidate is that the previous window's roles are
    released late under withheld grants. A configure artefact is excluded by the method.
- **For amendment 26's decision.** `Lmin(7) < 1,536` does not hold, and neither does `Lmin(3)`. Per the amendment,
  the next question is the one it names: whether a deadline-bearing successor should be allowed seven denials, a
  bounded "served at least every N opportunities" guarantee, or a deeper lookahead. A larger lead price cannot fix it:
  the request itself comes too late.

## Not measured here

- The cause of the ~770-tick request delay under denials (one trace of role release against request time would name
  it).
- Denial levels 1, 2 and 4–6.

## Follow-up trace: the "late request" is the loop's terminal one-bar window (owner ruling, same day)

- **Image and capture.** Image `092b3bc7a7456f8c` (the probe's per-seam `succ_seam` trace). Main log
  `captures/session_20261102_*` from `trace2`, plus a read-only drain of the device ring. Points `0:1536:6` and
  `3:1536:6`.
- **The five gates never held a demand** (pending, frozen, coverage role, outstanding grains, ready): `gates=0` on
  every seam. Each demand opened on its first eligible producer turn, 16–32 ticks after N's adoption.
- **Every missed seam is the successor of the loop's last window.** The loop is 25,344 ticks = 33 bars, so two-bar
  windows leave a final **one-bar** window [24,576, 25,344), and its successor gets at most 768 ticks of lead. The
  misses are at N.end 50,688, 76,032 and 101,376, each 768 after the previous seam.
- **Mid-loop seams at deny 3 all handed off:** lead 1,504–1,520, request to ready 440–1,032, margin at least 480.
  At deny 0 request to ready is 224–248, and the terminal seam hands off too (lead 768, ready 224).
- **Correction to the reading above.** The one-window pipeline fits a two-bar span at 3 denials. What fails is the
  successor of a **short terminal window** (odd-bar loops), whose lead is bounded by that window's own length.
  Neither arbitration nor role release is the cause.

## After the balanced tail (owner ruling, same day; `b592fccd`)

- **Image.** `c15d71fe68c93ab5`. On the routed slot the 33-bar partition now ends `... | 2 | 1.5 | 1.5 bars`, so
  every traversal has two 1,152-tick tail windows instead of a 768-tick one.
- **Run.** Fixed points of 50 seams each at a full-span lead: `--lane-cadence-points 0:1536:50,3:1536:50,7:1536:50`.

| deny | seams | missed | max request → ready | max request → admit | min ready margin | min actual lead |
|---|---|---|---|---|---|---|
| 0 | 50/50 | 0 | 288 | 136 | 880 | 1,136 (the tail) |
| 3 | 50/50 | 0 | 1,208 | 592 | **40** | 1,120 (the tail) |
| 7 | 0/1 | **1** | — | 1,192 | — | 1,520 (a full span) |

- **Deny 3** is now clean: the wrap miss was the partition. The margin is thin (40 ticks at the tail).
- **Deny 7 fails on a full two-bar window** with full lead: admission alone took 1,192 ticks. This is the genuine
  scheduling question amendment 26 names: a bounded service guarantee for deadline-bearing successors, or deeper
  lookahead.

### Deny 4–6 (same image, `--lane-cadence-points 4:1536:50,5:1536:50,6:1536:50`)

Each level was measured twice, because each failure fell on a tail seam (actual lead 1,120–1,144, the balanced
1.5-bar window).

| deny | attempt 1: handed/missed, max ready, min margin | attempt 2 | miss is on |
|---|---|---|---|
| 4 | 1/1, ready 1,032, margin 88 | 1/1, ready 904, margin 224 | a 1,152-tick tail seam |
| 5 | 1/1, ready 1,056, margin 64 | 1/1, ready 1,088, margin 48 | a 1,152-tick tail seam |
| 6 | 0/1, admit 824 | 0/1, admit 648 | a 1,152-tick tail seam (the first seam) |

**Reading:**
- With the balanced tail, the largest clean allowance for a dense manifest successor at 300 BPM is **3 consecutive
  withheld grants** (50/50, margin 40).
- From 4 up, the **1.5-bar tail windows** are what miss: about 1,120 ticks of lead against preparation that grows with
  denials (request to ready about 900–1,090 at 4–5, plus the turn variance).
- Full two-bar seams handed off at 4 and 5 before the tail missed. The answer is therefore partly geometric (the tail
  is the shortest lead) and partly the service rate.

## The enforced bound (`5424079e`): deny 4 injected, at most 3 effective

- **Image.** `dbc29983085e1b57`, run `4:1536:20` (twice); per-seam rows drained from the device ring.
- **The bound engages:** 6–12 grants forced per seam, so the successor is served at least every fourth opportunity.
- **Mid-loop seams** (two-bar N, lead 1,504–1,528) all handed off, with request to ready 1,184–1,392 ticks.
- **Every miss is the wrap successor.** It is the next traversal's first two-bar window, requested at the start of
  the second 1.5-bar tail window, with a lead of 1,128–1,152 ticks. It needs about 1,300–1,400 ticks to prepare
  under the 3-denial service rate (N.end 50,688, 76,032 and 101,376). The first tail window's successor (the second
  tail window, 1.5 bars) handed off: ready 704–776.
- **Reading.** Under the enforced 3-denial contract, a full two-bar window needs more preparation than the balanced
  tail's 1,152-tick lead. The earlier plain deny-3 run passed with a 40-tick margin; this run, at the same service
  rate, does not. The tail's lead and the wrap successor's work are mismatched.
- **The restart stuck-role shape did not reproduce.** The new `busy,roles` rows at the restart show only
  `BuildActive` (refusal 2) on other tracks, with no stuck role.

## The equal partition (`4f151551`), the enforced bound, injected deny 4

- **Image.** `d9afc37d78d8bf7f`. The routed 33-bar loop is now 6 × 1,496 + 11 × 1,488 ticks: no window shorter than
  its successor by more than one 8-tick quantum.
- **Run.** `--lane-cadence-points 4:1536:60`: the injector withholds 4 grants after every grant, and the 3-denial
  bound forces the 4th.

| seams | missed | max request → ready | max request → admit | min ready margin | min actual lead | grants withheld |
|---|---|---|---|---|---|---|
| **60/60** | **0** | 1,104 | 496 | **368** | 1,464 | 2,001 |

- **Reading.** The owner's qualification shape holds: 33 bars, 300 BPM, the bounded successor service (at most 3
  consecutive denials), a forced deny-4 injector, 50+ seams, 0 misses.
- The margin is 368 ticks, against 40 at the balanced tail's deny 3. One-window lookahead suffices under the enforced
  bound once the partition has no short-to-long seam.

## Re-measured leads under the equal partition and the enforced bound (pricing input)

- **Image.** `d9afc37d78d8bf7f` (equal partition and 3-denial bound), freshly flashed and boot-verified.
- **Run.** `--lane-cadence-deny 0,4` (full search; deny 4 is capped at 3 effective by the bound).
  `captures/session_20261102_2216*`.
- **First-window gate.** 25.4 s after the restart. The gate now waits 240 s, because the generic DEC-147 price can
  place the first window more than 90 s ahead.

| injected deny (effective) | Lmin, 50/50 seams | fails at | max request → ready | margin at Lmin |
|---|---|---|---|---|
| 0 (0) | **288 ticks** | 240 | 264 | 8 |
| 4 (3, bounded) | **1,152 ticks** | 1,104 (8/9) | 1,104 | 40 |

**Against DEC-147's formula** (`grains × (D + 1) × advance + leadTicks`), with the dense window's 39 grains, the
declared 64-tick advance and the 768-tick lead:

| D | formula price | measured Lmin | measured ticks per opportunity |
|---|---|---|---|
| 0 | 3,264 | 288 | 264 / 39 ≈ 6.8 |
| 3 | 10,752 | 1,152 | 1,104 / (39 × 4) ≈ 7.1 |
| 7 | 20,736 | — | — |

**Reading:**
- The denial term is **not** what makes the generic price unreachable. The declared per-turn advance is: 64 ticks
  against about 7 measured at 300 BPM under this load, with the fixed 768-tick lead on top.
- A price written as `grains × (3 + 1) × advance + overhead` fits the one-window lookahead (1,488-tick windows) only
  with an advance near the measured opportunity cadence: about 7 ticks, or 8 with the observed margin.
- The 64-tick declared advance cannot fit (39 × 4 × 64 = 9,984).
- Choosing that advance, a measured and enforced opportunity cadence or the declared turn bound, is the remaining
  pricing decision.

## The cadence contract measured directly (owner ruling: <= 8-tick opportunity, <= 32-tick grant)

- **Image.** `03596c3c5c11cddb` (equal partition, 3-denial bound, per-seam gap probe). Freshly flashed and
  boot-verified.
- **Run.** `--lane-cadence-points 4:1536:50`: 50/50 handed off, 0 missed, margin 504.
- **Data.** 53 seams' `succ_seam` rows, drained from the device ring.

| per seam, while the successor is owed | max | typical | contract proposed |
|---|---|---|---|
| eligible-opportunity gap | **40 ticks** (17 seams; the other 36 seams: 32) | 32–40 | ≤ 8 |
| served-grant gap | **104 ticks** | 96–104 | ≤ 32 |
| grants to finish the dense window | 11 (48 seams; 10 on 4) | 11 | — |

**Correction to the earlier "7 ticks per opportunity" reading.** That figure divided ticks by the window's 39 grains,
but the build completes in about **11 grants**, not 39. The lane opportunity actually comes every **32–40 transport
ticks** (4–5 external-clock pulses, about 35–42 ms at 300 BPM). With the 3-denial bound, the grant gap is 4 × that,
about 100. The real cost model is therefore:

- request to ready ≈ grants × grant gap ≈ 11 × ~100 ≈ 1,100 ticks, which is what was measured (976–1,104).

**Reading:**
- An `<= 8`-tick opportunity contract is **not** what the scheduler provides today: it is 4–5 times that.
- At the current cadence a formal worst case is 11 × (3 + 1) × 40 = **1,760 ticks**, more than the 1,488-tick
  windows, even though every measured seam fits (worst 1,104).
- One-window lookahead is therefore formally guaranteed only if either:
  - the lane opportunity is enforced at a cadence of about 33 ticks or better (1,488 / (11 × 4)), or
  - the grant count per window drops.
- Otherwise the price needs either an enforced cadence or earlier preparation.

## The grant bound per window (owner ruling: price grants × grant gap)

- **Image.** `f186c5471dad8f6e`; per-seam grants served, grants at admission, and the counted rows E and read units.
- **Run.** `--lane-cadence-points 0:1536:30,4:1536:30`: 30/30 at deny 0 and 30/30 at bounded deny 4, 0 missed.
- **Data.** 64 seam rows.

| rows E | read units | grants to admission | grants after admission | total grants | seams |
|---|---|---|---|---|---|
| 57 | 115 | 3 | 7 | 10 | 3 |
| 83 | 167 | 4 | 7 | 11 | 3 |
| 93 | 187 | 4 | 7 | 11 | 53 |
| 94 | 189 | 4 | 7 | 11 | 4 |

- **A grant is a fixed unit of work.** The total is the same at deny 0 and at bounded deny 4, so it does not depend on
  the service rate.
- **Read units are exactly `2E + 1`** (the manifest cursor's documented cost).
- **Fitted bound over this range:** `grants = ceil((2E + 1) / 64) + 1 + 7`, i.e. qualification at the 64-unit fill
  quantum, one admission grant, and a build that took a constant 7 grants for E 57–94.
- **Not yet proven:** how the 7 build grants scale with E. The view's cap is 2,048 events, where qualification alone
  is 65 grants. A single worst case for every window therefore cannot fit one window; admission has to price the
  counted candidate.
- **Gaps in this run:** opportunity gap up to **56** ticks, grant gap up to **144** at bounded deny 4 (40 / 40 at
  deny 0).
- **Budget at E = 93, 11 grants** in a 1,488-tick window: a guaranteed grant gap of at most 128 ticks fits
  (1,408). The observed maximum of 144 does not (1,584). So the grant deadline needs **enforcing**, not just
  observing.

## `buildGrantBound(E)` from the code (owner ruling: prove the 7 first)

The post-admission build of a committed manifest window runs (`RuntimePlaybackViewBuild::stepOrder`, fill already
done during qualification, `requiredFillGrains = 0`):

1. **Canonical order** (`SnapshotCanonicalOrder`, 48 entries per grant).
2. **Ranks** (`SnapshotRanks`): E entries at 48 per grant.
3. **Playback order** (`SnapshotPlaybackOrder`, 48 entries per grant).
4. **Publication**: one grant at `Complete`.

Each order pass (`RuntimePlaybackOrderBuilder::step`):
- starts with a **fill pass** of one unit per row, detecting already-ordered input (`orderedDuringFill_`, initially
  true);
- if the input arrives in the pass's own order, the merge completes immediately (`runWidth_ = orderedEventCount_`);
- otherwise it runs a bottom-up merge sort from width 1: about `E × ⌈log₂ E⌉` more units, plus a copy-back when the
  last merge lands in scratch.

Whether each pass's input is ordered follows from the store's merge order, `(tick, equal-tick class, run)` then
`(origin, seq)`: the DEC-244 order, NoteOff before NoteOn at equal ticks.

- **Playback order** `(phase, kind Off < On < other, rank)`: inside a non-wrapping window, phase is monotonic in tick
  and the class order is the kind order, so the input is **already ordered**: `ceil(E / 48)` grants.
- **Canonical order**: ~~`(raw tick, pass, event)`: ordered unless an equal-tick pair's class order disagrees with its
  pass order~~. **Corrected below (2026-11-03, host-pinned):** the key is the row's `canonicalOrder = origin << 16 |
  ordinal`, i.e. **pass-major**, so the merge sort runs whenever two passes interleave in time inside the window.

**Grant bound for a committed manifest window:**

```
G(E) = ceil((2E + 1) / 64)            qualification (read units 2E + 1, 64 per grant)
     + 1                              admission
     + C(E)                           canonical order
     + ceil(E / 48)                   ranks
     + ceil(E / 48)                   playback order (input ordered by construction)
     + 1                              publication

C(E) = ceil(E / 48)                              no equal-tick class/pass inversion in the window
C(E) <= ceil(E * (2 + ceil(log2 E)) / 48)        otherwise (fill + merge passes + copy-back)
```

- **Against the device** (no equal-tick inversions in the dense take): G(57) = 2 + 1 + 2 + 2 + 2 + 1 = 10, and
  G(83) = G(93) = G(94) = 3 + 1 + 2 + 2 + 2 + 1 = 11. These are **exactly** the measured 10 and 11. The flat 7 is
  `3 · ceil(E / 48) + 1` staying constant for E in 49–96. It steps to 10 at E = 97–144, and so on.
- **The adversarial branch matters.** At E = 93, an inversion makes C(93) ≤ ceil(93 × 9 / 48) = 18 instead of 2, so
  G(93) ≤ **27**. At 27 grants a 1,488-tick window needs a grant gap of at most 55 ticks.
- **Not yet verified on host:** the inverted branch, i.e. how many units the merge actually uses, and whether
  publication ever needs a second grant. A host fixture that counts grants for a window with an equal-tick cross-pass
  Off/On pair would pin it.

## Inverted canonical branch, pinned on host (2026-11-03)

Fixture: `test_c5_manifest_build_grant_bound_pins_the_canonical_merge_branch` (`test_playback_prewarm`). It drives the
real manifest build (`ManifestWindowCursor` -> qualification fill -> admission -> canonical order -> ranks -> playback
order) at the lane's quanta (fill 64, order 48) and again at quantum 1 for exact units, and checks each window's final
order, phase and rank against the published root's snapshot build.

| Window | E | read units | canonical units | ranks | playback | grants q / c+r / p | G(E) |
|---|---|---|---|---|---|---|---|
| one pass | 93 | 187 | 93 | 93 | 93 | 3 / 2+2 / 2 | **11** |
| two passes alternating in time | 93 | 187 | **837** (9E) | 93 | 93 | 3 / 18+2 / 2 | **27** |
| two passes alternating, parity case | 64 | 129 | **448** (7E) | 64 | 64 | 3 / 10+2 / 2 | **19** |
| one equal-tick cross-pass pair only | 93 | 187 | **837** | 93 | 93 | 3 / 18+2 / 2 | **27** |

(G adds the lane's admission and publication grants, 1 each, as measured on the device.) All parity checks pass.

**What is now exact:**

- **Canonical cost.** `C(E) = ceil(workEntriesUpperBound(E) / 48)`, where
  `workEntriesUpperBound(E) = E × (1 + L + (L odd))` and `L = ceil(log2 E)`. This is the builder's own declared bound,
  and the merge meets it exactly: each level writes all E entries, whatever the input's disorder. The earlier
  `ceil(E(2 + L)/48)` is exact for odd L and loose by E/48 for even L (64: 10 grants, not 11).
- **Stages never share a grant.** Each stage costs `ceil(units / quantum)`.
- **Publication** is one grant in every case.
- **The cost is a cliff.** A single out-of-order row, here one equal-tick pair, costs the full merge, just like a
  fully interleaved window.

**The trigger is broader than equal ticks.** `ManifestWindowCursor::step` keys each row `origin << 16 | ordinal`. The
fill stores that as the phase `SnapshotCanonical` sorts by, so canonical order is pass-major. Its input arrives in DEC-244
tick order, so it is ordered only while no later pass's row precedes an earlier pass's row in time. **Any overdub that
plays inside a window together with the record pass takes the merge path.** In a looper that is the normal case, not an
adversarial one. The dense device take matched the cheap path because it was a single pass.

**Why an equal-tick-local fix does not apply, and what does.** The disorder is not confined to equal-tick groups, so
reordering within equal-tick groups would not remove it. But the canonical order is used only to number ranks:
- `SnapshotRanks` reads `storage.order`;
- `SnapshotPlayback`'s `begin` then refills the order;
- no other reader exists.

The canonical order itself is a **stable partition by origin** of rows that already arrive in ordinal order within each
origin. The rank therefore has a closed form:

```
rank(i) = base[origin_i] + ordinal_i,    base[o] = rows of origins < o in the window
```

Computing it is one counting pass (E units) plus one assignment pass (E units), with no sort: the same 2E as today's
cheap path. The values are identical by construction to the sorted positions the snapshot path and its consumers
expect:
- `RuntimePlaybackBridge` indexes `canonicalCycleMasks` and predecessor ranks by them;
- `SnapshotPlayback` uses them as the equal-tick tie-break.

If a row can ever be filtered by retention, `ordinal` is replaced by a per-origin running count of retained rows; it is
still O(E). The cost is one small per-origin counter array. With it the bound becomes arrangement-independent:

```
G(E) = ceil((2E + 1)/64) + 1 + ceil(E/48) + ceil(E/48) + ceil(E/48) + 1
```

This is monotone in E alone, as DEC-147 pricing needs. **Not implemented**, per the owner ruling: no builder or
scheduler change yet.
