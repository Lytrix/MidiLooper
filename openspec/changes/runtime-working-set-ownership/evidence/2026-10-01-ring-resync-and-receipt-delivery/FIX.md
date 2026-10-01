# FIX — a corrupt head prefix no longer costs the ring, and the save receipt no longer waits for the stop

**Date:** 2026-10-01
**Image:** `a1f4e4c0d2f43ee354782cc6e4fa56ab4c8e268cb774e60a252f763189b2696c` (the day's third; RAM1 unchanged at
`variables 96,192`, no `ram1_budget.json` re-base)
**Runs:** `captures/s4b-gate-newimage-01` (the wipe), `-02` (clean regression), `captures/s4b9-cross-track-17 … -21`

## 1. The torn record: 7 corrupt bytes at the head wiped 9 524 good ones

`captures/s4b-gate-newimage-01`:

```
#CAP,194244560,RING,head_bad,1,op=4,head=604322,tail=613846,used=9524,type=55,plen=12588,prefix=37322c3130300002fb1d00234341502c
#CAP,194244574,RING,wiped,1,9524,194242578
```

`op=4` is the flush walk's own advance, and the header layout explains the numbers: `CaptureRecordHeader` is
`{uint8_t type; uint16_t payloadLen}` (a pad byte at +1), so a header read at the head gives `type=0x37` (`'7'`) and
`payloadLen=0x312C` — both read out of **text** ("72,100"). A **valid** header sits exactly **7 bytes later**:
`type=2` (`Text`), `payloadLen=29`, payload `#CAP,…`. So the ring held one record's readable remainder and the head
was inside it, while `head + used == tail` still held: the *structure* was self-consistent and the *bytes* under the
head were not. The whole-ring reset then discarded 9 524 bytes — and the host's `RING,wiped` rule
(`serial_collector`) invalidates the capture, which aborted a leg whose subject was unrelated.

