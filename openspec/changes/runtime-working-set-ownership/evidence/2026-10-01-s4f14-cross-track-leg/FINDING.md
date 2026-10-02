# S4f.14 cross-track leg: driven, and the oracle now attributes by track (2026-10-01)

## The gesture is now drivable, and it ran

`scripts/hitl_s4b8_slot_switch_during_finalization.py --track-number 4 --loop-slot 1
--switch-track-number 5 --switch-loop-slot 1 --continue-on-switched-track --continue-hold-ms 2000
--record-bars 2 --overdub-bars 2 --out-dir captures/s4f14-cross-track-01`

Runner extension: the switch presses are followed by a session started and stopped on the destination
(`--continue-on-switched-track`), committed as `4fab356d`. Source = track 4 Loop 1 (cleared and re-recorded by the
leg), destination = track 5 Loop 1, which is the **populated 24-bar loop**, so the destination press lands on a
filled slot and the continuation starts an **overdub** there.

Device rows (`captures/s4f14-cross-track-01/session_20261001_204344.log`), all uptimes in seconds:

| row | reads as |
|---|---|
| `261.720 Recording stopped @ tick 306 (recStart=0 length=1536)` | the source's 2-bar record |
| `263.540 Overdubbing started @ tick 889` | the source's overdub session |
| `265.559 Overdub wrap committed @ tick 1536` | the source's wrap — sealed, not deferred |
| `271.438 Overdubbing started @ tick 3347` | **the continuation's session on track 5**: tick 3347 exceeds the source's 1,536-tick loop, so only the 18,432-tick destination can carry it |
| `273.316 Overdub stop refused: canonical_output slot=0 interval=1` | the destination's stop did **not** proceed |
| `DIAG,latch,accept,4,0,1,0,playing` -> `wait,4,0,1,0,waiting` -> `consume,4,0,1,0,admitted` | the accepted-stop latch episode, **attributed to track 4** |
| `DIAG,canonical_release,0,36,1536,8` and `DIAG,canonical_release,0,0,18432,8` | both slots' finalization capacity released, once each, each with its own length |
| `#CAPTURE_RECONNECT` count 0 | no crash |

## What this leg does establish

- **The track-attributed oracle works.** `SC_ACCEPTED_LATCH` -> `interactionLatchTransition` is live, and the wait
  edge names its track: `DIAG,latch,wait,4,0,1,0,waiting` is **track 4's own** stop waiting on its own finalization.
  There is **no latch `wait` on track 5 at all**, so the forbidden case S4f.14 names — *track 5's stop waiting on
  track 4's job* — did not occur in this leg.
- Both tracks' slots released their finalization capacity independently, each exactly once, with their own loop
  lengths (1,536 and 18,432) — so the two finalizations did not share one owner.
- The continuation reached the destination and opened a session there (`tick 3347`), which is the half the runner
  could not drive before.

## What it does not establish, and why

1. **The destination's stop was refused, and the refusal is not attributable.** `Overdub stop refused:
   canonical_output slot=0 interval=1` is corroborated by `#CAP,...,ODUB,stop,admission,0,0,4294967295,4294967295,1,0,`
   **`canonical_output_busy`** (twice). That row family carries **no track and no slot**
   (`DebugSessionCapture.h` says so, and it is the reason `DIAG,latch` was added) — and here **both** tracks use
   Loop 1 = **slot 0**, so `slot=0` cannot disambiguate them either. The refusal therefore cannot be attributed to
   the source or the destination from this run. Note also that no `DIAG,latch` edge accompanies it: the latch covers
   the *accepted/waiting* path, not this admission denial.
2. **"Transport never stops" is not claimed.** The leg's log holds 2 `Transport started` against 1
   `Transport stopped`; the stop is presumed to be the teardown, but it was not timed against the continuation, so
   the clause stays unproven rather than assumed.
3. **The harness's `FAIL (FIRMWARE_CONTRADICTION)` is not safe to read as a firmware contradiction.** It is the
   *source-leg baseline* oracle, and the continuation added a second, differently-patterned overdub **on the same
   slot number** (`Loop 1`) on another track. Its issues — `overdub_out_of_range_notes`,
   `overdub_unmatched_open_notes`, `overdub_stored_span_too_short` — are exactly the shape a second OD sharing the
   slot number would produce. Attribution against the continuation is required before anyone treats them as defects.

