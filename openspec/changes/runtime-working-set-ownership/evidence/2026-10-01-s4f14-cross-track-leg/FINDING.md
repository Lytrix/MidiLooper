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