**Fix:** a bounded resynchronization replaces the blunt reset in both advance sites
(`flushCaptureBuffer`'s walk and `discardOldestRecord`). `headResyncDropBytes()` scans at most
`kHeadResyncScanBytes = 256` bytes forward for a plausible header (type in `{Revt, Text, DiagTrace}`, non-zero
length, fits in what remains) and accepts it only when the record *after* it is plausible too (or the candidate ends
the scan window), so one accidental byte pattern is not mistaken for a boundary. Dropping the unreadable prefix is
**counted and named** (`RING,head_resync,<count>,<bytes>,<first_us>,head=<idx>`) and the walk keeps draining the
records that are still intact, within a bounded number of recoveries per pass. With no boundary in the window the
behaviour is unchanged: reset and wipe.

**Evidence, and the regression that matters:**

| run | what it shows |
|---|---|
| `s4b-gate-newimage-01` (before) | `head_bad` + `wiped`; the capture was invalid and the intervention never fired |
| `s4b-gate-newimage-02` (after) | **`wiped=0`, `head_bad=0`, `integrity: COMPLETE`**, three switches queued and **two committed** (`0->2`, `2->1`), `overflow_free`/`call_site_gate_ok` with an **empty** warning list |

## 2. The receipt that arrived after its consumer had given up

With the ring healthy, the cross-track leg still refused at the successor-start gate — and the device's own rows say
why. `captures/s4b9-cross-track-18`: the record stop is at 216.772 s and the `PERS,result` receipt at 222.163 s
(+3.1 s device time), yet the harness's 25 s wait expired; at `-20` a **120 s** wait expired too, with the same row
in the capture. The mechanism is a deliberate policy: **the ring is retained, not drained, under transport** ("capture
must retain the full active-transport evidence burst until STOPPED", the `kCaptureRingBytes` rationale). A gesture
that keeps a track playing throughout therefore leaves the receipt in the ring until after the stop — which is after
the consumer has moved on. A receipt delayed past its consumer is not a receipt.

**Fix:** the save-completion row is emitted **where it is produced** when USB has room
(`DebugSessionCapture::persistenceResultImmediate`, wired to the two `SC_PERSIST("result", …)` sites through the
`#ifndef`-guarded `SC_PERSIST_RESULT` seam), falling back to the ring when USB has no room, so it is delayed rather
than lost. It is rare (a few per boot), so it costs no budget.

**Evidence:** `captures/s4b9-cross-track-21` — the gate reports
`overdub start admitted after stop receipt: 'PERS,result,'` and the leg completes.

## 3. The gesture: the destination *track* must be playing

The leg's own clause needed one more thing, and it is a gesture property rather than a state one: after a cross-track
switch the new track is *selected* but its loop is **not playing**, so a press on one of its slots is a
selection/toggle (runs 15/16/17: `Loop 2: Toggle Play/Stop`, and pressing the *focused* slot only refreshes the edit
session). The switch shape needs the destination **track playing** when a *different* filled slot is pressed. The
runner now does that before the pass that owns the finalization window
(`--preplay-track-number` / `--preplay-loop-slot`, then it hands the focus back to the source track), recorded in its
own ledger as `preplay_*` events.

**Result (`captures/s4b9-cross-track-21`) — the clause, on hardware:**

```
[39.193] Selected track 5                      <- preplay: destination track selected
[39.905] Loop 2: Toggle Play/Stop              <- its own loop starts playing
[47.861] [TRACK] Reentry window open slot=0    <- A's stop on track 4
[48.322] Selected track 5                      <- the cross-track switch, inside A's finalization window
[49.001] Loop 3: Queued playback switch        <- a *switch* to a different filled slot
49.008s  #CAP,…,capture_prewarm,track=4,slot=2,full=0,ready=0,credits=0   <- the destination is COLD at the press
[51.955] [TRACK] All Notes Off sent @ tick 5506
[51.992] LoopEnd playback commit track=4 1->2 start=0 len=2304            <- committed, 2.99 s later
```

and for A: one `canonical_release` in the session (`canonical_release,0,24,1536,8`), i.e. A's own stop's terminal, not
the switch's. Report: `integrity: COMPLETE`, `overflow_free`/`call_site_gate_ok` true, `timing_warnings: []`, **zero**
`slot_switch_hold_exceeded` rows, exactly one commit.

**Why the two rows together discharge the clause:** the commit is gated on the target's playback occupancy
(S4b.6's binding, `hasPlaybackOccupancyForSlot`), and the destination read `ready=0` at the press and committed
~3 s later — so the occupancy was taken **off-clock, between the press and the commit**, and neither the MIDI edge
nor the clock path took it (which is precisely what S4b.6's native fixture pins).

## Verification

* `pio test -e native`: **2,666 cases / 2,659 succeeded / 7 skipped / 0 failed** (the three fixtures this slice adds
  are the delta).
* `test_capture_ring` 14/14, including: a corrupt head prefix is dropped and the intact records still delivered
  (with the resync named and no wipe); a head with **no** boundary anywhere still wipes (the fallback); the
  save-completion receipt is emitted directly and enqueues nothing.
* **Three red-proofs**, each restored byte-identically: the resync disabled (`headResyncDropBytes` → 0) fails the
  corrupt-prefix fixture with the wipe back; the direct receipt mutated back to the ring fails its fixture.
* `teensy41-capture-serial` SUCCESS through the RAM1 budget guard, `variables 96,192 / padding 1,800 / locals 34,880`.

## What this does *not* claim

* The **cause** of the corrupt head is not identified: every head mutation site is self-consistent (`head + used ==
  tail` held at the wipe) and the fix is a *repair* — it bounds and names the loss instead of diagnosing where the
  bytes were interleaved. The `head_bad` rows remain the instrument for that.
* The ring-resync row is informational to the host: it is not in `serial_collector`'s invalidating set (unlike
  `overflow` / `critical_rejected` / `wiped`), because the records that mattered are still delivered — that
  judgement is recorded here rather than silently encoded.
* S4b.9's other clauses (acceptance, A's occupancy pinning, display transfer, MIDI continuity) were already measured
  in [`../2026-10-01-s4b9-cross-track/EVIDENCE.md`](../2026-10-01-s4b9-cross-track/EVIDENCE.md); this slice adds the
  provisioning clause and the commit.