## Next, in order

1. Disambiguate the leg: put the destination on a **different slot** (a populated track 5 Loop 2), so `slot=` alone
   separates the two — cheaper than a firmware change and it also removes the harness contamination.
2. Or give the `ODUB,stop,admission` family a track field, the same fix `DIAG,latch` received.
3. Then re-run and re-read the four discriminators, and attribute the harness issues.

## The owner's live observation, explained (2026-10-01)

Watching the `-02` leg the owner reported: *"it recorded a perfect 2 bar loop, then it started the overdub, but after
overdub stop it briefly showed a short high note and then extended the loop to 3 bars like the loop length fader was
modifying it unintentionally."* Both halves are in the device's rows, and neither is a length modification.

**The 3 bars is the destination's own length, shown when the display moves there — not a write.**

- The leg's source loop really is a perfect 2 bars: `[674.782] Record stop truncation rewind: raw=1728
  final=1536` -> `Recording stopped @ tick 192 (recStart=0 length=1536)`.
- The switch then presses `Ch16 Note64` (**Track** = index 4 = display track 5) at 682.651 and `Ch16 Note51`
  (**Loop** = `LOOP_SELECT_NOTE_BASE(50) + 2 - 1` = Loop 2) at 683.288.
- The display follows: `DISP,1,STOPPED,2304,...,3` at 683.205 -> `DISP,1,PLAYING,2304,...,3` at 683.769, i.e. the
  destination loop, whose genuine length is 2,304 ticks = **3 bars**. The device echoes that as
  `Sent loop length CC feedback: 3 bars (2304 ticks) -> CC=2`.
- **No length was written.** `LoopEditManager::handleLoopLengthInput` logs
  `LOOP EDIT: Changing loop length from X bars (Y ticks) to A bars (B ticks)` on every real change, and it also logs
  each ignored input (`not in LOOP_EDIT mode`, `session feedback settle`, `capture active`); the run has **none** of
  those rows. So the loop-length fader did not touch anything here.

**Why the destination is 3 bars: the seed, before the leg.** The destination was seeded with `--record-bars 2`, and
the seed's own rows show what happened: `[565.320] Recording started @ tick 317 (startLoopTick=317 loopStart=0)` ->
`[570.826] Recording stopped @ tick 2243 (recStart=317 length=2304)`. A **late record start** at tick 317 produced
2,304 ticks = 3 bars from a 2-bar request — the documented late-start quantization up to 3 bars (the same class as the
earlier "3-bar record loses the undo levels" defect). So the extra bar came from the seed, not from this leg.

**The "short high note" is real, and it corrects the previous attribution.** At the overdub-stop edge the device sent
`#CAP,682214884,MI,U,144,4,83,100` — note-on **83** (vel 100) on the track's channel (MIDI channel 4) — inside the
window the OD stop closed, before any of the continuation's presses (683.288+). The harness does play note 83 on that
channel (its stream there spans 24..83, 1,462 note-ons), so the device did not invent it; it is the row behind the
harness's `overdub_out_of_range_notes`, whose mechanism is the record pattern bleeding into the overdub window on the
same channel.

**Correction:** the earlier commit in this file blamed `overdub_out_of_range_notes` on continuation contamination. The
row above is on the **source** channel **before** the continuation pressed anything, so that blame was wrong for this
issue. `overdub_unmatched_open_notes` / `overdub_stored_span_too_short` remain unattributed, and the continuation
contamination question — a second OD on the same slot number — is now separate from the out-of-range issue.

## The different-slot re-run: a clean differential (2026-10-01)

Seeded the destination as **track 5 Loop 2** (`captures/s4f14-seed-t5s2-01`, `Result: PASS`) so the two tracks no longer
share a slot index, then re-ran the identical gesture into `captures/s4f14-cross-track-02` (source track 4 Loop 1 =
slot 0, destination track 5 Loop 2 = slot 1, `--continue-hold-ms 2000`).

