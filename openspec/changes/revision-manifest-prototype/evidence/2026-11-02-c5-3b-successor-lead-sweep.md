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
