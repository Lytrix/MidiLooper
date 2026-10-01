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
