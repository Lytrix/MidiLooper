# Record-stop canonical/bridge route — handoff (item 2 of the stall list)

**Date:** 2026-10-30
**Status:** closed for the stop route. Steps 1-2 and the device rerun landed as DEC-225; the remaining total-work
and host-clock items landed as DEC-227 (companion-work budget) and DEC-228 (all-stopped stop-lane turn), with
device `midifile-chain-22`. Step 3 is answered as a **total** bound, not a per-grain cost: the companion grains
measured sub-ms (`stop_grain` companion max 890 us, 509 grains), while a job's total family time reached 5-14 s --
so the budget, not the grain size, is the bound.

## Landed (2026-10-30, DEC-225) — the park was a tail that never **began**

Re-reading `session_20261030_143745.log` corrects the inference above: the job sat at `CanonicalTail` from
**63.0 s to 113.8 s** with **zero** `stop_grain` rows and `canonicalTailPhase` **Idle**. The refusal was
`beginFrozenCanonicalTail`, not the tail's cost: the rig's next window starts a **same-slot** successor at
61.99 s, and that successor owns the slot's shared canonical machinery (capture bridge / frozen companion apply /
a predecessor's retained `canonicalPassInputs`) for the whole chain. The job held the FIFO head, the stop owner
and the session-exit receipt, so no window opened; only the 113.8 s transport stop retargeted it
(`retargeted stop has no canonical base; raw pass only`). A begun tail that latches `Denied` is the second
unreachable state.

Landed:
- `kCanonicalTailStartStallGrains = 32` bounds **no-start** grains at `CanonicalTail` (the lane measured
  ~48 grains/s). `Idle` past the budget or `Denied` at any grain sets `canonicalBaseAbandoned` and takes the
  existing DEC-165 raw-only retirement: one pass, one publication, DEC-172 obligation for a Playing destination.
- One Tier-A row names it: `DIAG,stop_tail,<track>,<slot>,<phase>,<denied|no_start>,<grains>` (direct when USB
  has room, ring fallback). The logger's stall note is once per Track, so a second episode would be silent.
- Fixtures `test_6_2_window_denied_first_record_stop_retires_bounded` and
  `test_6_2b_denied_canonical_tail_retires_immediately`; both mutation-red (pre-fix: 512 turns / phase 5 /
  tail Idle / 0 passes, and 64 turns / tail `Denied`). Full native **3,713 / 7 skipped / 3,706 / 0**; target
  build SUCCESS at RAM1 `96,192`, ITCM `392,120 + 1,096`.
- RAM1 finding: DEC-216's `DIAG,idle_turn,` header literal had crossed the align edge; it moved with
  `stop_tail` into the DTCM-carved `src/Utils/StopDiagRows.cpp`.

Not claimed: the tail's per-grain cost is still unbounded, and successor contention is not removed. The device
run below completed the chain but did **not** exercise the bound (no tail was starved), so the fixture remains the
proof that the escape fires.

## What is measured

| Capture | Observation |
|---|---|
| `session_20261030_142445.log` (12-window rig, quantum 32) | `loop_rem,idle_maint,96,1` on `stop_grain,7,3,5,…` = phase **5 CanonicalTail**; `base_build_done,interval=3..7,outcome=complete,read` fast; repeated `win_frozen_readiness,…,build_drop,completed_unpublished`; `Deferred stop stalled: canonical bridge window denied; companion path kept`; stop job still pending at the end |
| `session_20261030_143745.log` (same rig, quantum 8) | only **4** `stop_grain` rows in 60 s; job pending in `AtomicTransfer` at 122 s (`#STOP_TRACE,…,phase=10,…,pub=1,passes=1,len=15360`); stop eventually commits **raw only** (`retargeted stop has no canonical base; raw pass only`) |
| `session_20261030_143427.log` | record stop reached `STOPPED_RECORDING -> STOPPED` but the job advanced 4 grains; no overdub window opened |

Sequence inferred from the rows: prefix completes (`base_build_done,outcome=complete`) -> window denied
(`canonicalBridgeUnavailable`, phase moves to `CanonicalOrder`) -> tail work (phase 5, 96 ms grains) ->
eventual raw-only commit ~50 s later.

**Correction (DEC-225, see *Landed* above):** the phase-5 span was **not** tail work. `session_20261030_143745.log`
has zero `stop_grain` rows for that job and `canonicalTailPhase` stayed `Idle`; the 96 ms `idle_maint` sample in
the first row is a different turn landing on the stop lane. The inference above is kept only as the reasoning
that preceded the landed measurement.

## Why it is not a grain-size problem

Quantizing the stop grain 32 -> 8 (DEC-224) kept the same denial and made the same job take four times as
many turns. Reverted.

## Why it is not a host fixture gap

`test_long_first_record_stop_with_early_playhead_normalizes` (20-bar first record, early playhead) passes on
host: it reaches `RecordPrepared` with `denial=none`. The device shape adds a **window** denial the fixture
does not produce, so the next fixture must inject a `canonicalWindowPhase == Denied` with a ready prefix and
assert the job retires (pass committed, companions published or raw, no park) within a bounded number of
owner turns.