| | `-01` destination on **Loop 1** (slot 0, same index as the source) | `-02` destination on **Loop 2** (slot 1) |
|---|---|---|
| continuation starts a session on the destination | yes (`Overdubbing started @ tick 3347`) | yes |
| destination's stop | **refused**: `Overdub stop refused: canonical_output slot=0 interval=1`, `ODUB,stop,admission,…,canonical_output_busy` | **not refused** — no refusal row of any kind in the run |
| `DIAG,latch` edges | `accept`/`wait`/`consume` on **track 4** (its own stop) | **none** |
| `DIAG,canonical_release` | `0,36,1536,8` and `0,0,18432,8` — both **slot 0**, so the two tracks could not be told apart | **`0,33,1536,8`** (source, its own 2-bar length) and **`1,11,2304,8`** / **`1,1,2304,8`** (destination, its own 3-bar length) |
| crash | 0 | 0 |
| transport | 2 starts / 1 untimed stop | 2 starts / 1 untimed stop |

**What this buys.** With the slot index shared (`-01`) the destination's stop was refused and nothing in the row
family could say *which* track was refused; with the slot index distinct (`-02`) the same gesture produced **no
refusal at all**, and the releases separate cleanly per slot with each loop's own length. The source released exactly
once with its own geometry while the destination released independently — the "one finalizer per track+slot, not one
device-wide" shape S4f.14 exists to test.

**The hypothesis this raises, stated as a hypothesis.** The refusal appears **only** when the two tracks use the same
slot **index**, which is the shape of a lookup keyed on the slot index without the track. The per-(track,slot)
`LoopPlaybackRuntime` objects are distinct, so a track-blind slot lookup elsewhere — the capture-stop owner, the
ledger, or a slot-indexed queue — is the candidate. **Not proven**: this differential is two runs, and the code path
was not traced. It is the next thing to trace, and it is the difference between "two tracks can never share a slot
index while one finalizes" (a real S4f.14 defect) and a coincidental timing difference.

**Not established:** `Transport never stops` (the one stop is untimed in both runs), and the two remaining harness
issues (`overdub_unmatched_open_notes`, `overdub_stored_span_too_short`).

## The trace: the shared-slot-index hypothesis is disproven (2026-10-01, read-only)

The hypothesis above was that a lookup keyed on the slot index without the track caused the `-01` refusal. The code
settles it the other way.

**The admission boundary resolves the interval's own slot on the owning track — by construction.**
`Track::admitDeferredOverdubStop` (the drive the refusal comes from, `src/Track/TrackOverdubLifecycle.cpp`) carries
the S4f.0 note and then does exactly that:

```
// S4f.0: every Loop/runtime-derived guard and binding below resolves the interval's own
// slot, so a later active-slot movement cannot make the drive test the wrong slot.
Loop& loop = getLoop(slot);
LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
```

`playbackRuntime` is a **`Track` member**, so `slotIfAllocated(slot)` indexes *that track's* per-slot runtimes;
`getLoop(slot)` likewise. The caller also validates `interval != captureInterval_` -> `stale_interval`, so a drive for
a dead interval is a superseded request rather than a refusal. There is no track-blind slot lookup on this path.

**What the refusal actually tests** (`src/Track/TrackPlaybackRuntime.cpp:2072-2075`):

```
bool LoopPlaybackRuntime::canonicalCaptureOutputAdmitted() const {
  return canonicalCaptureOutput.store.empty() && canonicalCaptureOutput.store.reservedAppendReady() &&
         canonicalCaptureChunks.availableChunks() >= PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS;  // 8
}
```

— that slot's **own** store empty, its append reserved, and at least **8** canonical-copy credits available. So
`canonical_output_busy` is a **per-slot readiness** condition, and it was the *destination slot's own* readiness that
failed.

**Why the two runs differ, then.** Not the track: the **loop sitting on the destination slot**.

| run | destination slot held | at the continuation's stop (≈1.9 s after its own OD start) |
|---|---|---|
| `-01` | track 5 **Loop 1**: 18,432 ticks, 1,716 events, just switched into playback | `canonicalCaptureOutputAdmitted()` false -> refused |
| `-02` | track 5 **Loop 2**: 2,304 ticks, 144 events | admitted -> no refusal |

A slot whose interval just ran on a 1,716-event loop is far likelier to be holding chunks / an un-drained store than
one carrying 144 events, so the differential is capacity-and-latency, not aliasing.

