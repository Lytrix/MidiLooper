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