## Next steps (in order)

1. ~~**Fixture (RED first):**~~ **Landed (DEC-225).** Two fixtures: the successor-starved no-start shape and the
   terminal `Denied` latch; both mutation-red, both assert bounded retirement, one pass, one publication and the
   owed obligation.
2. ~~**Named no-progress row:**~~ **Landed (DEC-225).** `DIAG,stop_tail,...` names `no_start`/`denied`, the phase,
   and the grains spent; Tier-A and direct-when-there-is-room.
3. **Bound the tail's per-grain cost** — still open, and the handoff's original caution stands: a **begun** tail is
   legitimate work, so this is a cost measurement, not a liveness fix. A parked job is now impossible on the two
   known causes; if a future capture shows a begun tail spending tens of seconds, that capture is the evidence.
4. **Device rerun:** **Ran 2026-10-30 on `midifile-chain-15`** (image `52f35ee9…`, boot proved
   `boot_reset,crash=0` / `BOOT,load,ok`). The first attempt (`-13`) was invalid: its clear was refused by
   the deferred-save `post_load_holdoff`, the rig assumed success and recorded an overdub over the old take.
   The rig now verifies and retries the clear (`MIDI: Clear selected slot N`; `--capture-log` follows
   `captures/.current_session`), so `-15` cleared on the **first attempt**, recorded a fresh 20-bar take
   (`tick 15336`) and committed **all 12 windows** (`cap=13`, `edit=124`, 480 display notes, `behind_pulses 3`).
   The `143745` parked-head/zero-window failure did not reproduce and **no `stop_tail` row fired**; two late
   jobs spent ~5 s and ~12 s in the companion phases (`#STOP_TRACE` gen 20: 911→916 s; gen 38: 935→949 s) --
   busy tail work, not a park. The final transport-stop retarget still retires raw-only (DEC-168).
   Capture limit: the 5 s telemetry family (`midi_gap`/`late_on`/`win_gap`) and `stop_grain` stop at ~899 s,
   so chain-time latency maxima are not available; the record-start window reads `midi_gap` 247 ms and
   `late_on` 163 ms, then 5-36 ms / 28-38 ms. One 144.6 ms `persist_save` (DEC-215) sits at the record stop.

## Do not

- Do not lower `DEFERRED_STOP_PREPARATION_QUANTUM` again (DEC-224).
- Do not treat `build_drop,completed_unpublished` as the drop site; DEC 2026-10-18 measured it as the
  live-capture builder resting in `Phase::Complete`.


## Update 2026-10-30 — DEC-227 / DEC-228 close the remaining stop-route items

- **DEC-227 — total companion-work budget.** `kCompanionWorkBudgetUs = 4 s` in `CanonicalTail`..`CompanionBridge`
  (one byte in `Track` padding). On expiry the raw pass commits and the DEC-172 obligation owns the derived work.
  Device: 2 s cut every window (`midifile-chain-19`, `edit=0`); 4 s cut only the late outliers and left the early
  windows inline (`-20`: one cut, `edit=13`; `-22`: two cuts, `edit=11`). The tail's per-grain cost is not the
  lever: `stop_grain` companion grains are sub-ms (max 890 us over 509 grains); the wall time is waiting/credits.
- **DEC-228 — all-stopped + host clock.** One bounded stop-lane turn is admitted before the all-stopped refusal
  when a stop job is pending. Device `-22` held the host pump through the record settle: the record stop retired in
  **4.5 s** (55.4 -> 59.95 s) and all 12/12 windows opened with `behind_pulses=0`, versus DEC-224's 4 grains/60 s
  without the fix. The rig's `--hold-clock-on-settle` is the gate; the old park comment is removed.
- **Not reopened:** successor contention is still present; the budget bounds its effect on the stop FIFO, not its
  existence. Raw-first publication (Option B) is not needed while the 4 s bound holds.

### Follow-up: convergence + the stop-preview tier

- The all-stopped/host-clock pass now also admits one bounded `serviceDeferredCanonicalConvergence` grain after the
  stop lane (`Track::hasDeferredConvergenceObligation` probe, public one-slot service). Device `-24`
  (`0eea3987…`, `--hold-clock-on-settle --stop-bars 8`): one late cut at 120.85 s, **edit=13 by 122.3 s**, no
  `no_start`, zero overruns, `tierA_evict=0`. The recovery happened while the chain was still running (the normal
  Snapshot convergence lane); the all-stopped admission is the settle hardening. `-23` (same 4 s budget, no
  admission) differed in cut count (three cuts, `edit=7`), so it is an observation, not a controlled comparison.
- F1 was already implemented by the stop-preview display tier (`captureStopPreviewForSlot`), not missing. The
  `disp_src` row now names `stops`/`stopnotes`; every pending row in `-23`/`-24` has `stops=1,stopnotes>0`, so the
  pending pass is proven present while the committed base lags. The 16 s `disp_turn` cadence is not note loss.