**What this means for S4f.14 — a positive result.** The *core* question of the leg ("one device-wide finalizer" vs
"one finalizer per **track+slot**") is answered in the code for this boundary: it is per-(track, slot), explicitly so
that an active-slot movement cannot make the drive test the wrong slot. The device corroborates it: in `-02` the
source's slot released once with its **own** 2-bar geometry (`canonical_release,0,33,1536,8`) independently of the
destination's (`canonical_release,1,11,2304,8`, `,1,1,2304,8`). A device-wide singleton could not produce that split.

**A reporting gap this leaves (named, not fixed).** The *code* is track-scoped, but the *rows* are not: the human row
is `Overdub stop refused: %s slot=%u interval=%lu` — slot and interval, **no track** — and the
`ODUB,stop,admission` family carries neither slot nor track. So a refusal cannot be attributed to a track from the
evidence, even though the behaviour it reports is per-track. That is the same class of gap `DIAG,latch` was added to
close for the admitted/waiting path; the *denial* path has no equivalent.

## `-03`: the denial is intermittent, and the destination's session can be empty by design (2026-10-01)

Re-ran the identical gesture with the destination back on **Loop 1** (the shared slot index, `-01`'s shape) on the image
carrying the new trailing `track=` field (`eb2a6212…`): `captures/s4f14-cross-track-03`.

**No denial occurred**, so the new field is **not yet exercised**. That is itself the useful result: the same
destination slot that produced `canonical_output_busy` in `-01` produced **no** refusal here, so the condition is a
**race on that slot's readiness** (`store.empty() && reservedAppendReady() && availableChunks() >= 8`), not a
deterministic rule keyed on the slot index. Two runs of the same shape differ; the third agrees with the
capacity/latency reading.

**The destination's session is structurally exercised but carries no content — and that is expected.** The owner
watching the bench reported *"I see overdub on over the 24 bar only and nothing really being added"*. Exactly right:
the harness keeps playing on the **source** track's channel (the device relays `MI,U,144,4,48..52`, channel 4) while
the destination (track 5) captures only its own channel's input. The rows agree — the leg's only release is the
source's

```
DIAG,canonical_release,0,33,1536,8
```

with **no** release for the destination, whereas `-01` (a contentful case) also had `canonical_release,0,0,18432,8`.
An empty overdub produces no pass and no finalization release, which is the correct behaviour, not a defect.

**Consequence for this leg's clauses, stated rather than glossed.** `-03` exercises the *interaction* (a session
started and stopped on the destination while the source finalizes) with **zero content** on the destination. So it is
weak evidence for the *content* clauses — `no lost capture`, `no wrong-track publication`, `content exact`
(leg item 2) — which need a destination session that actually captures something. The empty path does still exercise
`transport never stops`, the latch/release absence and the source's own single release.

**Still unexercised:** the new `track=` field on a real denial. Forcing it needs the stop to land inside the
un-admitted window (a much shorter `--continue-hold-ms`), and giving the destination real content needs the harness to
play on the destination's channel during the hold — the continuation currently presses only the record button.

## Clauses settled from the existing runs — and the premise that is not met (2026-10-01)

Three of S4f.14's clauses can be read off `-02`/`-03` without another run, and one of them exposes a problem with the
leg as configured.

**Clause "`overflow_free` / `call_site_gate_ok`" — PASSES.** `host_midi_automation_baseline_20261001_205329.json`
(`-02`), at `assertions.serial_verification.midi_boundary_timing`: `overflow_free = True`, `call_site_gate_ok = True`.

**Clause "transport never stops" — PASSES for the interaction, with its window named.** Each run has two
`Transport started` and exactly one `Transport stopped`, and in both runs the stop sits **inside the source leg's own
recipe, well before the switch**: `-02` started 659.435, stopped 665.739, started 669.417, switch at 682.640+; `-03`
started 40.469, stopped 46.713, started 50.907, switch at 63.716+. Nothing stops the transport at or after the switch
in either run. Stated precisely because it is narrower than the clause's words: the stop is not the teardown, and the
clause is satisfied *for the cross-track interaction window*, which is what it is about.

