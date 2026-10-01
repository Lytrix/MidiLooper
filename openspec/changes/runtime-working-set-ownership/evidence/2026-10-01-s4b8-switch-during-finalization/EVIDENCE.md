# S4b.8 device leg — a slot switch while the predecessor still finalizes

**Date:** 2026-10-01
**Image:** `teensy41-capture-serial`, hex sha256 `cdab16f4e5f65988a7383b9fd4633d965c76a77c226c73ce462abfa6e7afde3c`
(flashed with authorization in this session; `git_commit dbcd19f1` + this session's uncommitted host tooling)
**Authorization:** track 5, all slots (this session).
**Entry point:** `scripts/hitl_s4b8_slot_switch_during_finalization.py` — a wrapper variant of
`hitl_stop_interruption.py` that reuses the baseline preset's recipe and the harness's established presses, and
changes only what the intervention sends (a slot-select, and optionally a track-select first, instead of a
transport Stop).

## Runs

| run | purpose | delay | outstanding switch | verdict |
|---|---|---|---|---|
| `captures/s4b8-seed-t5s2` | seed the destination (track 5 slot 2) | — | 0 (its own) | `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` — the documented baseline issues |
| `captures/s4b8-leg-01` | first leg attempt | 305 ms | **2** (`1->0` at 148.550, `0->1` at 168.343) | base recipe `FAIL` (its known overdub issues) |
| `captures/s4b8-leg-02` | tighter delay | 120 ms | 0 — the overdub pass never ran, so nothing armed (`switch sent: False`) | not used |
| `captures/s4b8-leg-03` | **the leg below** | 180 ms | **1** (`0->1` at 295.607) | base recipe `FAIL` (its known overdub issues); boundary-timing gates green |

`captures/s4b8-leg-02` is recorded rather than deleted: it shows the runner's arming depends on the recipe's
overdub pass actually running, and that a run which does not arm says so (`switch_not_sent` in
`intervention.jsonl`) instead of reporting a phantom switch.

## The leg (`captures/s4b8-leg-03`, session `session_20261001_134116.log`)

Device clock, seconds into the session (verbatim rows):

```
[282.948] [INFO] Loop 1: Start Recording                     <- A records on track 5, slot 1
[294.613] [INFO] MIDI Button A: Stop Overdub                 <- A's stop press
[294.613] [INFO] [TRACK] Reentry window open slot=0
294.614  DIAG,reentry,bound,2702,7,0,0                       <- A's stop ADMITTED and BOUND to slot 0
294.614  DIAG,stop_state,4,0,6,7,2702,...
294.614  DIAG,stop_state,4,0,7,5,2702,...
294.738  ODUB,stop,bridge_base,0,0,...,prepared_window
294.748  ODUB,stop,bridge_begin,0,0,...,65280,denied         <- the busy-owner denial: A is still finalizing
[294.987] [INFO] Loop 2: Queued playback switch              <- THE SWITCH PRESS: queued inside that window
295.021  ODUB,stop,bridge_ready,0,0,...,2,1536,context_retained
295.034  DIAG,canonical_release,0,36,1536,8                    <- A's interval released, generation 36
[295.607] [INFO] LoopEnd playback commit track=4 0->1 start=0 len=2304   <- the switch COMMITS
165.584 (leg 01) ODUB,stop,prefix_reuse,...                    <- deferred finalization grains continue after a press
```

### Assertions of the entry, as observed

1. **The switch is accepted and completes.** `Loop 2: Queued playback switch` at 294.987 → `LoopEnd playback
   commit track=4 0->1 start=0 len=2304` at 295.607: one slot switch on track 4 (0-based) / track 5 (1-based),
   from slot 0 to slot 1, and it landed.
2. **`activeLoopIndex` does not move before A's stop is admitted and bound to its slot.** The binding is at
   294.614 (`DIAG,reentry,bound,…` for slot 0) — *before* the switch was even queued (294.987) — and the only
   commit/move row in the session is the 295.607 one. The switch was accepted, waited, and then moved the slot;
   it did not pre-empt the binding.
3. **The pre-switch slot finalizes exactly once.** `DIAG,canonical_release` rows naming slot 0 in the whole
   session: **1** (`295.035`, generation 36). The switch did not produce a second finalization.
4. **The successor slot's content is untouched.** Across the whole session, exactly **one** capture-family row
   (`RECA`/`RECS`/`RID`) names slot 0 or 1 — `RECA slot=0` at 283.526 s, the predecessor's own record. **None**
   names the successor (slot 1, 0-based). The commit prints the target loop's own geometry
   (`start=0 len=2304`), i.e. the successor's take as it was.
5. **Pool holders rise with the switch** (`[Memory] ext`, `phase=tick`, device µs):

   | device time | live | blocks | chunks_free |
   |---|---|---|---|
   | 292.730 s (A playing) | 6610 KB | 1298/1475 | 338 |
   | 294.731 s (just before the stop) | 6610 KB | 1298/1475 | 338 |
   | **295.730 s** (after the commit) | **6641 KB** | **1310/1475** | 338 |
   | 296.731 s | 6642 KB | 1310/1475 | 338 |
   | 297.734 s | 6643 KB | **1355/1475** | **335** |

   The successor's role costs one working set's worth of growth (+31…33 KB, +12…57 blocks, −3 pool chunks) — not
   two — and the level settles there rather than accumulating, which is the bounded-overlap behaviour S4b.2's
   fixture asserts natively.

### Boundary-timing gates (the S4b gate's own evidence)

From the run report (`assertions.serial_verification`):

* `midi_boundary_timing.overflow_free: true`
* `midi_boundary_timing.call_site_gate_ok: true`
* `timing_warnings: []`

Leg 01 (two playback switches in one run, `1->0` and `0->1`) reports the same two gates true; leg 03 (one switch)
likewise. A dedicated multi-switch run is still the tighter evidence for the gate's "across repeated switches"
wording, and is recorded as such rather than claimed.

## Not claimed

* The **base recipe's own verdict** is `FAIL` on these runs (`overdub_unmatched_open_notes`,
  `overdub_out_of_range_notes`, `overdub_stored_span_too_short`), and the *seed* run is
  `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` — the documented non-repeatable-green baseline problems, not this
  leg's subject. This evidence is the **switch rows plus the holder census and the timing gates**, read from the
  session log, not a green baseline.
* The predecessor's *stop* was **not** synchronised to a loop boundary by this runner; the leg shows the
  ordering (binding → queue → commit) and not a musical alignment claim.
* `captures/s4b8-leg-02` armed nothing; it is evidence about the runner, not about the device.