**Clause "track 4's finalization is track+slot-scoped" — the timings say the premise is NOT met.** The reentry
markers bracket the source's finalization and it is **fast**:

```
-02: [682.640] Reentry window open slot=0   ->   [683.012] Reentry released slot=0 generation=33   (0.372 s)
-03: [63.716]  Reentry window open slot=0   ->   [64.077]  Reentry released slot=0 generation=33   (0.361 s)
```

and the destination's session begins **after** that release. The gesture itself cannot get there with a 2-bar
source: the intervention fires 305 ms after the stop release, then the switch costs two presses
(`Ch16 Note64` then `Ch16 Note50`, ~0.3 s each), so the destination's own start press lands at ~0.9 s — by which time
the source retired 0.4 s earlier. **So the two finalizations were never concurrent in `-01`/`-02`/`-03`**, and this
leg has not yet exercised the overlap it exists for. That also re-reads `-01`'s refusal: it was not cross-track
contention (the source was long retired), which is consistent with the per-slot readiness trace.

**What has to change to meet the premise** (one of these, decided by whoever owns the leg):
1. a **longer source interval** — the source's finalization scales with its content, so `--record-bars 24` (matching
   the destination) should hold it open for seconds rather than 0.37 s; or
2. a source whose stop is deliberately owed rather than promptly retired (a pending deferred stop), which is the
   harder, more faithful case but needs the predecessor-job machinery to be live.

Until one of those is in place, the clauses that need concurrency — "track 5's stop executes with **no**
`accepted_stop_finalizer_wait` attributable to track 4's job **while track 4 finalizes concurrently**", and
"per-track holder counters rise and fall independently" — are **unproven**, and saying otherwise would be reading a
sequential run as a concurrent one.

## An aborted stop leaves a record job LIVELOCKED in its closure phase (2026-10-01, found while cleaning up)

The 24-bar source attempt (`captures/s4f14-cross-track-04`) exceeded the managed-run watchdog
(`[hitl-capture] scenario exceeded 360.0s watchdog; no run scope is registered; giving it 30.0s to stop`) and the
capture was released with the leg's stop unfinished. What the device was left doing is its own finding.

**It was not hung — it was spinning.** `!STOP_STATUS` kept answering, and the device streamed `DIAG,stop_grain` rows:

```
#CAP,959334014,DIAG,stop_grain,3,0,0,3,1,1,166005,959333965,959334001,892080032,892076922,892080032,0,4294967295,0
```

which decodes through `stopGrainTiming` as **track 3 (display track 4), slot 0, phase 0 (`ReservedClosure`),
destination 1 (Stopped), `serviceSequence = 166,005`** — i.e. the deferred lane had serviced **166,005 stop grains**
of one job. Over three `!STOP_STATUS` polls 18 s apart the job never moved:

```
us=921472406  state=STOPPED pending=1 phase=0 dest=1 order=0 ranked=0 canonical=137 prefix=2 tail=0 bridge=0 wrap=0 queue=1/4
us=939421677  state=STOPPED pending=1 phase=0 dest=1 order=0 ranked=0 canonical=137 prefix=2 tail=0 bridge=0 wrap=0 queue=1/4
```

`canonical=137` identical, `pending=1`, `queue=1/4`: a **livelock** — grains consumed forever with no progress.
Because the per-grain row is sampled one-in-sixteen and still arrives ~8/s, the capture ring is flooded, which is
also why the third poll's row never came back through the flood.

**This is the DEC-070 wedge family, on the *record closure* route.** DEC-070's own words — *"a predicate that can
never become true wedges the slot"* — describe it; the difference is that this one is *serviced* rather than returning
`Pending`, so it burns the lane and the ring instead of parking quietly.

**Not caused by this session's changes.** The job sits at phase 0 (`ReservedClosure`); DEC-107's bound acts at
`RecordPublished` (phase 16), and the trailing `track=` field is a format-string change.
**Trigger observed:** an *aborted* stop — the watchdog released the run mid-flight and the source's stop was left
unfinished. Whether the livelock needs the abort or merely a long record stop is **not established**.

**Cleared:** `!REBOOT` (transport stopped) — `crash=0`, `Current workspace loaded successfully`, `BOOT,load,ok`, and
**0** `stop_grain` rows in an 8 s post-reboot read.

**Consequence for the S4f.14 plan.** A 24-bar source does **not** fit the 360 s managed-run bound (this attempt was
released before the switch ever fired), so "make the source longer" is not a usable route to the leg's concurrency
premise. Either a moderate source that still fits the bound (the 2-bar case measured 0.37 s; 8 bars is the next point
to measure) or the owed-stop route is needed — and a route that aborts must be run knowing it can leave a livelocked
stop job behind.

## S4f.14 accepted: the four discriminators, and which run evidences each (2026-10-01)

The premise is met by **`-05` and `-07`** (8-bar source): the source's window is 6.911 s and 3.732 s respectively, and
the continuation's whole session falls inside it. `-07` is confirmed from two independent sources — the device rows
(`Reentry window open slot=0` 107.826 -> `Reentry released slot=0 generation=97` 111.558, with `Overdubbing started`
109.518 and `Stop Overdub` 110.917 between them) and the harness's own state timeline
(`OVERDUBBING 109.754 -> 110.964` inside `OVERDUB_STOPPING` 107.827 -> the next `PLAYING` at 110.964).

| S4f.14's discriminator | evidence |
|---|---|
| track 5's stop executes with **no** `accepted_stop_finalizer_wait` attributable to track 4's job, while track 4 finalizes concurrently | The only latch edges in `-05` and `-07` are track **4's own** `accept`/`wait`/`consume` (`DIAG,latch,wait,4,0,1,0,waiting`), and there is **no latch wait on track 5** in either run — while the concurrency above holds. Repeatable across both. |
| track 4's job retires exactly once with its content exact; track 5's interval is untouched | Each interval of the source's slot released once with **its own** geometry — `canonical_release,0,97,6144,8` (the 8-bar 1,536->6,144 case) and `,0,1,6912,8` / `,0,97,6912,8` in `-07` — and the destination released separately with **its** 18,432. The source's own passes stayed its own: track 4 Loop 1 reads `a=2, h=2, l=6912` with **no burst notes**, while the destination grew only by the burst. |
| `noLiveOwnerForSlot(track 4, slot)` stays false while its job is live | Observable through the same identity-qualified release marker: the source's owner was live from its stop edge to `Reentry released slot=0 generation=97 interval=416611838899`, and the destination's stop was admitted inside that window rather than waiting on it. |
| per-track counters independent; no lost capture; no wrong-track publication; `overflow_free`/`call_site_gate_ok`; transport never stops | Timing gates **True/True** in both runs (`assertions.serial_verification.midi_boundary_timing`). No `#CAPTURE_RECONNECT` in any run. Content check: destination `t=4,s=0` went `a=3 -> a=5` and `!CAPTURE_CONTENT` reads `record 1148 + overdub 454 + 114 + 8 + 4 = effective 1728` (the last two being the burst) while the source grew only its own take. The one `Transport stopped` in each run sits inside the source leg's own recipe, before the switch. |

**Accepted, with these qualifications recorded rather than smoothed over:**
- The window **varies with content** (0.37 s at 2 bars; 3.73 s and 6.91 s at 8 bars) — 2.5x apart between two 8-bar runs —
  so the leg's concurrency is real but not tightly bounded; a run that lands unluckily short would not overlap.
- The harness's **baseline content oracle still fails** (`overall_ok: False`; `overdub_out_of_range_notes`,
  `overdub_unmatched_open_notes`, and in `-05` also `record_unmatched_open_notes` / `record_stored_revt_missing`).
  These are **not** S4f.14's clauses, and `overdub_out_of_range_notes` is attributed above to the record pattern
  bleeding into the overdub window on the same channel; the rest stay unattributed. They do not gate this leg.
- The destination slot reads `kr=0, kc=0` after the runs — its **published root lags its committed content** (the
  one-pass-late readiness territory, S4c.1), recorded as an observation, not as a defect here.
- Repeating the leg **accumulates content on the destination** (one pass per continuation), and the device reached its
  **memory ceiling** during the aborted 24-bar attempt (`heap_free=16 KB`, PSRAM `live=8131 KB`, a
  `snapshot_reserve` denial after 128 s) — so this leg has a finite repeat budget.

